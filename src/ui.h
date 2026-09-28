#pragma once
#include "app.h"

namespace ui {
struct Layout { glm::ivec4 orbit, camera, info; };  // x, y, width, height in window pixels (top-left origin)

void init(GLFWwindow* w);
void shutdown();
void frame();
void present();
bool intro(App& a);  // true once the form is valid; the values are then in a.cfg and saved to the config file
void loading(const char* stage, float done);
Layout layout(glm::ivec2 size);
void run(App& a, const Scene& s, const Layout& l);
}
