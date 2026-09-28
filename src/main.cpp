#include <cstdio>
#include "app.h"
#include "ephem.h"
#include "ui.h"

// ask hybrid-graphics drivers for the dedicated GPU
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

namespace {

constexpr double POLAR_Z = 0.857167;  // sin 59°: SLDEM below, polar DEM above
constexpr int INTRO_W = 1120, INTRO_H = 800;

GLFWmonitor* monitorOf(GLFWwindow* w) {
    int wx, wy, ww, wh, n;
    glfwGetWindowPos(w, &wx, &wy);
    glfwGetWindowSize(w, &ww, &wh);
    GLFWmonitor** ms = glfwGetMonitors(&n);
    for (int i = 0; i < n; i++) {
        int mx, my;
        glfwGetMonitorPos(ms[i], &mx, &my);
        const GLFWvidmode* v = glfwGetVideoMode(ms[i]);
        if (wx + ww / 2 >= mx && wx + ww / 2 < mx + v->width && wy + wh / 2 >= my && wy + wh / 2 < my + v->height) return ms[i];
    }
    return glfwGetPrimaryMonitor();
}

void windowed(GLFWwindow* w) {
    GLFWmonitor* m = monitorOf(w);
    int mx, my, mw, mh;
    float sx, sy;
    glfwGetMonitorWorkarea(m, &mx, &my, &mw, &mh);
    glfwGetMonitorContentScale(m, &sx, &sy);
    int ww = std::min(int(INTRO_W * sx), mw), wh = std::min(int(INTRO_H * sy), mh - 40);
    glfwSetWindowAttrib(w, GLFW_DECORATED, GLFW_TRUE);
    glfwSetWindowMonitor(w, nullptr, mx + (mw - ww) / 2, my + (mh - wh) / 2, ww, wh, 0);
}

// borderless, covering the monitor the window is on
void fullscreen(GLFWwindow* w) {
    GLFWmonitor* m = monitorOf(w);
    int mx, my;
    glfwGetMonitorPos(m, &mx, &my);
    const GLFWvidmode* v = glfwGetVideoMode(m);
    glfwSetWindowAttrib(w, GLFW_DECORATED, GLFW_FALSE);
    glfwSetWindowMonitor(w, nullptr, mx, my, v->width, v->height, 0);
}

// the orbit is reloaded only when its file changed; terrain, sky and renderer are loaded once
void load(App& a) {
    double shown = -1;
    auto progress = [&](const char* stage, float done) {
        if (glfwGetTime() - shown < 1.0 / 30 && done < 1) return;
        shown = glfwGetTime();
        glfwPollEvents();
        if (glfwWindowShouldClose(a.win)) exit(0);
        ui::frame();
        ui::loading(stage, done);
        ui::present();
    };
    progress("Orbit: fit and interpolation", 0);
    if (a.orbitLoaded != a.cfg.orbitFile) {
        a.orbit.load(a.cfg.orbitFile, ephem::rotation("MOON_PA", "MOON_ME", 0));
        a.orbitLoaded = a.cfg.orbitFile;
    }
    a.et0 = ephem::et(a.orbit.utc0);
    a.tStart = a.t = std::clamp(ephem::et(a.cfg.startUtc) - a.et0, a.orbit.begin(), a.orbit.end());
    if (!a.sldem) {
        a.terrain.load(a.cfg, [&](const char* stage, float f) { progress(stage, 0.03f + 0.92f * f); });
        progress("Stars, polar caps and terrain streaming", 0.96f);
        a.sldem = std::make_unique<Sldem>(a.cfg.sldemDir);
        a.polar = std::make_unique<PolarDem>(a.cfg.polarDir);
        a.renderer.init(a.cfg, a.terrain, *a.sldem, *a.polar,
                        a.cfg.starCatalog.empty() ? std::vector<ephem::Source>() : ephem::stars(a.cfg.starCatalog, a.et0 + a.tStart));
    }
    progress("Ready", 1);
}

}

