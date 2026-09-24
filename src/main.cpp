#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include "config.h"
#include "ephem.h"
#include "orbit.h"
#include "render.h"
#include "terrain.h"

namespace {

constexpr double R = 1737.4, POLAR_Z = 0.857167;  // sin 59°: SLDEM below, polar DEM above
constexpr std::array<double, 10> SPEEDS{1, 2, 5, 10, 20, 50, 100, 200, 500, 1000};
const std::array<glm::dvec3, 2> VIEW0{glm::dvec3(0, 0.3, 6000), glm::dvec3(0, 0.3, 1)};

struct App {
    Config cfg;
    Orbit orbit;
    Terrain terrain;
    std::unique_ptr<Streamer> sldem, polar;
    Renderer renderer;
    GLFWwindow *orbitWin = nullptr, *camWin = nullptr;
    double et0 = 0, t = 0, tStart = 0, speed = 1;
    bool paused = false, follow = false, drag = false;
    std::array<glm::dvec3, 2> view = VIEW0;
    glm::dvec2 mouse{0};

    glm::dmat3 attitude(const State& s) const {
        glm::dvec3 U = glm::normalize(s.p), F = glm::normalize(s.v - glm::dot(s.v, U) * U);
        auto rz = [](double a) { return glm::dmat3(std::cos(a), std::sin(a), 0, -std::sin(a), std::cos(a), 0, 0, 0, 1); };
        double c = std::cos(glm::radians(cfg.tilt)), s1 = std::sin(glm::radians(cfg.tilt));
        return glm::dmat3(glm::cross(F, U), F, U) * rz(-glm::radians(cfg.azimuth)) * glm::dmat3(1, 0, 0, 0, c, s1, 0, -s1, c) * rz(glm::radians(cfg.roll));
    }

    glm::dvec3 focus(double time) const {
        State s = orbit.at(std::clamp(time, orbit.begin(), orbit.end()));
        glm::dvec3 d = -attitude(s)[2];
        double b = glm::dot(s.p, d), disc = b * b - glm::dot(s.p, s.p) + R * R;
        return glm::normalize(disc > 0 && -b - std::sqrt(disc) > 0 ? s.p + (-b - std::sqrt(disc)) * d : s.p);
    }

    Camera orbitCamera(const State& s, glm::ivec2 fb) const {
        glm::dvec3 k = view[follow], dir(std::cos(k.y) * std::cos(k.x), std::cos(k.y) * std::sin(k.x), std::sin(k.y)), target(0), up(0, 0, 1), eye;
        if (follow) {
            up = glm::normalize(s.p);
            glm::dvec3 F = glm::normalize(s.v - glm::dot(s.v, up) * up);
            target = s.p;
            eye = s.p + k.z * (-dir.x * F + dir.y * glm::cross(F, up) + dir.z * up);
            eye = std::max(glm::length(eye), R + 11.5) * glm::normalize(eye);
        } else eye = k.z * dir;
        glm::dvec3 fwd = glm::normalize(target - eye), right = glm::normalize(glm::cross(fwd, up));
        double ty = std::tan(glm::radians(cfg.orbitFov) / 2);
        return {eye, right, glm::cross(right, fwd), fwd, ty * fb.x / std::max(fb.y, 1), ty};
    }
};

App& app(GLFWwindow* w) { return *static_cast<App*>(glfwGetWindowUserPointer(w)); }

void onKey(GLFWwindow* w, int key, int, int action, int mods) {
    if (action == GLFW_RELEASE) return;
    App& a = app(w);
    double jump = mods & GLFW_MOD_CONTROL ? 86400 : mods & GLFW_MOD_SHIFT ? a.orbit.period : 60;
    switch (key) {
    case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(w, 1); break;
    case GLFW_KEY_SPACE: a.paused = !a.paused; break;
    case GLFW_KEY_UP: a.speed = *std::upper_bound(SPEEDS.begin(), SPEEDS.end() - 1, a.speed); break;
    case GLFW_KEY_DOWN: {
        auto it = std::lower_bound(SPEEDS.begin(), SPEEDS.end(), a.speed);
        a.speed = it == SPEEDS.begin() ? SPEEDS[0] : *(it - 1);
    } break;
    case GLFW_KEY_BACKSPACE: a.speed = 1; break;
    case GLFW_KEY_RIGHT: a.t += jump; break;
    case GLFW_KEY_LEFT: a.t -= jump; break;
    case GLFW_KEY_HOME: a.t = a.tStart; break;
    case GLFW_KEY_F: a.follow = !a.follow; break;
    case GLFW_KEY_R: a.view[a.follow] = VIEW0[a.follow]; break;
    }
    a.t = std::clamp(a.t, a.orbit.begin(), a.orbit.end());
}

GLFWwindow* window(const char* title, glm::ivec4 r, GLFWwindow* share) {
    glfwWindowHint(GLFW_POSITION_X, r.x);
    glfwWindowHint(GLFW_POSITION_Y, r.y);
    GLFWwindow* w = glfwCreateWindow(r.z, r.w, title, nullptr, share);
    if (!w) { fprintf(stderr, "nao foi possivel criar a janela OpenGL 4.6\n"); exit(1); }
    glfwSetKeyCallback(w, onKey);
    return w;
}

}

