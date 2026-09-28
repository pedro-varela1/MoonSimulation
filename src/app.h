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

constexpr double MOON_R = 1737.4;
inline const std::array<glm::dvec3, 2> VIEW0{glm::dvec3(0, 0.3, 6000), glm::dvec3(0, 0.3, 1)};  // yaw, pitch, distance: free, follow

struct App {
    std::string cfgPath, orbitLoaded;
    Config cfg;
    Orbit orbit;
    Terrain terrain;
    std::unique_ptr<Streamer> sldem, polar;
    Renderer renderer;
    GLFWwindow* win = nullptr;
    bool running = false, paused = false, follow = false;
    double et0 = 0, t = 0, tStart = 0, speed = 1, fps = 0;
    std::array<glm::dvec3, 2> view = VIEW0;

    void seek(double time) { t = std::clamp(time, orbit.begin(), orbit.end()); }

    // camera frame: x = image right, y = image top, -z = boresight
    glm::dmat3 attitude(const State& s) const {
        glm::dvec3 U = glm::normalize(s.p), F = glm::normalize(s.v - glm::dot(s.v, U) * U);
        auto rz = [](double a) { return glm::dmat3(std::cos(a), std::sin(a), 0, -std::sin(a), std::cos(a), 0, 0, 0, 1); };
        double c = std::cos(glm::radians(cfg.tilt)), s1 = std::sin(glm::radians(cfg.tilt));
        return glm::dmat3(glm::cross(F, U), F, U) * rz(-glm::radians(cfg.azimuth)) * glm::dmat3(1, 0, 0, 0, c, s1, 0, -s1, c) * rz(glm::radians(cfg.roll));
    }

    // boresight ground point (unit vector), or the nadir when the boresight misses the Moon
    glm::dvec3 focus(double time) const {
        State s = orbit.at(std::clamp(time, orbit.begin(), orbit.end()));
        glm::dvec3 d = -attitude(s)[2];
        double b = glm::dot(s.p, d), disc = b * b - glm::dot(s.p, s.p) + MOON_R * MOON_R;
        return glm::normalize(disc > 0 && -b - std::sqrt(disc) > 0 ? s.p + (-b - std::sqrt(disc)) * d : s.p);
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
