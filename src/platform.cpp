#include "platform.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <algorithm>

namespace {

std::wstring wide(const std::string& s) {
    std::wstring w(MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), int(w.size()));
    return w;
}

// Windows open dialog, or save dialog when ext is set; UTF-8 with forward slashes, empty when cancelled
std::string fileDialog(GLFWwindow* owner, const std::string& current, const wchar_t* filter, const wchar_t* ext) {
    wchar_t file[1024] = L"";
    std::wstring start = wide(current);
    std::replace(start.begin(), start.end(), L'/', L'\\');
    wcsncpy(file, start.c_str(), 1023);
    OPENFILENAMEW o{};
    o.lStructSize = sizeof o;
    o.hwndOwner = glfwGetWin32Window(owner);
    o.lpstrFilter = filter;
    o.lpstrDefExt = ext;
    o.lpstrFile = file;
    o.nMaxFile = 1024;
    o.Flags = OFN_NOCHANGEDIR | (ext ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (!(ext ? GetSaveFileNameW(&o) : GetOpenFileNameW(&o))) return "";
    char out[4096];
    WideCharToMultiByte(CP_UTF8, 0, file, -1, out, sizeof out, nullptr, nullptr);
    std::string s = out;
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

}

void fatal(const std::string& message) {
    MessageBoxW(nullptr, wide(message).c_str(), L"SelenITA Lunar Orbit Simulator", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    exit(1);
}

void useExeDir() {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    while (n > 0 && path[n - 1] != L'\\' && path[n - 1] != L'/') n--;
    path[n] = L'\0';
    SetCurrentDirectoryW(path);
}

std::string openFileDialog(GLFWwindow* owner, const std::string& current) {
    std::string s = fileDialog(owner, current, L"STK report (*.txt)\0*.txt\0All files\0*.*\0", nullptr);
    return s.empty() ? current : s;
}

std::string saveFileDialog(GLFWwindow* owner, const std::string& suggested) {
    return fileDialog(owner, suggested, L"CSV table (*.csv)\0*.csv\0All files\0*.*\0", L"csv");
}
