// LINNA-2 dataset generator: linna2.exe <config.ini> <stage> [--sample] [--bench n] [n]
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00  // Windows 10: BCryptHash
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include "imagegen.h"
#include "platform.h"
#include "stages.h"

namespace {

std::string now() {
    std::time_t t = std::time(nullptr);
    char s[32];
    std::strftime(s, sizeof s, "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
    return s;
}

std::string run(const char* cmd) {
    std::string out;
    if (FILE* p = _popen(cmd, "r")) {
        char buf[256];
        while (fgets(buf, sizeof buf, p)) out += buf;
        _pclose(p);
    }
    return out;
}

// git:<commit>, with -dirty when tracked files have uncommitted changes
std::string gitVersion() {
    std::string head = run("git rev-parse --short=12 HEAD 2>nul");
    head.erase(head.find_last_not_of(" \r\n") + 1);
    return head.empty() ? "git:unknown" : "git:" + head + (run("git status --porcelain --untracked-files=no 2>nul").empty() ? "" : "-dirty");
}

std::string sha1(const std::string& data) {
    uint8_t h[20];
    BCryptHash(BCRYPT_SHA1_ALG_HANDLE, nullptr, 0, (PUCHAR)data.data(), ULONG(data.size()), h, 20);
    std::string s;
    for (uint8_t b : h) s += fmt("%02x", b);
    return s;
}

const char* SPLITS[3] = {"train", "val", "test"};

}

std::string readText(const std::string& path) {
    std::ifstream in(u8path(path), std::ios::binary);
    if (!in) fatal("Could not read " + path);
    return {std::istreambuf_iterator<char>(in), {}};
}

void writeText(const std::string& path, const std::string& text) {
    std::filesystem::create_directories(u8path(path).parent_path());
    if (!writeFile(path, text)) fatal("Could not write " + path);
}

std::string stem(const std::string& utc) {
    std::string s;
    for (char c : utc.substr(0, 19))
        if (c != '-' && c != ':') s += c;
    return s;
}

std::vector<std::map<std::string, std::string>> readCsv(const std::string& path) {
    std::vector<std::map<std::string, std::string>> rows;
    std::ifstream in(u8path(path));
    std::string line;
    std::vector<std::string> head;
    auto split = [](const std::string& l) {
        std::vector<std::string> v;
        for (size_t a = 0, b; a <= l.size(); a = b + 1) v.push_back(l.substr(a, (b = std::min(l.find(',', a), l.size())) - a));
        return v;
    };
    if (std::getline(in, line)) head = split(line);
    while (std::getline(in, line)) {
        std::vector<std::string> v = split(line);
        if (v.size() != head.size()) continue;
        auto& r = rows.emplace_back();
        for (size_t i = 0; i < v.size(); i++) r[head[i]] = v[i];
    }
    return rows;
}

void runSet(Generator& g, const std::string& set, const std::vector<Shot>& shots) {
    Index idx(g.set.root + "/index/" + set + ".csv");
    size_t todo = std::count_if(shots.begin(), shots.end(), [&](const Shot& s) { return !idx.done(s.id); }), done = 0, night = 0;
    printf("%s: %zu frames, %zu to render\n", set.c_str(), shots.size(), todo), fflush(stdout);
    auto start = std::chrono::steady_clock::now();
    auto elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(); };
    double shown = 0;
    for (const Shot& s : shots) {
        if (idx.done(s.id)) continue;
        Frame f = g.shoot(s);
        g.write(s, f, idx);
        done++, night += f.night;
        if (elapsed() - shown > 10 || done == todo) {
            shown = elapsed();
            printf("  %s %zu/%zu (%zu night), %.2f s per frame, %.0f s left\n", set.c_str(), done, todo, night, shown / done, shown / done * (todo - done));
            fflush(stdout);
        }
    }
    g.drain();
    std::string runs = g.set.root + "/index/runs.csv";
    if (!std::filesystem::exists(u8path(runs))) writeText(runs, "set,frames,night,seconds,finished_utc,generator_version\n");
    if (done) {
        FILE* f = openFile(runs, L"ab");
        fprintf(f, "%s,%zu,%zu,%.1f,%s,%s\n", set.c_str(), done, night, elapsed(), now().c_str(), g.version.c_str());
        fclose(f);
    }
}

