// launcher.cpp - Sonic Heroes Resolution + Widescreen Patcher
// VS 2015, v140_XP Toolset, Windows XP compatible
#define NOMINMAX
#define WINVER 0x0501
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <shellapi.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cstring>
#include <string>
#include <iomanip>
#include <cstdint>
using namespace std;
#pragma warning(disable: 4996)

// === Check if running as Administrator ===
bool IsAdmin() {
    BOOL isAdmin = FALSE;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    PSID adminGroup;
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin == TRUE;
}

// === Request elevation WITH ARGUMENTS ===
bool RequestElevation(int argc, char* argv[]) {
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);

    // Собираем аргументы в строку
    string params;
    for (int i = 1; i < argc; ++i) {
        if (i > 1) params += " ";
        string arg = argv[i];
        if (arg.find(' ') != string::npos || arg.empty()) {
            params += "\"" + arg + "\"";
        }
        else {
            params += arg;
        }
    }

    SHELLEXECUTEINFOA sei = { sizeof(sei) };
    sei.lpVerb = "runas";
    sei.lpFile = exePath;
    sei.lpParameters = params.empty() ? NULL : params.c_str();
    sei.nShow = SW_NORMAL;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;

    if (ShellExecuteExA(&sei) && sei.hProcess) {
        WaitForSingleObject(sei.hProcess, INFINITE);
        CloseHandle(sei.hProcess);
        return true;
    }
    return false;
}

