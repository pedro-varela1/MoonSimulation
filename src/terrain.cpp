#include "terrain.h"
#include <tiffio.h>
#include <zlib.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>

namespace {

constexpr float HSCALE = 12, IF_SCALE = 0.25f;
constexpr double R = 1737.4;

TIFF* openTiff(const std::string& path) {
    TIFF* t = TIFFOpen(path.c_str(), "r");
    if (!t) { fprintf(stderr, "nao foi possivel abrir %s\n", path.c_str()); exit(1); }
    return t;
}

GLuint texture2D(GLenum format, int w, int h, int levels, GLenum wrapT) {
    GLuint t;
    glCreateTextures(GL_TEXTURE_2D, 1, &t);
    glTextureStorage2D(t, levels, format, w, h);
    glTextureParameteri(t, GL_TEXTURE_MIN_FILTER, levels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTextureParameteri(t, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(t, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(t, GL_TEXTURE_WRAP_T, wrapT);
    return t;
}

int levels(int w, int h) { return 1 + int(std::log2(std::max(w, h))); }
int16_t snorm(float km) { return int16_t(std::lround(std::clamp(km / HSCALE, -1.f, 1.f) * 32767)); }

GLuint loadLola(const std::string& path) {
    TIFF* tf = openTiff(path);
    uint32_t w, h;
    TIFFGetField(tf, TIFFTAG_IMAGEWIDTH, &w);
    TIFFGetField(tf, TIFFTAG_IMAGELENGTH, &h);
    GLuint tex = texture2D(GL_R16_SNORM, w, h, levels(w, h), GL_CLAMP_TO_EDGE);
    glTextureParameterf(tex, GL_TEXTURE_MAX_ANISOTROPY, 16);
    std::vector<float> row(w);
    std::vector<int16_t> band(size_t(w) * 512);
    for (uint32_t y = 0; y < h; y += 512) {
        uint32_t n = std::min(512u, h - y);
        for (uint32_t r = 0; r < n; r++) {
            TIFFReadScanline(tf, row.data(), y + r);
            std::transform(row.begin(), row.end(), &band[size_t(r) * w], snorm);
        }
        glTextureSubImage2D(tex, 0, 0, y, w, n, GL_RED, GL_SHORT, band.data());
    }
    TIFFClose(tf);
    glGenerateTextureMipmap(tex);
    return tex;
}

GLuint loadAlbedo(const Config& c) {
    constexpr int W = 27360, H = 13680, CAP = 1520, BLEND = 152, TW = 6840, CHUNK = 256;
    GLuint tex = texture2D(GL_RGB10_A2, W, H, levels(W, H), GL_CLAMP_TO_EDGE);
    glTextureParameterf(tex, GL_TEXTURE_MAX_ANISOTROPY, 16);
    float lut[256];
    for (int i = 0; i < 256; i++) { float v = i / 255.f; lut[i] = v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); }
    auto pack = [](glm::vec3 v) { glm::uvec3 q(glm::clamp(v / IF_SCALE, 0.f, 1.f) * 1023.f + 0.5f); return q.r | q.g << 10 | q.b << 20 | 3u << 30; };
    std::vector<uint32_t> buf(size_t(W) * CHUNK);
    std::vector<glm::vec3> blend(size_t(W) * BLEND * 2);
    auto upload = [&](int x, int y, int w, int n) { glTextureSubImage2D(tex, 0, x, y, w, n, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, buf.data()); };
    auto blendRow = [&](int y) { return y < CAP + BLEND ? &blend[size_t(y - CAP) * W] : &blend[size_t(y - (H - CAP - 2 * BLEND)) * W]; };

    TIFF* kit = openTiff(c.kitColor);
    uint16_t spp = 1;
    TIFFGetFieldDefaulted(kit, TIFFTAG_SAMPLESPERPIXEL, &spp);
    std::vector<uint8_t> line(TIFFScanlineSize(kit));
    auto kitIF = [&](int x) { const uint8_t* s = &line[size_t(x) * spp]; return c.kitGain * ((lut[s[0]] + lut[s[1]] + lut[s[2]]) / 3) + c.kitOffset; };
    for (int y = 0, n = 0; y < H; y++) {
        TIFFReadScanline(kit, line.data(), y);
        if (y < CAP || y >= H - CAP) {
            for (int x = 0; x < W; x++) buf[size_t(n) * W + x] = pack(kitIF(x));
            if (++n == CHUNK || y == CAP - 1 || y == H - 1) upload(0, y + 1 - n, W, n), n = 0;
        } else if (y < CAP + BLEND || y >= H - CAP - BLEND)
            for (int x = 0; x < W; x++) blendRow(y)[x] = kitIF(x);
    }
    TIFFClose(kit);

    const std::string* dirs[3] = {&c.wac643, &c.wac566, &c.wac415};
    const char* bands[3] = {"643", "566", "415"};
    std::vector<float> in(size_t(TW) * CHUNK * 3);
    for (int t = 0; t < 8; t++) {
        bool north = t < 4;
        int x0 = (t % 4 * TW + W / 2) % W, ky0 = north ? CAP : H / 2;
        std::ifstream f[3];
        for (int b = 0; b < 3; b++) {
            char name[64];
            snprintf(name, sizeof name, "/WAC_HAPKE_%sNM_E350%c%04d.IMG", bands[b], north ? 'N' : 'S', 450 + t % 4 * 900);
            f[b].open(*dirs[b] + name, std::ios::binary | std::ios::ate);
            if (!f[b]) { fprintf(stderr, "nao foi possivel abrir %s%s\n", dirs[b]->c_str(), name); exit(1); }
            f[b].seekg(size_t(f[b].tellg()) - size_t(TW) * 4 * 5320);
        }
        for (int y = 0; y < 5320; y += CHUNK) {
            int n = std::min(CHUNK, 5320 - y);
            for (int b = 0; b < 3; b++) f[b].read(reinterpret_cast<char*>(&in[size_t(b) * TW * CHUNK]), size_t(TW) * 4 * n);
            for (int r = 0; r < n; r++) {
                int ky = ky0 + y + r;
                const glm::vec3* kb = ky < CAP + BLEND || ky >= H - CAP - BLEND ? blendRow(ky) : nullptr;
                float s = std::clamp((70 - std::abs(90 - (ky + 0.5f) / 76)) / 2, 0.f, 1.f), wgt = s * s * (3 - 2 * s);
                glm::vec3 last(0.04f);
                for (int k = 0; k < TW; k++) {
                    size_t i = size_t(r) * TW + k, B = size_t(TW) * CHUNK;
                    glm::vec3 v(in[i], in[B + i], in[2 * B + i]);
                    if (v.r > 0 && v.g > 0 && v.b > 0) last = v;
                    buf[i] = pack(kb ? glm::mix(kb[x0 + k], last, wgt) : last);
                }
            }
            upload(x0, ky0 + y, TW, n);
        }
    }
    glGenerateTextureMipmap(tex);
    return tex;
}

GLuint loadHapke(const std::string& dir) {
    constexpr int N = 360 * 140, PARAM[5] = {0, 1, 2, 5, 6};
    const char* bands[3] = {"643NM", "566NM", "415NM"};
    std::vector<float> m[3];
    for (int b = 0; b < 3; b++) {
        std::string path = dir + "/WAC_HAPKEPARAMMAP_" + bands[b] + ".IMG";
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f) { fprintf(stderr, "nao foi possivel abrir %s\n", path.c_str()); exit(1); }
        m[b].resize(9 * N);
        f.seekg(size_t(f.tellg()) - m[b].size() * 4).read(reinterpret_cast<char*>(m[b].data()), m[b].size() * 4);
    }
    GLuint tex;
    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &tex);
    glTextureStorage3D(tex, 1, GL_RGBA32F, 360, 142, 5);
    glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    std::vector<glm::vec4> layer(N + 720);
    for (int l = 0; l < 5; l++) {
        glm::vec4 cap[2]{};
        for (int i = 0; i < N; i++) {
            glm::vec4 v = glm::max(glm::vec4(m[0][PARAM[l] * N + i], m[1][PARAM[l] * N + i], m[2][PARAM[l] * N + i], 0), l == 4 ? 1e-3f : -1e9f);
            layer[360 + i] = v;
            if (i < 360 || i >= N - 360) cap[i >= 360] += v / 360.f;
        }
        std::fill_n(layer.begin(), 360, cap[0]);
        std::fill_n(layer.end() - 360, 360, cap[1]);
        glTextureSubImage3D(tex, 0, 0, 0, l, 360, 142, 1, GL_RGBA, GL_FLOAT, layer.data());
    }
    return tex;
}

}