// 05 §5 on the index files: fails on frames that leak into a held-out region or time
bool leakageCheck(const Settings& S) {
    bool ok = true;
    std::map<std::string, double> testStart;  // per orbit: T_end - (test + val + guard) hours, from the file's first row
    for (auto& o : S.orbits) {
        std::string first;
        double seconds;
        if (Orbit::span(o.file, first, seconds)) testStart[o.name] = seconds - (S.testHours + S.valHours + S.guardHours) * 3600;
    }
    if (!std::filesystem::exists(u8path(S.root + "/index"))) return printf("no frames in %s\n", S.root.c_str()), true;
    for (auto& e : std::filesystem::directory_iterator(u8path(S.root + "/index"))) {
        std::string set = e.path().stem().string();
        if (set == "runs") continue;
        std::map<std::string, std::map<std::string, int>> bands;  // latitude band -> partition -> frames
        std::map<std::string, int> unseen;
        int bad = 0;
        for (auto& r : readCsv(e.path().string())) {
            int parts = std::stoi(r["parts"]);
            double lat = std::abs(std::stod(r["lat_b_deg"]));
            const std::string& ds = r["dataset"], &sp = r["split"], &part = r["partition"];
            bands[lat < 60 ? "|lat| < 60" : lat < 80 ? "60-80" : "> 80"][part]++;
            bool leak = ((ds == "S1" && sp != "test") || (ds == "S2-clip" && part == "train")) && (parts & 4);  // 1. test region
            leak = leak || (ds == "S1" && sp == "train" && (parts & 2));                                           // 2. val region
            leak = leak || (ds == "S2-clip" && std::stod(r["t_s"]) >= testStart[r["orbit"]]);                       // 3. clip time
            leak = leak || (ds == "S2-window" && part.empty());                                                     // 4. flag
            if (ds == "S2-window" && part == "test") unseen[r["orbit"] + " fov " + r["fov_class"] + " " + r["profile"] + "_k" + r["epoch_k"]]++;
            if (leak && bad++ < 5) printf("  LEAK %s (%s, parts %d)\n", r["frame_id"].c_str(), part.c_str(), parts);
        }
        printf("%s: %s\n", set.c_str(), bad ? fmt("%d leaking frames", bad).c_str() : "no leaks");
        for (auto& [band, p] : bands) {
            printf("  %-10s", band.c_str());
            for (auto& [name, n] : p) printf("  %s %d", name.c_str(), n);
            printf("\n");
        }
        for (auto& [k, n] : unseen) printf("  unseen (test region) frames, %s: %d\n", k.c_str(), n);
        if (set.rfind("S2-windows", 0) == 0 && !bands.count("|lat| < 60")) printf("  warning: no window frames below 60 deg latitude\n");
        ok = ok && !bad;
    }
    fflush(stdout);
    return ok;
}

