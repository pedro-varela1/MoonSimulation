#pragma once
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <memory>
#include "config.h"
#include "orbit.h"
#include "render.h"
#include "terrain.h"

inline const std::array<const char*, 3> QUALITY{"high", "medium", "low"};
inline const std::array<glm::dvec3, 2> VIEW0{glm::dvec3(0, 0.3, 6000), glm::dvec3(0, 0.3, 1)};  // yaw, pitch, distance: free, follow

// Sun seen below the point p: azimuth clockwise from north in [0, 360) and elevation over the local horizon, degrees
inline glm::dvec2 sunAzEl(const glm::dvec3& p, const glm::dvec3& sun) {
    glm::dvec3 up = glm::normalize(p), s = glm::normalize(sun), east = glm::cross(glm::dvec3(0, 0, 1), up);
    east = glm::length(east) > 1e-9 ? glm::normalize(east) : glm::dvec3(0, 1, 0);
    double az = glm::degrees(std::atan2(glm::dot(s, east), glm::dot(s, glm::cross(up, east))));
    return {az < 0 ? az + 360 : az, glm::degrees(std::asin(glm::dot(s, up)))};
}

struct App {
    std::string cfgPath, orbitLoaded, notice;  // notice: shown under the start screen buttons
    Config cfg;
    Orbit orbit;
    Terrain terrain;
    std::unique_ptr<Streamer> sldem, polar;
    Renderer renderer;
    GLFWwindow* win = nullptr;
    bool running = false, paused = false, follow = false, northPole = false, southPole = false, axes = true, fov = false;  // last four: orbit view overlays
    double et0 = 0, t = 0, tStart = 0, speed = 1, fps = 0, vramGb = 0;
    float satScale = 1;  // satellite magnification in the orbit view
    int autoLevel = 2;
    std::array<glm::dvec3, 2> view = VIEW0;

    void seek(double time) { t = std::clamp(time, orbit.begin(), orbit.end()); }
    int level() const {
        auto it = std::find(QUALITY.begin(), QUALITY.end(), cfg.quality);
        return it == QUALITY.end() ? autoLevel : int(it - QUALITY.begin());
    }

    // camera frame: x = image right, y = image top, -z = boresight
    glm::dmat3 attitude(const State& s) const {
        glm::dvec3 U = glm::normalize(s.p), F = glm::normalize(s.v - glm::dot(s.v, U) * U);
        auto rz = [](double a) { return glm::dmat3(std::cos(a), std::sin(a), 0, -std::sin(a), std::cos(a), 0, 0, 0, 1); };
        double c = std::cos(glm::radians(cfg.tilt)), s1 = std::sin(glm::radians(cfg.tilt));
        return glm::dmat3(glm::cross(F, U), F, U) * rz(-glm::radians(cfg.azimuth)) * glm::dmat3(1, 0, 0, 0, c, s1, 0, -s1, c) * rz(glm::radians(cfg.roll));
    }

    // distance (km) along the boresight to the reference sphere, -1 when the boresight misses the Moon
    double range(const State& s) const {
        glm::dvec3 d = -attitude(s)[2];
        double b = glm::dot(s.p, d), disc = b * b - glm::dot(s.p, s.p) + MOON_R * MOON_R;
        return disc > 0 && -b - std::sqrt(disc) > 0 ? -b - std::sqrt(disc) : -1;
    }

    // ground sample distance (m per pixel) at the image center, -1 when the boresight misses the Moon
    double gsd(const State& s) const {
        double r = range(s);
        return r > 0 ? r * 2 * std::tan(glm::radians(cfg.fov) / 2) / cfg.sensor.x * 1000 : -1;
    }

    // boresight ground point (unit vector), or the nadir when the boresight misses the Moon
    glm::dvec3 focus(double time) const {
        State s = orbit.at(std::clamp(time, orbit.begin(), orbit.end()));
        double r = range(s);
        return glm::normalize(r > 0 ? s.p - r * attitude(s)[2] : s.p);
    }

    Camera orbitCamera(const State& s, glm::ivec2 fb) const {
        glm::dvec3 k = view[follow], dir(std::cos(k.y) * std::cos(k.x), std::cos(k.y) * std::sin(k.x), std::sin(k.y)), target(0), up(0, 0, 1), eye;
        if (follow) {
            up = glm::normalize(s.p);
            glm::dvec3 F = glm::normalize(s.v - glm::dot(s.v, up) * up);
            target = s.p;
            eye = s.p + k.z * (-dir.x * F + dir.y * glm::cross(F, up) + dir.z * up);
            eye = std::max(glm::length(eye), MOON_R + 11.5) * glm::normalize(eye);
        } else eye = k.z * dir;
        glm::dvec3 fwd = glm::normalize(target - eye), right = glm::normalize(glm::cross(fwd, up));
        double ty = std::tan(glm::radians(cfg.orbitFov) / 2);
        return {eye, right, glm::cross(right, fwd), fwd, ty * fb.x / std::max(fb.y, 1), ty};
    }
};
