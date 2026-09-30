#pragma once
#include <glm/glm.hpp>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include "platform.h"

struct Config {
    std::string orbitFile, spiceDir, sldemDir, polarDir, wac643, wac566, wac415, kitColor, lolaDem, hapkeDir, starCatalog, earthTexture, objFile, startUtc;
    std::string quality = "auto";
    double objScale = 1, speed = 1, orbitFov = 45, orbitGain = 4, orbitStarMag = 6.5, orbitSatScale = 1, fov = 120, tilt = 0, azimuth = 0, roll = 0;
    glm::dvec3 objRotation{0};
    glm::ivec2 sensor{1024, 1024};
    bool gray = false, earthshine = true, orbitMe = false;  // orbitMe: the orbit file latitude and longitude are MOON_ME, not MOON_PA
    glm::vec3 kitGain{1}, kitOffset{0};
};

// a comment starts at a '#' that begins the line or follows a blank, so values such as paths may contain '#'
inline size_t commentAt(const std::string& s) {
    for (size_t i = 0; i < s.size(); i++)
        if (s[i] == '#' && (i == 0 || s[i - 1] == ' ' || s[i - 1] == '\t')) return i;
    return std::string::npos;
}

inline Config loadConfig(const char* path) {
    Config c;
    auto str = [](std::string& s) { return [&s](std::istream& in) { std::getline(in >> std::ws, s); s.erase(s.find_last_not_of(" \t\r") + 1); }; };
    auto num = [](auto& v) { return [&v](std::istream& in) { in >> v; }; };
    auto vec = [](auto& v) { return [&v](std::istream& in) { for (int i = 0; i < v.length(); i++) in >> v[i]; }; };
    std::map<std::string, std::function<void(std::istream&)>> keys = {
        {"orbit_file", str(c.orbitFile)}, {"spice_dir", str(c.spiceDir)}, {"sldem_dir", str(c.sldemDir)}, {"polar_dir", str(c.polarDir)},
        {"wac_643", str(c.wac643)}, {"wac_566", str(c.wac566)}, {"wac_415", str(c.wac415)}, {"kit_color", str(c.kitColor)}, {"lola_dem", str(c.lolaDem)},
        {"hapke_dir", str(c.hapkeDir)}, {"star_catalog", str(c.starCatalog)}, {"earth_texture", str(c.earthTexture)},
        {"obj_file", str(c.objFile)}, {"start_utc", str(c.startUtc)}, {"quality", str(c.quality)},
        {"obj_scale", num(c.objScale)}, {"obj_rotation", vec(c.objRotation)}, {"speed", num(c.speed)},
        {"orbit_fov", num(c.orbitFov)}, {"orbit_gain", num(c.orbitGain)}, {"orbit_star_mag", num(c.orbitStarMag)}, {"orbit_sat_scale", num(c.orbitSatScale)}, {"sensor", vec(c.sensor)},
        {"fov", num(c.fov)}, {"tilt", num(c.tilt)}, {"azimuth", num(c.azimuth)}, {"roll", num(c.roll)},
        {"color", [&](std::istream& in) { std::string v; in >> v; c.gray = v == "gray"; }}, {"earthshine", num(c.earthshine)},
        {"orbit_frame", [&](std::istream& in) { std::string v; in >> v; c.orbitMe = v == "ME"; }},
        {"kit_gain", vec(c.kitGain)}, {"kit_offset", vec(c.kitOffset)},
    };
    std::ifstream f(path);
    if (!f) fatal(std::string("Could not open the configuration file ") + path);
    for (std::string line; std::getline(f, line);) {
        line = line.substr(0, commentAt(line));
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        key.erase(key.find_last_not_of(" \t") + 1);
        key.erase(0, key.find_first_not_of(" \t"));
        std::istringstream val(line.substr(eq + 1));
        if (auto k = keys.find(key); k != keys.end()) k->second(val);
        else fprintf(stderr, "config: chave desconhecida '%s'\n", key.c_str());
    }
    return c;
}

// rewrites the values of the given keys in place, keeping comments and their column; missing keys are appended
inline void saveConfig(const char* path, std::map<std::string, std::string> values) {
    std::ifstream in(path, std::ios::binary);
    std::string out;
    for (std::string line; std::getline(in, line);) {
        size_t eq = line.find('='), hash = commentAt(line);
        std::string key = eq < hash ? line.substr(0, eq) : "";
        key.erase(key.find_last_not_of(" \t") + 1);
        key.erase(0, key.find_first_not_of(" \t"));
        if (auto v = values.find(key); v != values.end()) {
            size_t end = hash != std::string::npos ? hash : line.size() - (!line.empty() && line.back() == '\r');
            std::string value = " " + v->second;
            if (hash != std::string::npos) value.resize(std::max(value.size() + 1, end - eq - 1), ' ');
            line = line.substr(0, eq + 1) + value + line.substr(end);
            values.erase(v);
        }
        out += line + '\n';
    }
    for (auto& [k, v] : values) out += k + " = " + v + '\n';
    std::ofstream(path, std::ios::binary) << out;
}
