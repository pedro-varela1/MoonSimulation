#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <vector>
#include "config.h"
#include "ephem.h"
#include "terrain.h"

struct Camera { glm::dvec3 eye, right, up, fwd; double tanX, tanY; };

struct Scene {
    Camera cam, view;
    glm::dvec3 sat, sun, earth;
    glm::dmat3 att, j2000ToMe, earthToMe;
    std::array<ephem::Source, 5> planets;
    std::vector<glm::dvec3> track;
    int now = 0;
    double dt = 0;
    float satScale = 1;    // satellite drawn at this many times its real size in the orbit view
    bool showFov = false;  // camera field of view drawn in the orbit view
    bool cameraOnly = false;  // image generation: no orbit view
};

// quality from the video memory the driver reports (NVIDIA dedicated, AMD free): 0 high, 1 medium, 2 low (integrated or unknown)
int detectQuality(double& gb);

// found at a point: Sun visibility (0 eclipse .. 1), height above the terrain (km), albedo I/F below it (643, 566, 415 nm)
struct Probe { float vis, above; glm::vec3 albedo; };

class Renderer {
public:
    Probe probed{1, 0, glm::vec3(0)};  // at the satellite, one frame late
    float ev = 0;
    void init(const Config& c, const Terrain& t, const Streamer& sldem, const Streamer& polar, const std::vector<ephem::Source>& stars);
    void buildCaps();
    void draw(const Scene& s, glm::ivec2 orbitSize);  // orbit view and camera image, both sRGB RGBA8 textures
    // many points at once, with the global terrain only (not the streamed windows); positions and Sun positions in MOON_ME, km
    std::vector<Probe> probeAll(const std::vector<glm::dvec3>& pos, const std::vector<glm::dvec3>& sun);
    // terrain height (km) at unit vectors (MOON_ME) with the terrain of the last frame and, if sun, the Sun's visible fraction there
    std::vector<glm::vec2> sample(const std::vector<glm::vec3>& n, bool sun);
    float exposure();                   // EV of the last camera image, once the GPU is done with it
    std::vector<uint8_t> cameraPixels();  // last camera image as shown: sRGB RGB rows, bottom row first
    GLuint orbitImage() const { return viewColor; }
    GLuint cameraImage() const { return camView; }

private:
    const Config* cfg = nullptr;
    const Terrain* terrain = nullptr;
    const Streamer *sldem = nullptr, *polar = nullptr;
    GLuint moon = 0, sky = 0, show = 0, overlay = 0, hist = 0, resolve = 0, probe = 0, sampler = 0, vao = 0;
    GLuint frameUbo = 0, camUbo = 0, viewUbo = 0, probeBuf = 0, probeIn = 0, expBuf = 0, trackBuf = 0, fovBuf = 0, meshBuf = 0, skyBuf = 0;
    GLuint camTex = 0, camDepth = 0, camFbo = 0, camView = 0, camViewFbo = 0, viewColor = 0, viewDepth = 0, viewFbo = 0, capH = 0, capA = 0;
    const float *probeMap = nullptr, *expMap = nullptr;
    glm::ivec2 viewSize{0};
    int meshVerts = 0, stars = 0;
    void loadObj(const std::string& path);
    void bindData();  // terrain textures and frame uniforms
    void moonAndSky();
    void drawOrbit(const Scene& s, glm::ivec2 fb);
};
