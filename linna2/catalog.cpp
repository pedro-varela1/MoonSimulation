#include "stages.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include "imagegen.h"

namespace {

// craters in blocks that fit the streaming windows with room for their rims: 8 degree bands, 24 degrees wide within 60 degrees of
// the equator, and blocks of at most about 240 km in the polar caps; bands from north to south, sectors back and forth
struct Block { double lat, lon; std::vector<const Crater*> craters; };

std::vector<Block> blocks(const Catalogue& cat) {
    struct Band { double top, bottom; int sectors; };
    std::vector<Band> bands{{90, 86, 1}, {86, 80, 8}, {80, 74, 12}, {74, 67, 16}, {67, 60, 20}};
    for (double top = 60; top > -60; top -= 8) bands.push_back({top, top - 8, 15});
    for (int i = 4; i >= 0; i--) bands.push_back({-bands[i].bottom, -bands[i].top, bands[i].sectors});
    std::vector<std::vector<Block>> grid(bands.size());
    for (size_t b = 0; b < bands.size(); b++)
        for (int j = 0; j < bands[b].sectors; j++)
            grid[b].push_back({std::abs(bands[b].top) == 90 ? bands[b].top : (bands[b].top + bands[b].bottom) / 2, (j + 0.5) * 360 / bands[b].sectors});
    for (const Crater& c : cat.all()) {
        size_t b = 0;
        while (b + 1 < bands.size() && c.lat < bands[b].bottom) b++;
        int n = bands[b].sectors;
        grid[b][std::min(int(std::fmod(c.lon + 360, 360) / (360.0 / n)), n - 1)].craters.push_back(&c);
    }
    std::vector<Block> out;
    for (size_t b = 0; b < grid.size(); b++) {
        if (b % 2) std::reverse(grid[b].begin(), grid[b].end());
        for (auto& k : grid[b])
            if (!k.craters.empty()) out.push_back(std::move(k));
    }
    return out;
}

// the n points of a crater's rim ellipse (on the sphere), counter-clockwise from the major axis
std::vector<glm::dvec3> rimPoints(const Crater& c, int n) {
    Enu E = enu(c.lat, c.lon);
    double a = c.major / 2, b = c.minor / 2, psi = glm::radians(c.angle);
    std::vector<glm::dvec3> p(n);
    for (int j = 0; j < n; j++) {
        double t = 2 * PI * j / n, u = a * std::cos(t), v = b * std::sin(t);
        p[j] = glm::normalize(MOON_R * E.up + (u * std::cos(psi) - v * std::sin(psi)) * E.east + (u * std::sin(psi) + v * std::cos(psi)) * E.north);
    }
    return p;
}

}

void catalogStage(Generator& g) {
    const Settings& S = g.set;
    if (!g.cat.load(g.cfg.craterDb, {0, 1e9}, true)) fatal("Could not read the crater catalogue " + g.cfg.craterDb);
    std::vector<std::string> ids;  // CRATER_ID by data row
    {
        std::string text = readText(g.cfg.craterDb);
        for (size_t a = text.find('\n') + 1, b; a < text.size(); a = b + 1) {
            b = std::min(text.find('\n', a), text.size());
            if (b > a && text[a] != '\r') ids.push_back(text.substr(a, text.find(',', a) - a));
        }
    }
    std::vector<Block> bl = blocks(g.cat);
    std::vector<double> erim(ids.size(), NAN);
    auto start = std::chrono::steady_clock::now();
    for (size_t i = 0; i < bl.size(); i++) {
        // wait for the terrain under every rim: the centre covers a rim within 10 km, larger rims add points every 20 km
        std::vector<glm::dvec3> path, pts;
        for (const Crater* c : bl[i].craters) {
            path.push_back(unitAt(c->lat, c->lon));
            if (c->major > 20) for (auto& p : rimPoints(*c, std::min(400, int(std::ceil(PI * c->major / 20))))) path.push_back(p);
            for (auto& p : rimPoints(*c, S.rimSamples)) pts.push_back(p);
        }
        double waited;
        bool ok = g.stream(unitAt(bl[i].lat, bl[i].lon), path, 34, waited);
        std::vector<glm::vec3> fp(pts.begin(), pts.end());
        std::vector<glm::vec2> h = g.renderer.sample(fp, false);
        for (size_t j = 0; j < bl[i].craters.size(); j++) {
            double sum = 0;
            for (int k = 0; k < S.rimSamples; k++) sum += h[j * S.rimSamples + k].x;
            erim[bl[i].craters[j]->cid] = sum / S.rimSamples;
        }
        double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        printf("catalog block %zu/%zu (%+.0f, %.0f): %zu craters, terrain %.1f s%s, %.0f s in all\n", i + 1, bl.size(), bl[i].lat, bl[i].lon,
               bl[i].craters.size(), waited, ok ? "" : " (wait limit)", el);
        fflush(stdout);
    }
    std::string out = "cid,crater_id,lat_deg,lon_deg,a_km,b_km,psi_deg,d_eff_km,diam_circ_km,eccentricity,e_rim_km\n";
    for (const Crater& c : g.cat.all()) {
        glm::dvec2 l = latLon(unitAt(c.lat, c.lon));
        out += fmt("%d,%s,%.6f,%.6f,%.5f,%.5f,%.4f,%.5f,%.5f,%.6f,%.6f\n", c.cid, ids[c.cid].c_str(), c.lat, l.y, c.major / 2, c.minor / 2, c.angle,
                   std::sqrt(c.major * c.minor), c.diam, std::sqrt(std::max(0.0, 1 - c.minor * c.minor / (c.major * c.major))), erim[c.cid]);
    }
    writeText(S.output + "/catalog/robbins_linna2.csv", out);
    printf("catalog: %zu craters with e_rim\n", g.cat.all().size());
}

