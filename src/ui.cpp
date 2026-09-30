#include "ui.h"
#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl3.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>
#include <cfloat>
#include <cstdarg>
#include <filesystem>
#include <functional>
#include "ephem.h"
#include "platform.h"

namespace ui {
namespace {

// palette taken from the mission logos: navy (SelenITA, LINNA), gold and green rims, CubeSat orange
constexpr uint32_t BG = 0x070C19, BG2 = 0x0C1630, PANEL = 0x0E162B, FIELD = 0x0A1224, LINE = 0x243457, TEXT = 0xE8ECF5,
                   MUTED = 0x8391B0, GOLD = 0xF2C230, ORANGE = 0xF29A2E, GREEN = 0x2BB673, RED = 0xFF6B6B;
constexpr std::array<double, 10> SPEEDS{1, 2, 5, 10, 20, 50, 100, 200, 500, 1000};
constexpr const char* LEVEL[3] = {"High", "Medium", "Low"};

ImVec4 hex(uint32_t c, float a = 1) { return {((c >> 16) & 255) / 255.f, ((c >> 8) & 255) / 255.f, (c & 255) / 255.f, a}; }
ImU32 u32(uint32_t c, float a = 1) { return ImGui::ColorConvertFloat4ToU32(hex(c, a)); }
ImTextureID tex(GLuint t) { return ImTextureID(intptr_t(t)); }

struct Logo { GLuint tex = 0; float aspect = 1; };

GLFWwindow* window = nullptr;
ImFont *regular = nullptr, *bold = nullptr, *mono = nullptr;
Logo cei, ita, selenita, linna;
float S = 1;  // content (DPI) scale
std::string gpu;

struct Form {
    char file[1024] = "", start[64] = "";
    double orbitFov = 45, fov = 120, tilt = 0, azimuth = 0, roll = 0, span = -1;
    int color = 0, frame = 0, quality = 0;  // frame: 0 MOON_PA, 1 MOON_ME; quality: 0 auto, then high, medium, low
    bool earthshine = true, ready = false;
    std::string checked, first;
} form;

// mip chain averaged with alpha weights, so transparent black or white texels do not fringe the edges
Logo loadLogo(const char* path, bool icon = false) {
    int w, h, n;
    unsigned char* px = stbi_load(path, &w, &h, &n, 4);
    if (!px) fatal(std::string("Could not open the image ") + path);
    Logo l;
    l.aspect = float(w) / h;
    int levels = 1 + int(std::log2(std::max(w, h)));
    glCreateTextures(GL_TEXTURE_2D, 1, &l.tex);
    glTextureStorage2D(l.tex, levels, GL_RGBA8, w, h);
    glTextureParameteri(l.tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(l.tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(l.tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    std::vector<unsigned char> img(px, px + size_t(w) * h * 4), next;
    stbi_image_free(px);
    for (int lv = 0; lv < levels; lv++) {
        glTextureSubImage2D(l.tex, lv, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, img.data());
        if (icon && std::max(w, h) <= 64) {
            GLFWimage g{w, h, img.data()};
            glfwSetWindowIcon(window, 1, &g);
            icon = false;
        }
        int w2 = std::max(w / 2, 1), h2 = std::max(h / 2, 1);
        next.assign(size_t(w2) * h2 * 4, 0);
        for (int y = 0; y < h2; y++)
            for (int x = 0; x < w2; x++) {
                float c[4] = {};
                for (int k = 0; k < 4; k++) {
                    const unsigned char* p = &img[(size_t(std::min(2 * y + k / 2, h - 1)) * w + std::min(2 * x + k % 2, w - 1)) * 4];
                    for (int i = 0; i < 3; i++) c[i] += p[i] * p[3];
                    c[3] += p[3];
                }
                unsigned char* q = &next[(size_t(y) * w2 + x) * 4];
                for (int i = 0; i < 3; i++) q[i] = c[3] > 0 ? (unsigned char)(c[i] / c[3] + 0.5f) : 0;
                q[3] = (unsigned char)(c[3] / 4 + 0.5f);
            }
        img.swap(next);
        w = w2, h = h2;
    }
    return l;
}

void theme() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowPadding = {0, 0};
    s.WindowBorderSize = 0;
    s.ChildRounding = 10;
    s.ChildBorderSize = 1;
    s.FrameRounding = 6;
    s.FrameBorderSize = 1;
    s.FramePadding = {10, 6};
    s.ItemSpacing = {10, 8};
    s.GrabRounding = 6;
    s.GrabMinSize = 14;
    s.PopupRounding = 6;
    s.ScrollbarRounding = 6;
    s.ScrollbarSize = 10;
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = hex(BG);
    c[ImGuiCol_ChildBg] = hex(PANEL);
    c[ImGuiCol_PopupBg] = hex(PANEL);
    c[ImGuiCol_Border] = hex(LINE);
    c[ImGuiCol_Text] = hex(TEXT);
    c[ImGuiCol_TextDisabled] = hex(MUTED);
    c[ImGuiCol_FrameBg] = hex(FIELD);
    c[ImGuiCol_FrameBgHovered] = hex(0x14203D);
    c[ImGuiCol_FrameBgActive] = hex(0x1B2A4E);
    c[ImGuiCol_Button] = hex(0x17264A);
    c[ImGuiCol_ButtonHovered] = hex(0x223866);
    c[ImGuiCol_ButtonActive] = hex(0x2A4478);
    c[ImGuiCol_Header] = hex(0x1B2A4E);
    c[ImGuiCol_HeaderHovered] = hex(0x223866);
    c[ImGuiCol_HeaderActive] = hex(0x2A4478);
    c[ImGuiCol_CheckMark] = hex(GOLD);
    c[ImGuiCol_SliderGrab] = hex(GOLD);
    c[ImGuiCol_SliderGrabActive] = hex(ORANGE);
    c[ImGuiCol_Separator] = hex(LINE);
    c[ImGuiCol_TextSelectedBg] = hex(GOLD, 0.35f);
    c[ImGuiCol_ScrollbarBg] = hex(PANEL, 0);
    c[ImGuiCol_ScrollbarGrab] = hex(LINE);
    s.ScaleAllSizes(S);
}

ImVec2 measure(ImFont* f, float size, const char* s) { return f->CalcTextSizeA(size * S, FLT_MAX, 0, s); }

void text(ImDrawList* dl, ImFont* f, float size, ImVec2 p, uint32_t col, const char* s, float align = 0) {
    if (align) p.x -= measure(f, size, s).x * align;
    dl->AddText(f, size * S, p, u32(col), s);
}

// logo with its bottom-left (or bottom-right) corner at p
void logo(ImDrawList* dl, const Logo& l, ImVec2 p, float h, bool right) {
    float w = h * l.aspect;
    ImVec2 a(right ? p.x - w : p.x, p.y - h);
    dl->AddImage(tex(l.tex), a, {a.x + w, p.y});
}

// deep-space backdrop with a faint star field and the four logos in the corners
void backdrop(ImDrawList* dl, ImVec2 size, float top, float bottom, float m) {
    dl->AddRectFilledMultiColor({0, 0}, size, u32(BG2), u32(BG2), u32(BG), u32(BG));
    uint32_t seed = 12345;
    auto rnd = [&] { seed = seed * 1664525 + 1013904223; return (seed >> 8) / 16777216.f; };
    for (int i = 0; i < 180; i++) {
        ImVec2 p(rnd() * size.x, rnd() * size.y);
        float r = (rnd() < 0.1f ? 1.4f : 0.8f) * S, alpha = 0.15f + 0.45f * rnd();
        if (p.y > 110 * S || std::abs(p.x - size.x / 2) > 380 * S) dl->AddCircleFilled(p, r, u32(TEXT, alpha));  // keep the title clear
    }
    logo(dl, cei, {m, m + top}, top, false);
    logo(dl, ita, {size.x - m, m + top * 0.8f}, top * 0.8f, true);
    logo(dl, selenita, {m, size.y - m}, bottom, false);
    logo(dl, linna, {size.x - m, size.y - m}, bottom, true);
}

void fullWindow(const char* id) {
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin(id, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                  ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground);
}

void title(ImDrawList* dl, float x, float y, float size, float align) {
    text(dl, bold, size, {x, y}, GOLD, "SelenITA", align);
    text(dl, regular, size * 0.42f, {x, y + size * 1.15f * S}, MUTED, "Lunar orbit and CubeSat camera simulator", align);
}

void section(const char* name) {
    ImGui::PushFont(bold, 12.5f);
    ImGui::TextColored(hex(GOLD), "%s", name);
    ImGui::PopFont();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine({p.x, p.y - 4 * S}, {p.x + ImGui::GetContentRegionAvail().x, p.y - 4 * S}, u32(LINE));
}

// label on the left, value right-aligned in the monospaced font
void row(const char* label, const char* fmt, ...) {
    char v[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(v, sizeof v, fmt, ap);
    va_end(ap);
    ImGui::TextColored(hex(MUTED), "%s", label);
    ImGui::SameLine();
    ImGui::PushFont(mono, 15);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(v).x);
    ImGui::TextUnformatted(v);
    ImGui::PopFont();
}

// form row: label column, widget, then an error line when the value is invalid
void field(const char* label, const std::string& error, const std::function<void()>& widget, const char* hint = nullptr) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(190 * S);
    ImGui::SetNextItemWidth(-1);
    widget();
    if (!error.empty() || hint) {
        ImGui::SetCursorPosX(190 * S);
        ImGui::PushFont(regular, 13);
        ImGui::TextColored(hex(error.empty() ? MUTED : RED), "%s", error.empty() ? hint : error.c_str());
        ImGui::PopFont();
    }
}

std::string num(double v) {
    char s[32];
    snprintf(s, sizeof s, "%g", v);
    return s;
}

std::string range(double v, double lo, double hi) { return v >= lo && v <= hi ? "" : "Between " + num(lo) + " and " + num(hi); }

// orbit view overlays: poles with their 60 deg rings, the MOON_ME axes, and the toggles for both
void overlays(App& a, const Camera& c, glm::ivec4 r, float k) {
    constexpr uint32_t NORTH = 0x6FB1FF, SOUTH = 0xC49BFF;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 o(float(r.x), float(r.y)), p1(o.x + r.z, o.y + r.w);
    auto project = [&](glm::dvec3 p, ImVec2& q) {  // false behind the camera or on the far side of the Moon
        glm::dvec3 d = p - c.eye;
        double z = glm::dot(d, c.fwd);
        if (z <= 0 || glm::dot(d, p) > 0) return false;
        q = {o.x + float((1 + glm::dot(d, c.right) / (z * c.tanX)) / 2) * r.z, o.y + float((1 - glm::dot(d, c.up) / (z * c.tanY)) / 2) * r.w};
        return true;
    };
    dl->PushClipRect(o, p1, true);
    for (int south = 0; south < 2; south++) {
        if (!(south ? a.southPole : a.northPole)) continue;
        double z = south ? -1 : 1, lat = glm::radians(60.0);
        ImU32 col = u32(south ? SOUTH : NORTH);
        auto ring = [&](int i) { double l = glm::radians(2.0 * i); return MOON_R * glm::dvec3(std::cos(lat) * std::cos(l), std::cos(lat) * std::sin(l), z * std::sin(lat)); };
        ImVec2 q0, q1;
        for (int i = 0; i < 180; i++)
            if (project(ring(i), q0) && project(ring(i + 1), q1)) dl->AddLine(q0, q1, col, 1.5f * k);
        if (project({0, 0, z * MOON_R}, q0)) {
            dl->AddCircleFilled(q0, 3.5f * k, col);
            dl->AddCircle(q0, 7 * k, col, 0, 1.5f * k);
            text(dl, bold, 13, {q0.x + 10 * k, q0.y - 9 * k}, south ? SOUTH : NORTH, south ? "SP" : "NP");
        }
    }

    // axis triad in the top right corner, seen with the view's rotation; axes pointing away are dimmed and drawn first
    if (a.axes) {
        struct Axis { glm::dvec3 e; uint32_t col; const char* name; } axes[3] = {
            {{1, 0, 0}, 0xFF6B6B, "0°"}, {{0, 1, 0}, 0x5EDB8E, "90°E"}, {{0, 0, 1}, NORTH, "N"}};
        std::sort(axes, axes + 3, [&](const Axis& p, const Axis& q) { return glm::dot(p.e, c.fwd) > glm::dot(q.e, c.fwd); });
        float len = 40 * k;
        ImVec2 ctr(p1.x - len - 30 * k, o.y + len + 30 * k);
        dl->AddCircleFilled(ctr, len + 22 * k, u32(BG, 0.6f));
        for (auto& x : axes) {
            ImVec2 d(float(glm::dot(x.e, c.right)), -float(glm::dot(x.e, c.up))), tip(ctr.x + d.x * len, ctr.y + d.y * len);
            ImU32 col = u32(x.col, glm::dot(x.e, c.fwd) > 0 ? 0.4f : 1.f);
            dl->AddLine(ctr, tip, col, 2 * k);
            float n = std::hypot(d.x, d.y);
            ImVec2 u = n > 1e-3f ? ImVec2(d.x / n, d.y / n) : ImVec2(0, -1), side(-u.y, u.x);
            dl->AddTriangleFilled(tip, {tip.x - u.x * 8 * k + side.x * 4 * k, tip.y - u.y * 8 * k + side.y * 4 * k},
                                  {tip.x - u.x * 8 * k - side.x * 4 * k, tip.y - u.y * 8 * k - side.y * 4 * k}, col);
            ImVec2 w = measure(bold, 12, x.name);
            dl->AddText(bold, 12 * S, {tip.x + u.x * 11 * k - w.x / 2, tip.y + u.y * 11 * k - w.y / 2}, col, x.name);
        }
        text(dl, regular, 11, {ctr.x, ctr.y + len + 24 * k}, MUTED, "Moon axes (MOON_ME)", 0.5f);
    }
    dl->PopClipRect();

    ImGui::PushFont(regular, 13);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {3 * k, 2 * k});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {6 * k, 4 * k});
    ImVec2 b0(o.x + 12 * k, o.y + 44 * k);
    float h = 5 * ImGui::GetFrameHeight() + 4 * 4 * k + 12 * k;
    dl->AddRectFilled(b0, {b0.x + 220 * k, b0.y + h}, u32(BG, 0.75f), 8 * k);
    ImGui::SetCursorScreenPos({b0.x + 6 * k, b0.y + 6 * k});
    ImGui::BeginGroup();
    ImGui::Checkbox("North Pole", &a.northPole);
    ImGui::Checkbox("South Pole", &a.southPole);
    ImGui::Checkbox("Axes", &a.axes);
    ImGui::Checkbox("Camera FOV", &a.fov);
    ImGui::SetNextItemWidth(130 * k);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 6 * k);
    ImGui::SliderFloat("Satellite", &a.satScale, 1, 100000, a.satScale < 1.5f ? "Real" : "×%.0f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
    ImGui::PopStyleVar();
    ImGui::SetItemTooltip("Satellite size, in times its real size");
    ImGui::EndGroup();
    ImGui::PopStyleVar(2);
    ImGui::PopFont();
}

}

