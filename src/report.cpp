#include "report.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include "app.h"

bool writeReport(App& a, const std::string& path, const std::function<void(size_t)>& progress) {
    std::ofstream out(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
    if (!out) return false;
    out << "date,time_utc,orbit,latitude_deg,longitude_deg,altitude_km,above_terrain_km,speed_km_s,sun_azimuth_deg,sun_elevation_deg,"
           "illumination_pct,albedo_643,albedo_566,albedo_415,camera_tilt_deg,camera_azimuth_deg,camera_roll_deg,center_gsd_m\n";
    const std::vector<double>& t = a.orbit.times();
    constexpr size_t CHUNK = 1 << 16;
    std::vector<State> st;
    std::vector<glm::dvec3> pos, sun;
    std::string text;
    char line[400], gsdText[32];
    for (size_t i0 = 0; i0 < t.size(); i0 += CHUNK) {
        size_t n = std::min(CHUNK, t.size() - i0);
        st.resize(n), pos.resize(n), sun.resize(n);
        for (size_t i = 0; i < n; i++) st[i] = a.orbit.at(t[i0 + i]), pos[i] = st[i].p, sun[i] = ephem::sun(a.et0 + t[i0 + i]);
        std::vector<Probe> probe = a.renderer.probeAll(pos, sun);  // global terrain, so above_terrain may differ by tens of meters from the live SLDEM value
        text.clear();
        for (size_t i = 0; i < n; i++) {
            const State& s = st[i];
            std::string utc = a.orbit.utc(t[i0 + i]);
            glm::dvec2 azEl = sunAzEl(s.p, sun[i]);
            double r = glm::length(s.p), gsd = a.gsd(s);
            gsdText[0] = '\0';
            if (gsd > 0) snprintf(gsdText, sizeof gsdText, "%.2f", gsd);
            snprintf(line, sizeof line, "%s,%s,%d,%.6f,%.6f,%.4f,%.4f,%.5f,%.3f,%.3f,%.1f,%.5f,%.5f,%.5f,%g,%g,%g,%s\n", utc.substr(0, 10).c_str(),
                     utc.substr(11).c_str(), int((t[i0 + i] - a.orbit.begin()) / a.orbit.period) + 1, glm::degrees(std::asin(s.p.z / r)),
                     glm::degrees(std::atan2(s.p.y, s.p.x)), r - MOON_R, probe[i].above, glm::length(s.v), azEl.x, azEl.y, probe[i].vis * 100,
                     probe[i].albedo.r, probe[i].albedo.g, probe[i].albedo.b, a.cfg.tilt, a.cfg.azimuth, a.cfg.roll, gsdText);
            text += line;
        }
        out << text;
        progress(i0 + n);
    }
    return bool(out.flush());
}