void Terrain::load(const Config& c) {
    printf("carregando LOLA...\n");
    fflush(stdout);
    lola = loadLola(c.lolaDem);
    printf("carregando albedo (kit + WAC 643/566/415 nm)...\n");
    fflush(stdout);
    albedo = loadAlbedo(c);
    hapke = loadHapke(c.hapkeDir);
}

Sldem::Sldem(const std::string& d) : dir(d) {
    slot.fill(-1);
    tex = texture2D(GL_R16_SNORM, 16384, 8192, 4, GL_REPEAT);
    cover = texture2D(GL_R8, 64, 32, 1, GL_REPEAT);
    threads.emplace_back(&Sldem::reader, this);
    for (int i = 0; i < 2; i++) threads.emplace_back(&Sldem::decoder, this);
}

Sldem::~Sldem() {
    { std::lock_guard lk(m); running = false; }
    readCv.notify_all();
    decodeCv.notify_all();
    for (auto& t : threads) t.join();
}

void Sldem::update(const glm::dvec3& center, const std::vector<glm::dvec3>& path, double needKm, bool on, double dt) {
    auto key = [](int tu, int ty) { return (long long)tu * 1024 + ty; };
    auto slotOf = [](int tu, int ty) { return (ty & 31) * 64 + (tu & 63); };
    double lat = glm::degrees(std::asin(center.z / glm::length(center))), lon = glm::degrees(std::atan2(center.y, center.x));
    int cx = int(std::floor((lon < 0 ? lon + 360 : lon) * 2)) - 32, cy = std::clamp(int(std::floor((60 - lat) * 2)) - 16, 0, 208);
    int dx = ((cx - x0) % 720 + 1080) % 720 - 360;
    if (!placed || std::abs(x0 + dx) > 1440) { x0 = cx; y0 = cy; placed = true; slot.fill(-1); }
    else if (std::abs(dx) > 4 || std::abs(cy - y0) > 4) { x0 += dx; y0 = cy; }
    active = on;

    std::vector<Tile> got;
    {
        std::lock_guard lk(m);
        size_t n = std::min<size_t>(ready.size(), 32);
        got.assign(std::make_move_iterator(ready.begin()), std::make_move_iterator(ready.begin() + n));
        ready.erase(ready.begin(), ready.begin() + n);
        for (auto& t : got) inflight.erase(t.id);
    }
    for (auto& t : got) {
        int tx = t.id % 720, ty = t.id / 720, tu = x0 + ((tx - x0) % 720 + 720) % 720;
        if (!t.valid) { dead.insert(t.id); continue; }
        if (tu >= x0 + 64 || ty < y0 || ty >= y0 + 32) continue;
        for (int k = 0, off = 0; k < 4; off += 65536 >> 2 * k, k++)
            glTextureSubImage2D(tex, k, (tu & 63) * (256 >> k), (ty & 31) * (256 >> k), 256 >> k, 256 >> k, GL_RED, GL_SHORT, t.data.data() + off);
        slot[slotOf(tu, ty)] = key(tu, ty);
        fade[slotOf(tu, ty)] = 0;
    }

    auto has = [&](int tu, int ty) { return tu >= x0 && tu < x0 + 64 && ty >= y0 && ty < y0 + 32 && slot[slotOf(tu, ty)] == key(tu, ty); };
    std::array<uint8_t, 64 * 32> cov{};
    std::vector<std::pair<double, uint32_t>> order;
    double cosNeed = std::cos(needKm / R);
    resident = 0;
    for (int ty = y0; ty < y0 + 32; ty++)
        for (int tu = x0; tu < x0 + 64; tu++) {
            int s = slotOf(tu, ty);
            if (has(tu, ty)) {
                bool full = true;
                for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) full = full && has(tu + i, ty + j);
                fade[s] = full ? std::min(fade[s] + float(dt) / 0.5f, 1.f) : 0.f;
                cov[s] = uint8_t(fade[s] * 255);
                resident++;
                continue;
            }
            fade[s] = 0;
            uint32_t id = ty * 720 + (tu % 720 + 720) % 720;
            if (dead.count(id)) continue;
            double la = glm::radians(60 - (ty + 0.5) * 0.5), lo = glm::radians((tu + 0.5) * 0.5);
            glm::dvec3 p(std::cos(la) * std::cos(lo), std::cos(la) * std::sin(lo), std::sin(la));
            size_t k = 0;
            while (k < path.size() && glm::dot(p, path[k]) < cosNeed) k++;
            order.push_back({k * 10.0 + std::acos(std::clamp(glm::dot(p, path[0]), -1.0, 1.0)), id});
        }
    glTextureSubImage2D(cover, 0, 0, 0, 64, 32, GL_RED, GL_UNSIGNED_BYTE, cov.data());
    std::sort(order.begin(), order.end());
    {
        std::lock_guard lk(m);
        need.clear();
        if (on) for (auto& o : order) need.push_back(o.second);
        pending = int(inflight.size());
    }
    readCv.notify_one();
}

