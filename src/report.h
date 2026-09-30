#pragma once
#include <functional>
#include <string>

struct App;

// orbit report: one CSV line per orbit file row with the time, the orbit number, the satellite, the Sun and the camera;
// progress(rows done) is called now and then; false when the file cannot be written
bool writeReport(App& a, const std::string& path, const std::function<void(size_t)>& progress);