void writeManifest(const Settings& S, const Config& c, const std::string& version, const std::string& hash) {
    auto data = [](const std::string& path) {
        size_t files = 0, bytes = 0;
        std::filesystem::file_time_type newest{};
        auto add = [&](const std::filesystem::path& p) { files++, bytes += std::filesystem::file_size(p), newest = std::max(newest, std::filesystem::last_write_time(p)); };
        std::filesystem::path p = u8path(path);
        if (std::filesystem::is_directory(p)) {
            for (auto& e : std::filesystem::recursive_directory_iterator(p))
                if (e.is_regular_file()) add(e.path());
        } else if (std::filesystem::exists(p)) add(p);
        auto t = std::chrono::system_clock::to_time_t(std::chrono::clock_cast<std::chrono::system_clock>(newest));
        char when[32];
        std::strftime(when, sizeof when, "%Y-%m-%d", std::gmtime(&t));
        return fmt("{\"path\": \"%s\", \"files\": %zu, \"bytes\": %zu, \"newest\": \"%s\"}", path.c_str(), files, bytes, when);
    };
    std::string kernels, orbits, counts, seeds;
    for (auto& e : std::filesystem::directory_iterator(u8path(c.spiceDir))) kernels += (kernels.empty() ? "\"" : ", \"") + e.path().filename().string() + "\"";
    for (auto& o : S.orbits) {
        std::string first, fovs, steps;
        double seconds = 0;
        Orbit::span(o.file, first, seconds);
        for (size_t i = 0; i < o.fovs.size(); i++) fovs += fmt(i ? ", %d" : "%d", o.fovs[i]), steps += fmt(i ? ", %g" : "%g", o.steps[i]);
        orbits += fmt("%s\n    {\"name\": \"%s\", \"file\": \"%s\", \"frame\": \"MOON_ME\", \"first_utc\": \"%s\", \"seconds\": %.0f, \"fovs\": [%s], \"steps_s\": [%s]}",
                      orbits.empty() ? "" : ",", o.name.c_str(), o.file.c_str(), first.c_str(), seconds, fovs.c_str(), steps.c_str());
    }
    if (std::filesystem::exists(u8path(S.root + "/index")))
        for (auto& e : std::filesystem::directory_iterator(u8path(S.root + "/index"))) {
            if (e.path().stem() == "runs") continue;
            std::map<std::string, int> part;
            int n = 0, night = 0;
            for (auto& r : readCsv(e.path().string())) n++, night += r["night_shortcut"] == "1", part[r["partition"]]++;
            std::string p;
            for (auto& [k, v] : part) p += fmt("%s\"%s\": %d", p.empty() ? "" : ", ", k.c_str(), v);
            counts += fmt("%s\n    \"%s\": {\"frames\": %d, \"night_shortcut\": %d, \"partition\": {%s}}", counts.empty() ? "" : ",", e.path().stem().string().c_str(), n, night, p.c_str());
        }
    std::string regionsSeed = "null", regions = S.output + "/splits/regions.json";
    if (std::filesystem::exists(u8path(regions))) {
        std::string t = readText(regions);
        size_t a = t.find("\"seed\"");
        if (a != std::string::npos) regionsSeed = std::to_string(std::atoll(t.c_str() + t.find(':', a) + 1));
    }
    std::string json = "{\n" + fmt("  \"generator_version\": \"%s\",\n  \"config_hash\": \"%s\",\n  \"written_utc\": \"%s\",\n  \"sample\": %s,\n", version.c_str(),
                                   hash.c_str(), now().c_str(), S.sample ? "true" : "false") +
        "  \"data\": {\n    \"sldem\": " + data(c.sldemDir) + ",\n    \"polar_dem\": " + data(c.polarDir) + ",\n    \"lola_dem\": " + data(c.lolaDem) +
        ",\n    \"wac_643nm\": " + data(c.wac643) + ",\n    \"polar_color_kit\": " + data(c.kitColor) + ",\n    \"hapke_maps\": " + data(c.hapkeDir) +
        ",\n    \"crater_catalogue\": " + data(c.craterDb) + ",\n    \"catalog\": " + data(S.output + "/catalog/robbins_linna2.csv") +
        ",\n    \"regions\": " + data(regions) + "\n  },\n  \"spice_kernels\": [" + kernels + "],\n  \"orbits\": [" + orbits + "\n  ],\n" +
        fmt("  \"seeds\": {\"regions\": %s, \"s1\": [%llu, %llu, %llu], \"s2_clips\": %llu, \"s2_profile_b\": %llu, \"repro\": %llu},\n", regionsSeed.c_str(),
            S.s1Seed[0], S.s1Seed[1], S.s1Seed[2], S.clipSeed, S.profileSeed, S.reproSeed) +
        "  \"counts\": {" + counts + "\n  }\n}\n";
    writeText(S.root + "/MANIFEST.json", json);
    printf("MANIFEST.json written\n");
}

