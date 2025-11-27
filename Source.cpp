// Sonic Heroes Resolution + Widescreen Patcher - Silent GUI version
// Works with ANY Character Set (MBCS/Unicode), XP-compatible
#define NOMINMAX
#define WINVER 0x0501
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <shellapi.h>
#include <vector>
#include <algorithm>
#include <string>
#include <fstream>
#include <cstdint>

#pragma warning(disable: 4996)
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#pragma comment(lib, "shell32.lib")

using namespace std;

// === Admin check ===
bool IsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin == TRUE;
}

// === Relaunch as admin with args ===
void RelaunchAsAdmin(const string& cmdLine) {
    char exePath[MAX_PATH] = { 0 };
    GetModuleFileNameA(NULL, exePath, MAX_PATH);

    SHELLEXECUTEINFOA sei = { sizeof(sei) };
    sei.lpVerb = "runas";
    sei.lpFile = exePath;
    sei.lpParameters = cmdLine.empty() ? NULL : cmdLine.c_str();
    sei.nShow = SW_NORMAL;

    ShellExecuteExA(&sei);
    ExitProcess(0);
}

// === Find Tsonic_win.exe ===
string FindTsonicWin() {
    char path[MAX_PATH] = { 0 };
    GetModuleFileNameA(NULL, path, MAX_PATH);
    string dir = path;
    size_t pos = dir.find_last_of("\\/");
    if (pos != string::npos) dir.erase(pos);

    string candidate = dir + "\\Tsonic_win.exe";
    if (GetFileAttributesA(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
        return candidate;

    GetCurrentDirectoryA(MAX_PATH, path);
    candidate = string(path) + "\\Tsonic_win.exe";
    if (GetFileAttributesA(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
        return candidate;

    return "";
}

// === Get current resolution ===
bool GetCurrentResolution(DWORD& w, DWORD& h) {
    DEVMODEA dm = { 0 };
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &dm)) {
        w = dm.dmPelsWidth;
        h = dm.dmPelsHeight;
        return true;
    }
    return false;
}

// === Validate resolution table ===
bool ValidateResolutionTable(const vector<uint8_t>& data, size_t offset) {
    if (offset + 160 > data.size()) return false;
    for (int i = 0; i < 8; ++i) {
        size_t slot = i * 20;
        uint32_t width = *(uint32_t*)(data.data() + offset + slot);
        uint32_t height = *(uint32_t*)(data.data() + offset + slot + 4);
        if (width == 0 || height == 0) return false;
    }
    return true;
}

// === Widescreen fix ===
void ApplyWidescreenFix(vector<uint8_t>& data, DWORD w, DWORD h) {
    float aspect = static_cast<float>(w) / h;
    const size_t fovOffset = 0x31CA0B;
    const size_t hudOffset = 0x31C9D8;

    if (aspect < 1.4f) {
        if (fovOffset + 4 <= data.size()) *(float*)(data.data() + fovOffset) = 1.0f;
        if (hudOffset + 4 <= data.size()) *(float*)(data.data() + hudOffset) = 1.25f;
    }
    else {
        float scale = aspect / (4.0f / 3.0f);
        if (fovOffset + 4 <= data.size()) *(float*)(data.data() + fovOffset) = scale;
        if (hudOffset + 4 <= data.size()) *(float*)(data.data() + hudOffset) = aspect;
    }
}

// === Parse "--custom 1920x1080" ===
bool ParseResolution(const string& s, DWORD& w, DWORD& h) {
    size_t x = s.find('x');
    if (x == string::npos) return false;
    w = atol(s.substr(0, x).c_str());
    h = atol(s.substr(x + 1).c_str());
    return w > 0 && h > 0;
}

// === Entry point ===
int APIENTRY WinMain(HINSTANCE, HINSTANCE, LPSTR lpCmdLine, int) {
    string cmdLine = lpCmdLine ? lpCmdLine : "";
    for (char& c : cmdLine) c = tolower(c);

    if (!IsAdmin()) {
        RelaunchAsAdmin(cmdLine);
        return 0;
    }

    DWORD width = 0, height = 0;

    if (cmdLine.find("--custom") != string::npos) {
        size_t sp = cmdLine.find(' ');
        if (sp == string::npos || !ParseResolution(cmdLine.substr(sp + 1), width, height)) {
            MessageBoxA(NULL, "Invalid format. Use: --custom 1920x1080", "Sonic Heroes Patcher", MB_ICONERROR);
            return 1;
        }
    }
    else if (cmdLine.find("--max") != string::npos) {
        DEVMODEA dm = { 0 };
        dm.dmSize = sizeof(dm);
        DWORD maxW = 0, maxH = 0;
        int i = 0;
        while (EnumDisplaySettingsA(NULL, i++, &dm)) {
            if (dm.dmPelsWidth > maxW) {
                maxW = dm.dmPelsWidth;
                maxH = dm.dmPelsHeight;
            }
        }
        width = maxW; height = maxH;
    }
    else {
        if (!GetCurrentResolution(width, height)) {
            MessageBoxA(NULL, "Failed to detect current resolution.", "Sonic Heroes Patcher", MB_ICONERROR);
            return 1;
        }
    }

    string exePath = FindTsonicWin();
    if (exePath.empty()) {
        MessageBoxA(NULL, "Tsonic_win.exe not found!\nPlace launcher in the game folder.", "Sonic Heroes Patcher", MB_ICONERROR);
        return 1;
    }

    ifstream in(exePath.c_str(), ios::binary);
    if (!in) {
        MessageBoxA(NULL, "Cannot open Tsonic_win.exe", "Error", MB_ICONERROR);
        return 1;
    }
    in.seekg(0, ios::end);
    size_t size = (size_t)in.tellg();
    in.seekg(0);
    vector<uint8_t> data(size);
    in.read((char*)data.data(), size);
    in.close();

    vector<uint8_t> sig = { 0x80, 0x02, 0x00, 0x00, 0xE0, 0x01, 0x00, 0x00 };
    auto it = search(data.begin(), data.end(), sig.begin(), sig.end());
    if (it == data.end()) {
        MessageBoxA(NULL, "Resolution table not found.", "Error", MB_ICONERROR);
        return 1;
    }

    size_t base = it - data.begin();
    if (!ValidateResolutionTable(data, base)) {
        MessageBoxA(NULL, "Invalid resolution table.", "Error", MB_ICONERROR);
        return 1;
    }

    size_t patchOffset = base + 140;
    *(uint32_t*)(data.data() + patchOffset) = width;
    *(uint32_t*)(data.data() + patchOffset + 4) = height;

    ApplyWidescreenFix(data, width, height);

    ofstream out(exePath.c_str(), ios::binary);
    if (!out) {
        MessageBoxA(NULL, "Failed to write Tsonic_win.exe\nRun as administrator.", "Error", MB_ICONERROR);
        return 1;
    }
    out.write((char*)data.data(), size);
    out.close();

    ShellExecuteA(NULL, "open", exePath.c_str(), NULL, NULL, SW_SHOWNORMAL);

    return 0;
}