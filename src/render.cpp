#include "render.h"
#include "platform.h"
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <fstream>

namespace {

struct FrameUBO { glm::vec4 sunDir, earthDir, light, sldemWin, polarWin; glm::mat3x4 earthToMe; };
struct ViewUBO { glm::vec4 eye, right, up, fwd, size; };
constexpr double PI = 3.14159265358979, ZNEAR = 1e-5, VSUN = -26.74;
constexpr double STAR_PEAK = 0.009;  // orbit window: linear display value of a star at orbit_star_mag (sRGB ~24/255)
constexpr int FOV_N = 16;  // orbit window: camera field of view grid, cells per image side

std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) fatal("Could not open " + path);
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
        if (!ok) {
            glGetShaderInfoLog(sh, sizeof log, nullptr, log);
            fatal(std::string("The graphics driver could not compile the shader ") + file + " (source 1 = common.glsl, 2 = " + file + "):\n\n" + log);
        }
        glAttachShader(p, sh);
        glDeleteShader(sh);
    }
    glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { glGetProgramInfoLog(p, sizeof log, nullptr, log); fatal(std::string("The graphics driver could not link a shader program:\n\n") + log); }
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
    sampler = program({{GL_COMPUTE_SHADER, "sample.comp"}});
    glCreateVertexArrays(1, &vao);
    frameUbo = buffer(sizeof(FrameUBO), nullptr, GL_DYNAMIC_STORAGE_BIT);
    camUbo = buffer(sizeof(ViewUBO), nullptr, GL_DYNAMIC_STORAGE_BIT);
    viewUbo = buffer(sizeof(ViewUBO), nullptr, GL_DYNAMIC_STORAGE_BIT);
    const GLbitfield map = GL_MAP_READ_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
    float probe0[5] = {1, 0}, exp0[132] = {};
    exp0[128] = 3;
    probeBuf = buffer(sizeof probe0, probe0, map);
    probeIn = buffer(2 * sizeof(glm::vec4), nullptr, GL_DYNAMIC_STORAGE_BIT);
    expBuf = buffer(sizeof exp0, exp0, map);
    probeMap = static_cast<const float*>(glMapNamedBufferRange(probeBuf, 0, sizeof probe0, map));
    expMap = static_cast<const float*>(glMapNamedBufferRange(expBuf, 0, sizeof exp0, map));
    trackBuf = buffer(sizeof(glm::vec4) * 1025, nullptr, GL_DYNAMIC_STORAGE_BIT);
    fovBuf = buffer(sizeof(glm::vec4) * (4 * FOV_N + 2 + 6 * FOV_N * FOV_N), nullptr, GL_DYNAMIC_STORAGE_BIT);
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
    glCreateTextures(GL_TEXTURE_2D, 1, &camView);
    glTextureStorage2D(camView, 1 + int(std::log2(std::max(c.sensor.x, c.sensor.y))), GL_RGBA8, c.sensor.x, c.sensor.y);
    glTextureParameteri(camView, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glCreateFramebuffers(1, &camViewFbo);
    glNamedFramebufferTexture(camViewFbo, GL_COLOR_ATTACHMENT0, camView, 0);
    glCreateFramebuffers(1, &viewFbo);
    glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
    glEnable(GL_PROGRAM_POINT_SIZE);
    if (!c.objFile.empty()) loadObj(c.objFile);

    for (auto [tex, format] : {std::pair{&capH, GL_R16_SNORM}, std::pair{&capA, GL_RGB10_A2}}) {
        glCreateTextures(GL_TEXTURE_2D, 1, tex);
        glTextureStorage2D(*tex, 13, format, 4096, 2048);
        glTextureParameteri(*tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTextureParameteri(*tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(*tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    buildCaps();
}

// LOLA and albedo above 75 deg resampled once into the azimuthal polar grid
void Renderer::buildCaps() {
    GLuint cap = program({{GL_COMPUTE_SHADER, "cap.comp"}});
    glUseProgram(cap);
    glBindTextureUnit(0, terrain->lola);
    glBindTextureUnit(1, terrain->albedo);
    glBindImageTexture(0, capH, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R16_SNORM);
    glBindImageTexture(1, capA, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGB10_A2);
    glDispatchCompute(4096 / 16, 2048 / 16, 1);
    glMemoryBarrier(GL_ALL_BARRIER_BITS);
    glGenerateTextureMipmap(capH);
    glGenerateTextureMipmap(capA);
    glDeleteProgram(cap);
}

int detectQuality(double& gb) {
    GLint kb[4] = {};
    if (GLEW_NVX_gpu_memory_info) glGetIntegerv(GL_GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX, kb);
    else if (GLEW_ATI_meminfo) glGetIntegerv(GL_TEXTURE_FREE_MEMORY_ATI, kb);
    gb = kb[0] / 1048576.0;
    return gb >= 5.5 ? 0 : gb >= 2.5 ? 1 : 2;
}

void Renderer::loadObj(const std::string& path) {
    tinyobj::ObjReader rd;
    tinyobj::ObjReaderConfig rc;
    rc.triangulate = true;
    if (!rd.ParseFromFile(path, rc)) fatal("Could not read the CubeSat model " + path + ":\n" + rd.Error());
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
            }
        }
    meshVerts = int(v.size() / 3);
    if (meshVerts) meshBuf = buffer(v.size() * sizeof(glm::vec4), v.data(), 0);
}

void Renderer::draw(const Scene& s, glm::ivec2 fb) {
    glm::dvec3 SE = s.sun - s.earth;
    double dS = glm::length(s.sun), dE = glm::length(s.earth);
    double a = std::acos(glm::clamp(glm::dot(SE / glm::length(SE), -s.earth / dE), -1.0, 1.0));
    double kE = cfg->earthshine * 2.0 / 3 * 0.306 * std::pow(EARTH_R / dE, 2) * (std::sin(a) + (PI - a) * std::cos(a)) / PI * std::pow(dS / glm::length(SE), 2);
    FrameUBO f{glm::vec4(glm::vec3(s.sun / dS), std::asin(SUN_R / dS)), glm::vec4(glm::vec3(s.earth / dE), std::asin(EARTH_R / dE)),
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

    bindData();
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, probeBuf);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, expBuf);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, skyBuf);
    glBindVertexArray(vao);

    glm::vec4 at[2] = {glm::vec4(glm::vec3(s.sat), 0), f.sunDir};
    glNamedBufferSubData(probeIn, 0, sizeof at, at);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, probeIn);
    glUseProgram(probe);
    glUniform1ui(0, 1);
    glDispatchCompute(1, 1, 1);

    glBindFramebuffer(GL_FRAMEBUFFER, camFbo);
    glViewport(0, 0, cfg->sensor.x, cfg->sensor.y);
    glEnable(GL_DEPTH_TEST);
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, camUbo);
    moonAndSky();

    glUseProgram(hist);
    glDispatchCompute((cfg->sensor.x + 15) / 16, (cfg->sensor.y + 15) / 16, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    glUseProgram(resolve);
    glUniform1f(0, float(s.dt));
    glDispatchCompute(1, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, camViewFbo);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(show);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glGenerateTextureMipmap(camView);
    if (!s.cameraOnly) drawOrbit(s, fb);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    probed = {probeMap[0], probeMap[1], glm::vec3(probeMap[2], probeMap[3], probeMap[4])};
    ev = expMap[128];
}

