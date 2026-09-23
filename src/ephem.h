#pragma once
#include <glm/glm.hpp>
#include <string>

namespace ephem {
void init(const std::string& kernelDir);
double et(const std::string& utc);
std::string utc(double et);
glm::dvec3 sun(double et);
glm::dvec3 earth(double et);
glm::dmat3 paToMe();
}
