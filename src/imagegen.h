#pragma once
#include <functional>
#include <string>

struct App;

// one camera image per orbit file row from start_utc to images_end_utc, as folder/img/<stem>.png with folder/json/<stem>.json
// (camera with its exposure, Sun) and folder/label/<stem>.txt (the catalogue craters in view, class 0); stem = hh_mm_ss-YYYYMMDD.
// progress(images done, images in all, stage) is called now and then and stops the generation by returning false.
// Returns the number of images written, or -1 with error set.
int generateImages(App& a, const std::string& folder, std::string& error, const std::function<bool(size_t, size_t, const char*)>& progress);
