#pragma once
#include <cstdio>
#include <string>

struct GLFWwindow;

[[noreturn]] void fatal(const std::string& message);  // error dialog, then exit
void useExeDir();                                    // relative data paths resolve next to the executable, from any shortcut
std::string openFileDialog(GLFWwindow* owner, const std::string& current);
std::string saveFileDialog(GLFWwindow* owner, const std::string& suggested);  // empty when cancelled
std::string folderDialog(GLFWwindow* owner);                                  // empty when cancelled
FILE* openFile(const std::string& path, const wchar_t* mode);                 // UTF-8 path
