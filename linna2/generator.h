#pragma once
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include "app.h"
#include "craters.h"
#include "geometry.h"
#include "settings.h"

// one frame: the true camera, the Sun, and where it belongs
struct Shot {
    std::string id, path;                    // frame_id; output file stem relative to the root
    std::string dataset, split, orbit, clip;
    std::string windowSplit, profile;        // S2 windows: val | test, A | B
    Pose pose;
    int fovClass = 0, epoch = -1, rev = -1;  // S2: Sun epoch k; windows: revolution (ascending nodes from the file start)
    double tilt = 0, azimuth = 0, roll = 0;
    bool lvlh = false;               // azimuth from the velocity (S2) instead of from north (S1)
    glm::dvec3 sun{0};               // Sun position, MOON_ME km
    double et = NAN, t = NAN, shift = 0;  // S2: ephemeris time, seconds from the orbit file's first row, Sun epoch shift (days)
    std::string utc;
};

// CSV of the frames written so far in one set; a frame is done once its line is here (written after its PNG and JSON)
class Index {
public:
    explicit Index(const std::string& path);
    ~Index();
    bool done(const std::string& id) const { return ids.count(id) > 0; }
    size_t size() const { return ids.size(); }
    void add(const std::string& id, const std::string& line);

private:
    std::set<std::string> ids;
    FILE* f = nullptr;
    std::mutex m;
};

// rendered frame before labels: the 16-bit image (image_res², top row first) and the direct sunlight pass (render_res², bottom row first)
struct Frame {
    std::vector<uint16_t> image;
    std::vector<float> direct;
    double range = -1, ev = NAN, waited = 0, seconds = 0;  // boresight range (km), exposure (EV, NaN without light), terrain wait and whole shoot (s)
    bool night = false, timeout = false;
};

class Generator {
public:
    const Settings& set;
    Config cfg;
    std::string version, configHash, root;  // root: where frames are written (the run's output folder)
    Terrain terrain;
    std::unique_ptr<Sldem> sldem;
    std::unique_ptr<PolarDem> polar;
    Renderer renderer;
    Catalogue cat;
    std::vector<float> rim;  // e_rim (km above MOON_R) by cid, NaN when not computed

    Generator(const Settings& s, const Config& c, const std::string& version, const std::string& configHash);
    ~Generator();
    void loadCatalogue();  // Robbins with ellipse centres, and e_rim from catalog/robbins_linna2.csv
    void loadRegions();    // splits/regions_raster.npy of regions.py
    int partition(const glm::dvec3& p) const;  // 0 train, 1 val, 2 test
    std::vector<double> lolaHeights(const std::vector<glm::dvec3>& units);  // global LOLA terrain height (km), the same in every run
    // loads the high-resolution terrain within needKm of the path, on windows centred at centre; false after the wait limit
    bool stream(const glm::dvec3& centre, const std::vector<glm::dvec3>& path, double needKm, double& waited);
    Frame shoot(const Shot& s);                            // renders the frame unless it is a night frame
    void write(const Shot& s, const Frame& f, Index& idx);  // labels, PNG, JSON and index line (PNG and JSON on the writer threads)
    void drain();                                          // waits for the writer threads

private:
    struct Counts { int all = 0, pos = 0, ign = 0, bg = 0; };
    std::vector<uint8_t> regions;
    std::string craters(const Shot& s, const Frame& f, const glm::dvec3& centre, double angle, bool dark, Counts& n);
    std::deque<std::function<void()>> jobs;
    std::vector<std::thread> pool;
    std::mutex m;
    std::condition_variable cv, room;
    bool closing = false;
    int busy = 0;
    void push(std::function<void()> job);
};

std::string fmt(const char* f, ...);
std::vector<glm::dvec3> footprint(const Pose& p, int n);  // n x n image points (top row first) on the Moon sphere; zero where they miss
std::filesystem::path u8path(const std::string& s);
extern const char* INDEX_HEADER;
