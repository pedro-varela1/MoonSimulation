#include "generator.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdarg>
#include <cstring>
#include <fstream>
#include "imagegen.h"
#include "platform.h"

const char* INDEX_HEADER =
    "frame_id,file,dataset,split,partition,parts,orbit,clip_id,window_split,profile,epoch_k,rev,fov_class,fov_deg,tilt_deg,azimuth_deg,roll_deg,"
    "q_w,q_x,q_y,q_z,r_me_x,r_me_y,r_me_z,t_s,utc,lat_ss_deg,lon_ss_deg,alt_terrain_km,alt_sphere_km,lat_b_deg,lon_b_deg,range_km,gsd_m,sun_el_b_deg,sun_az_b_deg,"
    "sun_el_max_deg,regime,night_shortcut,dark,lit_pixel_fraction,ev,n_craters,n_pos,n_ign,n_bg,seconds,wait_s,terrain_timeout,png_bytes,json_bytes";

namespace {

constexpr double HMAX = 11.0, LOD_FINE = 0.0592253 * 11.3137;  // relief shell top (km); pixel footprint (km) above which only LOLA is drawn
constexpr int REG_ROWS = 1800, REG_COLS = 3600;

double srgb(double v) { return v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1 / 2.4) - 0.055; }

std::string vec(const glm::dvec3& v, const char* f = "%.9f") { return "[" + fmt(f, v.x) + ", " + fmt(f, v.y) + ", " + fmt(f, v.z) + "]"; }
std::string latLonPair(const glm::dvec3& p) { glm::dvec2 l = latLon(p); return fmt("[%.6f, %.6f]", l.x, l.y); }
std::string num(double v, const char* f = "%.6f") { return std::isnan(v) ? "null" : fmt(f, v); }
std::string str(const std::string& s) { return s.empty() ? "null" : "\"" + s + "\""; }

}

std::string fmt(const char* f, ...) {
    va_list a, b;
    va_start(a, f);
    va_copy(b, a);
    std::string s(std::vsnprintf(nullptr, 0, f, a), '\0');
    std::vsnprintf(s.data(), s.size() + 1, f, b);
    va_end(a);
    va_end(b);
    return s;
}

std::filesystem::path u8path(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }

std::vector<glm::dvec3> footprint(const Pose& p, int n) {
    std::vector<glm::dvec3> g(size_t(n) * n, glm::dvec3(0));
    for (int j = 0; j < n; j++)
        for (int i = 0; i < n; i++) {
            glm::dvec3 d = p.ray(-1 + 2.0 * i / (n - 1), 1 - 2.0 * j / (n - 1));
            double t = sphereHit(p.eye, d, MOON_R);
            if (t > 0) g[j * n + i] = p.eye + t * d;
        }
    return g;
}

Index::Index(const std::string& path) {
    std::filesystem::create_directories(u8path(path).parent_path());
    std::string text;
    if (std::ifstream in{u8path(path), std::ios::binary}) text.assign(std::istreambuf_iterator<char>(in), {});
    for (size_t a = text.find('\n'), b; a != std::string::npos && (b = text.find('\n', a + 1)) != std::string::npos; a = b)
        ids.insert(text.substr(a + 1, text.find(',', a + 1) - a - 1));
    f = openFile(path, L"ab");
    if (!f) fatal("Could not write " + path);
    if (text.empty()) fprintf(f, "%s\n", INDEX_HEADER);
    else if (text.back() != '\n') fputc('\n', f);  // a line cut by a crash is dropped (its frame is not in ids)
    fflush(f);
}

Index::~Index() { fclose(f); }

void Index::add(const std::string& id, const std::string& line) {
    std::lock_guard lk(m);
    fputs(line.c_str(), f);
    fflush(f);
    ids.insert(id);
}

