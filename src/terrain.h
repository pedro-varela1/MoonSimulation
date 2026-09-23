#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <array>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>
#include "config.h"

struct Terrain {
    GLuint lola = 0, albedo = 0, hapke = 0;
    void load(const Config& c);
};

class Sldem {
public:
    GLuint tex = 0, cover = 0;
    int resident = 0, pending = 0;
    explicit Sldem(const std::string& dir);
    ~Sldem();
    void update(const glm::dvec3& center, const std::vector<glm::dvec3>& path, double needKm, bool active, double dt);
    glm::vec4 window() const { return {float(x0), float(y0), active ? 1.f : 0.f, 0.f}; }

private:
    struct Tile { uint32_t id; std::vector<int16_t> data; bool valid; };
    struct Job { uint32_t id; std::shared_ptr<std::vector<char>> buf; size_t at, size; };
    std::string dir;
    int x0 = 0, y0 = 0;
    bool active = false, placed = false, running = true;
    std::array<long long, 64 * 32> slot;
    std::array<float, 64 * 32> fade{};
    std::unordered_set<uint32_t> dead, inflight;
    std::vector<uint32_t> need;
    std::deque<Job> jobs;
    std::vector<Tile> ready;
    std::mutex m;
    std::condition_variable readCv, decodeCv;
    std::vector<std::thread> threads;
    void reader();
    void decoder();
};
