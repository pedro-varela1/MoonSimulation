#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>

// rim of a catalogue crater: centre and circle diameter, and the ellipse fitted to the rim (axes in km, major axis angle
// counter-clockwise from local east, degrees), in MOON_ME; cid = data row of the catalogue file, from 0
struct Crater { double lat, lon, diam, major, minor, angle; int cid; };

// Robbins (2018) lunar crater catalogue, rows with a circle diameter inside a range, binned on a 2 degree grid
class Catalogue {
public:
    // false when the file cannot be read; ellipse: centres of the ellipse fits instead of the circle fits
    bool load(const std::string& path, glm::dvec2 diameter, bool ellipse = false);
    std::vector<const Crater*> within(const glm::dvec3& n, double angle) const;  // centres within angle (rad) of the unit vector n
    const std::vector<Crater>& all() const { return craters; }
    double maxDiameter = 0;

private:
    std::vector<Crater> craters;
    std::vector<std::vector<int>> cells;
};

struct Enu { glm::dvec3 east, north, up; };
Enu enu(double lat, double lon);  // local axes at a latitude and longitude (degrees)

// image of the ellipse with semi-axes a, b (km) and major axis th (rad) from east toward north, mapped to the image by the
// homography H of its plane: centre (px), semi-axes (px, a >= b) and major axis angle (rad in [0, pi), from +x toward +y);
// false when the image is not a real ellipse
bool imageEllipse(const glm::dmat3& H, double a, double b, double th, double out[5]);
