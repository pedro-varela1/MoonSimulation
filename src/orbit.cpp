#include "orbit.h"
#include "platform.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <thread>

namespace {

constexpr double R = 1737.4, WINDOW = 61;
constexpr size_t SIDE = 4;  // fewest samples on each side of a fit window

long long daysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    long long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = unsigned(y - era * 400), doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
}

template <class T> const char* parse(const char* s, const char* e, T& v) {
    while (s < e && (*s == ' ' || *s == '\t' || *s == ':')) s++;
    auto r = std::from_chars(s, e, v);
    return r.ec == std::errc() ? r.ptr : nullptr;
}

// STK report row: "7 Aug 2029 00:00:00.000  lat  lon  alt"
struct Row {
    int d, mo, y, hh, mm;
    double ss, lat, lon, alt;
    double time() const { return daysFromCivil(y, mo, d) * 86400.0 + hh * 3600 + mm * 60 + ss; }
    std::string iso() const {
        char s[40];
        snprintf(s, sizeof s, "%04d-%02d-%02dT%02d:%02d:%06.3f", y, mo, d, hh, mm, ss);
        return s;
    }
};

bool parseRow(const char* q, const char* e, Row& r) {
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    if (!(q = parse(q, e, r.d))) return false;
    while (q < e && *q == ' ') q++;
    const char* m = e - q > 3 ? std::search(months, months + 36, q, q + 3) : months + 36;
    r.mo = int(m - months) / 3 + 1;
    return m < months + 36 && (q = parse(q + 3, e, r.y)) && (q = parse(q, e, r.hh)) && (q = parse(q, e, r.mm)) &&
           (q = parse(q, e, r.ss)) && (q = parse(q, e, r.lat)) && (q = parse(q, e, r.lon)) && parse(q, e, r.alt);
}

// calls fn(row) for every data row after the "----" header line
template <class F> void rows(const std::string& buf, bool header, F fn) {
    bool data = !header;
    for (const char *s = buf.data(), *end = s + buf.size(); s < end;) {
        const char* e = static_cast<const char*>(memchr(s, '\n', end - s));
        if (!e) e = end;
        const char* q = s;
        s = e + 1;
        Row r;
        if (!data) { while (q < e && *q == ' ') q++; data = e - q >= 4 && !strncmp(q, "----", 4); }
        else if (parseRow(q, e, r)) fn(r);
    }
}

void solve5(double A[5][5], double B[5][3]) {
    for (int c = 0; c < 5; c++) {
        int piv = c;
        for (int r = c + 1; r < 5; r++) if (std::abs(A[r][c]) > std::abs(A[piv][c])) piv = r;
        std::swap(A[c], A[piv]);
        std::swap(B[c], B[piv]);
        for (int r = 0; r < 5; r++) {
            if (r == c) continue;
            double f = A[r][c] / A[c][c];
            for (int k = c; k < 5; k++) A[r][k] -= f * A[c][k];
            for (int k = 0; k < 3; k++) B[r][k] -= f * B[c][k];
        }
    }
    for (int r = 0; r < 5; r++) for (int k = 0; k < 3; k++) B[r][k] /= A[r][r];
}

}

