#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <vector>
#include "config.h"
#include "terrain.h"

struct Camera { glm::dvec3 eye, right, up, fwd; double tanX, tanY; };

struct Scene {
    Camera cam, view;
    glm::dvec3 sat, sun, earth;
    glm::dmat3 att;
    std::vector<glm::dvec3> track;
    int now = 0;
    double dt = 0;
};

class Renderer {
public:
    float satVis = 1, satAbove = 0, ev = 0;
    void init(const Config& c, const Terrain& t, const Sldem& s);
    void initCameraContext();
    void draw(const Scene& s, glm::ivec2 fb);
    void present(glm::ivec2 fb);

private:
    const Config* cfg = nullptr;
    const Terrain* terrain = nullptr;
    const Sldem* sldem = nullptr;
    GLuint moon = 0, show = 0, overlay = 0, hist = 0, resolve = 0, probe = 0, vao = 0, vaoCam = 0;
    GLuint frameUbo = 0, camUbo = 0, viewUbo = 0, probeBuf = 0, expBuf = 0, trackBuf = 0, meshBuf = 0;
    GLuint camTex = 0, camFbo = 0, viewColor = 0, viewDepth = 0, viewFbo = 0, polarTex = 0, polarAlb = 0;
    const float *probeMap = nullptr, *expMap = nullptr;
    glm::ivec2 viewSize{0};
    int meshVerts = 0;
    float meshRadius = 0;
    GLsync fence = nullptr;
    void loadObj(const std::string& path);
};
