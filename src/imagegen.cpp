#include "imagegen.h"
#include <png.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include "app.h"
#include "craters.h"
#include "platform.h"

namespace {

constexpr int RING = 12, RIM = 64, GRID = 48;  // rim height samples, projected rim points, depth grid cells per side
constexpr double PI = 3.14159265358979;

std::filesystem::path u8(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }

bool writeFile(const std::string& path, const std::string& text) {
    FILE* f = openFile(path, L"wb");
    if (!f) return false;
    bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    return fclose(f) == 0 && ok;
}

// 8-bit RGB PNG from OpenGL rows (bottom row first)
bool writePng(const std::string& path, int w, int h, const std::vector<uint8_t>& rgb) {
    FILE* f = openFile(path, L"wb");
    if (!f) return false;
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png_create_info_struct(png);
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        fclose(f);
        return false;
    }
    png_init_io(png, f);
    png_set_compression_level(png, 3);
    png_set_IHDR(png, info, w, h, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_set_sRGB(png, info, PNG_sRGB_INTENT_PERCEPTUAL);
    png_write_info(png, info);
    for (int y = h - 1; y >= 0; y--) png_write_row(png, &rgb[size_t(y) * w * 3]);
    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    return fclose(f) == 0;
}

// linearly interpolated, as numpy.percentile
double percentile(std::vector<double> v, double p) {
    std::sort(v.begin(), v.end());
    double x = p / 100 * double(v.size() - 1);
    size_t i = size_t(x);
    return i + 1 < v.size() ? v[i] + (v[i + 1] - v[i]) * (x - double(i)) : v[i];
}

struct Local { glm::dvec3 east, north, up; };

Local local(double lat, double lon) {
    double la = glm::radians(lat), lo = glm::radians(lon);
    glm::dvec3 up(std::cos(la) * std::cos(lo), std::cos(la) * std::sin(lo), std::sin(la)), east(-std::sin(lo), std::cos(lo), 0);
    return {east, glm::cross(up, east), up};
}

// image conic of the ellipse with semi-axes a, b (km), major axis th (rad) from east toward north, mapped to the image by the
// homography H of its plane: centre (px), semi-axes (px, a >= b) and major axis angle (rad in [0, pi), from +x toward +y)
bool conic(const glm::dmat3& H, double a, double b, double th, double out[5]) {
    glm::dmat3 R(std::cos(th), std::sin(th), 0, -std::sin(th), std::cos(th), 0, 0, 0, 1), Hi = glm::inverse(H);
    glm::dmat3 Q = glm::transpose(Hi) * R * glm::dmat3(1 / (a * a), 0, 0, 0, 1 / (b * b), 0, 0, 0, -1) * glm::transpose(R) * Hi;
    double A = Q[0][0], B = Q[1][0] + Q[0][1], C = Q[1][1], D = Q[2][0] + Q[0][2], E = Q[2][1] + Q[1][2], F = Q[2][2];
    double det = B * B - 4 * A * C;
    if (std::abs(det) < 1e-300) return false;
    double xc = (2 * C * D - B * E) / det, yc = (2 * A * E - B * D) / det, Fc = A * xc * xc + B * xc * yc + C * yc * yc + D * xc + E * yc + F;
    if (std::abs(Fc) < 1e-300) return false;
    double m00 = -A / Fc, m01 = -B / 2 / Fc, m11 = -C / Fc, s = std::sqrt((m00 - m11) * (m00 - m11) + 4 * m01 * m01);
    double small = (m00 + m11 - s) / 2, large = (m00 + m11 + s) / 2;
    if (small <= 0) return false;
    double vx = 1, vy = 0;  // eigenvector of the smaller eigenvalue: the major axis
    if (std::abs(m01) > 1e-12 * (std::abs(m00) + std::abs(m11))) vx = m01, vy = small - m00;
    else if (m00 > m11) vx = 0, vy = 1;
    out[0] = xc, out[1] = yc, out[2] = 1 / std::sqrt(small), out[3] = 1 / std::sqrt(large), out[4] = std::fmod(std::atan2(vy, vx) + 2 * PI, PI);
    return true;
}

// labels of the craters in view, as linna-imagegenerator writes them. Each centre sits at its rim height (median of the terrain
// on the crater circle); depth and rim height follow measure_depth (15th percentile of the floor within 0.35 R, 85th of the
// 0.8-1.2 R annulus), all on the terrain of the last frame.
//   centre + radius: 0 x_norm y_norm r_norm r_px r_km depth_km rim_elev_km, for centres inside the image
//   conic:           0 xc yc a b theta_deg r_km depth_km rim_elev_km vis_frac shadow_frac offnadir_deg, for rims partly inside it
std::string labels(Renderer& rd, const Catalogue& cat, const Camera& c, glm::ivec2 size, bool conicLabels) {
    double W = size.x, H = size.y, D = std::max(W, H), R2 = MOON_R * MOON_R, fx = W / (2 * c.tanX), fy = H / (2 * c.tanY);
    auto pixel = [&](const glm::dvec3& p, double& z) {  // image position (px, y down) and depth along the boresight
        glm::dvec3 d = p - c.eye;
        z = glm::dot(d, c.fwd);
        return glm::dvec2(W / 2 + fx * glm::dot(d, c.right) / z, H / 2 - fy * glm::dot(d, c.up) / z);
    };
    std::vector<const Crater*> cand = cat.within(glm::normalize(c.eye), std::acos(MOON_R / glm::length(c.eye)) + cat.maxDiameter / 2 / MOON_R);
    std::vector<Local> frame;
    std::vector<glm::vec3> pts;
    for (const Crater* k : cand) {
        Local f = frame.emplace_back(local(k->lat, k->lon));
        for (int i = 0; i < RING; i++)
            pts.push_back(glm::normalize(MOON_R * f.up + k->diam / 2 * (std::cos(2 * PI * i / RING) * f.east + std::sin(2 * PI * i / RING) * f.north)));
    }
    std::vector<glm::vec2> ring = rd.sample(pts, false);

    struct Hit { const Crater* k; Local f; std::string head; std::vector<glm::dvec3> rim; double visFrac, offnadir; };
    std::vector<Hit> hits;
    glm::dmat3 KM = glm::dmat3(fx, 0, 0, 0, -fy, 0, W / 2, H / 2, 1) * glm::transpose(glm::dmat3(c.right, c.up, c.fwd));
    char head[200];
    for (size_t i = 0; i < cand.size(); i++) {
        const Crater& k = *cand[i];
        const Local& f = frame[i];
        std::vector<double> h(RING);
        for (int j = 0; j < RING; j++) h[j] = ring[i * RING + j].x;
        glm::dvec3 C = (MOON_R + percentile(h, 50)) * f.up;
        if (glm::dot(C, c.eye) <= R2) continue;  // beyond the limb
        double z;
        glm::dvec2 q = pixel(C, z) / glm::dvec2(W, H);
        if (!conicLabels) {
            if (z <= 0 || q.x < 0 || q.x > 1 || q.y < 0 || q.y > 1) continue;
            int r = std::max(1, int(std::lround(k.diam / 2 * fx / z)));
            snprintf(head, sizeof head, "0 %.8f %.8f %.8f %d %.6f", q.x, q.y, r / D, r, k.diam / 2);
            hits.push_back({&k, f, head});
            continue;
        }
        double a = k.major / 2, b = k.minor / 2, th = glm::radians(k.angle), e[5];
        std::vector<glm::dvec3> rim;  // visible rim points
        bool front = true;
        for (int j = 0; j < RIM; j++) {
            double t = 2 * PI * j / RIM, ca = std::cos(t), sa = std::sin(t), zj;
            glm::dvec3 p = C + (a * ca * std::cos(th) - b * sa * std::sin(th)) * f.east + (a * ca * std::sin(th) + b * sa * std::cos(th)) * f.north;
            glm::dvec2 pj = pixel(p, zj);
            front = front && zj > 0;
            if (zj > 0 && glm::dot(p, c.eye) > R2 && pj.x >= 0 && pj.x <= W && pj.y >= 0 && pj.y <= H) rim.push_back(p);
        }
        if (!front || rim.empty() || !conic(KM * glm::dmat3(f.east, f.north, C - c.eye), a, b, th, e)) continue;
        snprintf(head, sizeof head, "0 %.8f %.8f %.8f %.8f %.4f %.6f", e[0] / W, e[1] / H, e[2] / D, e[3] / D, glm::degrees(e[4]), k.diam / 2);
        double offnadir = glm::degrees(std::acos(std::clamp(glm::dot(glm::normalize(c.eye - C), f.up), -1.0, 1.0)));
        hits.push_back({&k, f, head, rim, double(rim.size()) / RIM, offnadir});
    }

    // depth: the floor disc and the rim annulus sampled on the same grid cells around every crater
    std::vector<glm::dvec2> cells;
    std::vector<bool> onRim;
    for (int gy = 0; gy < GRID; gy++)
        for (int gx = 0; gx < GRID; gx++) {
            double u = (gx + 0.5) / GRID * 2.4 - 1.2, v = (gy + 0.5) / GRID * 2.4 - 1.2, rho = std::hypot(u, v);
            if (rho <= 0.35 || (rho >= 0.8 && rho <= 1.2)) cells.push_back({u, v}), onRim.push_back(rho > 0.35);
        }
    pts.clear();
    for (const Hit& hit : hits)
        for (glm::dvec2 uv : cells) pts.push_back(glm::normalize(MOON_R * hit.f.up + hit.k->diam / 2 * (uv.x * hit.f.east + uv.y * hit.f.north)));
    std::vector<glm::vec2> grid = rd.sample(pts, false);
    // shadow: Sun visibility at the visible rim points
    pts.clear();
    for (const Hit& hit : hits)
        for (const glm::dvec3& p : hit.rim) pts.push_back(glm::normalize(p));
    std::vector<glm::vec2> lit = rd.sample(pts, true);

    std::string out;
    char tail[120];
    for (size_t h = 0, l = 0; h < hits.size(); h++) {
        std::vector<double> floor, rim;
        for (size_t j = 0; j < cells.size(); j++) (onRim[j] ? rim : floor).push_back(grid[h * cells.size() + j].x);
        double top = percentile(rim, 85), depth = std::max(top - percentile(floor, 15), 0.0);
        if (!conicLabels) snprintf(tail, sizeof tail, " %.6f %.6f\n", depth, top);
        else {
            int shade = 0;
            for (size_t j = 0; j < hits[h].rim.size(); j++) shade += lit[l++].y < 0.5f;
            snprintf(tail, sizeof tail, " %.6f %.6f %.4f %.4f %.4f\n", depth, top, hits[h].visFrac, double(shade) / hits[h].rim.size(), hits[h].offnadir);
        }
        out += hits[h].head + tail;
    }
    return out;
}

// "YYYY-MM-DDThh:mm:ss.sss" -> "hh_mm_ss-YYYYMMDD", as linna-imagegenerator names its frames
std::string stem(const std::string& utc) {
    return utc.substr(11, 2) + "_" + utc.substr(14, 2) + "_" + utc.substr(17, 2) + "-" + utc.substr(0, 4) + utc.substr(5, 2) + utc.substr(8, 2);
}

}

