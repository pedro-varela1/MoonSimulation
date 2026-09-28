#pragma once
#include <string>

struct GLFWwindow;

[[noreturn]] void fatal(const std::string& message);  // error dialog, then exit
void useExeDir();                                    // relative data paths resolve next to the executable, from any shortcut
std::string openFileDialog(GLFWwindow* owner, const std::string& current);
