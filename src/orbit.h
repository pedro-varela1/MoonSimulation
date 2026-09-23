#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>

struct State { glm::dvec3 p, v; };

struct Orbit {
    std::string utc0;
    double period = 0;
    void load(const std::string& path, const glm::dmat3& rot);
    State at(double t) const;
    double begin() const { return t.front(); }
    double end() const { return t.back(); }
private:
    std::vector<double> t;
    std::vector<glm::dvec3> p, v;
};
