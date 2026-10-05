#pragma once
#include <functional>
#include <string>
#include <vector>

struct App;

// one camera image per orbit file row from start_utc to images_end_utc, as folder/img/<stem>.png with folder/json/<stem>.json
// (camera with its exposure, Sun) and folder/label/<stem>.txt (the catalogue craters in view, class 0); stem = hh_mm_ss-YYYYMMDD.
// progress(images done, images in all, stage) is called now and then and stops the generation by returning false.
// Returns the number of images written, or -1 with error set.
int generateImages(App& a, const std::string& folder, std::string& error, const std::function<bool(size_t, size_t, const char*)>& progress);

bool writeFile(const std::string& path, const std::string& text);  // UTF-8 path
// sRGB PNG, gray (1 channel) or RGB (3), 8 or 16 bits (native-endian samples); rows top first unless bottomUp (OpenGL order)
bool writePng(const std::string& path, int w, int h, int channels, int depth, const void* pixels, bool bottomUp);
double percentile(std::vector<double> v, double p);  // linearly interpolated, as numpy.percentile
