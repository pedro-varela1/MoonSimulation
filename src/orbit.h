#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>

struct State { glm::dvec3 p, v; };

struct Orbit {
    std::string utc0;
    double period = 0;
    void load(const std::string& path, const glm::dmat3& rot);
    static bool span(const std::string& path, std::string& first, double& seconds);  // first epoch (ISO UTC) and duration, cheap
    State at(double t) const;
    double begin() const { return t.front(); }
    double end() const { return t.back(); }
    const std::vector<double>& times() const { return t; }  // one per file row, seconds from utc0
    std::string utc(double time) const;  // ISO UTC with milliseconds, on the file's own clock, of a time from utc0
private:
    double t0 = 0;  // first row, seconds from 1970 in UTC
    std::vector<double> t;
    std::vector<glm::dvec3> p, v;
};
