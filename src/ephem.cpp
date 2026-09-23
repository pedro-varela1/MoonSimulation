#include "ephem.h"
#include <SpiceUsr.h>
#include <filesystem>
#include <glm/gtc/type_ptr.hpp>

namespace ephem {

void init(const std::string& dir) {
    for (auto& f : std::filesystem::directory_iterator(dir)) furnsh_c(f.path().string().c_str());
}

double et(const std::string& utc) {
    SpiceDouble e;
    str2et_c(utc.c_str(), &e);
    return e;
}

std::string utc(double et) {
    char s[32];
    et2utc_c(et, "ISOC", 0, sizeof s, s);
    return s;
}

static glm::dvec3 position(const char* body, double et) {
    SpiceDouble p[3], lt;
    spkpos_c(body, et, "MOON_ME", "LT", "MOON", p, &lt);
    return {p[0], p[1], p[2]};
}

glm::dvec3 sun(double et) { return position("SUN", et); }
glm::dvec3 earth(double et) { return position("EARTH", et); }

glm::dmat3 paToMe() {
    SpiceDouble m[3][3];
    pxform_c("MOON_PA", "MOON_ME", 0, m);
    return glm::transpose(glm::make_mat3(&m[0][0]));
}

}
