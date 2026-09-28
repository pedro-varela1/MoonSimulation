#include "ephem.h"
#include <SpiceUsr.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string_view>
#include <glm/gtc/type_ptr.hpp>

namespace ephem {

namespace {

constexpr double AU = 149597870.7, TSUN = 5772;

glm::dvec3 position(const char* body, double et, const char* frame = "MOON_ME") {
    SpiceDouble p[3], lt;
    spkpos_c(body, et, frame, "LT", "MOON", p, &lt);
    return {p[0], p[1], p[2]};
}

// effective temperature from the spectral type (Pecaut & Mamajek dwarf scale, every 5 subclasses from O0 to M10)
double teff(const char* sp) {
    static constexpr double T[15] = {50000, 41400, 31400, 15700, 9700, 8080, 7220, 6510, 5920, 5660, 5280, 4450, 3850, 3060, 2300};
    while (*sp && !std::isupper(static_cast<unsigned char>(*sp))) sp++;
    size_t k = std::string_view("OBAFGKM").find(*sp);
    if (k == std::string_view::npos) return *sp == 'W' ? 45000 : *sp == 'D' ? 10000 : *sp && std::strchr("CRNS", *sp) ? 3000 : TSUN;
    double x = (k * 10.0 + (std::isdigit(static_cast<unsigned char>(sp[1])) ? std::strtod(sp + 1, nullptr) : 5)) / 5;
    int i = std::min(int(x), 13);
    return glm::mix(T[i], T[i + 1], x - i);
}

}

void init(const std::string& dir) {
    for (auto& f : std::filesystem::directory_iterator(dir)) furnsh_c(f.path().string().c_str());
}

double et(const std::string& utc) {
    SpiceDouble e;
    str2et_c(utc.c_str(), &e);
    return e;
}

bool tryEt(const std::string& utc, double& e) {
    char ret[] = "RETURN", abort[] = "ABORT", none[] = "NONE", all[] = "DEFAULT";
    erract_c("SET", 0, ret);
    errprt_c("SET", 0, none);
    str2et_c(utc.c_str(), &e);
    bool ok = !failed_c();
    reset_c();
    erract_c("SET", 0, abort);
    errprt_c("SET", 0, all);
    return ok;
}

std::string utc(double et) {
    char s[32];
    et2utc_c(et, "ISOC", 0, sizeof s, s);
    return s;
}

glm::dvec3 sun(double et) { return position("SUN", et); }
glm::dvec3 earth(double et) { return position("EARTH", et); }
glm::dmat3 rotation(const char* from, const char* to, double et) {
    SpiceDouble m[3][3];
    pxform_c(from, to, et, m);
    return glm::transpose(glm::make_mat3(&m[0][0]));
}

// SPICE type 1 star catalog (EK), proper motion applied up to et
std::vector<Source> stars(const std::string& catalog, double et) {
    furnsh_c(catalog.c_str());
    SpiceInt n;
    SpiceBoolean bad, null, found;
    char msg[1024];
    ekfind_c("SELECT RA, DEC, RA_PM, DEC_PM, RA_EPOCH, DEC_EPOCH, VISUAL_MAGNITUDE, SPECTRAL_TYPE FROM HIPPARCOS", sizeof msg, &n, &bad, msg);
    if (bad) { fprintf(stderr, "estrelas: %s\n", msg); exit(1); }
    double years = et / (86400 * 365.25) + 50;
    std::vector<Source> out(n);
    for (int r = 0; r < n; r++) {
        SpiceDouble v[7];
        char sp[8];
        for (int c = 0; c < 7; c++) ekgd_c(c, r, 0, &v[c], &null, &found);
        ekgc_c(7, r, 0, sizeof sp, sp, &null, &found);
        double ra = glm::radians(v[0] + (years - v[4]) * v[2]), dec = glm::radians(v[1] + (years - v[5]) * v[3]);
        out[r] = {glm::dvec4(std::cos(dec) * std::cos(ra), std::cos(dec) * std::sin(ra), std::sin(dec), 0), v[6], teff(sp)};
    }
    unload_c(catalog.c_str());
    return out;
}

// Mercury to Saturn, V magnitudes from Mallama & Hilton (2018)
std::array<Source, 5> planets(double et) {
    static const char* NAMES[5] = {"MERCURY", "VENUS", "MARS BARYCENTER", "JUPITER BARYCENTER", "SATURN BARYCENTER"};
    static constexpr double BV[5] = {0.93, 0.82, 1.36, 0.83, 1.04};
    glm::dvec3 sun = position("SUN", et, "J2000");
    std::array<Source, 5> out;
    for (int i = 0; i < 5; i++) {
        glm::dvec3 p = position(NAMES[i], et, "J2000"), s = sun - p;
        double dp = glm::length(p), ds = glm::length(s), a = glm::degrees(std::acos(glm::clamp(glm::dot(s, -p) / (ds * dp), -1.0, 1.0)));
        double v = 5 * std::log10(ds * dp / (AU * AU));
        switch (i) {
        case 0: v += -0.613 + a * (6.328e-2 + a * (-1.6336e-3 + a * (3.3644e-5 + a * (-3.4265e-7 + a * (1.6893e-9 - a * 3.0334e-12))))); break;
        case 1: v += a <= 163.7 ? -4.384 + a * (-1.044e-3 + a * (3.687e-4 + a * (-2.814e-6 + a * 8.938e-9))) : 236.05828 + a * (-2.81914 + a * 8.39034e-3); break;
        case 2: v += a <= 50 ? -1.601 + a * (2.267e-2 - a * 1.302e-4) : -0.367 + a * (-2.573e-2 + a * 3.445e-4); break;
        case 3: v += -9.395 + a * (-3.7e-4 + a * 6.16e-4); break;
        case 4: {
            glm::dvec3 pole = rotation("IAU_SATURN", "J2000", et)[2];
            double so = -glm::dot(pole, p) / dp, ss = glm::dot(pole, s) / ds, sb = so * ss > 0 ? std::abs(so) : 0;  // rings lit face
            v += -8.914 - 1.825 * sb + 0.026 * a - 0.378 * sb * std::exp(-2.25 * a);
        }
        }
        out[i] = {glm::dvec4(p, 1), v, 4600 * (1 / (0.92 * BV[i] + 1.7) + 1 / (0.92 * BV[i] + 0.62))};
    }
    return out;
}

}
