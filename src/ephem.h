#pragma once
#include <glm/glm.hpp>
#include <array>
#include <string>
#include <vector>

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
