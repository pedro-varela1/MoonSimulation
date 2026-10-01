#pragma once
#include <glm/glm.hpp>
#include <array>
#include <string>
#include <vector>

// reference sphere of the maps and altitudes, Earth and Sun radii, astronomical unit (km); solar effective temperature (K)
constexpr double MOON_R = 1737.4, EARTH_R = 6371, SUN_R = 695700, AU = 149597870.7, TSUN = 5772;

namespace ephem {
// point source in J2000: p.w = 0 unit direction (star), p.w = 1 position in km from the Moon (planet)
struct Source { glm::dvec4 p; double vmag, teff; };

void init(const std::string& kernelDir);
double et(const std::string& utc);
bool tryEt(const std::string& utc, double& et);  // false instead of aborting on an invalid time string
std::string utc(double et);
glm::dvec3 sun(double et);
glm::dvec3 earth(double et);
glm::dmat3 rotation(const char* from, const char* to, double et);
std::vector<Source> stars(const std::string& catalog, double et);
std::array<Source, 5> planets(double et);
}