void init(GLFWwindow* w) {
    window = w;
    float sy;
    glfwGetWindowContentScale(w, &S, &sy);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    auto font = [&](const char* path) {
        if (!std::filesystem::exists(path)) fatal(std::string("Could not open the font ") + path);
        return io.Fonts->AddFontFromFileTTF(path, 16);
    };
    regular = font("assets/fonts/Inter-Regular.ttf");
    bold = font("assets/fonts/Inter-SemiBold.ttf");
    mono = font("assets/fonts/JetBrainsMono-Regular.ttf");
    io.FontDefault = regular;
    ImGui::GetStyle().FontSizeBase = 16;
    ImGui::GetStyle().FontScaleDpi = S;
    theme();
    ImGui_ImplGlfw_InitForOpenGL(w, true);
    ImGui_ImplOpenGL3_Init("#version 460");
    cei = loadLogo("assets/logos/logo - transp.png");
    ita = loadLogo("assets/logos/ita-logo.png");
    selenita = loadLogo("assets/logos/Logo_new.png", true);
    linna = loadLogo("assets/logos/linna_logo.png");
    gpu = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    gpu = gpu.substr(0, gpu.find('/'));
}

void shutdown() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void frame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void present() {
    ImGui::Render();
    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    ImVec4 c = hex(BG);
    glClearColor(c.x, c.y, c.z, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
}

Start intro(App& a) {
    Config& c = a.cfg;
    if (!form.ready) {
        form = Form{};
        snprintf(form.file, sizeof form.file, "%s", c.orbitFile.c_str());
        snprintf(form.start, sizeof form.start, "%s", c.startUtc.c_str());
        form.orbitFov = c.orbitFov, form.fov = c.fov, form.tilt = c.tilt, form.azimuth = c.azimuth, form.roll = c.roll;
        form.color = c.gray, form.frame = c.orbitMe, form.earthshine = c.earthshine, form.ready = true;
        form.quality = int(std::find(QUALITY.begin(), QUALITY.end(), c.quality) - QUALITY.begin() + 1) % 4;
        form.checked = "\1";
    }
    if (form.checked != form.file) {
        double span;
        form.checked = form.file;
        form.span = Orbit::span(form.file, form.first, span) ? span : -1;
        if (!*form.start && form.span >= 0) snprintf(form.start, sizeof form.start, "%s", form.first.c_str());
    }

    std::string errFile = form.span < 0 ? "File not found or without orbit rows (STK report)" : "", errStart, hint;
    double e, e0;
    bool known = form.span >= 0 && ephem::tryEt(form.first, e0);
    std::string span = known ? form.first.substr(0, 19) + " to " + ephem::utc(e0 + form.span).substr(0, 19) + " UTC (" + num(std::round(form.span / 8640) / 10) + " days)" : "";
    if (!ephem::tryEt(form.start, e)) errStart = "Invalid date: use YYYY-MM-DDThh:mm:ss";
    else if (known && (e < e0 - 1e-3 || e > e0 + form.span + 1e-3)) errStart = "Outside the file: " + span;
    if (known) hint = "File: " + span;
    std::string errOrbitFov = range(form.orbitFov, 5, 120), errFov = range(form.fov, 1, 170), errTilt = range(form.tilt, 0, 180),
                errAz = range(form.azimuth, -360, 360), errRoll = range(form.roll, -360, 360);
    bool valid = (errFile + errStart + errOrbitFov + errFov + errTilt + errAz + errRoll).empty();

    ImVec2 size = ImGui::GetIO().DisplaySize;
    fullWindow("intro");
    ImDrawList* dl = ImGui::GetWindowDrawList();
    backdrop(dl, size, 104 * S, 112 * S, 20 * S);
    title(dl, size.x / 2, 26 * S, 40, 0.5f);

    float cw = 660 * S;
    ImGui::SetCursorPos({(size.x - cw) / 2, 150 * S});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {24 * S, 18 * S});
    ImGui::SetNextWindowSizeConstraints({0, 0}, {FLT_MAX, size.y - 280 * S});  // on small or scaled screens the form scrolls and the buttons stay visible
    ImGui::BeginChild("form", {cw, 0}, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY);
    section("ORBIT");
    field("Orbit file", errFile, [&] {
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 110 * S);
        ImGui::InputText("##file", form.file, sizeof form.file);
        ImGui::SameLine();
        if (ImGui::Button("Browse…", {-1, 0})) snprintf(form.file, sizeof form.file, "%s", openFileDialog(window, form.file).c_str());
    });
    field("Start (UTC)", errStart, [&] { ImGui::InputText("##start", form.start, sizeof form.start); }, hint.c_str());
    field("Lat/lon frame", "", [&] { ImGui::Combo("##frame", &form.frame, "MOON_PA (principal axes)\0MOON_ME (mean Earth)\0"); });
    ImGui::Spacing();
    section("ORBIT VIEW");
    field("Vertical FOV", errOrbitFov, [&] { ImGui::InputDouble("##ofov", &form.orbitFov, 1, 10, "%.1f°"); });
    ImGui::Spacing();
    section("CUBESAT CAMERA");
    field("Horizontal FOV", errFov, [&] { ImGui::InputDouble("##fov", &form.fov, 1, 10, "%.1f°"); });
    field("Tilt (off nadir)", errTilt, [&] { ImGui::InputDouble("##tilt", &form.tilt, 1, 10, "%.1f°"); });
    field("Tilt azimuth", errAz, [&] { ImGui::InputDouble("##az", &form.azimuth, 5, 45, "%.1f°"); }, "0° = forward (velocity), 90° = right");
    field("Roll", errRoll, [&] { ImGui::InputDouble("##roll", &form.roll, 5, 45, "%.1f°"); });
    field("Color", "", [&] { ImGui::Combo("##color", &form.color, "Natural (643/566/415 nm)\0Gray (643 nm)\0"); });
    field("Earthshine", "", [&] { ImGui::Checkbox("Earth light on the night side", &form.earthshine); });
    ImGui::Spacing();
    section("PERFORMANCE");
    std::string autoItem = std::string("Auto: ") + LEVEL[a.autoLevel], gpuHint = "Detected " + gpu;
    gpuHint += a.vramGb > 0 ? ", " + num(std::round(a.vramGb * 10) / 10) + " GB of video memory" : ", integrated or unknown video memory";
    const char* items[4] = {autoItem.c_str(), "High (full resolution, ~3.6 GB)", "Medium (global maps at half resolution, ~1.6 GB)",
                            "Low (as Medium, orbit view at half resolution, ~1.4 GB)"};
    field("Quality", "", [&] { ImGui::Combo("##quality", &form.quality, items, 4); }, gpuHint.c_str());
    ImGui::EndChild();
    ImGui::PopStyleVar();

    float bw = 320 * S, bh = 50 * S, gap = 16 * S;
    ImGui::SetCursorPos({(size.x - 2 * bw - gap) / 2, ImGui::GetCursorPosY() + 14 * S});
    ImGui::BeginDisabled(!valid);
    ImGui::PushFont(bold, 18);
    ImGui::PushStyleColor(ImGuiCol_Button, hex(ORANGE));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hex(GOLD));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, hex(GOLD));
    ImGui::PushStyleColor(ImGuiCol_Text, hex(BG));
    Start pick = ImGui::Button(valid ? "START SIMULATION" : "FIX THE FIELDS", {bw, bh}) ? Start::simulate : Start::none;
    ImGui::PopStyleColor(4);
    ImGui::SameLine(0, gap);
    ImGui::PushStyleColor(ImGuiCol_Text, hex(GOLD));
    ImGui::PushStyleColor(ImGuiCol_Border, hex(GOLD));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f * S);
    if (ImGui::Button("GENERATE ORBIT REPORT", {bw, bh})) pick = Start::report;
    ImGui::SetItemTooltip("Instead of simulating, save a CSV with one line per orbit file row: time, satellite, Sun and camera");
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    ImGui::PopFont();
    ImGui::EndDisabled();
    if (!a.notice.empty()) {
        ImGui::SetCursorPos({(size.x - cw) / 2, ImGui::GetCursorPosY() + 10 * S});
        ImGui::PushTextWrapPos((size.x + cw) / 2);
        ImGui::TextColored(hex(MUTED), "%s", a.notice.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::End();

    if (pick == Start::none) return pick;
    a.notice.clear();
    c.orbitFile = form.file, c.startUtc = form.start, c.orbitFov = form.orbitFov, c.fov = form.fov, c.tilt = form.tilt;
    c.azimuth = form.azimuth, c.roll = form.roll, c.gray = form.color == 1, c.orbitMe = form.frame == 1, c.earthshine = form.earthshine;
    c.quality = form.quality ? QUALITY[form.quality - 1] : "auto";
    saveConfig(a.cfgPath.c_str(), {{"orbit_file", c.orbitFile}, {"start_utc", c.startUtc}, {"orbit_frame", c.orbitMe ? "ME" : "PA"}, {"orbit_fov", num(c.orbitFov)}, {"fov", num(c.fov)},
                                   {"tilt", num(c.tilt)}, {"azimuth", num(c.azimuth)}, {"roll", num(c.roll)},
                                   {"color", c.gray ? "gray" : "natural"}, {"earthshine", c.earthshine ? "1" : "0"}, {"quality", c.quality}});
    form.ready = false;
    return pick;
}

void progress(const char* heading, const char* stage, float done, const char* note) {
    ImVec2 size = ImGui::GetIO().DisplaySize;
    fullWindow("progress");
    ImDrawList* dl = ImGui::GetWindowDrawList();
    backdrop(dl, size, 104 * S, 112 * S, 20 * S);
    title(dl, size.x / 2, 26 * S, 40, 0.5f);
    float w = 620 * S, y = size.y * 0.45f;
    ImVec2 p0((size.x - w) / 2, y), p1(p0.x + w, y + 12 * S);
    char pct[16];
    snprintf(pct, sizeof pct, "%.0f%%", done * 100);
    text(dl, bold, 22, {size.x / 2, y - 76 * S}, TEXT, heading, 0.5f);
    text(dl, regular, 15, {size.x / 2, y - 38 * S}, MUTED, stage, 0.5f);
    dl->AddRectFilled(p0, p1, u32(FIELD), 6 * S);
    dl->AddRect(p0, p1, u32(LINE), 6 * S);
    if (done > 0) dl->AddRectFilled(p0, {p0.x + w * std::min(done, 1.f), p1.y}, u32(GOLD), 6 * S);
    text(dl, mono, 15, {size.x / 2, y + 26 * S}, GOLD, pct, 0.5f);
    text(dl, regular, 13, {size.x / 2, y + 64 * S}, MUTED, note, 0.5f);
    ImGui::End();
}

Layout layout(glm::ivec2 size) {
    float k = std::max(size.y / 1080.f, 0.6f);
    int m = int(12 * k), top = int(88 * k), bottom = int(112 * k), h = size.y - top - bottom;
    int cam = std::min(int(0.6f * h), int(0.3f * size.x)), cx = size.x - m - cam;
    return {{m, top, cx - 2 * m, h}, {cx, top, cam, cam}, {cx, top + cam + m, cam, h - cam - m}};
}

void run(App& a, const Scene& s, const Layout& L) {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 size = io.DisplaySize;
    float k = std::max(size.y / 1080.f, 0.6f), m = 12 * k, top = 88 * k, bottom = 112 * k, round = 10 * k;
    fullWindow("run");
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilledMultiColor({0, 0}, size, u32(BG2), u32(BG2), u32(BG), u32(BG));

    // header: logos, title, clock
    logo(dl, cei, {m, top - 8 * k}, top - 16 * k, false);
    logo(dl, ita, {size.x - m, top - 16 * k}, top - 32 * k, true);
    title(dl, m + (top - 16 * k) * cei.aspect + 18 * k, 14 * k, 28, 0);
    double et = a.et0 + a.t, el = std::abs(a.t - a.tStart);
    std::string utc = ephem::utc(et);
    utc[10] = ' ';
    char buf[160];
    text(dl, mono, 28, {size.x / 2, 12 * k}, TEXT, (utc + " UTC").c_str(), 0.5f);
    snprintf(buf, sizeof buf, "T%c %dd %02d:%02d:%02d   ·   ×%g   ·   %s", a.t < a.tStart ? '-' : '+', int(el / 86400), int(el / 3600) % 24,
             int(el / 60) % 60, int(el) % 60, a.speed, a.paused ? "PAUSED" : "RUNNING");
    text(dl, mono, 15, {size.x / 2, 52 * k}, a.paused ? ORANGE : GOLD, buf, 0.5f);
    dl->AddRectFilledMultiColor({0, top - 2}, {size.x / 2, top - 1}, u32(GOLD, 0), u32(GOLD, 0.6f), u32(GOLD, 0.6f), u32(GOLD, 0));
    dl->AddRectFilledMultiColor({size.x / 2, top - 2}, {size.x, top - 1}, u32(GOLD, 0.6f), u32(GOLD, 0), u32(GOLD, 0), u32(GOLD, 0.6f));

    // orbit view: drag to rotate, wheel to zoom
    auto panel = [&](glm::ivec4 r, GLuint image, const char* name, const char* detail) {
        ImVec2 p0(float(r.x), float(r.y)), p1(p0.x + r.z, p0.y + r.w);
        dl->AddImageRounded(tex(image), p0, p1, {0, 1}, {1, 0}, IM_COL32_WHITE, round);
        dl->AddRect(p0, p1, u32(LINE), round);
        ImVec2 t0(p0.x + 12 * k, p0.y + 12 * k), w0 = measure(bold, 12.5f, name), w1 = measure(regular, 13, detail);
        dl->AddRectFilled(t0, {t0.x + w0.x + w1.x + 30 * k, t0.y + 26 * k}, u32(BG, 0.75f), 13 * k);
        text(dl, bold, 12.5f, {t0.x + 10 * k, t0.y + 5 * k}, GOLD, name);
        text(dl, regular, 13, {t0.x + w0.x + 20 * k, t0.y + 5 * k}, TEXT, detail);
    };
    panel(L.orbit, a.renderer.orbitImage(), "ORBIT", a.follow ? "following the satellite" : "free view");
    text(dl, regular, 13, {L.orbit.x + 14 * k, L.orbit.y + L.orbit.w - 26 * k}, MUTED, "Drag to rotate  ·  scroll to zoom");
    ImGui::SetCursorScreenPos({float(L.orbit.x), float(L.orbit.y)});
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("orbit", {float(L.orbit.z), float(L.orbit.w)});
    glm::dvec3& v = a.view[a.follow];
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0)) {
        v.x -= glm::radians(0.3) * io.MouseDelta.x;
        v.y = std::clamp(v.y + glm::radians(0.3) * io.MouseDelta.y, -glm::radians(89.0), glm::radians(89.0));
    }
    if (ImGui::IsItemHovered() && io.MouseWheel != 0)
        v.z = std::clamp(v.z * std::pow(1.15, -io.MouseWheel), a.follow ? 0.002 : MOON_R + 12, a.follow ? 20000.0 : 1e6);
    overlays(a, s.view, L.orbit, k);

    // camera image (view only)
    double fovV = glm::degrees(2 * std::atan(s.cam.tanY));
    snprintf(buf, sizeof buf, "%d×%d  ·  FOV %.1f° × %.1f°", a.cfg.sensor.x, a.cfg.sensor.y, a.cfg.fov, fovV);
    panel(L.camera, a.renderer.cameraImage(), "CAMERA", buf);

    // telemetry
    State st = a.orbit.at(a.t);
    glm::dvec2 sun = sunAzEl(st.p, s.sun);
    double r = glm::length(st.p), gsd = a.gsd(st), orbitN = (a.t - a.orbit.begin()) / a.orbit.period;
    ImGui::SetCursorScreenPos({float(L.info.x), float(L.info.y)});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {16 * k, 12 * k});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {8 * k, 4 * k});
    ImGui::BeginChild("info", {float(L.info.z), float(L.info.w)}, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
    if (ImGui::BeginTable("cols", 2, ImGuiTableFlags_SizingStretchSame, {0, 0})) {
        ImGui::TableSetupColumn("a", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("b", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextColumn();
        section("TIME");
        row("Date", "%s", utc.substr(0, 10).c_str());
        row("Time (UTC)", "%s", utc.substr(11, 8).c_str());
        row("Elapsed", "%c%dd %02d:%02d:%02d", a.t < a.tStart ? '-' : '+', int(el / 86400), int(el / 3600) % 24, int(el / 60) % 60, int(el) % 60);
        row("Orbit", "#%d  (%.0f%%)", int(orbitN) + 1, (orbitN - std::floor(orbitN)) * 100);
        ImGui::Spacing();
        section("SATELLITE");
        row("Latitude", "%.3f°", glm::degrees(std::asin(st.p.z / r)));
        row("Longitude", "%.3f°", glm::degrees(std::atan2(st.p.y, st.p.x)));
        row("Altitude", "%.2f km", r - MOON_R);
        row("Above terrain", "%.2f km", a.renderer.probed.above);
        row("Speed", "%.3f km/s", glm::length(st.v));
        row("Sun az / el", "%.1f° / %.1f°", sun.x, sun.y);
        ImGui::SetItemTooltip("Sun below the satellite: azimuth clockwise from north, elevation over the local horizon");
        row("Illumination", a.renderer.probed.vis > 0 ? "%.0f%%" : "eclipse", a.renderer.probed.vis * 100);
        row("Albedo I/F", "%.3f %.3f %.3f", a.renderer.probed.albedo.r, a.renderer.probed.albedo.g, a.renderer.probed.albedo.b);
        ImGui::SetItemTooltip("Surface albedo below the satellite: WAC Hapke-normalized reflectance (i = 60°, e = 0°, g = 60°) at 643, 566 and 415 nm");
        ImGui::TableNextColumn();
        section("CAMERA");
        row("Tilt / azimuth", "%.1f° / %.1f°", a.cfg.tilt, a.cfg.azimuth);
        row("Roll", "%.1f°", a.cfg.roll);
        row("Color", "%s", a.cfg.gray ? "gray 643 nm" : "natural");
        row("Earthshine", "%s", a.cfg.earthshine ? "on" : "off");
        row("Exposure", "EV %.2f", a.renderer.ev);
        if (gsd > 0) row("Center GSD", "%.1f m/px", gsd);
        else row("Center GSD", "sky");
        ImGui::Spacing();
        section("SYSTEM");
        row("Frame rate", "%.0f fps", a.fps);
        row("Quality", "%s", LEVEL[a.terrain.level]);
        for (auto [name, s] : {std::pair{"SLDEM", a.sldem.get()}, std::pair{"Polar DEM", a.polar.get()}})
            s->present ? row(name, "%d / %d", s->resident, s->pending) : row(name, "not installed");
        ImGui::TextColored(hex(MUTED), "%s", gpu.c_str());
        ImGui::EndTable();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);

    // footer: logos, timeline and controls
    float lh = bottom - 16 * k, x0 = m + lh * selenita.aspect + 24 * k, x1 = size.x - m - lh * linna.aspect - 24 * k, fy = size.y - bottom;
    logo(dl, selenita, {m, size.y - 8 * k}, lh, false);
    logo(dl, linna, {size.x - m, size.y - 8 * k}, lh, true);
    std::string t0 = ephem::utc(a.et0 + a.orbit.begin()).substr(0, 10), t1 = ephem::utc(a.et0 + a.orbit.end()).substr(0, 10);
    text(dl, mono, 13, {x0, fy + 16 * k}, MUTED, t0.c_str());
    text(dl, mono, 13, {x1, fy + 16 * k}, MUTED, t1.c_str(), 1);
    float tw = measure(mono, 13, t0.c_str()).x + 14 * k;
    ImGui::SetCursorScreenPos({x0 + tw, fy + 10 * k});
    ImGui::SetNextItemWidth(x1 - x0 - 2 * tw);
    double tt = a.t, lo = a.orbit.begin(), hi = a.orbit.end();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {10 * k, 3 * k});
    if (ImGui::SliderScalar("##timeline", ImGuiDataType_Double, &tt, &lo, &hi, "", ImGuiSliderFlags_NoInput)) a.seek(tt);
    ImGui::PopStyleVar();
    ImGui::SetItemTooltip("Drag to jump to any instant of the orbit file");

    struct Item { std::string label; const char* tip; std::function<void()> act; bool primary = false, on = false, gap = false; };
    auto step = [&](int dir) {
        auto it = std::lower_bound(SPEEDS.begin(), SPEEDS.end(), a.speed);
        a.speed = dir > 0 ? *std::upper_bound(SPEEDS.begin(), SPEEDS.end() - 1, a.speed) : it == SPEEDS.begin() ? SPEEDS[0] : *(it - 1);
    };
    snprintf(buf, sizeof buf, "×%g", a.speed);
    std::vector<Item> items = {
        {"Start", "Back to the start time (start_utc)", [&] { a.seek(a.tStart); }},
        {"« 1 day", "Back 1 day", [&] { a.seek(a.t - 86400); }},
        {"« Orbit", "Back 1 orbit", [&] { a.seek(a.t - a.orbit.period); }},
        {"‹ 60 s", "Back 60 s", [&] { a.seek(a.t - 60); }},
        {a.paused ? "▶  Resume" : "‖  Pause", "Pause or resume time", [&] { a.paused = !a.paused; }, true},
        {"60 s ›", "Forward 60 s", [&] { a.seek(a.t + 60); }},
        {"Orbit »", "Forward 1 orbit", [&] { a.seek(a.t + a.orbit.period); }},
        {"1 day »", "Forward 1 day", [&] { a.seek(a.t + 86400); }},
        {"▼", "Slower", [&] { step(-1); }, false, false, true},
        {buf, "Time speed; click to reset to ×1", [&] { a.speed = 1; }},
        {"▲", "Faster", [&] { step(1); }},
        {"Follow satellite", "Switch between the free view and following the satellite", [&] { a.follow = !a.follow; }, false, a.follow, true},
        {"↺ View", "Reset the orbit view", [&] { a.view[a.follow] = VIEW0[a.follow]; }},
        {"Settings", "Back to the start screen to change the orbit or the camera", [&] { a.running = false; }, false, false, true},
        {"Quit", "Close the program", [&] { glfwSetWindowShouldClose(a.win, 1); }},
    };
    ImGui::PushFont(bold, 14);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {14 * k, 8 * k});
    float pad = ImGui::GetStyle().FramePadding.x, sp = 6 * k, gap = 22 * k, total = 0;
    for (auto& it : items) total += ImGui::CalcTextSize(it.label.c_str()).x + 2 * pad + sp + (it.gap ? gap : 0);
    ImGui::SetCursorScreenPos({(x0 + x1 - total) / 2, fy + 48 * k});
    for (size_t i = 0; i < items.size(); i++) {
        Item& it = items[i];
        if (i) ImGui::SameLine(0, sp + (it.gap ? gap : 0));
        int colors = 0;
        if (it.primary || it.on) {
            ImGui::PushStyleColor(ImGuiCol_Button, hex(it.primary ? ORANGE : GREEN));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hex(GOLD));
            ImGui::PushStyleColor(ImGuiCol_Text, hex(BG));
            colors = 3;
        }
        if (ImGui::Button(it.label.c_str())) it.act();
        ImGui::PopStyleColor(colors);
        ImGui::SetItemTooltip("%s", it.tip);
    }
    ImGui::PopStyleVar();
    ImGui::PopFont();
    ImGui::End();
}

}
