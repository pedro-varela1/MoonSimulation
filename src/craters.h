#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>

// rim of a catalogue crater: circle centre and diameter, and the ellipse fitted to the rim (axes in km, major axis angle
// counter-clockwise from local east, degrees), in MOON_ME
struct Crater { double lat, lon, diam, major, minor, angle; };

// Robbins (2018) lunar crater catalogue, rows with a circle diameter inside a range, binned on a 2 degree grid
class Catalogue {
public:
    bool load(const std::string& path, glm::dvec2 diameter);  // false when the file cannot be read
    std::vector<const Crater*> within(const glm::dvec3& n, double angle) const;  // centres within angle (rad) of the unit vector n
    double maxDiameter = 0;

private:
    std::vector<Crater> craters;
    std::vector<std::vector<int>> cells;
};