void angleCheck(Generator& g) {
    const Settings& S = g.set;
    std::vector<float> arc;  // ARC_IMG by data row: the fraction of the rim the catalogue traced
    {
        std::string text = readText(g.cfg.craterDb);
        size_t eol = text.find('\n'), col = 0;
        for (size_t a = 0; a < eol && text.compare(a, 8, "ARC_IMG,") && text.compare(a, 8, "ARC_IMG\r"); a = text.find(',', a) + 1) col++;
        for (size_t a = eol + 1, b; a < text.size(); a = b + 1) {
            b = std::min(text.find('\n', a), text.size());
            if (b == a || text[a] == '\r') continue;
            size_t f = a;
            for (size_t c = 0; c < col && f < b; c++) f = text.find(',', f) + 1;
            arc.push_back(float(std::atof(text.c_str() + f)));
        }
    }
    auto ecc = [](const Crater* c) { return 1 - c->minor * c->minor / (c->major * c->major); };
    std::vector<const Crater*> pick(20, nullptr);
    for (const Crater& c : g.cat.all())
        if (c.diam > 20 && !std::isnan(g.rim[c.cid]) && size_t(c.cid) < arc.size() && arc[c.cid] >= 0.9) {
            const Crater*& p = pick[std::clamp(int((c.lat + 90) / 9), 0, 19)];
            if (!p || ecc(&c) > ecc(p)) p = &c;
        }
    int N = S.imageRes, T = N / 2;
    std::vector<uint8_t> montage(size_t(5 * T) * 4 * T * 3, 0);
    std::string csv = "cid,lat_deg,lon_deg,major_km,minor_km,angle_deg,eccentricity,altitude_km,file\n", dir = S.root + "/qc/angle_check/";
    int n = 0;
    for (const Crater* c : pick) {
        if (!c) continue;
        Enu E = enu(c->lat, c->lon);
        double e = g.rim[c->cid], h = c->major / 0.3 / (2 * std::tan(glm::radians(30.0)));  // major axis over 30 % of the image width
        Shot s;
        s.id = fmt("angle-%d", c->cid), s.fovClass = 60;
        s.pose = {(MOON_R + e + h) * E.up, pointing(glm::dmat3(E.east, E.north, E.up), 0, 0, 0), 60};  // nadir, north up
        s.sun = AU * (0.5 * E.up - 0.866025403784 * E.east);  // 30 degrees high, from the west
        Frame f = g.shoot(s);
        double fN = s.pose.focal(N), cN = (N - 1) / 2.0, el[5];
        glm::dmat3 K(fN, 0, 0, 0, fN, 0, cN, cN, 1);
        glm::dvec3 C = (MOON_R + e) * E.up;
        std::vector<uint8_t> rgb(size_t(N) * N * 3);
        for (size_t i = 0; i < f.image.size(); i++) rgb[3 * i] = rgb[3 * i + 1] = rgb[3 * i + 2] = uint8_t(f.image[i] >> 8);
        // red: the angle counter-clockwise from east (used); blue, thinner: clockwise from north, the alternative reading
        for (auto [psi, w, color] : {std::tuple{90 - c->angle, 1, 0x3060ff}, std::tuple{c->angle, 2, 0xff0000}})
            if (imageEllipse(K * glm::transpose(s.pose.cvToMe()) * glm::dmat3(E.east, E.north, C - s.pose.eye), c->major / 2, c->minor / 2, glm::radians(psi), el))
                for (double t = 0; t < 2 * PI; t += 0.002) {
                    int x = int(std::lround(el[0] + el[2] * std::cos(t) * std::cos(el[4]) - el[3] * std::sin(t) * std::sin(el[4])));
                    int y = int(std::lround(el[1] + el[2] * std::cos(t) * std::sin(el[4]) + el[3] * std::sin(t) * std::cos(el[4])));
                    for (int dy = 0; dy < w; dy++)
                        for (int dx = 0; dx < w; dx++)
                            if (x + dx >= 0 && x + dx < N && y + dy >= 0 && y + dy < N) {
                                uint8_t* p = &rgb[(size_t(y + dy) * N + x + dx) * 3];
                                p[0] = uint8_t(color >> 16), p[1] = uint8_t(color >> 8), p[2] = uint8_t(color);
                            }
                }
        std::string file = fmt("%d.png", c->cid);
        std::filesystem::create_directories(u8path(dir));
        writePng(dir + file, N, N, 3, 8, rgb.data(), false);
        for (int y = 0; y < T; y++)
            for (int x = 0; x < T; x++)
                for (int k = 0; k < 3; k++)
                    montage[((size_t(n / 5 * T + y) * 5 * T) + n % 5 * T + x) * 3 + k] =
                        uint8_t((rgb[(size_t(2 * y) * N + 2 * x) * 3 + k] + rgb[(size_t(2 * y) * N + 2 * x + 1) * 3 + k] +
                                 rgb[(size_t(2 * y + 1) * N + 2 * x) * 3 + k] + rgb[(size_t(2 * y + 1) * N + 2 * x + 1) * 3 + k] + 2) / 4);
        csv += fmt("%d,%.5f,%.5f,%.4f,%.4f,%.3f,%.4f,%.3f,%s\n", c->cid, c->lat, c->lon, c->major, c->minor, c->angle, std::sqrt(ecc(c)), h, file.c_str());
        printf("angle check %d: crater %d at %+.2f, %.2f, %.1f x %.1f km\n", ++n, c->cid, c->lat, c->lon, c->major, c->minor);
        fflush(stdout);
    }
    writePng(S.root + "/qc/angle_check.png", 5 * T, 4 * T, 3, 8, montage.data(), false);
    writeText(S.root + "/qc/angle_check.csv", csv);
}
