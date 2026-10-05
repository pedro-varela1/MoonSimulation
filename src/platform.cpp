#include "platform.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shobjidl.h>
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

// UTF-8 with forward slashes
std::string narrow(const wchar_t* w) {
    std::string s(WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), int(s.size()), nullptr, nullptr);
    s.pop_back();
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
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
    return (ext ? GetSaveFileNameW(&o) : GetOpenFileNameW(&o)) ? narrow(file) : "";
}

}

void fatal(const std::string& message) {
    if (GetConsoleWindow()) fprintf(stderr, "error: %s\n", message.c_str());  // console tools (linna2, the debug build)
    else MessageBoxW(nullptr, wide(message).c_str(), L"SelenITA Lunar Orbit Simulator", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
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

std::string folderDialog(GLFWwindow* owner) {
    std::string out;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IFileOpenDialog* d = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)))) return out;
    DWORD o;
    d->GetOptions(&o);
    d->SetOptions(o | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    IShellItem* item;
    if (SUCCEEDED(d->Show(glfwGetWin32Window(owner))) && SUCCEEDED(d->GetResult(&item))) {
        PWSTR path;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) out = narrow(path), CoTaskMemFree(path);
        item->Release();
    }
    d->Release();
    return out;
}

FILE* openFile(const std::string& path, const wchar_t* mode) { return _wfopen(wide(path).c_str(), mode); }