int main(int argc, char** argv) {
    auto app = std::make_unique<App>();
    App& a = *app;
    a.cfgPath = argc > 1 ? argv[1] : "config.ini";
    a.cfg = loadConfig(a.cfgPath.c_str());
    a.speed = a.cfg.speed;
    ephem::init(a.cfg.spiceDir);

    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_DEPTH_BITS, 0);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    a.win = glfwCreateWindow(INTRO_W, INTRO_H, "SelenITA — Lunar Orbit Simulator", nullptr, nullptr);
    if (!a.win) { fprintf(stderr, "nao foi possivel criar a janela OpenGL 4.6\n"); return 1; }
    windowed(a.win);
    glfwMakeContextCurrent(a.win);
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) return 1;
    glGetError();
    glEnable(GL_DEBUG_OUTPUT);
    glDebugMessageCallback([](GLenum, GLenum type, GLuint, GLenum sev, GLsizei, const GLchar* msg, const void*) {
        if (type == GL_DEBUG_TYPE_ERROR || sev == GL_DEBUG_SEVERITY_HIGH) fprintf(stderr, "GL: %s\n", msg);
    }, nullptr);
    glfwSwapInterval(1);
    ui::init(a.win);

    double last = glfwGetTime(), fpsAt = last;
    int frames = 0;
    while (!glfwWindowShouldClose(a.win)) {
        glfwPollEvents();
        if (!a.running) {
            ui::frame();
            bool start = ui::intro(a);
            ui::present();
            if (start) {
                load(a);
                fullscreen(a.win);
                a.running = true;
                last = glfwGetTime();
            }
            continue;
        }
        double now = glfwGetTime(), dt = now - last;
        last = now;
        if (!a.paused) a.seek(a.t + dt * a.speed);
        glm::ivec2 size;
        glfwGetFramebufferSize(a.win, &size.x, &size.y);
        if (size.x == 0 || size.y == 0) continue;
        ui::Layout L = ui::layout(size);
        glm::ivec2 orbitSize(L.orbit.z, L.orbit.w);

        State st = a.orbit.at(a.t);
        Scene s;
        s.att = a.attitude(st);
        double tx = std::tan(glm::radians(a.cfg.fov) / 2), et = a.et0 + a.t;
        s.cam = {st.p, s.att[0], s.att[1], -s.att[2], tx, tx * a.cfg.sensor.y / a.cfg.sensor.x};
        s.view = a.orbitCamera(st, orbitSize);
        s.sat = st.p;
        s.sun = ephem::sun(et);
        s.earth = ephem::earth(et);
        s.j2000ToMe = ephem::rotation("J2000", "MOON_ME", et);
        s.earthToMe = ephem::rotation("IAU_EARTH", "MOON_ME", et);
        s.planets = ephem::planets(et);
        s.dt = dt;
        for (int i = 0; i <= 1024; i++) s.track.push_back(a.orbit.at(std::clamp(a.t + (i / 1024.0 - 0.5) * a.orbit.period, a.orbit.begin(), a.orbit.end())).p);
        s.now = 512;

        auto ahead = [&](bool polarCap) {
            glm::dvec3 c = a.focus(a.t + std::min(2 * a.speed, 60.0));
            for (double dt2 = 10; (std::abs(c.z) > POLAR_Z) != polarCap && dt2 < a.orbit.period; dt2 += 10) c = a.focus(a.t + dt2);
            return c;
        };
        std::vector<glm::dvec3> path(64);
        for (int k = 0; k < 64; k++) path[k] = a.focus(a.t + 4.5 * k);
        double needKm = std::min(0.67 * a.cfg.sensor.x / (2 * tx), 250.0);
        a.sldem->update(ahead(false), path, needKm, a.speed <= 150, dt);
        a.polar->update(ahead(true), path, needKm, a.speed <= 150, dt);
        a.renderer.draw(s, orbitSize);

        ui::frame();
        ui::run(a, s, L);
        ui::present();
        if (!a.running) windowed(a.win);
        frames++;
        if (now - fpsAt > 0.5) a.fps = frames / (now - fpsAt), frames = 0, fpsAt = now;
    }
    a.sldem.reset();
    a.polar.reset();
    ui::shutdown();
    glfwTerminate();
}