void Sldem::reader() {
    static const char* rows[4] = {"30n_60n", "00n_30n", "30s_00s", "60s_30s"};
    std::ifstream files[32];
    std::vector<uint64_t> off[32], cnt[32];
    std::unique_lock lk(m);
    while (running) {
        auto it = std::find_if(need.begin(), need.end(), [&](uint32_t id) { return !inflight.count(id); });
        if (it == need.end() || inflight.size() >= 256) { readCv.wait(lk); continue; }
        int tx = *it % 720, ty = *it / 720, lo = tx, hi = tx;
        std::unordered_set<uint32_t> want(need.begin(), need.end());
        auto ok = [&](int x) { uint32_t id = ty * 720 + x; return x / 90 == tx / 90 && want.count(id) && !inflight.count(id); };
        while (hi - lo < 31 && lo > 0 && ok(lo - 1)) lo--;
        while (hi - lo < 31 && hi < 719 && ok(hi + 1)) hi++;
        for (int x = lo; x <= hi; x++) inflight.insert(ty * 720 + x);
        lk.unlock();
        int fi = ty / 60 * 8 + tx / 90;
        if (!files[fi].is_open()) {
            char name[64];
            snprintf(name, sizeof name, "/sldem2015_512_%s_%03d_%03d_float.tif", rows[ty / 60], tx / 90 * 45, tx / 90 * 45 + 45);
            TIFF* t = openTiff(dir + name);
            uint64_t *o, *c;
            TIFFGetField(t, TIFFTAG_TILEOFFSETS, &o);
            TIFFGetField(t, TIFFTAG_TILEBYTECOUNTS, &c);
            off[fi].assign(o, o + 5400);
            cnt[fi].assign(c, c + 5400);
            TIFFClose(t);
            files[fi].open(dir + name, std::ios::binary);
        }
        size_t a = ty % 60 * 90 + lo % 90, b = a + hi - lo;
        auto buf = std::make_shared<std::vector<char>>(off[fi][b] + cnt[fi][b] - off[fi][a]);
        files[fi].seekg(off[fi][a]).read(buf->data(), buf->size());
        lk.lock();
        for (int x = lo; x <= hi; x++)
            jobs.push_back({uint32_t(ty * 720 + x), buf, size_t(off[fi][a + x - lo] - off[fi][a]), size_t(cnt[fi][a + x - lo])});
        decodeCv.notify_all();
    }
}