void Renderer::moonAndSky() {
    glDepthFunc(GL_ALWAYS);
    glUseProgram(moon);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDepthFunc(GL_GEQUAL);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ZERO, GL_ONE);
    glUseProgram(sky);
    glDrawArrays(GL_POINTS, 0, stars + 5);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
}

// orbit view: Moon and sky, track, camera field of view and the satellite
void Renderer::drawOrbit(const Scene& s, glm::ivec2 fb) {
    glEnable(GL_DEPTH_TEST);
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
    moonAndSky();

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
    if (s.showFov) {
        // camera field of view, translucent: the sides of the view pyramid (a fan from the satellite) and the imaged area; a ray missing the Moon stops at the horizon distance
        const Camera& c = s.cam;
        auto ground = [&](double x, double y) {
            glm::dvec3 d = glm::normalize(c.fwd + x * c.tanX * c.right + y * c.tanY * c.up);
            double b = glm::dot(c.eye, d), disc = b * b - glm::dot(c.eye, c.eye) + MOON_R * MOON_R, t = -b - std::sqrt(std::max(disc, 0.0));
            return glm::vec4(glm::vec3(c.eye + (disc > 0 && t > 0 ? t : std::sqrt(glm::dot(c.eye, c.eye) - MOON_R * MOON_R)) * d - v.eye), 1);
        };
        const double cx[5] = {-1, 1, 1, -1, -1}, cy[5] = {1, 1, -1, -1, 1};
        std::vector<glm::vec4> fov{glm::vec4(glm::vec3(c.eye - v.eye), 1)};
        for (int k = 0; k < 4; k++)
            for (int i = 0; i < FOV_N; i++) fov.push_back(ground(glm::mix(cx[k], cx[k + 1], double(i) / FOV_N), glm::mix(cy[k], cy[k + 1], double(i) / FOV_N)));
        fov.push_back(fov[1]);
        for (int j = 0; j < FOV_N; j++)
            for (int i = 0; i < FOV_N; i++)
                for (int q : {0, 1, 3, 0, 3, 2}) fov.push_back(ground(2.0 * (i + q % 2) / FOV_N - 1, 2.0 * (j + q / 2) / FOV_N - 1));
        glNamedBufferSubData(fovBuf, 0, fov.size() * sizeof(glm::vec4), fov.data());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, fovBuf);
        glUniform1i(2, 3);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4 * FOV_N + 2);
        glDrawArrays(GL_TRIANGLES, 4 * FOV_N + 2, 6 * FOV_N * FOV_N);
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    }
    // the satellite at satScale times its size; enlarged, it is drawn over the Moon unless it is behind it
    double dist = glm::length(s.sat - v.eye), b = glm::dot(v.eye, s.sat - v.eye) / dist, disc = b * b - glm::dot(v.eye, v.eye) + MOON_R * MOON_R;
    double hit = -b - std::sqrt(std::max(disc, 0.0));
    if (meshVerts) {
        glm::dvec3 r = glm::radians(cfg->objRotation);
        glm::dmat4 model = glm::translate(glm::dmat4(1), s.sat - v.eye) * glm::dmat4(s.att) * glm::rotate(glm::dmat4(1), r.z, {0, 0, 1}) *
                           glm::rotate(glm::dmat4(1), r.y, {0, 1, 0}) * glm::rotate(glm::dmat4(1), r.x, {1, 0, 0}) *
                           glm::scale(glm::dmat4(1), glm::dvec3(cfg->objScale / 1000 * s.satScale));
        if (s.satScale > 1 && !(disc > 0 && hit > 0 && hit < dist)) glClearDepth(0), glClear(GL_DEPTH_BUFFER_BIT);
        glUniformMatrix4fv(1, 1, GL_FALSE, glm::value_ptr(glm::mat4(model)));
        glUniform1i(2, 2);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, meshBuf);
        glDrawArrays(GL_TRIANGLES, 0, meshVerts);
    } else {  // marker at the track's current point
        glUniform1i(2, 1);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, trackBuf);
        glDrawArrays(GL_POINTS, 0, 1);
    }
    glDisable(GL_DEPTH_TEST);
}

