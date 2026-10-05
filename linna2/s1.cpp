#include "stages.h"
#include <algorithm>

namespace {
const char* SPLITS[3] = {"train", "val", "test"};
const char* STRATA[4] = {"night", "terminator", "low", "high"};
}

std::vector<Shot> s1Shots(Generator& g, int split) {
    const Settings& S = g.set;
    Rng rng(S.s1Seed[split]);
    int count = (S.sample && !S.bench ? S.s1Sample : S.s1Count)[split];
    double polarSin = std::sin(glm::radians(S.polarLat));
    std::vector<Shot> shots;
    std::string plan = "frame_id,lat_ss_deg,lon_ss_deg,lola_km,alt_terrain_km,fov_class,fov_deg,tilt_deg,azimuth_deg,roll_deg,sun_el_deg,sun_az_deg,stratum\n";
    for (int i = 0; i < count; i++) {
        Shot s;
        s.dataset = "S1", s.split = SPLITS[split], s.fovClass = S.fovClasses[i % S.fovClasses.size()];  // balanced FOV schedule
        s.id = fmt("S1-%s-%06d", SPLITS[split], i), s.path = "S1/" + s.split + "/" + s.id;
        double lat, lon, hDem, h;
        glm::dvec3 up, b;
        for (;;) {
            // 1. sub-satellite point, area-uniform, with the polar share above polar_lat
            double sl;
            if (rng() < S.polarShare) {
                double sign = rng() < 0.5 ? -1 : 1;
                sl = sign * rng(polarSin, 1);
            } else sl = rng(-polarSin, polarSin);
            lat = glm::degrees(std::asin(sl)), lon = rng(0, 360) - 180, up = unitAt(lat, lon);
            // 2-4. field of view, altitude above the local (LOLA) surface, attitude from north
            s.pose.fov = s.fovClass * rng(1 - S.fovJitter, 1 + S.fovJitter);
            hDem = g.lolaHeights({up})[0];
            h = rng(S.altitudeKm[0], S.altitudeKm[1]);
            s.tilt = rng(0, S.tiltMax), s.azimuth = rng(0, 360), s.roll = rng(0, 360);
            // 5. GSD rule (redraw the altitude) and frame-filled rule (redraw the tilt, then everything)
            int tries = 0;
            for (;;) {
                s.pose.eye = (MOON_R + hDem + h) * up;
                s.pose.att = pointing(enuAxes(up), s.tilt, s.azimuth, s.roll);
                double range = sphereHit(s.pose.eye, s.pose.ray(0, 0), MOON_R + hDem);
                if (range > 0 && range / s.pose.focal(S.imageRes) * 1000 < S.gsdMinM) { h = rng(S.altitudeKm[0], S.altitudeKm[1]); continue; }
                if (s.pose.filled()) break;
                if (++tries > S.limbRetries) break;
                s.tilt = rng(0, S.tiltMax);
            }
            if (tries > S.limbRetries) continue;
            // 6. the whole footprint in the split's region
            int parts = 0;
            for (auto& p : footprint(s.pose, 9)) parts |= 1 << g.partition(p);
            if (parts != 1 << split) continue;
            break;
        }
        // 7. Sun at the boresight ground point: stratum, elevation under the physical cap, azimuth
        b = footprint(s.pose, 3)[4];
        double cap = 90 - std::abs(latLon(b).x) + S.capMarginDeg, el, az;
        int k;
        do {
            double u = rng(), acc = 0;
            for (k = 0; k < 3 && u >= (acc += S.sunShare[k]); k++) {}
        } while (S.sunEdges[k] >= cap);
        do el = rng(S.sunEdges[k], S.sunEdges[k + 1]);
        while (el > cap);
        az = rng(0, 360);
        glm::dmat3 enu = enuAxes(b);
        double e = glm::radians(el), a = glm::radians(az);
        s.sun = AU * (std::sin(e) * enu[2] + std::cos(e) * (std::sin(a) * enu[0] + std::cos(a) * enu[1]));
        plan += fmt("%s,%.6f,%.6f,%.6f,%.6f,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%s\n", s.id.c_str(), lat, lon, hDem, h, s.fovClass, s.pose.fov, s.tilt,
                    s.azimuth, s.roll, el, az, STRATA[k]);
        shots.push_back(s);
    }
    writeText(S.root + "/plan/S1-" + SPLITS[split] + ".csv", plan);

    // rendering order along a Hilbert curve of the boresight ground points (each polar cap on its own), so the terrain streams once
    auto key = [&](const Shot& s) {
        glm::dvec2 l = latLon(footprint(s.pose, 3)[4]);
        double rho = (90 - std::abs(l.x)) / 32;
        if (std::abs(l.x) > 58)
            return (l.x > 0 ? 0ull : 2ull << 40) | hilbert(uint32_t((1 + rho * std::cos(glm::radians(l.y))) * 32767), uint32_t((1 + rho * std::sin(glm::radians(l.y))) * 32767), 16);
        return 1ull << 40 | hilbert(uint32_t((l.y + 180) / 360 * 65535), uint32_t((l.x + 60) / 120 * 65535), 16);
    };
    std::vector<std::pair<uint64_t, size_t>> order;
    for (size_t i = 0; i < shots.size(); i++) order.push_back({key(shots[i]), i});
    std::sort(order.begin(), order.end());
    std::vector<Shot> sorted;
    for (auto& [k, i] : order) sorted.push_back(shots[i]);
    if (S.bench) sorted.resize(std::min(sorted.size(), size_t(S.bench)));
    return sorted;
}
