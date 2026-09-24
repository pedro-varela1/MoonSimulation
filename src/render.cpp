#include "render.h"
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstdio>
#include <fstream>

namespace {

struct FrameUBO { glm::vec4 sunDir, earthDir, light, sldemWin, polarWin; glm::mat3x4 earthToMe; };
struct ViewUBO { glm::vec4 eye, right, up, fwd, size; };
constexpr double PI = 3.14159265358979, AU = 149597870.7, RSUN = 695700, REARTH = 6371, ZNEAR = 1e-5, VSUN = -26.74, TSUN = 5772;
constexpr double STAR_PEAK = 0.009;  // orbit window: linear display value of a star at orbit_star_mag (sRGB ~24/255)

std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { fprintf(stderr, "nao foi possivel abrir %s\n", path.c_str()); exit(1); }
    return {std::istreambuf_iterator<char>(f), {}};
}

GLuint program(std::initializer_list<std::pair<GLenum, const char*>> stages, const char* defs = "") {
    static const std::string common = readFile("shaders/common.glsl");
    GLuint p = glCreateProgram();
    GLint ok;
    char log[8192];
    for (auto [type, file] : stages) {
        std::string src = std::string("#version 460 core\n") + defs + "#line 1 1\n" + common + "\n#line 1 2\n" + readFile(std::string("shaders/") + file);
        const char* s = src.c_str();
        GLuint sh = glCreateShader(type);
        glShaderSource(sh, 1, &s, nullptr);
        glCompileShader(sh);
        glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok) { glGetShaderInfoLog(sh, sizeof log, nullptr, log); fprintf(stderr, "%s (1 = common.glsl, 2 = %s):\n%s\n", file, file, log); exit(1); }
        glAttachShader(p, sh);
        glDeleteShader(sh);
    }
    glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { glGetProgramInfoLog(p, sizeof log, nullptr, log); fprintf(stderr, "link:\n%s\n", log); exit(1); }
    return p;
}

GLuint buffer(size_t size, const void* data, GLbitfield flags) {
    GLuint b;
    glCreateBuffers(1, &b);
    glNamedBufferStorage(b, size, data, flags);
    return b;
}

// gain > 0: illustrative view with a fixed display gain and a fixed star scale; gain = 0: physical, auto-exposed
ViewUBO viewData(const Camera& c, glm::vec2 size, float gain, float starScale) {
    return {glm::vec4(glm::vec3(c.eye), gain), glm::vec4(glm::vec3(c.right), c.tanX), glm::vec4(glm::vec3(c.up), c.tanY),
            glm::vec4(glm::vec3(c.fwd), ZNEAR), glm::vec4(size, starScale, 0)};
}

// pi x irradiance relative to the Sun at 1 AU in each band (643/566/415 nm): V magnitude, blackbody color relative to the Sun
glm::vec4 flux(const ephem::Source& s) {
    auto rel = [&](double nm) { return std::expm1(1.4387769e7 / (nm * TSUN)) / std::expm1(1.4387769e7 / (nm * s.teff)); };
    double f = PI * std::pow(10, -0.4 * (s.vmag - VSUN)) / rel(551);
    return {f * rel(643), f * rel(566), f * rel(415), 0};
}

}