void Renderer::bindData() {
    GLuint tex[11] = {terrain->lola, terrain->albedo, terrain->hapke, sldem->tex, sldem->cover, camTex, capH, capA, polar->tex, polar->cover, terrain->earth};
    glBindTextures(0, 11, tex);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, frameUbo);
}

std::vector<Probe> Renderer::probeAll(const std::vector<glm::dvec3>& pos, const std::vector<glm::dvec3>& sun) {
    constexpr size_t CHUNK = 1 << 16;  // points per dispatch, far below the driver's time limit
    std::vector<Probe> out(pos.size());
    std::vector<glm::vec4> in(2 * CHUNK);
    GLuint inBuf = buffer(sizeof(glm::vec4) * in.size(), nullptr, GL_DYNAMIC_STORAGE_BIT), outBuf = buffer(sizeof(Probe) * CHUNK, nullptr, 0);
    FrameUBO global{};  // no streamed windows until the next draw uploads the frame again
    glNamedBufferSubData(frameUbo, 0, sizeof global, &global);
    bindData();
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, outBuf);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, inBuf);
    glUseProgram(probe);
    for (size_t i0 = 0; i0 < pos.size(); i0 += CHUNK) {
        size_t n = std::min(CHUNK, pos.size() - i0);
        for (size_t i = 0; i < n; i++) {
            double d = glm::length(sun[i0 + i]);
            in[2 * i] = glm::vec4(glm::vec3(pos[i0 + i]), 0);
            in[2 * i + 1] = glm::vec4(glm::vec3(sun[i0 + i] / d), std::asin(SUN_R / d));
        }
        glNamedBufferSubData(inBuf, 0, sizeof(glm::vec4) * 2 * n, in.data());
        glUniform1ui(0, GLuint(n));
        glDispatchCompute(GLuint((n + 63) / 64), 1, 1);
        glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
        glGetNamedBufferSubData(outBuf, 0, sizeof(Probe) * n, &out[i0]);
    }
    glDeleteBuffers(1, &inBuf);
    glDeleteBuffers(1, &outBuf);
    return out;
}

std::vector<glm::vec2> Renderer::sample(const std::vector<glm::vec3>& n, bool sun) {
    constexpr size_t CHUNK = 1 << 16;
    std::vector<glm::vec2> out(n.size());
    std::vector<glm::vec4> in(CHUNK);
    GLuint inBuf = buffer(sizeof(glm::vec4) * CHUNK, nullptr, GL_DYNAMIC_STORAGE_BIT), outBuf = buffer(sizeof(glm::vec2) * CHUNK, nullptr, 0);
    bindData();
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, outBuf);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, inBuf);
    glUseProgram(sampler);
    glUniform1i(1, sun);
    for (size_t i0 = 0; i0 < n.size(); i0 += CHUNK) {
        size_t k = std::min(CHUNK, n.size() - i0);
        for (size_t i = 0; i < k; i++) in[i] = glm::vec4(n[i0 + i], 0);
        glNamedBufferSubData(inBuf, 0, sizeof(glm::vec4) * k, in.data());
        glUniform1ui(0, GLuint(k));
        glDispatchCompute(GLuint((k + 63) / 64), 1, 1);
        glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
        glGetNamedBufferSubData(outBuf, 0, sizeof(glm::vec2) * k, &out[i0]);
    }
    glDeleteBuffers(1, &inBuf);
    glDeleteBuffers(1, &outBuf);
    return out;
}

float Renderer::exposure() {
    glFinish();
    return ev = expMap[128];
}

std::vector<uint8_t> Renderer::cameraPixels() {
    std::vector<uint8_t> px(size_t(cfg->sensor.x) * cfg->sensor.y * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTextureImage(camView, 0, GL_RGB, GL_UNSIGNED_BYTE, GLsizei(px.size()), px.data());
    return px;
}
