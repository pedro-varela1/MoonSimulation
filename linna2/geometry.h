#pragma once
#include <glm/glm.hpp>
#include <cmath>
#include <cstdint>
#include <random>
#include "ephem.h"

constexpr double PI = 3.14159265358979323846;

inline glm::dvec3 unitAt(double lat, double lon) {
    double la = glm::radians(lat), lo = glm::radians(lon);
    return {std::cos(la) * std::cos(lo), std::cos(la) * std::sin(lo), std::sin(la)};
}

// latitude and longitude (degrees, longitude in [-180, 180)) of a direction
inline glm::dvec2 latLon(const glm::dvec3& p) {
    glm::dvec3 n = glm::normalize(p);
    double lon = glm::degrees(std::atan2(n.y, n.x));
    return {glm::degrees(std::asin(std::clamp(n.z, -1.0, 1.0))), lon >= 180 ? lon - 360 : lon};
}

// east, north and up at a point, as the base of pointing() (x right, y ahead, z up): azimuths then run clockwise from north
inline glm::dmat3 enuAxes(const glm::dvec3& p) {
    glm::dvec3 up = glm::normalize(p), east = glm::cross(glm::dvec3(0, 0, 1), up);
    east = glm::length(east) > 1e-12 ? glm::normalize(east) : glm::dvec3(0, 1, 0);
    return {east, glm::cross(up, east), up};
}

// distance along the unit ray o + t d to the sphere of radius r, entering it; -1 when it misses
inline double sphereHit(const glm::dvec3& o, const glm::dvec3& d, double r) {
    double b = glm::dot(o, d), disc = b * b - glm::dot(o, o) + r * r;
    return disc >= 0 && -b - std::sqrt(disc) > 0 ? -b - std::sqrt(disc) : -1;
}

// a point at a distance (km, on the sphere) from the unit vector n toward the unit tangent direction t
inline glm::dvec3 along(const glm::dvec3& n, const glm::dvec3& t, double km) {
    return std::cos(km / MOON_R) * n + std::sin(km / MOON_R) * t;
}

// uniform draws that are the same with every compiler and library
struct Rng {
    std::mt19937_64 g;
    explicit Rng(uint64_t seed) : g(seed) {}
    double operator()(double a = 0, double b = 1) { return a + (b - a) * double(g() >> 11) * 0x1.0p-53; }
};

// true camera: position and axes (x = image right, y = image top, -z = boresight; MOON_ME) and full field of view across the
// square image (degrees). Pixels in the OpenCV convention: +x right, +y down, integer centres, principal point (res - 1) / 2.
struct Pose {
    glm::dvec3 eye{0};
    glm::dmat3 att{1};
    double fov = 60;

    double tanHalf() const { return std::tan(glm::radians(fov) / 2); }
    double focal(int res) const { return res / 2.0 / tanHalf(); }
    glm::dvec3 ray(double x, double y) const { return glm::normalize(-att[2] + tanHalf() * (x * att[0] + y * att[1])); }  // x, y in [-1, 1], y up
    glm::dmat3 cvToMe() const { return {att[0], -att[1], -att[2]}; }
    glm::dvec3 camera(const glm::dvec3& p) const { return glm::transpose(cvToMe()) * (p - eye); }  // OpenCV camera coordinates
    glm::dvec2 pixel(const glm::dvec3& c, int res) const { return focal(res) * glm::dvec2(c) / c.z + (res - 1) / 2.0; }
    // frame-filled rule: the four corner rays hit the Moon sphere
    bool filled() const {
        for (double x : {-1, 1})
            for (double y : {-1, 1})
                if (sphereHit(eye, ray(x, y), MOON_R) < 0) return false;
        return true;
    }
};

// rotation matrix -> quaternion (w, x, y, z), w >= 0
inline glm::dvec4 quaternion(const glm::dmat3& m) {
    double t = m[0][0] + m[1][1] + m[2][2];
    glm::dvec4 q;
    if (t > 0) {
        double s = 2 * std::sqrt(1 + t);
        q = {s / 4, (m[1][2] - m[2][1]) / s, (m[2][0] - m[0][2]) / s, (m[0][1] - m[1][0]) / s};
    } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
        double s = 2 * std::sqrt(1 + m[0][0] - m[1][1] - m[2][2]);
        q = {(m[1][2] - m[2][1]) / s, s / 4, (m[1][0] + m[0][1]) / s, (m[2][0] + m[0][2]) / s};
    } else if (m[1][1] > m[2][2]) {
        double s = 2 * std::sqrt(1 + m[1][1] - m[0][0] - m[2][2]);
        q = {(m[2][0] - m[0][2]) / s, (m[1][0] + m[0][1]) / s, s / 4, (m[2][1] + m[1][2]) / s};
    } else {
        double s = 2 * std::sqrt(1 + m[2][2] - m[0][0] - m[1][1]);
        q = {(m[0][1] - m[1][0]) / s, (m[2][0] + m[0][2]) / s, (m[2][1] + m[1][2]) / s, s / 4};
    }
    return q.x < 0 ? -q : q;
}

// position on a Hilbert curve of side 2^order
inline uint64_t hilbert(uint32_t x, uint32_t y, int order) {
    uint64_t d = 0;
    for (uint32_t s = 1u << (order - 1); s > 0; s /= 2) {
        uint32_t rx = (x & s) > 0, ry = (y & s) > 0;
        d += uint64_t(s) * s * ((3 * rx) ^ ry);
        if (!ry) {
            if (rx) x = s - 1 - x, y = s - 1 - y;
            std::swap(x, y);
        }
    }
    return d;
}
