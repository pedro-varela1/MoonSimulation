#pragma once
#include <glm/glm.hpp>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>

struct Config {
    std::string orbitFile, spiceDir, sldemDir, polarDir, wac643, wac566, wac415, kitColor, lolaDem, hapkeDir, objFile, startUtc;
    double objScale = 1, speed = 1, orbitFov = 45, orbitGain = 4, fov = 120, tilt = 0, azimuth = 0, roll = 0;
    glm::dvec3 objRotation{0};
    glm::ivec4 orbitWindow{100, 100, 1280, 800}, cameraWindow{1400, 100, 900, 900};
    glm::ivec2 sensor{1024, 1024};
    bool gray = false, earthshine = true;
    glm::vec3 kitGain{1}, kitOffset{0};
};

inline Config loadConfig(const char* path) {
    Config c;
    auto str = [](std::string& s) { return [&s](std::istream& in) { std::getline(in >> std::ws, s); s.erase(s.find_last_not_of(" \t\r") + 1); }; };
    auto num = [](auto& v) { return [&v](std::istream& in) { in >> v; }; };
    auto vec = [](auto& v) { return [&v](std::istream& in) { for (int i = 0; i < v.length(); i++) in >> v[i]; }; };
    std::map<std::string, std::function<void(std::istream&)>> keys = {
        {"orbit_file", str(c.orbitFile)}, {"spice_dir", str(c.spiceDir)}, {"sldem_dir", str(c.sldemDir)}, {"polar_dir", str(c.polarDir)},
        {"wac_643", str(c.wac643)}, {"wac_566", str(c.wac566)}, {"wac_415", str(c.wac415)}, {"kit_color", str(c.kitColor)}, {"lola_dem", str(c.lolaDem)},
        {"hapke_dir", str(c.hapkeDir)}, {"obj_file", str(c.objFile)}, {"start_utc", str(c.startUtc)},
        {"obj_scale", num(c.objScale)}, {"obj_rotation", vec(c.objRotation)}, {"speed", num(c.speed)},
        {"orbit_window", vec(c.orbitWindow)}, {"camera_window", vec(c.cameraWindow)},
        {"orbit_fov", num(c.orbitFov)}, {"orbit_gain", num(c.orbitGain)}, {"sensor", vec(c.sensor)},
        {"fov", num(c.fov)}, {"tilt", num(c.tilt)}, {"azimuth", num(c.azimuth)}, {"roll", num(c.roll)},
        {"color", [&](std::istream& in) { std::string v; in >> v; c.gray = v == "gray"; }}, {"earthshine", num(c.earthshine)},
        {"kit_gain", vec(c.kitGain)}, {"kit_offset", vec(c.kitOffset)},
    };
    std::ifstream f(path);
    if (!f) { fprintf(stderr, "config: nao foi possivel abrir %s\n", path); exit(1); }
    for (std::string line; std::getline(f, line);) {
        line = line.substr(0, line.find('#'));
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
