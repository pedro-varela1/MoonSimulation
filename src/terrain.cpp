#include "terrain.h"
#include <tiffio.h>
#include <zlib.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>

namespace {

constexpr float HSCALE = 12, IF_SCALE = 0.25f;
constexpr double R = 1737.4, PC = 15520, PS = 0.06;
constexpr int PN = 31040;

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

using Step = std::function<void(float)>;

int levels(int w, int h) { return 1 + int(std::log2(std::max(w, h))); }
int16_t snorm(float km) { return int16_t(std::lround(std::clamp(km / HSCALE, -1.f, 1.f) * 32767)); }

GLuint loadLola(const std::string& path, const Step& step) {
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
        step(float(y) / h);
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

GLuint loadAlbedo(const Config& c, const Step& step) {
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
        if (y % CHUNK == 0) step(0.2f * y / H);
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
            step(0.2f + 0.8f * (t * 5320 + y) / (8 * 5320));
            for (int b = 0; b < 3; b++) f[b].read(reinterpret_cast<char*>(&in[size_t(b) * TW * CHUNK]), size_t(TW) * 4 * n);
            for (int r = 0; r < n; r++) {
                int ky = ky0 + y + r;
                const glm::vec3* kb = ky < CAP + BLEND || ky >= H - CAP - BLEND ? blendRow(ky) : nullptr;
                float s = std::clamp((70 - std::abs(90 - (ky + 0.5f) / 76)) / 2, 0.f, 1.f), wgt = s * s * (3 - 2 * s);
                glm::vec3 last(0.04f);
                for (int k = 0; k < TW; k++) {
                    size_t i = size_t(r) * TW + k, B = size_t(TW) * CHUNK;
                    glm::vec3 v(in[i], in[B + i], in[2 * B + i]);
                    if (v.r > 0 && std::abs(v.g / v.r - 0.875f) < 0.15f && std::abs(v.b / v.r - 0.6f) < 0.15f) last = v;
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

GLuint loadEarth(const std::string& path, const Step& step) {
    TIFF* tf = openTiff(path);
    uint32_t w, h;
    TIFFGetField(tf, TIFFTAG_IMAGEWIDTH, &w);
    TIFFGetField(tf, TIFFTAG_IMAGELENGTH, &h);
    GLuint tex = texture2D(GL_SRGB8, w, h, levels(w, h), GL_CLAMP_TO_EDGE);
    glTextureParameterf(tex, GL_TEXTURE_MAX_ANISOTROPY, 16);
    std::vector<uint8_t> band(size_t(w) * 3 * 256);
    for (uint32_t y = 0; y < h; y += 256) {
        uint32_t n = std::min(256u, h - y);
        step(float(y) / h);
        for (uint32_t r = 0; r < n; r++) TIFFReadScanline(tf, &band[size_t(r) * w * 3], y + r);
        glTextureSubImage2D(tex, 0, 0, y, w, n, GL_RGB, GL_UNSIGNED_BYTE, band.data());
    }
    TIFFClose(tf);
    glGenerateTextureMipmap(tex);
    return tex;
}

}

void Terrain::load(const Config& c, const std::function<void(const char*, float)>& progress) {
    TIFFSetWarningHandler(nullptr);
    auto stage = [&](const char* name, float a, float b) { return [=, &progress](float f) { progress(name, a + (b - a) * f); }; };
    lola = loadLola(c.lolaDem, stage("Global relief (LOLA)", 0, 0.08f));
    albedo = loadAlbedo(c, stage("Multispectral albedo (WAC 643/566/415 nm)", 0.08f, 0.94f));
    hapke = loadHapke(c.hapkeDir);
    earth = loadEarth(c.earthTexture, stage("Earth texture (Blue Marble)", 0.94f, 1));
}

Streamer::Streamer(int nx, int ny, int wx, int wy, bool w)
    : NX(nx), NY(ny), WX(wx), WY(wy), wrap(w), slot(size_t(wx) * wy, -1), fade(size_t(wx) * wy) {
    tex = texture2D(GL_R16_SNORM, WX * 256, WY * 256, 4, GL_REPEAT);
    cover = texture2D(GL_R8, WX, WY, 1, GL_REPEAT);
}

void Streamer::start() {
    threads.emplace_back(&Streamer::reader, this);
    for (int i = 0; i < 2; i++) threads.emplace_back(&Streamer::decoder, this);
}

void Streamer::stop() {
    { std::lock_guard lk(m); running = false; }
    readCv.notify_all();
    decodeCv.notify_all();
    for (auto& t : threads) t.join();
}

void Streamer::update(const glm::dvec3& center, const std::vector<glm::dvec3>& path, double needKm, bool on, double dt) {
    auto key = [](int tag, int tu, int ty) { return ((long long)tu * 4096 + ty) * 2 + tag; };
    auto slotOf = [&](int tu, int ty) { return (ty % WY + WY) % WY * WX + (tu % WX + WX) % WX; };
    auto global = [&](int tu) { return wrap ? (tu % NX + NX) % NX : tu; };
    int cx, cy, ct;
    if (place(center, cx, cy, ct)) {
        int dx = wrap ? ((cx - x0) % NX + NX * 3 / 2) % NX - NX / 2 : cx - x0;
        if (!placed || ct != tag || std::abs(x0 + dx) > 2 * NX) { x0 = cx; y0 = cy; tag = ct; placed = true; std::fill(slot.begin(), slot.end(), -1); }
        else if (std::abs(dx) > 4 || std::abs(cy - y0) > 4) { x0 += dx; y0 = cy; }
    }
    active = on && placed;

    std::vector<Tile> got;
    {
        std::lock_guard lk(m);
        size_t n = std::min<size_t>(ready.size(), 32);
        got.assign(std::make_move_iterator(ready.begin()), std::make_move_iterator(ready.begin() + n));
        ready.erase(ready.begin(), ready.begin() + n);
        for (auto& t : got) inflight.erase(t.id);
    }
    for (auto& t : got) {
        int tx = t.id % NX, ty = t.id / NX % NY, tu = wrap ? x0 + ((tx - x0) % NX + NX) % NX : tx;
        if (!t.valid) { dead.insert(t.id); continue; }
        if (int(t.id / (NX * NY)) != tag || tu < x0 || tu >= x0 + WX || ty < y0 || ty >= y0 + WY) continue;
        int s = slotOf(tu, ty);
        for (int k = 0, off = 0; k < 4; off += 65536 >> 2 * k, k++)
            glTextureSubImage2D(tex, k, s % WX * (256 >> k), s / WX * (256 >> k), 256 >> k, 256 >> k, GL_RED, GL_SHORT, t.data.data() + off);
        slot[s] = key(tag, tu, ty);
        fade[s] = 0;
    }

    auto has = [&](int tu, int ty) { return tu >= x0 && tu < x0 + WX && ty >= y0 && ty < y0 + WY && slot[slotOf(tu, ty)] == key(tag, tu, ty); };
    std::vector<uint8_t> cov(size_t(WX) * WY);
    std::vector<std::pair<double, uint32_t>> order;
    double cosNeed = std::cos(needKm / R);
    resident = 0;
    for (int ty = y0; ty < y0 + WY; ty++)
        for (int tu = x0; tu < x0 + WX; tu++) {
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
            uint32_t i = id(tag, global(tu), ty);
            if (dead.count(i)) continue;
            glm::dvec3 p = tileCenter(tag, global(tu), ty);
            size_t k = 0;
            while (k < path.size() && glm::dot(p, path[k]) < cosNeed) k++;
            order.push_back({k * 10.0 + std::acos(std::clamp(glm::dot(p, path[0]), -1.0, 1.0)), i});
        }
    glTextureSubImage2D(cover, 0, 0, 0, WX, WY, GL_RED, GL_UNSIGNED_BYTE, cov.data());
    std::sort(order.begin(), order.end());
    {
        std::lock_guard lk(m);
        need.clear();
        if (active) for (auto& o : order) need.push_back(o.second);
        pending = int(inflight.size());
    }
    readCv.notify_one();
}

void Streamer::reader() {
    std::unique_lock lk(m);
    while (running) {
        auto it = std::find_if(need.begin(), need.end(), [&](uint32_t i) { return !inflight.count(i); });
        if (it == need.end() || inflight.size() >= 256) { readCv.wait(lk); continue; }
        int tx = *it % NX, ty = *it / NX % NY, tg = *it / (NX * NY), lo = tx, hi = tx;
        std::unordered_set<uint32_t> want(need.begin(), need.end());
        auto ok = [&](int x) { return runKey(x) == runKey(tx) && want.count(id(tg, x, ty)) && !inflight.count(id(tg, x, ty)); };
        while (hi - lo < 31 && lo > 0 && ok(lo - 1)) lo--;
        while (hi - lo < 31 && hi < NX - 1 && ok(hi + 1)) hi++;
        for (int x = lo; x <= hi; x++) inflight.insert(id(tg, x, ty));
        lk.unlock();
        std::vector<Job> out;
        read(tg, ty, lo, hi, out);
        lk.lock();
        jobs.insert(jobs.end(), out.begin(), out.end());
        decodeCv.notify_all();
    }
}

void Streamer::decoder() {
    std::vector<float> km(65536);
    std::unique_lock lk(m);
    while (running) {
        if (jobs.empty()) { decodeCv.wait(lk); continue; }
        Job j = std::move(jobs.front());
        jobs.pop_front();
        lk.unlock();
        Tile t{j.id, std::vector<int16_t>(87040), decode(j, km.data())};
        if (t.valid) {
            std::transform(km.begin(), km.end(), t.data.begin(), snorm);
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

Sldem::Sldem(const std::string& d) : Streamer(720, 240, 64, 32, true), dir(d) { start(); }

bool Sldem::place(const glm::dvec3& c, int& x, int& y, int& tag) const {
    double lat = glm::degrees(std::asin(c.z / glm::length(c))), lon = glm::degrees(std::atan2(c.y, c.x));
    x = int(std::floor((lon < 0 ? lon + 360 : lon) * 2)) - WX / 2;
    y = std::clamp(int(std::floor((60 - lat) * 2)) - WY / 2, 0, NY - WY);
    tag = 0;
    return true;
}

glm::dvec3 Sldem::tileCenter(int, int tx, int ty) const {
    double la = glm::radians(60 - (ty + 0.5) * 0.5), lo = glm::radians((tx + 0.5) * 0.5);
    return {std::cos(la) * std::cos(lo), std::cos(la) * std::sin(lo), std::sin(la)};
}

void Sldem::read(int, int ty, int tx0, int tx1, std::vector<Job>& out) {
    static const char* rows[4] = {"30n_60n", "00n_30n", "30s_00s", "60s_30s"};
    int fi = ty / 60 * 8 + tx0 / 90;
    if (!files[fi].is_open()) {
        char name[64];
        snprintf(name, sizeof name, "/sldem2015_512_%s_%03d_%03d_float.tif", rows[ty / 60], tx0 / 90 * 45, tx0 / 90 * 45 + 45);
        TIFF* t = openTiff(dir + name);
        uint64_t *o, *c;
        TIFFGetField(t, TIFFTAG_TILEOFFSETS, &o);
        TIFFGetField(t, TIFFTAG_TILEBYTECOUNTS, &c);
        off[fi].assign(o, o + 5400);
        cnt[fi].assign(c, c + 5400);
        TIFFClose(t);
        files[fi].open(dir + name, std::ios::binary);
    }
    size_t a = ty % 60 * 90 + tx0 % 90, b = a + tx1 - tx0;
    auto buf = std::make_shared<std::vector<char>>(off[fi][b] + cnt[fi][b] - off[fi][a]);
    files[fi].seekg(off[fi][a]).read(buf->data(), buf->size());
    for (int x = tx0; x <= tx1; x++)
        out.push_back({id(0, x, ty), buf, size_t(off[fi][a + x - tx0] - off[fi][a]), size_t(cnt[fi][a + x - tx0])});
}

bool Sldem::decode(const Job& j, float* km) const {
    uLongf n = 65536 * 4;
    if (uncompress(reinterpret_cast<Bytef*>(km), &n, reinterpret_cast<const Bytef*>(j.buf->data() + j.at), j.size) != Z_OK || n != 65536 * 4) return false;
    double sum = 0;
    int valid = 0;
    for (int i = 0; i < 65536; i++) if (km[i] > -1e30f) sum += km[i], valid++;
    float mean = valid ? float(sum / valid) : 0;
    for (int i = 0; i < 65536; i++) if (km[i] <= -1e30f) km[i] = mean;
    return valid > 32768;
}

PolarDem::PolarDem(const std::string& d) : Streamer((PN + 255) / 256, (PN + 255) / 256, 32, 32, false), dir(d) { start(); }

bool PolarDem::place(const glm::dvec3& c, int& x, int& y, int& tag) const {
    glm::dvec3 n = glm::normalize(c);
    double p = n.z < 0 ? -1 : 1, t = 2 * R / (1 + p * n.z);
    tag = p < 0;
    x = std::clamp(int((PC + t * n.y / PS) / 256) - WX / 2, 0, NX - WX);
    y = std::clamp(int((PC + p * t * n.x / PS) / 256) - WY / 2, 0, NY - WY);
    return true;
}

glm::dvec3 PolarDem::tileCenter(int tag, int tx, int ty) const {
    double p = tag ? -1 : 1, v = ((tx + 0.5) * 256 - PC) * PS, u = p * ((ty + 0.5) * 256 - PC) * PS, s = (u * u + v * v) / (4 * R * R);
    return {u / (R * (1 + s)), v / (R * (1 + s)), p * (1 - s) / (1 + s)};
}

void PolarDem::read(int tag, int ty, int tx0, int tx1, std::vector<Job>& out) {
    std::ifstream& f = files[tag];
    if (!f.is_open()) f.open(dir + (tag ? "/ldem_60s_60m.img" : "/ldem_60n_60m.img"), std::ios::binary);
    if (!f) { fprintf(stderr, "nao foi possivel ler o DEM polar em %s\n", dir.c_str()); exit(1); }
    int c0 = tx0 * 256, c1 = std::min((tx1 + 1) * 256, PN), r0 = ty * 256, r1 = std::min(r0 + 256, PN);
    size_t stride = size_t(c1 - c0) * 2;
    auto buf = std::make_shared<std::vector<char>>(stride * (r1 - r0));
    for (int r = r0; r < r1; r++) f.seekg((size_t(r) * PN + c0) * 2).read(buf->data() + (r - r0) * stride, stride);
    for (int x = tx0; x <= tx1; x++) out.push_back({id(tag, x, ty), buf, size_t(x - tx0) * 512, stride});
}

bool PolarDem::decode(const Job& j, float* km) const {
    const int16_t* dn = reinterpret_cast<const int16_t*>(j.buf->data());
    int rows = int(j.buf->size() / j.size), cols = int(std::min<size_t>(256, (j.size - j.at) / 2)), stride = int(j.size / 2), col0 = int(j.at / 2);
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++) km[y * 256 + x] = dn[std::min(y, rows - 1) * stride + col0 + std::min(x, cols - 1)] * 0.0005f;
    return true;
}