// === Find Tsonic_win.exe ===
string FindTsonicWin() {
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    string exeDir = string(buffer);
    size_t pos = exeDir.find_last_of("\\/");
    if (pos != string::npos) exeDir = exeDir.substr(0, pos);
    string candidate = exeDir + "\\Tsonic_win.exe";
    if (GetFileAttributesA(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) return candidate;
    GetCurrentDirectoryA(MAX_PATH, buffer);
    candidate = string(buffer) + "\\Tsonic_win.exe";
    if (GetFileAttributesA(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) return candidate;
    return "";
}

// === Validate resolution table (NO LIMITS) ===
bool ValidateResolutionTable(const vector<uint8_t>& data, size_t offset) {
    if (offset + 160 > data.size()) return false;
    for (int i = 0; i < 8; ++i) {
        size_t slot = i * 20;
        uint32_t w = *(uint32_t*)(data.data() + offset + slot);
        uint32_t h = *(uint32_t*)(data.data() + offset + slot + 4);
        if (w == 0 || h == 0) return false;
    }
    return true;
}

// === Get current resolution ===
bool GetCurrentResolution(uint32_t& w, uint32_t& h) {
    DEVMODE dm = { sizeof(dm) };
    if (EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &dm)) {
        w = dm.dmPelsWidth;
        h = dm.dmPelsHeight;
        return true;
    }
    return false;
}

// === Widescreen Fix: 4:3 or ANY widescreen ===
void ApplyWidescreenFix(vector<uint8_t>& data, uint32_t w, uint32_t h) {
    float aspect = static_cast<float>(w) / h;
    const size_t fovOffset = 0x31CA0B;
    const size_t hudOffset = 0x31C9D8;

    if (aspect < 1.4f) {
        if (fovOffset + 4 <= data.size()) *(float*)(data.data() + fovOffset) = 1.0f;
        if (hudOffset + 4 <= data.size()) *(float*)(data.data() + hudOffset) = 1.25f;
        cout << "4:3 FOV & HUD restored\n";
        return;
    }

    float scale = aspect / (4.0f / 3.0f);
    if (fovOffset + 4 <= data.size()) *(float*)(data.data() + fovOffset) = scale;
    if (hudOffset + 4 <= data.size()) *(float*)(data.data() + hudOffset) = aspect;
    cout << "Widescreen FOV: " << scale << ", HUD: " << aspect << "\n";
}

// === Parse "800x600" ===
bool ParseResolution(const string& s, uint32_t& w, uint32_t& h) {
    size_t x = s.find('x');
    if (x == string::npos) return false;
    w = static_cast<uint32_t>(atoi(s.substr(0, x).c_str()));
    h = static_cast<uint32_t>(atoi(s.substr(x + 1).c_str()));
    return w > 0 && h > 0;
}

int main(int argc, char* argv[]) {
    // === ELEVATE WITH ARGS ===
    if (!IsAdmin()) {
        cout << "Requesting admin rights...\n";
        if (RequestElevation(argc, argv)) {
            return 0;  // Перезапуск с аргументами
        }
        cerr << "Failed to elevate.\n";
        system("pause");
        return 1;
    }
    cout << "Running as Administrator.\n";

    // === Find EXE ===
    string exePath = FindTsonicWin();
    if (exePath.empty()) {
        cerr << "Tsonic_win.exe not found!\n";
        system("pause");
        return 1;
    }
    cout << "Found: " << exePath << "\n";

    // === READ EXE ===
    ifstream in(exePath.c_str(), ios::binary | ios::ate);
    if (!in) {
        cerr << "Cannot read file.\n";
        system("pause");
        return 1;
    }
    streampos size = in.tellg();
    in.seekg(0);
    vector<uint8_t> data(size);
    in.read((char*)data.data(), size);
    in.close();

    // === FIND TABLE ===
    vector<uint8_t> sig = { 0x80, 0x02, 0x00, 0x00, 0xE0, 0x01, 0x00, 0x00 };
    auto it = search(data.begin(), data.end(), sig.begin(), sig.end());
    if (it == data.end()) {
        cerr << "Resolution table not found.\n";
        system("pause");
        return 1;
    }
    size_t base = it - data.begin();
    if (!ValidateResolutionTable(data, base)) {
        cerr << "Invalid table.\n";
        system("pause");
        return 1;
    }
    size_t patchOffset = base + 140;

    // === RESOLUTION LOGIC ===
    uint32_t width = 0, height = 0;

    if (argc == 3 && strcmp(argv[1], "--custom") == 0) {
        if (!ParseResolution(argv[2], width, height)) {
            cerr << "Invalid format. Use: --custom 800x600\n";
            system("pause");
            return 1;
        }
        cout << "Custom resolution: " << width << "x" << height << "\n";
    }
    else if (argc == 2 && strcmp(argv[1], "--max") == 0) {
        DEVMODE dm = { sizeof(dm) };
        int i = 0, mw = 0, mh = 0;
        while (EnumDisplaySettings(NULL, i++, &dm)) {
            if (dm.dmPelsWidth > mw) { mw = dm.dmPelsWidth; mh = dm.dmPelsHeight; }
        }
        width = mw; height = mh;
        cout << "Max resolution: " << width << "x" << height << "\n";
    }
    else if (argc == 1) {
        if (!GetCurrentResolution(width, height)) {
            cerr << "Cannot detect current resolution.\n";
            return 1;
        }
        cout << "Current resolution: " << width << "x" << height << "\n";
    }
    else {
        cerr << "Usage:\n";
        cerr << "  launcher.exe\n";
        cerr << "  launcher.exe --max\n";
        cerr << "  launcher.exe --custom 800x600\n";
        system("pause");
        return 1;
    }

    // === PATCH ===
    *(uint32_t*)(data.data() + patchOffset) = width;
    *(uint32_t*)(data.data() + patchOffset + 4) = height;
    ApplyWidescreenFix(data, width, height);

    // === WRITE ===
    ofstream out(exePath.c_str(), ios::binary);
    if (!out) {
        cerr << "Cannot write file.\n";
        system("pause");
        return 1;
    }
    out.write((char*)data.data(), size);
    out.close();

    Sleep(1000);
    cout << "Patched to " << width << "x" << height << ". Launching...\n";

    // === LAUNCH ===
    string cmd = "\"" + exePath + "\"";
    system(cmd.c_str());

    return 0;
}