Generator::Generator(const Settings& s, const Config& c, const std::string& v, const std::string& h) : set(s), cfg(c), version(v), configHash(h), root(s.root) {
    cfg.sensor = {s.renderRes, s.renderRes};
    cfg.gray = true;
    cfg.earthshine = false;
    cfg.objFile.clear();
    const char* shown = "";
    terrain.load(cfg, 0, [&](const char* stage, float) {
        if (std::strcmp(stage, shown)) printf("  %s\n", shown = stage), fflush(stdout);
    });
    sldem = std::make_unique<Sldem>(cfg.sldemDir);
    polar = std::make_unique<PolarDem>(cfg.polarDir);
    if (!sldem->present || !polar->present) fatal("linna2 needs the high-resolution terrain (sldem_dir and polar_dir)");
    sldem->snap = polar->snap = true;
    renderer.init(cfg, terrain, *sldem, *polar, {});
    renderer.addDirectPass();
    for (int i = 0; i < s.writers; i++)
        pool.emplace_back([this] {
            for (;;) {
                std::function<void()> job;
                {
                    std::unique_lock lk(m);
                    cv.wait(lk, [&] { return closing || !jobs.empty(); });
                    if (jobs.empty()) return;
                    job = std::move(jobs.front());
                    jobs.pop_front();
                    busy++;
                }
                room.notify_all();
                job();
                { std::lock_guard lk(m); busy--; }
                room.notify_all();
            }
        });
}

Generator::~Generator() {
    { std::lock_guard lk(m); closing = true; }
    cv.notify_all();
    for (auto& t : pool) t.join();
}

void Generator::push(std::function<void()> job) {
    std::unique_lock lk(m);
    room.wait(lk, [&] { return jobs.size() < 2 * pool.size(); });
    jobs.push_back(std::move(job));
    cv.notify_one();
}

void Generator::drain() {
    std::unique_lock lk(m);
    room.wait(lk, [&] { return jobs.empty() && busy == 0; });
}

void Generator::loadCatalogue() {
    if (!cat.load(cfg.craterDb, {0, 1e9}, true)) fatal("Could not read the crater catalogue " + cfg.craterDb);
    rim.assign(cat.all().empty() ? 0 : cat.all().back().cid + 1, NAN);
    std::string path = set.output + "/catalog/robbins_linna2.csv";
    std::ifstream in(u8path(path));
    std::string line;
    if (!std::getline(in, line)) fatal("Missing " + path + ": run the catalog stage first");
    int cid = -1, erim = -1, c = 0;
    for (size_t a = 0, b; a <= line.size(); a = b + 1, c++) {
        std::string name = line.substr(a, (b = std::min(line.find(',', a), line.size())) - a);
        cid = name == "cid" ? c : cid, erim = name == "e_rim_km" ? c : erim;
    }
    while (std::getline(in, line)) {
        std::vector<double> v;
        for (size_t a = 0, b; a <= line.size(); a = b + 1) v.push_back(std::atof(line.c_str() + a)), b = std::min(line.find(',', a), line.size());
        if (size_t(std::max(cid, erim)) < v.size() && size_t(v[cid]) < rim.size()) rim[size_t(v[cid])] = float(v[erim]);
    }
}

// regions.py raster: uint8 1800 x 3600 (rows from -90 deg latitude, columns from -180 deg longitude), 0 train, 1 val, 2 test
void Generator::loadRegions() {
    std::string path = set.output + "/splits/regions_raster.npy";
    std::ifstream in(u8path(path), std::ios::binary);
    char head[10];
    if (!in.read(head, 10) || std::string(head + 1, 5) != "NUMPY") fatal("Missing " + path + ": run regions.py first");
    std::string dict(uint8_t(head[8]) | uint8_t(head[9]) << 8, '\0');
    in.read(dict.data(), dict.size());
    if (dict.find("'|u1'") == std::string::npos || dict.find("(1800, 3600)") == std::string::npos) fatal("Unexpected raster in " + path);
    regions.resize(size_t(REG_ROWS) * REG_COLS);
    if (!in.read(reinterpret_cast<char*>(regions.data()), regions.size())) fatal("Truncated " + path);
}

int Generator::partition(const glm::dvec3& p) const {
    glm::dvec2 l = latLon(p);
    int i = std::clamp(int((l.x + 90) / 0.1), 0, REG_ROWS - 1), j = std::clamp(int((l.y + 180) / 0.1), 0, REG_COLS - 1);
    return regions[size_t(i) * REG_COLS + j];
}