int main(int argc, char** argv) {
    auto a = std::make_unique<App>();
    a->cfg = loadConfig(argc > 1 ? argv[1] : "config.ini");
    ephem::init(a->cfg.spiceDir);
    a->orbit.load(a->cfg.orbitFile, ephem::rotation("MOON_PA", "MOON_ME", 0));
    a->et0 = ephem::et(a->orbit.utc0);
    a->tStart = a->t = std::clamp(a->cfg.startUtc.empty() ? a->orbit.begin() : ephem::et(a->cfg.startUtc) - a->et0, a->orbit.begin(), a->orbit.end());
    a->speed = a->cfg.speed;

    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_DEPTH_BITS, 0);
    a->orbitWin = window("Órbita", a->cfg.orbitWindow, nullptr);
    a->camWin = window("Câmera", a->cfg.cameraWindow, a->orbitWin);
    for (GLFWwindow* w : {a->orbitWin, a->camWin}) glfwSetWindowUserPointer(w, a.get());
    glfwSetMouseButtonCallback(a->orbitWin, [](GLFWwindow* w, int b, int action, int) { if (b == GLFW_MOUSE_BUTTON_LEFT) app(w).drag = action == GLFW_PRESS; });
    glfwSetCursorPosCallback(a->orbitWin, [](GLFWwindow* w, double x, double y) {
        App& p = app(w);
        if (p.drag) {
            glm::dvec3& k = p.view[p.follow];
            k.x -= glm::radians(0.3) * (x - p.mouse.x);
            k.y = std::clamp(k.y + glm::radians(0.3) * (y - p.mouse.y), -glm::radians(89.0), glm::radians(89.0));
        }
        p.mouse = {x, y};
    });
    glfwSetScrollCallback(a->orbitWin, [](GLFWwindow* w, double, double dy) {
        App& p = app(w);
        glm::dvec3& k = p.view[p.follow];
        k.z = std::clamp(k.z * std::pow(1.15, -dy), p.follow ? 0.002 : R + 12, p.follow ? 20000.0 : 1e6);
    });

    glfwMakeContextCurrent(a->orbitWin);
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) return 1;
    glGetError();
    glEnable(GL_DEBUG_OUTPUT);
    glDebugMessageCallback([](GLenum, GLenum type, GLuint, GLenum sev, GLsizei, const GLchar* msg, const void*) {
        if (type == GL_DEBUG_TYPE_ERROR || sev == GL_DEBUG_SEVERITY_HIGH) fprintf(stderr, "GL: %s\n", msg);
    }, nullptr);
    glfwSwapInterval(1);
    a->terrain.load(a->cfg);
    a->sldem = std::make_unique<Sldem>(a->cfg.sldemDir);
    a->polar = std::make_unique<PolarDem>(a->cfg.polarDir);
    a->renderer.init(a->cfg, a->terrain, *a->sldem, *a->polar, a->cfg.starCatalog.empty() ? std::vector<ephem::Source>() : ephem::stars(a->cfg.starCatalog, a->et0 + a->tStart));
    glfwMakeContextCurrent(a->camWin);
    glfwSwapInterval(0);
    a->renderer.initCameraContext();

    const double tanPix = 2 * std::tan(glm::radians(a->cfg.fov) / 2) / a->cfg.sensor.x, needKm = std::min(0.67 / tanPix, 250.0);
    double last = glfwGetTime(), titleAt = last;
    int frames = 0;
    printf("pronto em %.1f s\n", last);
    fflush(stdout);
    while (!glfwWindowShouldClose(a->orbitWin) && !glfwWindowShouldClose(a->camWin)) {
        glfwPollEvents();
        double now = glfwGetTime(), dt = now - last;
        last = now;
        if (!a->paused) a->t = std::clamp(a->t + dt * a->speed, a->orbit.begin(), a->orbit.end());
        State st = a->orbit.at(a->t);
        glm::ivec2 fo, fc;
        glfwGetFramebufferSize(a->orbitWin, &fo.x, &fo.y);
        glfwGetFramebufferSize(a->camWin, &fc.x, &fc.y);

        Scene s;
        s.att = a->attitude(st);
        double tx = std::tan(glm::radians(a->cfg.fov) / 2);
        s.cam = {st.p, s.att[0], s.att[1], -s.att[2], tx, tx * a->cfg.sensor.y / a->cfg.sensor.x};
        s.view = a->orbitCamera(st, fo);
        s.sat = st.p;
        double et = a->et0 + a->t;
        s.sun = ephem::sun(et);
        s.earth = ephem::earth(et);
        s.j2000ToMe = ephem::rotation("J2000", "MOON_ME", et);
        s.earthToMe = ephem::rotation("IAU_EARTH", "MOON_ME", et);
        s.planets = ephem::planets(et);
        s.dt = dt;
        for (int i = 0; i <= 1024; i++) s.track.push_back(a->orbit.at(std::clamp(a->t + (i / 1024.0 - 0.5) * a->orbit.period, a->orbit.begin(), a->orbit.end())).p);
        s.now = 512;

        auto ahead = [&](bool polarCap) {
            glm::dvec3 c = a->focus(a->t + std::min(2 * a->speed, 60.0));
            for (double dt2 = 10; (std::abs(c.z) > POLAR_Z) != polarCap && dt2 < a->orbit.period; dt2 += 10) c = a->focus(a->t + dt2);
            return c;
        };
        std::vector<glm::dvec3> path(64);
        for (int k = 0; k < 64; k++) path[k] = a->focus(a->t + 4.5 * k);

        glfwMakeContextCurrent(a->orbitWin);
        a->sldem->update(ahead(false), path, needKm, a->speed <= 150, dt);
        a->polar->update(ahead(true), path, needKm, a->speed <= 150, dt);
        a->renderer.draw(s, fo);
        glfwMakeContextCurrent(a->camWin);
        a->renderer.present(fc);
        glfwSwapBuffers(a->camWin);
        glfwMakeContextCurrent(a->orbitWin);
        glfwSwapBuffers(a->orbitWin);

        frames++;
        if (now - titleAt > 0.25) {
            double r = glm::length(st.p), fps = frames / (now - titleAt);
            char title[320];
            snprintf(title, sizeof title, "Órbita — %s UTC — %s×%g — lat %.3f° lon %.3f° alt %.2f km — h %.2f km — Sol %.1f° — %.0f%% iluminado — SLDEM %d/%d — polar %d/%d — %.0f fps",
                     ephem::utc(et).c_str(), a->paused ? "pausado " : "", a->speed, glm::degrees(std::asin(st.p.z / r)),
                     glm::degrees(std::atan2(st.p.y, st.p.x)), r - R, a->renderer.satAbove,
                     glm::degrees(std::asin(glm::dot(glm::normalize(s.sun), st.p / r))), a->renderer.satVis * 100, a->sldem->resident,
                     a->sldem->pending, a->polar->resident, a->polar->pending, fps);
            glfwSetWindowTitle(a->orbitWin, title);
            snprintf(title, sizeof title, "Câmera — EV %.2f — %.0f fps", a->renderer.ev, fps);
            glfwSetWindowTitle(a->camWin, title);
            titleAt = now;
            frames = 0;
        }
    }
    a->sldem.reset();
    a->polar.reset();
    glfwTerminate();
}
