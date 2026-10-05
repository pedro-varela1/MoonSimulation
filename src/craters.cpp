#include "craters.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace {

constexpr int CELL = 2, NLAT = 180 / CELL, NLON = 360 / CELL;
constexpr double PI = 3.14159265358979;

glm::dvec3 unit(double lat, double lon) {
    double la = glm::radians(lat), lo = glm::radians(lon);
    return {std::cos(la) * std::cos(lo), std::cos(la) * std::sin(lo), std::sin(la)};
}

int cellRow(double lat) { return std::clamp(int(std::floor((lat + 90) / CELL)), 0, NLAT - 1); }

}

bool Catalogue::load(const std::string& path, glm::dvec2 diameter, bool ellipse) {
    std::ifstream f(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary | std::ios::ate);
    if (!f) return false;
    std::string buf(f.tellg(), '\0');
    f.seekg(0).read(buf.data(), buf.size());
    craters.clear();
    cells.assign(NLAT * NLON, {});
    maxDiameter = 0;

    // columns found by name in the header; rows with a missing value are skipped
    const char* names[6] = {ellipse ? "LAT_ELLI_IMG" : "LAT_CIRC_IMG", ellipse ? "LON_ELLI_IMG" : "LON_CIRC_IMG", "DIAM_CIRC_IMG",
                            "DIAM_ELLI_MAJOR_IMG", "DIAM_ELLI_MINOR_IMG", "DIAM_ELLI_ANGLE_IMG"};
    int col[6], last = 0, row = -1;
    size_t eol = buf.find('\n');
    std::vector<std::string> fields;
    for (size_t a = 0, b = 0; b < eol; a = b + 1) fields.push_back(buf.substr(a, (b = std::min(buf.find(',', a), eol)) - a));
    for (int k = 0; k < 6; k++) {
        auto it = std::find(fields.begin(), fields.end(), names[k]);
        if (it == fields.end()) return false;
        last = std::max(last, col[k] = int(it - fields.begin()));
    }
    for (size_t s = eol + 1; s < buf.size();) {
        size_t e = std::min(buf.find('\n', s), buf.size());
        const char *p = buf.data() + s, *end = buf.data() + e;
        s = e + 1;
        if (p == end || *p == '\r') continue;
        row++;
        double v[6];
        int got = 0;
        for (int c = 0; c <= last && p < end; c++) {
            const char* q = std::find(p, end, ',');
            for (int k = 0; k < 6; k++)
                if (col[k] == c && std::from_chars(p, q, v[k]).ec == std::errc()) got++;
            p = q + 1;
        }
        if (got < 6 || v[2] < diameter.x || v[2] > diameter.y) continue;
        Crater cr{v[0], v[1], v[2], v[3], v[4], v[5], row};
        cells[cellRow(cr.lat) * NLON + int(std::fmod(cr.lon + 360, 360.0) / CELL) % NLON].push_back(int(craters.size()));
        craters.push_back(cr);
        maxDiameter = std::max(maxDiameter, cr.diam);
    }
    return true;
}

std::vector<const Crater*> Catalogue::within(const glm::dvec3& n, double angle) const {
    std::vector<const Crater*> out;
    double lat = glm::degrees(std::asin(std::clamp(n.z, -1.0, 1.0))), lon = glm::degrees(std::atan2(n.y, n.x)), a = glm::degrees(angle);
    double edge = std::min(std::max(std::abs(lat - a), std::abs(lat + a)), 90.0);
    double dlon = edge >= 89.9 ? 180 : std::min(a / std::cos(glm::radians(edge)), 180.0), c = std::cos(angle);
    int j0 = int(std::floor((lon - dlon + 720) / CELL)), nj = std::min(NLON, int(std::floor((lon + dlon + 720) / CELL)) - j0 + 1);
    for (int i = cellRow(lat - a); i <= cellRow(lat + a); i++)
        for (int j = j0; j < j0 + nj; j++)
            for (int k : cells[i * NLON + j % NLON])
                if (glm::dot(unit(craters[k].lat, craters[k].lon), n) >= c) out.push_back(&craters[k]);
    return out;
}

Enu enu(double lat, double lon) {
    double la = glm::radians(lat), lo = glm::radians(lon);
    glm::dvec3 up(std::cos(la) * std::cos(lo), std::cos(la) * std::sin(lo), std::sin(la)), east(-std::sin(lo), std::cos(lo), 0);
    return {east, glm::cross(up, east), up};
}

bool imageEllipse(const glm::dmat3& H, double a, double b, double th, double out[5]) {
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