std::vector<double> Generator::lolaHeights(const std::vector<glm::dvec3>& units) {
    std::vector<glm::dvec3> pos(units.size()), sun(units.size(), glm::dvec3(0, 0, AU));
    for (size_t i = 0; i < units.size(); i++) pos[i] = MOON_R * glm::normalize(units[i]);
    std::vector<Probe> p = renderer.probeAll(pos, sun);
    std::vector<double> h(p.size());
    for (size_t i = 0; i < p.size(); i++) h[i] = -p[i].above;
    return h;
}

// a streamer that no path point needs is left inactive, so it adds nothing to the image either
bool Generator::stream(const glm::dvec3& centre, const std::vector<glm::dvec3>& path, double needKm, double& waited) {
    bool sOn = false, pOn = false;
    for (auto& p : path) sOn = sOn || std::abs(p.z) < 0.875, pOn = pOn || std::abs(p.z) > 0.819;  // |lat| below 61 deg, above 55 deg
    auto start = std::chrono::steady_clock::now();
    for (;;) {
        sldem->update(centre, path, needKm, sOn, 1);
        polar->update(centre, path, needKm, pOn, 1);
        waited = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if ((!sOn || sldem->missing == 0) && (!pOn || polar->missing == 0)) return true;
        if (waited > set.waitLimitS) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

Frame Generator::shoot(const Shot& s) {
    auto start = std::chrono::steady_clock::now();
    Frame f;
    auto done = [&] { f.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(); return f; };
    const Pose& P = s.pose;
    int N = set.imageRes, RR = set.renderRes;
    std::vector<glm::dvec3> g9 = footprint(P, 9);
    glm::dvec3 b = g9[40];
    f.night = std::none_of(g9.begin(), g9.end(), [&](const glm::dvec3& g) {
        return g != glm::dvec3(0) && glm::dot(glm::normalize(s.sun - g), glm::normalize(g)) >= std::sin(glm::radians(set.nightDeg));
    });
    if (f.night) {  // nothing rendered: black image, boresight range to the global terrain level
        f.image.assign(size_t(N) * N, 0);
        f.range = sphereHit(P.eye, P.ray(0, 0), MOON_R + lolaHeights({b})[0]);
        return done();
    }

    // terrain to stream: where pixels are fine enough for the high-resolution maps, and the terrain within the shadow reach toward the Sun
    constexpr int G = 17;
    std::vector<glm::dvec3> grid = footprint(P, G), path;
    double spacing = 0, th = P.tanHalf();
    for (int j = 0; j < G; j++)
        for (int i = 0; i < G; i++) {
            const glm::dvec3& g = grid[j * G + i];
            if (g == glm::dvec3(0)) continue;
            for (int k : {i + 1 < G ? j * G + i + 1 : -1, j + 1 < G ? (j + 1) * G + i : -1})
                if (k >= 0 && grid[k] != glm::dvec3(0)) spacing = std::max(spacing, glm::length(grid[k] - g));
            glm::dvec3 D = -P.att[2] + th * ((-1 + 2.0 * i / (G - 1)) * P.att[0] + (1 - 2.0 * j / (G - 1)) * P.att[1]), d = glm::normalize(D);
            auto across = [&](const glm::dvec3& a) { glm::dvec3 v = 2 * th / RR * a; return glm::length(v - d * glm::dot(d, v)) / glm::length(D); };
            double t = glm::length(P.eye) > MOON_R + HMAX ? sphereHit(P.eye, d, MOON_R + HMAX) : 0;  // nearest the terrain can be
            if (t * std::max(across(P.att[0]), across(P.att[1])) < LOD_FINE * 1.05) path.push_back(glm::normalize(g));
        }
    double needKm = 24 + 0.75 * spacing, step = std::min(30.0, needKm);
    glm::dvec3 sd = glm::normalize(s.sun);
    for (size_t k = 0, n = path.size(); k < n; k++) {
        glm::dvec3 p = path[k], h = sd - glm::dot(sd, p) * p;
        double el = std::asin(glm::dot(sd, p));
        if (el < glm::radians(-6.5) || glm::length(h) < 1e-9) continue;
        double reach = std::min(set.shadowReachKm, 20 / std::tan(std::max(el, glm::radians(0.1))));
        for (double km = step; km <= reach; km += step) path.push_back(along(p, glm::normalize(h), km));
    }
    f.timeout = !stream(glm::normalize(b), path, needKm, f.waited);

    Scene sc;
    sc.cam = {P.eye, P.att[0], P.att[1], -P.att[2], th, th};
    sc.view = sc.cam;
    sc.att = P.att;
    sc.sat = P.eye;
    sc.sun = s.sun;
    sc.earth = std::isnan(s.et) ? glm::dvec3(0, 0, 384400) : ephem::earth(s.et);  // earthshine is off
    sc.j2000ToMe = sc.earthToMe = glm::dmat3(1);
    sc.planets.fill({glm::dvec4(0, 0, 1, 0), 99, TSUN});
    sc.dt = 1;
    sc.cameraOnly = true;
    renderer.draw(sc, {1, 1});
    std::vector<float> L = renderer.cameraLinear(false);
    f.direct = renderer.cameraLinear(true);
    f.range = renderer.centerRange();

    // auto-exposure as the simulator's: log2 histogram of 128 bins over [-24, 2), the 98th percentile bin centre brought to 0.8
    std::array<uint32_t, 128> bins{};
    uint32_t total = 0, acc = 0, k = 0;
    for (float v : L)
        if (v > 0) bins[std::clamp(int((std::log2(v) + 24) * 128 / 26), 0, 127)]++, total++;
    for (; total && k < 127; k++)
        if ((acc += bins[k]) >= uint32_t(0.98f * float(total))) break;
    if (total) f.ev = std::log2(0.8) - ((k + 0.5) * 26 / 128 - 24);
    // linear 2 x 2 box to the image grid, then the exposure and the sRGB curve, 16 bits
    int r = RR / N;
    double gain = total ? std::exp2(f.ev) / (r * r) : 0;
    f.image.resize(size_t(N) * N);
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            double sum = 0;
            for (int a = 0; a < r; a++)
                for (int c = 0; c < r; c++) sum += L[size_t(RR - 1 - (y * r + a)) * RR + x * r + c];
            f.image[size_t(y) * N + x] = uint16_t(std::lround(65535 * srgb(std::clamp(sum * gain, 0.0, 1.0))));
        }
    return done();
}

// catalogue craters whose projected rim ellipse has its centre in the image, with their visibility, light and label class (04 §1-5)
std::string Generator::craters(const Shot& s, const Frame& f, const glm::dvec3& centre, double angle, bool dark, Counts& n) {
    const Pose& P = s.pose;
    int N = set.imageRes, RR = set.renderRes, M = set.rimSamples;
    double fN = P.focal(N), cN = (N - 1) / 2.0;
    glm::dmat3 K(fN, 0, 0, 0, fN, 0, cN, cN, 1), toCam = glm::transpose(P.cvToMe());
    // lit test: 3 x 3 maximum of the direct pass above lit_tau of its lit_percentile; nothing is lit when the pass is empty
    float peak = 0, tau = 0;
    if (!f.direct.empty()) {
        std::vector<float> d = f.direct;
        double x = set.litPercentile / 100 * double(d.size() - 1);
        size_t i = size_t(x);
        std::nth_element(d.begin(), d.begin() + i, d.end());
        float lo = d[i], hi = i + 1 < d.size() ? *std::min_element(d.begin() + i + 1, d.end()) : lo;
        tau = float(set.litTau * (lo + (hi - lo) * (x - double(i))));
        peak = *std::max_element(d.begin() + i, d.end());
    }
    auto lit = [&](glm::dvec2 px) {
        int x0 = int(std::lround(px.x)), y0 = int(std::lround(px.y));
        float m = 0;
        for (int y = std::max(y0 - 1, 0); y <= std::min(y0 + 1, RR - 1); y++)
            for (int x = std::max(x0 - 1, 0); x <= std::min(x0 + 1, RR - 1); x++) m = std::max(m, f.direct[size_t(RR - 1 - y) * RR + x]);
        return m > tau;
    };

    std::string out;
    for (const Crater* k : cat.within(centre, angle)) {
        float e = rim[k->cid];
        if (std::isnan(e)) continue;
        Enu E = enu(k->lat, k->lon);
        glm::dvec3 C = (MOON_R + e) * E.up;
        if (glm::dot(P.eye - C, C) <= 0) continue;  // far side of the Moon
        double a = k->major / 2, b = k->minor / 2, psi = glm::radians(k->angle), el[5];
        if (!imageEllipse(K * toCam * glm::dmat3(E.east, E.north, C - P.eye), a, b, psi, el)) continue;
        if (el[0] < -0.5 || el[0] > N - 0.5 || el[1] < -0.5 || el[1] > N - 0.5) continue;  // dropped: centre outside the image
        glm::dvec3 cc = P.camera(C);
        glm::dvec2 c3 = P.pixel(cc, N);
        int seen = 0, lights = 0;
        for (int j = 0; j < M; j++) {
            double t = 2 * PI * j / M, u = a * std::cos(t), v = b * std::sin(t);
            glm::dvec3 X = C + (u * std::cos(psi) - v * std::sin(psi)) * E.east + (u * std::sin(psi) + v * std::cos(psi)) * E.north, xc = P.camera(X);
            glm::dvec2 px = P.pixel(xc, RR);
            bool in = xc.z > 0 && px.x >= -0.5 && px.x <= RR - 0.5 && px.y >= -0.5 && px.y <= RR - 0.5;
            if (in && glm::dot(P.eye - X, glm::normalize(X)) > 0) seen++, lights += peak > 0 && lit(px);
        }
        double inFrame = double(seen) / M, litFrac = seen ? double(lights) / seen : 0, dEff = std::sqrt(k->major * k->minor);
        double emission = glm::degrees(std::acos(std::clamp(glm::dot(glm::normalize(P.eye - C), E.up), -1.0, 1.0)));
        double sunEl = glm::degrees(std::asin(std::clamp(glm::dot(glm::normalize(s.sun - C), E.up), -1.0, 1.0)));
        bool band = dEff >= set.dEffMinKm && 2 * std::sqrt(el[2] * el[3]) >= set.sizeMinPx && 2 * el[2] <= set.sizeMaxFrac * N;
        const char *label = "pos", *why = nullptr;
        if (litFrac < set.litBg) label = "bg";
        else if (litFrac < set.litPos) why = "lit";
        else if (inFrame < set.inFrameMin) why = "partial";
        else if (emission > set.emissionMaxDeg) why = "emission";
        else if (!band) why = "size";
        else if (dark) why = "dark";  // a frame the detector treats as dark (user decision)
        if (why) label = "ign";
        (*label == 'p' ? n.pos : *label == 'b' ? n.bg : n.ign)++;
        n.all++;
        out += fmt("%s\n    {\"cid\": %d, \"D_eff_km\": %.4f, \"ellipse_px\": [%.3f, %.3f, %.3f, %.3f, %.5f], \"center3d_px\": [%.3f, %.3f], "
                   "\"emission_deg\": %.3f, \"sun_elev_deg\": %.3f, \"lit_fraction\": %.4f, \"in_frame_fraction\": %.4f, \"label\": \"%s\", "
                   "\"ignore_reason\": %s}",
                   out.empty() ? "" : ",", k->cid, dEff, el[0], el[1], el[2], el[3], el[4], c3.x, c3.y, emission, sunEl, litFrac, inFrame, label,
                   why ? ("\"" + std::string(why) + "\"").c_str() : "null");
    }
    return out;
}

void Generator::write(const Shot& s, const Frame& f, Index& idx) {
    auto start = std::chrono::steady_clock::now();
    const Pose& P = s.pose;
    int N = set.imageRes;
    std::vector<glm::dvec3> g9 = footprint(P, 9);
    int parts = 0;
    double elMax = -90, edge = 0;
    glm::dvec3 b = g9[40], bu = glm::normalize(b);
    for (auto& g : g9)
        if (g != glm::dvec3(0)) {
            parts |= 1 << partition(g);
            elMax = std::max(elMax, glm::degrees(std::asin(glm::dot(glm::normalize(s.sun - g), glm::normalize(g)))));
            edge = std::max(edge, std::acos(std::clamp(glm::dot(glm::normalize(g), bu), -1.0, 1.0)));
        }
    const char* part = parts == 1 ? "train" : parts == 2 ? "val" : parts == 4 ? "test" : "straddle";
    glm::dvec3 ground = P.eye + f.range * P.ray(0, 0), ss = glm::normalize(P.eye), sd = glm::normalize(s.sun - P.eye);
    double width = glm::length(g9[36] - g9[44]), r = glm::length(P.eye), hTerrain = lolaHeights({ss})[0];
    glm::dvec2 sunB = sunAzEl(b, s.sun - b), lb = latLon(ground), lss = latLon(P.eye);
    const std::vector<double>& e = set.sunEdges;
    const char* regime = sunB.y < e[1] ? "night" : sunB.y < e[2] ? "terminator" : sunB.y < e[3] ? "low" : "high";
    double gsd = f.range / P.focal(N) * 1000, emission = glm::degrees(std::acos(std::clamp(glm::dot(glm::normalize(P.eye - ground), glm::normalize(ground)), -1.0, 1.0)));
    size_t lit = std::count_if(f.image.begin(), f.image.end(), [&](uint16_t v) { return v > set.darkLevel * 65535; });
    double litShare = double(lit) / f.image.size();
    Counts n;
    std::string list = craters(s, f, bu, edge + (0.2 * width + 2) / MOON_R, litShare < set.darkShare, n);
    glm::dvec4 q = quaternion(P.cvToMe());
    double fN = P.focal(N), cN = (N - 1) / 2.0;
    std::string name = s.path.substr(s.path.find_last_of('/') + 1);
    std::string window = s.windowSplit.empty() ? "null" : fmt("{\"split\": \"%s\", \"profile\": \"%s\", \"epoch_k\": %d, \"rev\": %d}", s.windowSplit.c_str(), s.profile.c_str(), s.epoch, s.rev);
    std::string json = "{\n" +
        fmt("  \"frame_id\": \"%s\",\n  \"generator_version\": \"%s\", \"config_hash\": \"%s\",\n", s.id.c_str(), version.c_str(), configHash.c_str()) +
        fmt("  \"dataset\": \"%s\",\n  \"split\": \"%s\",\n  \"footprint_partition\": \"%s\",\n", s.dataset.c_str(), s.split.c_str(), part) +
        "  \"orbit\": " + str(s.orbit) + ", \"clip_id\": " + str(s.clip) + ",\n  \"window\": " + window + ",\n" +
        "  \"time\": {\"utc\": " + str(s.utc) + ", \"et\": " + num(s.et, "%.6f") + ", \"sun_epoch_shift_days\": " + (std::isnan(s.et) ? "null" : fmt("%.6f", s.shift)) + "},\n" +
        "  \"spacecraft\": {\"r_me_km\": " + vec(P.eye, "%.6f") + fmt(", \"lat_deg\": %.6f, \"lon_deg\": %.6f, \"alt_terrain_km\": %.6f, \"alt_sphere_km\": %.6f},\n",
                                                                   lss.x, lss.y, r - MOON_R - hTerrain, r - MOON_R) +
        "  \"camera_true\": {\"q_cam_to_me\": " + fmt("[%.12f, %.12f, %.12f, %.12f]", q.x, q.y, q.z, q.w) +
        fmt(", \"K512\": [[%.6f, 0, %.1f], [0, %.6f, %.1f], [0, 0, 1]],\n", fN, cN, fN, cN) +
        fmt("                  \"fov_nominal_deg\": %d, \"fov_deg\": %.6f, \"tilt_deg\": %.6f, \"azimuth_deg\": %.6f, \"azimuth_ref\": \"%s\", \"roll_deg\": %.6f,\n",
            s.fovClass, P.fov, s.tilt, s.azimuth, s.lvlh ? "lvlh" : "north", s.roll) +
        fmt("                  \"render_res\": %d, \"distortion\": [0, 0]},\n", set.renderRes) +
        fmt("  \"boresight\": {\"range_km\": %.6f, \"gsd_center_m\": %.4f, \"ground_lat_deg\": %.6f, \"ground_lon_deg\": %.6f, \"emission_deg\": %.4f},\n",
            f.range, gsd, lb.x, lb.y, emission) +
        "  \"footprint\": {\"corners_latlon_deg\": [" + latLonPair(g9[0]) + ", " + latLonPair(g9[8]) + ", " + latLonPair(g9[80]) + ", " + latLonPair(g9[72]) +
        "],\n                \"edge_mid_latlon_deg\": [" + latLonPair(g9[4]) + ", " + latLonPair(g9[44]) + ", " + latLonPair(g9[76]) + ", " + latLonPair(g9[36]) + "]},\n" +
        "  \"sun\": {\"dir_me\": " + vec(sd) + ", \"dir_cam_true\": " + vec(glm::transpose(P.cvToMe()) * sd) + ",\n" +
        fmt("          \"elev_boresight_deg\": %.6f, \"azim_boresight_deg\": %.6f, \"elev_max_footprint_deg\": %.6f, \"regime\": \"%s\", \"night_cause\": %s},\n",
            sunB.y, sunB.x, elMax, regime, *regime != 'n' ? "null" : b.x > 0 ? "\"nearside\"" : "\"farside\"") +
        fmt("  \"image\": {\"file\": \"%s.png\", \"dark\": %s, \"lit_pixel_fraction\": %.6f, \"night_shortcut\": %s, \"lit_method\": \"pass\", \"exposure_ev\": %s},\n",
            name.c_str(), litShare < set.darkShare ? "true" : "false", litShare, f.night ? "true" : "false", num(f.ev).c_str()) +
        "  \"craters\": [" + list + (list.empty() ? "]\n}\n" : "\n  ]\n}\n");

    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::string row = fmt("%s,%s,%s,%s,%s,%d,%s,%s,%s,%s,%d,%d,%d,%.6f,%.6f,%.6f,%.6f,%.12f,%.12f,%.12f,%.12f,%.6f,%.6f,%.6f,%s,%s,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.4f,%.6f,%.6f,%.6f,%s,%d,%d,%.6f,%s,%d,%d,%d,%d,",
        s.id.c_str(), s.path.c_str(), s.dataset.c_str(), s.split.c_str(), part, parts, s.orbit.c_str(), s.clip.c_str(), s.windowSplit.c_str(), s.profile.c_str(),
        s.epoch, s.rev, s.fovClass, P.fov, s.tilt, s.azimuth, s.roll, q.x, q.y, q.z, q.w, P.eye.x, P.eye.y, P.eye.z,
        num(s.t, "%.3f").c_str(), s.utc.c_str(), lss.x, lss.y, r - MOON_R - hTerrain, r - MOON_R,
        lb.x, lb.y, f.range, gsd, sunB.y, sunB.x, elMax, regime, int(f.night), int(litShare < set.darkShare), litShare, num(f.ev).c_str(), n.all, n.pos, n.ign, n.bg);
    std::string base = root + "/" + s.path;
    push([this, &idx, base, json, row, id = s.id, image = f.image, extra = fmt("%.4f,%.3f,%d", seconds + f.seconds, f.waited, int(f.timeout))] {
        std::filesystem::create_directories(u8path(base).parent_path());
        if (!writePng(base + ".png", set.imageRes, set.imageRes, 1, 16, image.data(), false) || !writeFile(base + ".json", json))
            fatal("Could not write " + base);
        idx.add(id, row + extra + fmt(",%llu,%zu\n", (unsigned long long)std::filesystem::file_size(u8path(base + ".png")), json.size()));
    });
}