void repro(Generator& g, int n) {
    const Settings& S = g.set;
    if (!std::filesystem::exists(u8path(S.root + "/index"))) fatal("No frames in " + S.root + " to render again");
    std::vector<Shot> all;
    for (int sp = 0; sp < 3; sp++)
        for (auto& s : s1Shots(g, sp)) all.push_back(s);
    for (size_t o = 0; o < S.orbits.size(); o++) {
        for (auto& s : clipShots(g, o)) all.push_back(s);
        for (auto& s : windowShots(g, o)) all.push_back(s);
    }
    std::set<std::string> done;
    for (auto& e : std::filesystem::directory_iterator(u8path(S.root + "/index")))
        for (auto& r : readCsv(e.path().string())) done.insert(r["frame_id"]);
    std::erase_if(all, [&](const Shot& s) { return !done.count(s.id); });
    Rng rng(S.reproSeed);
    for (size_t i = 0; i < all.size() && i < size_t(n); i++) std::swap(all[i], all[i + size_t(rng() * double(all.size() - i))]);
    all.resize(std::min(all.size(), size_t(n)));
    std::string original = S.root, again = S.root + "/qc/repro";
    std::filesystem::remove_all(u8path(again));
    g.root = again;
    {
        Index idx(again + "/index.csv");
        for (auto& s : all) g.write(s, g.shoot(s), idx);
        g.drain();
    }
    g.root = original;
    std::string out = "frame_id,png_equal,json_equal,max_dn_diff\n";
    int same = 0;
    for (auto& s : all) {
        std::string a = original + "/" + s.path, b = again + "/" + s.path;
        bool png = readText(a + ".png") == readText(b + ".png"), json = readText(a + ".json") == readText(b + ".json");
        same += png && json;
        out += fmt("%s,%d,%d,%s\n", s.id.c_str(), int(png), int(json), png ? "0" : "");  // the DN difference of unequal PNGs is measured by qc.py
    }
    writeText(original + "/qc/repro.csv", out);
    printf("repro: %d of %zu frames bit-identical\n", same, all.size());
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: linna2 <config.ini> <catalog|anglecheck|truth|s1|clips|windows|check|manifest|repro> [--sample] [--bench n] [n]\n");
        return 2;
    }
    std::string cfgPath = std::filesystem::absolute(argv[1]).string(), stage = argv[2];
    bool sample = false;
    int n = 100, bench = 0;
    for (int i = 3; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--sample") sample = true;
        else if (a == "--bench" && i + 1 < argc) sample = true, bench = std::atoi(argv[++i]);
        else n = std::atoi(argv[i]);
    }
    useExeDir();
    std::filesystem::current_path(std::filesystem::current_path().parent_path());  // the MoonSimulation folder: shaders, assets and relative paths
    std::map<std::string, std::string> rest;
    Config cfg = loadConfig(cfgPath.c_str(), &rest);
    Settings set = loadSettings(rest);
    set.sample = sample;
    set.bench = bench;
    set.root = bench ? set.sampleOutput + "/bench" : sample ? set.sampleOutput : set.output;
    if (set.renderRes % set.imageRes) fatal("render_res must be a multiple of image_res");
    std::string version = gitVersion(), hash = "sha1:" + sha1(readText(cfgPath));
    printf("linna2 %s, %s, %s -> %s\n", stage.c_str(), version.c_str(), hash.c_str(), set.root.c_str());
    fflush(stdout);
    ephem::init(cfg.spiceDir);
    if (stage == "check") return leakageCheck(set) ? 0 : 1;
    if (stage == "manifest") return writeManifest(set, cfg, version, hash), 0;
    if (stage == "truth") return truthStage(set), 0;

    // an invisible window for the OpenGL 4.6 context
    if (!glfwInit()) fatal("GLFW could not start");
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* win = glfwCreateWindow(64, 64, "linna2", nullptr, nullptr);
    if (!win) fatal("No OpenGL 4.6 context");
    glfwMakeContextCurrent(win);
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) fatal("GLEW could not start");
    glGetError();
    glEnable(GL_DEBUG_OUTPUT);
    glDebugMessageCallback([](GLenum, GLenum type, GLuint, GLenum sev, GLsizei, const GLchar* msg, const void*) {
        if (type == GL_DEBUG_TYPE_ERROR || sev == GL_DEBUG_SEVERITY_HIGH) fprintf(stderr, "GL: %s\n", msg);
    }, nullptr);
    printf("loading the terrain\n"), fflush(stdout);
    {
        Generator g(set, cfg, version, hash);
        if (stage == "catalog") catalogStage(g);
        else {
            g.loadCatalogue();
            if (stage == "anglecheck") angleCheck(g);
            else {
                g.loadRegions();
                if (stage == "s1")
                    for (int sp = 0; sp < 3; sp++) runSet(g, std::string("S1-") + SPLITS[sp], s1Shots(g, sp));
                else if (stage == "clips")
                    for (size_t o = 0; o < set.orbits.size(); o++) runSet(g, "S2-clips-" + set.orbits[o].name, clipShots(g, o));
                else if (stage == "windows")
                    for (size_t o = 0; o < set.orbits.size(); o++) runSet(g, "S2-windows-" + set.orbits[o].name, windowShots(g, o));
                else if (stage == "repro") repro(g, n);
                else fatal("Unknown stage " + stage);
            }
        }
    }
    glfwTerminate();
    if (stage == "s1" || stage == "clips" || stage == "windows") return leakageCheck(set) ? 0 : 1;
    return 0;
}