void Orbit::load(const std::string& path, const glm::dmat3& rot) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) fatal("Could not open the orbit file " + path);
    std::string buf(f.tellg(), '\0');
    f.seekg(0).read(buf.data(), buf.size());

    std::vector<glm::dvec3> raw;
    double t0 = 0;
    t.clear();
    rows(buf, true, [&](const Row& w) {
        if (t.empty()) t0 = w.time(), utc0 = w.iso();
        double la = glm::radians(w.lat), lo = glm::radians(w.lon), r = R + w.alt;
        t.push_back(w.time() - t0);
        raw.push_back(rot * (r * glm::dvec3(std::cos(la) * std::cos(lo), std::cos(la) * std::sin(lo), std::sin(la))));
    });
    size_t n = t.size();
    if (n < 5) fatal("The orbit file has too few valid rows: " + path);

    p.resize(n);
    v.resize(n);
    auto fit = [&](size_t a, size_t b) {
        for (size_t i = a; i < b; i++) {
            // ±WINDOW/2, widened to SIDE samples on each side (2·SIDE+1 at the ends of the file) so sparse, irregular reports stay well posed
            size_t lo = std::lower_bound(t.begin(), t.end(), t[i] - WINDOW / 2) - t.begin();
            size_t hi = std::upper_bound(t.begin(), t.end(), t[i] + WINDOW / 2) - t.begin();
            lo = std::min(lo, i - std::min(i, SIDE)), hi = std::max(hi, std::min(n, i + SIDE + 1));
            lo = std::min(lo, hi - std::min(hi, 2 * SIDE + 1)), hi = std::max(hi, std::min(n, lo + 2 * SIDE + 1));
            double hw = std::max(t[i] - t[lo], t[hi - 1] - t[i]), A[5][5] = {}, B[5][3] = {};
            for (size_t j = lo; j < hi; j++) {
                double x = (t[j] - t[i]) / hw, phi[5] = {1, x, x * x, x * x * x, x * x * x * x};
                for (int r = 0; r < 5; r++) {
                    for (int c = 0; c < 5; c++) A[r][c] += phi[r] * phi[c];
                    for (int k = 0; k < 3; k++) B[r][k] += phi[r] * raw[j][k];
                }
            }
            solve5(A, B);
            p[i] = {B[0][0], B[0][1], B[0][2]};
            v[i] = glm::dvec3(B[1][0], B[1][1], B[1][2]) / hw;
        }
    };
    std::vector<std::thread> pool;
    unsigned nt = std::max(1u, std::thread::hardware_concurrency());
    for (unsigned k = 0; k < nt; k++) pool.emplace_back(fit, n * k / nt, n * (k + 1) / nt);
    for (auto& th : pool) th.join();

    std::vector<double> nodes;
    for (size_t i = 0; i + 1 < n && nodes.size() < 2; i++)
        if (p[i].z < 0 && p[i + 1].z >= 0) nodes.push_back(t[i] + (t[i + 1] - t[i]) * -p[i].z / (p[i + 1].z - p[i].z));
    period = nodes.size() == 2 ? nodes[1] - nodes[0] : end() - begin();
    printf("orbita: %zu amostras, %.0f s, periodo %.1f s\n", n, end() - begin(), period);
    fflush(stdout);
}

bool Orbit::span(const std::string& path, std::string& first, double& seconds) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    size_t size = f.tellg();
    std::string head(std::min<size_t>(size, 1 << 16), '\0'), tail(std::min<size_t>(size, 4096), '\0');
    f.seekg(0).read(head.data(), head.size());
    f.seekg(size - tail.size()).read(tail.data(), tail.size());
    Row a{}, b{};
    bool found = false;
    rows(head, true, [&](const Row& r) { if (!found) a = r, found = true; });
    rows(tail, false, [&](const Row& r) { b = r; });
    if (!found || b.time() <= a.time()) return false;
    first = a.iso();
    seconds = b.time() - a.time();
    return true;
}

State Orbit::at(double x) const {
    size_t k = std::clamp<size_t>(std::upper_bound(t.begin(), t.end(), x) - t.begin(), 1, t.size() - 1) - 1;
    double h = t[k + 1] - t[k], s = (x - t[k]) / h, s2 = s * s, s3 = s2 * s;
    return {(2 * s3 - 3 * s2 + 1) * p[k] + (s3 - 2 * s2 + s) * h * v[k] + (3 * s2 - 2 * s3) * p[k + 1] + (s3 - s2) * h * v[k + 1],
            ((6 * s2 - 6 * s) * p[k] + (3 * s2 - 4 * s + 1) * h * v[k] + (6 * s - 6 * s2) * p[k + 1] + (3 * s2 - 2 * s) * h * v[k + 1]) / h};
}
