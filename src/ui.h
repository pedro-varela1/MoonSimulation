#pragma once
#include "app.h"

namespace ui {
struct Layout { glm::ivec4 orbit, camera, info; };  // x, y, width, height in window pixels (top-left origin)

void init(GLFWwindow* w);
void shutdown();
void frame();
void present();
enum class Start { none, simulate, report, images };
Start intro(App& a);  // a button pressed on a valid form: the values are then in a.cfg and saved to the config file
// progress screen, with the image preview (a texture) and a Cancel button when asked; true when Cancel was pressed
bool progress(const char* heading, const char* stage, float done, const char* note = "", GLuint preview = 0, bool cancel = false);
Layout layout(glm::ivec2 size);
void run(App& a, const Scene& s, const Layout& l);
}
