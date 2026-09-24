#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>
#include "config.h"

struct Terrain {
    GLuint lola = 0, albedo = 0, hapke = 0, earth = 0;
    void load(const Config& c);
};

class Streamer {
public:
    GLuint tex = 0, cover = 0;
    int resident = 0, pending = 0;
    virtual ~Streamer() = default;
    void update(const glm::dvec3& center, const std::vector<glm::dvec3>& path, double needKm, bool active, double dt);
    glm::vec4 window() const { return {float(x0), float(y0), active ? 1.f : 0.f, float(tag)}; }

protected:
    struct Job { uint32_t id; std::shared_ptr<std::vector<char>> buf; size_t at, size; };
    const int NX, NY, WX, WY;
    const bool wrap;
    Streamer(int nx, int ny, int wx, int wy, bool wrap);
    void start();
    void stop();
    uint32_t id(int tag, int tx, int ty) const { return uint32_t((tag * NY + ty) * NX + tx); }
    virtual bool place(const glm::dvec3& c, int& x, int& y, int& tag) const = 0;
    virtual glm::dvec3 tileCenter(int tag, int tx, int ty) const = 0;
    virtual int runKey(int) const { return 0; }
    virtual void read(int tag, int ty, int tx0, int tx1, std::vector<Job>& out) = 0;
    virtual bool decode(const Job& j, float* km) const = 0;

private:
    struct Tile { uint32_t id; std::vector<int16_t> data; bool valid; };
    int x0 = 0, y0 = 0, tag = 0;
    bool active = false, placed = false, running = true;
    std::vector<long long> slot;
    std::vector<float> fade;
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

class Sldem final : public Streamer {
public:
    explicit Sldem(const std::string& dir);
    ~Sldem() override { stop(); }

private:
    std::string dir;
    std::ifstream files[32];
    std::vector<uint64_t> off[32], cnt[32];
    bool place(const glm::dvec3& c, int& x, int& y, int& tag) const override;
    glm::dvec3 tileCenter(int tag, int tx, int ty) const override;
    int runKey(int tx) const override { return tx / 90; }
    void read(int tag, int ty, int tx0, int tx1, std::vector<Job>& out) override;
    bool decode(const Job& j, float* km) const override;
};

class PolarDem final : public Streamer {
public:
    explicit PolarDem(const std::string& dir);
    ~PolarDem() override { stop(); }

private:
    std::string dir;
    std::ifstream files[2];
    bool place(const glm::dvec3& c, int& x, int& y, int& tag) const override;
    glm::dvec3 tileCenter(int tag, int tx, int ty) const override;
    void read(int tag, int ty, int tx0, int tx1, std::vector<Job>& out) override;
    bool decode(const Job& j, float* km) const override;
};