void Sldem::decoder() {
    std::vector<float> raw(65536);
    std::unique_lock lk(m);
    while (running) {
        if (jobs.empty()) { decodeCv.wait(lk); continue; }
        Job j = std::move(jobs.front());
        jobs.pop_front();
        lk.unlock();
        Tile t{j.id, std::vector<int16_t>(87040), false};
        uLongf n = 65536 * 4;
        if (uncompress(reinterpret_cast<Bytef*>(raw.data()), &n, reinterpret_cast<const Bytef*>(j.buf->data() + j.at), j.size) == Z_OK && n == 65536 * 4) {
            double sum = 0;
            int valid = 0;
            for (float h : raw) if (h > -1e30f) sum += h, valid++;
            t.valid = valid > 32768;
            float mean = valid ? float(sum / valid) : 0;
            for (int i = 0; i < 65536; i++) t.data[i] = snorm(raw[i] > -1e30f ? raw[i] : mean);
            int16_t* s = t.data.data();
            for (int w = 128; w >= 32; s += 4 * w * w, w /= 2)
                for (int y = 0; y < w; y++)
                    for (int x = 0; x < w; x++) {
                        const int16_t* q = s + 4 * w * y + 2 * x;
                        s[4 * w * w + w * y + x] = int16_t((q[0] + q[1] + q[2 * w] + q[2 * w + 1] + 2) >> 2);
                    }
        }
        lk.lock();
        ready.push_back(std::move(t));
    }
}