void Renderer::init(const Config& c, const Terrain& t, const Streamer& s, const Streamer& p, const std::vector<ephem::Source>& catalog) {
    cfg = &c;
    terrain = &t;
    sldem = &s;
    polar = &p;
    moon = program({{GL_VERTEX_SHADER, "fullscreen.vert"}, {GL_FRAGMENT_SHADER, "moon.frag"}});
    sky = program({{GL_VERTEX_SHADER, "sky.vert"}, {GL_FRAGMENT_SHADER, "sky.frag"}});
    show = program({{GL_VERTEX_SHADER, "fullscreen.vert"}, {GL_FRAGMENT_SHADER, "present.frag"}});
    overlay = program({{GL_VERTEX_SHADER, "overlay.vert"}, {GL_FRAGMENT_SHADER, "overlay.frag"}});
    hist = program({{GL_COMPUTE_SHADER, "exposure.comp"}}, "#define STAGE 0\n");
    resolve = program({{GL_COMPUTE_SHADER, "exposure.comp"}}, "#define STAGE 1\n");
    probe = program({{GL_COMPUTE_SHADER, "probe.comp"}});
    glCreateVertexArrays(1, &vao);
    frameUbo = buffer(sizeof(FrameUBO), nullptr, GL_DYNAMIC_STORAGE_BIT);
    camUbo = buffer(sizeof(ViewUBO), nullptr, GL_DYNAMIC_STORAGE_BIT);
    viewUbo = buffer(sizeof(ViewUBO), nullptr, GL_DYNAMIC_STORAGE_BIT);
    const GLbitfield map = GL_MAP_READ_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
    float probe0[2] = {1, 0}, exp0[132] = {};
    exp0[128] = 3;
    probeBuf = buffer(sizeof probe0, probe0, map);
    expBuf = buffer(sizeof exp0, exp0, map);
    probeMap = static_cast<const float*>(glMapNamedBufferRange(probeBuf, 0, sizeof probe0, map));
    expMap = static_cast<const float*>(glMapNamedBufferRange(expBuf, 0, sizeof exp0, map));
    trackBuf = buffer(sizeof(glm::vec4) * 1025, nullptr, GL_DYNAMIC_STORAGE_BIT);
    std::vector<glm::vec4> pts;
    for (auto& src : catalog) pts.insert(pts.end(), {glm::vec4(src.p), flux(src)});
    stars = int(catalog.size());
    pts.resize(2 * (stars + 5));
    skyBuf = buffer(pts.size() * sizeof(glm::vec4), pts.data(), GL_DYNAMIC_STORAGE_BIT);
    glCreateTextures(GL_TEXTURE_2D, 1, &camTex);
    glTextureStorage2D(camTex, 1, GL_RGBA32F, c.sensor.x, c.sensor.y);
    glTextureParameteri(camTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(camTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(camTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glCreateTextures(GL_TEXTURE_2D, 1, &camDepth);
    glTextureStorage2D(camDepth, 1, GL_DEPTH_COMPONENT32F, c.sensor.x, c.sensor.y);
    glCreateFramebuffers(1, &camFbo);
    glNamedFramebufferTexture(camFbo, GL_COLOR_ATTACHMENT0, camTex, 0);
    glNamedFramebufferTexture(camFbo, GL_DEPTH_ATTACHMENT, camDepth, 0);
    glCreateFramebuffers(1, &viewFbo);
    glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
    glEnable(GL_PROGRAM_POINT_SIZE);
    glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ZERO, GL_ONE);
    if (!c.objFile.empty()) loadObj(c.objFile);

    for (auto [tex, format] : {std::pair{&capH, GL_R16_SNORM}, std::pair{&capA, GL_RGB10_A2}}) {
        glCreateTextures(GL_TEXTURE_2D, 1, tex);
        glTextureStorage2D(*tex, 13, format, 4096, 2048);
        glTextureParameteri(*tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTextureParameteri(*tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(*tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    GLuint cap = program({{GL_COMPUTE_SHADER, "cap.comp"}});
    glUseProgram(cap);
    glBindTextureUnit(0, t.lola);
    glBindTextureUnit(1, t.albedo);
    glBindImageTexture(0, capH, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R16_SNORM);
    glBindImageTexture(1, capA, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGB10_A2);
    glDispatchCompute(4096 / 16, 2048 / 16, 1);
    glMemoryBarrier(GL_ALL_BARRIER_BITS);
    glGenerateTextureMipmap(capH);
    glGenerateTextureMipmap(capA);
    glDeleteProgram(cap);
}

void Renderer::initCameraContext() { glCreateVertexArrays(1, &vaoCam); }

void Renderer::loadObj(const std::string& path) {
    tinyobj::ObjReader rd;
    tinyobj::ObjReaderConfig rc;
    rc.triangulate = true;
    if (!rd.ParseFromFile(path, rc)) { fprintf(stderr, "obj: %s\n", rd.Error().c_str()); exit(1); }
    auto& a = rd.GetAttrib();
    auto& mats = rd.GetMaterials();
    std::vector<glm::vec4> v;
    for (auto& sh : rd.GetShapes())
        for (size_t f = 0; f < sh.mesh.indices.size() / 3; f++) {
            const tinyobj::index_t* ix = &sh.mesh.indices[3 * f];
            glm::vec3 p[3];
            for (int k = 0; k < 3; k++) p[k] = glm::make_vec3(&a.vertices[3 * ix[k].vertex_index]);
            glm::vec3 fn = glm::normalize(glm::cross(p[1] - p[0], p[2] - p[0]));
            int mat = sh.mesh.material_ids[f];
            glm::vec3 kd = mat >= 0 ? glm::make_vec3(mats[mat].diffuse) : glm::vec3(0.7f);
            for (int k = 0; k < 3; k++) {
                v.push_back(glm::vec4(p[k], 1));
                v.push_back(glm::vec4(ix[k].normal_index >= 0 ? glm::make_vec3(&a.normals[3 * ix[k].normal_index]) : fn, 0));
                v.push_back(glm::vec4(kd, 1));
                meshRadius = std::max(meshRadius, glm::length(p[k]));
            }
        }
    meshVerts = int(v.size() / 3);
    if (meshVerts) meshBuf = buffer(v.size() * sizeof(glm::vec4), v.data(), 0);
}

void Renderer::draw(const Scene& s, glm::ivec2 fb) {
    glm::dvec3 SE = s.sun - s.earth;
    double dS = glm::length(s.sun), dE = glm::length(s.earth);
    double a = std::acos(glm::clamp(glm::dot(SE / glm::length(SE), -s.earth / dE), -1.0, 1.0));
    double kE = cfg->earthshine * 2.0 / 3 * 0.306 * std::pow(REARTH / dE, 2) * (std::sin(a) + (PI - a) * std::cos(a)) / PI * std::pow(dS / glm::length(SE), 2);
    FrameUBO f{glm::vec4(glm::vec3(s.sun / dS), std::asin(RSUN / dS)), glm::vec4(glm::vec3(s.earth / dE), std::asin(REARTH / dE)),
               glm::vec4(std::pow(AU / dS, 2), kE, std::tan(glm::radians(23.657)), cfg->gray),
               sldem->window(), polar->window(), glm::mat3x4(glm::mat3(s.earthToMe))};
    double starScale = STAR_PEAK / (cfg->orbitGain * PI * std::pow(10, -0.4 * (cfg->orbitStarMag - VSUN)));
    ViewUBO vc = viewData(s.cam, cfg->sensor, 0, 0), vv = viewData(s.view, fb, float(cfg->orbitGain), float(starScale));
    glNamedBufferSubData(frameUbo, 0, sizeof f, &f);
    glNamedBufferSubData(camUbo, 0, sizeof vc, &vc);
    glNamedBufferSubData(viewUbo, 0, sizeof vv, &vv);
    glm::vec4 planets[10];
    for (int i = 0; i < 5; i++) planets[2 * i] = glm::vec4(s.planets[i].p), planets[2 * i + 1] = flux(s.planets[i]);
    glNamedBufferSubData(skyBuf, 2 * stars * sizeof(glm::vec4), sizeof planets, planets);
    glProgramUniformMatrix3fv(sky, 0, 1, GL_FALSE, glm::value_ptr(glm::mat3(s.j2000ToMe)));

    GLuint tex[11] = {terrain->lola, terrain->albedo, terrain->hapke, sldem->tex, sldem->cover, camTex, capH, capA, polar->tex, polar->cover, terrain->earth};
    glBindTextures(0, 11, tex);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, frameUbo);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, probeBuf);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, expBuf);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, skyBuf);
    glBindVertexArray(vao);
    auto drawMoonAndSky = [&] {
        glDepthFunc(GL_ALWAYS);
        glUseProgram(moon);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glDepthFunc(GL_GEQUAL);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glUseProgram(sky);
        glDrawArrays(GL_POINTS, 0, stars + 5);
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    };

    glUseProgram(probe);
    glUniform3fv(0, 1, glm::value_ptr(glm::vec3(s.sat)));
    glDispatchCompute(1, 1, 1);

    glBindFramebuffer(GL_FRAMEBUFFER, camFbo);
    glViewport(0, 0, cfg->sensor.x, cfg->sensor.y);
    glEnable(GL_DEPTH_TEST);
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, camUbo);
    drawMoonAndSky();

    glUseProgram(hist);
    glDispatchCompute((cfg->sensor.x + 15) / 16, (cfg->sensor.y + 15) / 16, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    glUseProgram(resolve);
    glUniform1f(0, float(s.dt));
    glDispatchCompute(1, 1, 1);

    if (fb != viewSize) {
        glDeleteTextures(1, &viewColor);
        glDeleteTextures(1, &viewDepth);
        glCreateTextures(GL_TEXTURE_2D, 1, &viewColor);
        glTextureStorage2D(viewColor, 1, GL_RGBA8, fb.x, fb.y);
        glCreateTextures(GL_TEXTURE_2D, 1, &viewDepth);
        glTextureStorage2D(viewDepth, 1, GL_DEPTH_COMPONENT32F, fb.x, fb.y);
        glNamedFramebufferTexture(viewFbo, GL_COLOR_ATTACHMENT0, viewColor, 0);
        glNamedFramebufferTexture(viewFbo, GL_DEPTH_ATTACHMENT, viewDepth, 0);
        viewSize = fb;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, viewFbo);
    glViewport(0, 0, fb.x, fb.y);
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, viewUbo);
    drawMoonAndSky();

    glDepthFunc(GL_GREATER);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT);
    const Camera& v = s.view;
    glm::mat4 proj(0.f);
    proj[0][0] = float(1 / v.tanX);
    proj[1][1] = float(1 / v.tanY);
    proj[2][3] = -1;
    proj[3][2] = float(ZNEAR);
    glm::mat4 viewProj = proj * glm::mat4(glm::transpose(glm::mat3(glm::vec3(v.right), glm::vec3(v.up), glm::vec3(-v.fwd))));
    std::vector<glm::vec4> pts(s.track.size());
    for (size_t i = 0; i < pts.size(); i++) pts[i] = glm::vec4(glm::vec3(s.track[i] - v.eye), 1);
    glNamedBufferSubData(trackBuf, 0, pts.size() * sizeof(glm::vec4), pts.data());
    glUseProgram(overlay);
    glUniformMatrix4fv(0, 1, GL_FALSE, glm::value_ptr(viewProj));
    glUniform1i(3, s.now);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, trackBuf);
    glUniform1i(2, 0);
    glDrawArrays(GL_LINE_STRIP, 0, GLsizei(pts.size()));
    if (meshVerts) {
        double unit = cfg->objScale / 1000, dist = glm::length(s.sat - v.eye);
        double scale = unit * std::max(1.0, 12 * 2 * v.tanY / fb.y * dist / (meshRadius * unit));
        glm::dvec3 r = glm::radians(cfg->objRotation);
        glm::dmat4 model = glm::translate(glm::dmat4(1), s.sat - v.eye) * glm::dmat4(s.att) * glm::rotate(glm::dmat4(1), r.z, {0, 0, 1}) *
                           glm::rotate(glm::dmat4(1), r.y, {0, 1, 0}) * glm::rotate(glm::dmat4(1), r.x, {1, 0, 0}) * glm::scale(glm::dmat4(1), glm::dvec3(scale));
        glUniformMatrix4fv(1, 1, GL_FALSE, glm::value_ptr(glm::mat4(model)));
        glUniform1i(2, 2);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, meshBuf);
        glDrawArrays(GL_TRIANGLES, 0, meshVerts);
    } else {
        glUniform1i(2, 1);
        glDrawArrays(GL_POINTS, 0, 1);
    }
    glDisable(GL_DEPTH_TEST);
    glBlitNamedFramebuffer(viewFbo, 0, 0, 0, fb.x, fb.y, 0, 0, fb.x, fb.y, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    glFlush();
    satVis = probeMap[0];
    satAbove = probeMap[1];
    ev = expMap[128];
}

void Renderer::present(glm::ivec2 fb) {
    if (fence) { glWaitSync(fence, 0, GL_TIMEOUT_IGNORED); glDeleteSync(fence); fence = nullptr; }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, fb.x, fb.y);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    double k = std::min(double(fb.x) / cfg->sensor.x, double(fb.y) / cfg->sensor.y);
    int w = int(cfg->sensor.x * k), h = int(cfg->sensor.y * k);
    glViewport((fb.x - w) / 2, (fb.y - h) / 2, w, h);
    glBindVertexArray(vaoCam);
    glUseProgram(show);
    glBindTextureUnit(5, camTex);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, frameUbo);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, expBuf);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}