int generateImages(App& a, const std::string& folder, std::string& error, const std::function<bool(size_t, size_t, const char*)>& progress) {
    double end;
    if (!ephem::tryEt(a.cfg.imagesEnd, end)) return error = "Invalid end time: " + a.cfg.imagesEnd, -1;
    std::vector<double> rows;
    for (double t : a.orbit.times())
        if (t >= a.tStart - 1e-6 && t <= end - a.et0 + 1e-6) rows.push_back(t);
    progress(0, rows.size(), "Loading the crater catalogue");
    Catalogue cat;
    if (!cat.load(a.cfg.craterDb, a.cfg.craterDiam)) return error = "Could not read the crater catalogue " + a.cfg.craterDb, -1;
    std::error_code ec;
    for (const char* sub : {"/img", "/json", "/label"})
        if (!std::filesystem::create_directories(u8(folder + sub), ec) && ec) return error = "Could not create the folders in " + folder, -1;

    glm::ivec2 size = a.cfg.sensor;
    size_t done = 0;
    for (double t : rows) {
        Scene s = a.scene(t);
        s.cameraOnly = true;
        s.dt = 1e9;  // exposure converged on this image alone
        const Camera& c = s.cam;
        // wait until the high-resolution terrain covers what the camera sees finely (pixels under 0.6 km)
        std::vector<glm::dvec3> path{a.focus(t)};
        for (int j = 0; j <= 8; j++)
            for (int i = 0; i <= 8; i++) {
                glm::dvec3 d = glm::normalize(c.fwd + (i / 4.0 - 1) * c.tanX * c.right + (j / 4.0 - 1) * c.tanY * c.up);
                double b = glm::dot(c.eye, d), disc = b * b - glm::dot(c.eye, c.eye) + MOON_R * MOON_R, r = -b - std::sqrt(std::max(disc, 0.0));
                if (disc > 0 && r > 0 && r * 2 * c.tanX / size.x < 0.6) path.push_back(glm::normalize(c.eye + r * d));
            }
        for (auto waited = std::chrono::steady_clock::now();;) {
            a.sldem->update(path[0], path, 20, true, 1);
            a.polar->update(path[0], path, 20, true, 1);
            if (a.sldem->missing + a.polar->missing == 0 || std::chrono::steady_clock::now() - waited > std::chrono::seconds(60)) break;
            if (!progress(done, rows.size(), "Loading the terrain under the camera")) return int(done);
        }
        a.renderer.draw(s, {1, 1});
        float ev = a.renderer.exposure();

        std::string name = stem(a.orbit.utc(t));
        State st = a.orbit.at(t);
        double r = glm::length(st.p), heading = azimuthAt(st.p, st.v);
        glm::dvec2 sun = sunAzEl(st.p, s.sun);
        // Earth from the ground below the satellite: direction, size and the fraction of its disc lit by the Sun
        glm::dvec3 g = MOON_R * st.p / r, e = s.earth - g;
        double de = glm::length(e), earthAz = azimuthAt(g, e), lit = (1 + glm::dot(glm::normalize(s.sun - s.earth), -e / de)) / 2;
        char json[1400];
        snprintf(json, sizeof json,
                 "{\n  \"camera\": {\n    \"lat_deg\": %.10g,\n    \"lon_deg\": %.10g,\n    \"height_km\": %.10g,\n    \"fov_deg\": %g,\n"
                 "    \"tilt_deg\": %g,\n    \"azimuth_deg\": %g,\n    \"roll_deg\": %g,\n    \"heading_deg\": %.10g,\n    \"ev\": %.6g\n  },\n"
                 "  \"sun\": {\n    \"azimuth_deg\": %.10g,\n    \"azimuth_rel_deg\": %.10g,\n    \"elevation_deg\": %.10g\n  },\n"
                 "  \"earth\": {\n    \"azimuth_deg\": %.10g,\n    \"azimuth_rel_deg\": %.10g,\n    \"elevation_deg\": %.10g,\n    \"distance_km\": %.10g,\n"
                 "    \"angle_deg\": %.10g,\n    \"illuminated_fraction\": %.6g\n  }\n}\n",
                 glm::degrees(std::asin(st.p.z / r)), std::fmod(glm::degrees(std::atan2(st.p.y, st.p.x)) + 360, 360.0), r - MOON_R, a.cfg.fov,
                 a.cfg.tilt, a.cfg.azimuth, a.cfg.roll, heading, ev, sun.x, std::fmod(sun.x - heading + 360, 360.0), sun.y,
                 earthAz, std::fmod(earthAz - heading + 360, 360.0), glm::degrees(std::asin(glm::dot(e / de, st.p / r))), de,
                 glm::degrees(2 * std::asin(EARTH_R / de)), lit);
        if (!writePng(folder + "/img/" + name + ".png", size.x, size.y, a.renderer.cameraPixels()) || !writeFile(folder + "/json/" + name + ".json", json) ||
            !writeFile(folder + "/label/" + name + ".txt", labels(a.renderer, cat, c, size, a.cfg.labelConic)))
            return error = "Could not write the image " + name + " in " + folder, -1;
        if (!progress(++done, rows.size(), "")) break;
    }
    return int(done);
}

