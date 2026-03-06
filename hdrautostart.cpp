// =============================================================================
// hdrautostart.cpp  v3 — system-tray HDR auto-activator (HDRAutostart)
//   · Steam/game folders trigger HDR
//   · Browser fullscreen triggers HDR (auto-off when leaving fullscreen)
//   · KTC Local Dimming via DDC/CI (VCP 0xF4)
// =============================================================================
#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <shlobj.h>
#include <commdlg.h>
// DDC/CI — declared manually to avoid WIN32_LEAN_AND_MEAN conflicts
#define PHYSICAL_MONITOR_DESCRIPTION_SIZE 128
typedef struct _PHYSICAL_MONITOR {
    HANDLE hPhysicalMonitor;
    WCHAR  szPhysicalMonitorDescription[PHYSICAL_MONITOR_DESCRIPTION_SIZE];
} PHYSICAL_MONITOR, *LPPHYSICAL_MONITOR;
extern "C" {
    BOOL WINAPI GetNumberOfPhysicalMonitorsFromHMONITOR(HMONITOR, LPDWORD);
    BOOL WINAPI GetPhysicalMonitorsFromHMONITOR(HMONITOR, DWORD, LPPHYSICAL_MONITOR);
    BOOL WINAPI DestroyPhysicalMonitors(DWORD, LPPHYSICAL_MONITOR);
    BOOL WINAPI SetVCPFeature(HANDLE, BYTE, DWORD);
}

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <ctime>
#include <winhttp.h>
#include <urlmon.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "dxva2.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "urlmon.lib")
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")

#define APP_VERSION "0.13"

// =============================================================================
// Localisation
// =============================================================================
struct Lang {
    const char *menuFolders, *menuWhitelist, *menuBlacklist, *menuExclude, *menuStartup, *menuExit;
    const char *dlgFolders,  *dlgWhitelist,  *dlgBlacklist,  *dlgExclude;
    const char *btnAdd, *btnRemove, *btnClose;
    const char *btnAddFolder, *btnAddFile;
    const char *tipOn, *tipOff;
    const char *menuLocalDimming, *menuKTC, *menuKTCSDR;
    const char *ktcOff, *ktcAuto, *ktcLow, *ktcStd, *ktcHigh;
    const char *menuGithub;
};

static const Lang kES = {
    "Carpetas monitoreadas...", "Activar HDR siempre...", "Nunca activar HDR...", "Excluir...",
    "Ejecutar al inicio", "Salir",
    "Carpetas monitoreadas", "Activar HDR siempre", "Nunca activar HDR", "Excluir",
    "Agregar", "Eliminar", "Cerrar",
    "Agregar carpeta", "Agregar archivo",
    "HDRAutostart \x97 HDR activo", "HDRAutostart \x97 HDR inactivo",
    "Local Dimming", "HDR (KTC)", "SDR (KTC)",
    "Desactivado", "Auto", "Bajo", "Est\xe1ndar", "Alto",
    "GitHub"
};
static const Lang kEN = {
    "Monitored folders...", "Always enable HDR...", "Never enable HDR...", "Exclude...",
    "Run at startup", "Exit",
    "Monitored folders", "Always enable HDR", "Never enable HDR", "Exclude",
    "Add", "Remove", "Close",
    "Add folder", "Add file",
    "HDRAutostart \x97 HDR active", "HDRAutostart \x97 HDR inactive",
    "Local Dimming", "HDR (KTC)", "SDR (KTC)",
    "Off", "Auto", "Low", "Standard", "High",
    "GitHub"
};
static const Lang* L = &kEN;

static void DetectLang()
{
    if (PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_SPANISH) L = &kES;
}

// =============================================================================
// Config  (hdrautostart.ini next to exe)
// =============================================================================
struct Config {
    std::vector<std::string> folders;    // paths that trigger HDR (prefix match)
    std::vector<std::string> whitelist;  // specific exe paths that always trigger
    std::vector<std::string> blacklist;  // specific exe paths that never trigger HDR but may trigger KTC dimming
    std::vector<std::string> exclude;    // completely ignored — no HDR, no KTC dimming
    int ktcLocalDimming    = 0;  // 0=Off(default) 1=Auto 2=Low 3=Std 4=High  (juegos HDR)
    int ktcSdrLocalDimming = 0;  // 0=Off(default) 1=Auto 2=Low 3=Std 4=High  (juegos blacklist/SDR)
};

static CRITICAL_SECTION g_cfgLock;
static Config           g_cfg;

static std::string ExeDir()
{
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    char* s = strrchr(buf, '\\');
    if (s) *(s + 1) = '\0';
    return buf;
}

// ConfigDir: where config and log files are stored.
// If the installer wrote a ConfigPath registry key, use that.
// Otherwise fall back to ExeDir() (portable / developer mode).
static std::string ConfigDir()
{
    char buf[MAX_PATH] = {};  DWORD sz = sizeof(buf);
    if (RegGetValueA(HKEY_LOCAL_MACHINE, "Software\\HDRAutostart", "ConfigPath",
                     RRF_RT_REG_SZ, nullptr, buf, &sz) == ERROR_SUCCESS && buf[0]) {
        std::string s(buf);
        if (s.back() != '\\') s += '\\';
        return s;
    }
    sz = sizeof(buf);
    if (RegGetValueA(HKEY_CURRENT_USER, "Software\\HDRAutostart", "ConfigPath",
                     RRF_RT_REG_SZ, nullptr, buf, &sz) == ERROR_SUCCESS && buf[0]) {
        std::string s(buf);
        if (s.back() != '\\') s += '\\';
        return s;
    }
    return ExeDir();
}

static void SaveConfig()
{
    std::string path = ConfigDir() + "hdrautostart.ini";
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return;
    EnterCriticalSection(&g_cfgLock);
    fprintf(f, "[settings]\n");
    fprintf(f, "ktc_local_dimming=%d\n",     g_cfg.ktcLocalDimming);
    fprintf(f, "ktc_sdr_local_dimming=%d\n", g_cfg.ktcSdrLocalDimming);
    fprintf(f, "[folders]\n");
    for (auto& s : g_cfg.folders)   fprintf(f, "%s\n", s.c_str());
    fprintf(f, "[whitelist]\n");
    for (auto& s : g_cfg.whitelist) fprintf(f, "%s\n", s.c_str());
    fprintf(f, "[blacklist]\n");
    for (auto& s : g_cfg.blacklist) fprintf(f, "%s\n", s.c_str());
    fprintf(f, "[exclude]\n");
    for (auto& s : g_cfg.exclude)   fprintf(f, "%s\n", s.c_str());
    LeaveCriticalSection(&g_cfgLock);
    fclose(f);
}

static void LoadConfig()
{
    // Detect Steam default path
    char val[MAX_PATH] = {};  DWORD sz = sizeof(val);
    std::string steam;
    if (RegGetValueA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", "SteamPath",
                     RRF_RT_REG_SZ, nullptr, val, &sz) == ERROR_SUCCESS && val[0]) {
        steam = val;
        std::replace(steam.begin(), steam.end(), '/', '\\');
        if (steam.back() != '\\') steam += '\\';
        steam += "steamapps\\common\\";
    } else {
        steam = "C:\\Program Files (x86)\\Steam\\steamapps\\common\\";
    }

    std::string cfgDir = ConfigDir();
    std::string path   = cfgDir + "hdrautostart.ini";

    FILE* f = fopen(path.c_str(), "r");
    // --- Migration: portable/dev config next to exe → installed config dir ---
    if (!f) {
        // hdrautostart.ini next to the exe (portable or pre-install run)
        std::string o = ExeDir() + "hdrautostart.ini";
        if (o != path)
            if (CopyFileA(o.c_str(), path.c_str(), FALSE)) f = fopen(path.c_str(), "r");
    }
    // --- Migration from old steamhdr.ini ---
    if (!f) {
        // Try: same config dir, old name
        { std::string o = cfgDir + "steamhdr.ini";
          if (rename(o.c_str(), path.c_str()) == 0) f = fopen(path.c_str(), "r"); }
    }
    if (!f) {
        // Try: next to exe (when exe moved to dist\ subdirectory)
        { std::string o = ExeDir() + "steamhdr.ini";
          if (CopyFileA(o.c_str(), path.c_str(), FALSE)) f = fopen(path.c_str(), "r"); }
    }
    if (!f) {
        // Try: parent directory of exe (common case: old exe in hdr2\, new in hdr2\dist\)
        std::string exeD = ExeDir();
        if (!exeD.empty() && exeD.back() == '\\') exeD.pop_back();
        size_t p2 = exeD.rfind('\\');
        if (p2 != std::string::npos) {
            std::string o = exeD.substr(0, p2 + 1) + "steamhdr.ini";
            if (CopyFileA(o.c_str(), path.c_str(), FALSE)) f = fopen(path.c_str(), "r");
        }
    }
    if (!f) {
        g_cfg.folders.push_back(steam);
        SaveConfig();
        return;
    }

    enum Section { SEC_NONE, SEC_SETTINGS, SEC_FOLDERS, SEC_WHITELIST, SEC_BLACKLIST, SEC_EXCLUDE };
    Section sec = SEC_NONE;
    char line[MAX_PATH];
    while (fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = '\0';
        if (!n) continue;
        if (!strcmp(line, "[settings]"))  { sec = SEC_SETTINGS;  continue; }
        if (!strcmp(line, "[folders]"))   { sec = SEC_FOLDERS;   continue; }
        if (!strcmp(line, "[whitelist]")) { sec = SEC_WHITELIST; continue; }
        if (!strcmp(line, "[blacklist]")) { sec = SEC_BLACKLIST; continue; }
        if (!strcmp(line, "[exclude]"))   { sec = SEC_EXCLUDE;   continue; }
        switch (sec) {
        case SEC_SETTINGS:
            if (strncmp(line, "ktc_local_dimming=", 18) == 0) {
                int v = atoi(line + 18);
                if (v >= 1 && v <= 4) g_cfg.ktcLocalDimming = v;
            }
            if (strncmp(line, "ktc_sdr_local_dimming=", 22) == 0) {
                int v = atoi(line + 22);
                if (v >= 1 && v <= 4) g_cfg.ktcSdrLocalDimming = v;
            }
            break;
        case SEC_FOLDERS:   g_cfg.folders.push_back(line);   break;
        case SEC_WHITELIST: g_cfg.whitelist.push_back(line); break;
        case SEC_BLACKLIST: g_cfg.blacklist.push_back(line); break;
        case SEC_EXCLUDE:   g_cfg.exclude.push_back(line);   break;
        default: break;
        }
    }
    fclose(f);
    if (g_cfg.folders.empty()) { g_cfg.folders.push_back(steam); SaveConfig(); }
}

// =============================================================================
// Logging
// =============================================================================
static FILE* g_log = nullptr;

static void OpenLog()
{
    std::string p = ConfigDir() + "hdrautostart.log";
    g_log = fopen(p.c_str(), "a");
}

static void Log(const char* fmt, ...)
{
    time_t now = time(nullptr);
    struct tm t = {};  localtime_s(&t, &now);
    char ts[32];
    snprintf(ts, sizeof(ts), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
    char msg[512];
    va_list a;  va_start(a, fmt);  vsnprintf(msg, sizeof(msg), fmt, a);  va_end(a);
    if (g_log) { fprintf(g_log, "[%s] %s\n", ts, msg);  fflush(g_log); }
}

// =============================================================================
// DisplayConfig HDR toggle
// =============================================================================
static const DISPLAYCONFIG_DEVICE_INFO_TYPE kGetACI = (DISPLAYCONFIG_DEVICE_INFO_TYPE)9;
static const DISPLAYCONFIG_DEVICE_INFO_TYPE kSetACS = (DISPLAYCONFIG_DEVICE_INFO_TYPE)10;

struct ACI {
    DISPLAYCONFIG_DEVICE_INFO_HEADER h;
    union { struct { UINT32 sup:1; UINT32 en:1; UINT32 p:30; }; UINT32 v; };
    UINT32 enc, bpc;
};
struct ACS {
    DISPLAYCONFIG_DEVICE_INFO_HEADER h;
    union { struct { UINT32 on:1; UINT32 p:31; }; UINT32 v; };
};

static bool QPaths(std::vector<DISPLAYCONFIG_PATH_INFO>& p,
                   std::vector<DISPLAYCONFIG_MODE_INFO>& m)
{
    UINT32 np = 0, nm = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &np, &nm) != ERROR_SUCCESS) return false;
    p.resize(np); m.resize(nm);
    return QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &np, p.data(),
                              &nm, m.data(), nullptr) == ERROR_SUCCESS;
}

static bool SetHDR(bool on)
{
    std::vector<DISPLAYCONFIG_PATH_INFO> p;
    std::vector<DISPLAYCONFIG_MODE_INFO> m;
    if (!QPaths(p, m)) return false;
    bool any = false;
    for (auto& pi : p) {
        ACI info = {};
        info.h.type      = kGetACI;
        info.h.size      = sizeof(info);
        info.h.adapterId = pi.targetInfo.adapterId;
        info.h.id        = pi.targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&info.h) != ERROR_SUCCESS || !info.sup) continue;
        ACS st = {};
        st.h.type      = kSetACS;
        st.h.size      = sizeof(st);
        st.h.adapterId = pi.targetInfo.adapterId;
        st.h.id        = pi.targetInfo.id;
        st.on          = on ? 1u : 0u;
        if (DisplayConfigSetDeviceInfo(&st.h) == ERROR_SUCCESS) any = true;
    }
    return any;
}

// =============================================================================
// KTC Local Dimming via DDC/CI  (VCP code 0xF4)
//   1=Auto  2=Low  3=Standard  4=High
// =============================================================================
static BOOL CALLBACK KTCEnumProc(HMONITOR hmon, HDC, LPRECT, LPARAM lParam)
{
    DWORD count = 0;
    if (!GetNumberOfPhysicalMonitorsFromHMONITOR(hmon, &count) || count == 0) return TRUE;
    std::vector<PHYSICAL_MONITOR> mons(count);
    if (GetPhysicalMonitorsFromHMONITOR(hmon, count, mons.data())) {
        for (DWORD i = 0; i < count; i++) {
            BOOL ok = SetVCPFeature(mons[i].hPhysicalMonitor, 0xF4, (DWORD)lParam);
            if (!ok) Log("  KTC DDC: monitor %lu not supported or failed", i);
        }
        DestroyPhysicalMonitors(count, mons.data());
    }
    return TRUE;
}

static void SetKTCLocalDimming(int level)
{
    if (level == 0) return;   // KTC feature disabled — send no DDC commands
    Log("KTC LocalDimming -> %d (1=Auto,2=Low,3=Std,4=High)", level);
    EnumDisplayMonitors(nullptr, nullptr, KTCEnumProc, (LPARAM)level);
}

// =============================================================================
// Elevation
// =============================================================================
static bool IsElevated()
{
    BOOL e = FALSE;  HANDLE tok = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
        TOKEN_ELEVATION te = {};  DWORD sz = sizeof(te);
        if (GetTokenInformation(tok, TokenElevation, &te, sz, &sz)) e = te.TokenIsElevated;
        CloseHandle(tok);
    }
    return e != FALSE;
}

static void RelaunchElevated()
{
    char self[MAX_PATH] = {};  GetModuleFileNameA(nullptr, self, MAX_PATH);
    SHELLEXECUTEINFOA sei = {};
    sei.cbSize  = sizeof(sei);
    sei.lpVerb  = "runas";
    sei.lpFile  = self;
    sei.nShow   = SW_NORMAL;
    ShellExecuteExA(&sei);
}

// =============================================================================
// Startup registration  (via Task Scheduler — no UAC prompt at startup)
// =============================================================================

// Run any console command silently (hidden window), wait for completion.
static void RunSilent(const char* cmd)
{
    STARTUPINFOA si = {};  si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    char buf[640];  strncpy_s(buf, cmd, _TRUNCATE);
    if (CreateProcessA(nullptr, buf, nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 10000);
        CloseHandle(pi.hProcess);  CloseHandle(pi.hThread);
    }
}

// Check if the scheduled task exists (Task Scheduler stores tasks in registry).
static bool IsInStartup()
{
    HKEY hk;
    LONG r = RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Schedule\\TaskCache\\Tree\\HDRAutostart",
        0, KEY_QUERY_VALUE, &hk);
    if (r == ERROR_SUCCESS) { RegCloseKey(hk); return true; }
    return false;
}

// Create or delete a scheduled task that runs HDRAutostart at user logon
// with highest privileges — Windows will NOT show a UAC prompt for this task.
static void SetStartup(bool on)
{
    if (on) {
        char self[MAX_PATH] = {};  GetModuleFileNameA(nullptr, self, MAX_PATH);
        char cmd[MAX_PATH + 200] = {};
        snprintf(cmd, sizeof(cmd),
            "schtasks /create /tn HDRAutostart /tr \"\\\"%s\\\"\" /sc onlogon /rl highest /f",
            self);
        RunSilent(cmd);
        // Clean up old registry Run key if it existed
        HKEY hk;
        if (RegOpenKeyExA(HKEY_CURRENT_USER,
                "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                0, KEY_SET_VALUE, &hk) == ERROR_SUCCESS) {
            RegDeleteValueA(hk, "HDRAutostart");
            RegCloseKey(hk);
        }
    } else {
        RunSilent("schtasks /delete /tn HDRAutostart /f");
    }
}

// =============================================================================
// Process helpers
// =============================================================================
static std::string GetProcessPath(DWORD pid)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return "";
    char path[MAX_PATH] = {};  DWORD sz = MAX_PATH;
    QueryFullProcessImageNameA(h, 0, path, &sz);
    CloseHandle(h);
    return path;
}

static std::string ToLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

// Launchers/platform clients always ignored regardless of folder location
static const char* kLauncherExes[] = {
    // Steam
    "steam.exe", "steamwebhelper.exe", "steamservice.exe", "streaming_client.exe",
    // GOG Galaxy
    "gogalaxy.exe", "galaxyclient.exe", "galaxyclient helper.exe", "gogcomwebhelper.exe",
    // Xbox / Game Bar
    "xboxapp.exe", "gamebar.exe", "gamebarftserver.exe", "xboxpcappftserver.exe",
        "gameinputsvc.exe", "xgpuuncapsvc.exe",
    // Epic Games
    "epicgameslauncher.exe", "epicwebhelper.exe", "unrealcefsubprocess.exe",
    // Ubisoft Connect
    "ubisoft connect.exe", "ubisoftconnect.exe", "uplay.exe",
        "ubisoftgamelauncher.exe", "uplaywebcore.exe",
    // EA App
    "eadesktop.exe", "ealauncher.exe", "eabackgroundservice.exe",
        "eaconnect_me.exe", "eacefsubproc.exe", "link2ea.exe",
    nullptr
};

// Returns  1 = activate HDR
//          0 = ignore
//         -1 = block (blacklist)
static int ClassifyProcess(const std::string& path)
{
    std::string lo = ToLower(path);

    // Always ignore known platform launchers (even if inside a monitored folder)
    {
        const char* p = lo.c_str();
        const char* b = strrchr(p, '\\');
        b = b ? b + 1 : p;
        for (int i = 0; kLauncherExes[i]; ++i)
            if (!strcmp(b, kLauncherExes[i])) return 0;
    }
    EnterCriticalSection(&g_cfgLock);
    // User-defined exclude list: exact exe match or recursive folder prefix match
    for (auto& e : g_cfg.exclude) {
        std::string elo = ToLower(e);
        bool match = (!elo.empty() && elo.back() == '\\') ? (lo.find(elo) == 0) : (elo == lo);
        if (match) { LeaveCriticalSection(&g_cfgLock); return 0; }
    }
    for (auto& e : g_cfg.blacklist)
        if (ToLower(e) == lo) { LeaveCriticalSection(&g_cfgLock); return -1; }
    for (auto& e : g_cfg.whitelist)
        if (ToLower(e) == lo) { LeaveCriticalSection(&g_cfgLock); return 1; }
    for (auto& f : g_cfg.folders)
        if (lo.find(ToLower(f)) == 0) { LeaveCriticalSection(&g_cfgLock); return 1; }
    LeaveCriticalSection(&g_cfgLock);
    return 0;
}

// =============================================================================
// Browser fullscreen detection
// =============================================================================
static const char* kBrowserExes[] = {
    "chrome.exe", "msedge.exe", "firefox.exe", "opera.exe",
    "brave.exe", "vivaldi.exe", "iexplore.exe", "waterfox.exe",
    "librewolf.exe", "thorium.exe", nullptr
};

static bool IsBrowserExe(const std::string& path)
{
    std::string lo = ToLower(path);
    const char* p = lo.c_str();
    const char* base = strrchr(p, '\\');
    base = base ? base + 1 : p;
    for (int i = 0; kBrowserExes[i]; ++i)
        if (!strcmp(base, kBrowserExes[i])) return true;
    return false;
}

// Returns true if a browser window is currently covering a full monitor
static bool CheckBrowserFullscreen()
{
    HWND fg = GetForegroundWindow();
    if (!fg) return false;

    // Must have no caption/title bar (a real fullscreen window)
    if (GetWindowLongA(fg, GWL_STYLE) & WS_CAPTION) return false;

    // Must belong to a known browser
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (!IsBrowserExe(GetProcessPath(pid))) return false;

    // Window must cover the entire monitor
    RECT wrc;
    GetWindowRect(fg, &wrc);
    HMONITOR hmon = MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};  mi.cbSize = sizeof(mi);
    GetMonitorInfo(hmon, &mi);

    return wrc.left  <= mi.rcMonitor.left  &&
           wrc.top   <= mi.rcMonitor.top   &&
           wrc.right >= mi.rcMonitor.right &&
           wrc.bottom>= mi.rcMonitor.bottom;
}

// =============================================================================
// Tray icon — "HDR" drawn with GDI
// =============================================================================
static HICON CreateHDRIcon(bool active)
{
    // Use the actual tray icon size so the text is never scaled down
    const int SZ = GetSystemMetrics(SM_CXSMICON);  // 16 @ 100%, 20 @ 125%, etc.

    HDC hScr = GetDC(nullptr);
    HDC hdc  = CreateCompatibleDC(hScr);
    HBITMAP hbm = CreateCompatibleBitmap(hScr, SZ, SZ);
    SelectObject(hdc, hbm);

    COLORREF bg = active ? RGB(220, 100, 0) : RGB(50, 50, 50);
    HBRUSH br = CreateSolidBrush(bg);
    RECT r = {0, 0, SZ, SZ};
    FillRect(hdc, &r, br);
    DeleteObject(br);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));

    // Auto-fit: start at full icon height, shrink until "HDR" fits on one row.
    // Negative height = character height in pixels (not logical units).
    HFONT hf = nullptr;
    SIZE  ts  = {};
    for (int fh = SZ; fh >= 4; fh--) {
        if (hf) DeleteObject(hf);
        hf = CreateFontA(-fh, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Arial");
        HGDIOBJ tmp = SelectObject(hdc, hf);
        GetTextExtentPoint32A(hdc, "HDR", 3, &ts);
        SelectObject(hdc, tmp);
        if (ts.cx <= SZ - 2 && ts.cy <= SZ) break;
    }

    HGDIOBJ old = SelectObject(hdc, hf);
    int x = (SZ - ts.cx) / 2;  if (x < 0) x = 0;
    int y = (SZ - ts.cy) / 2;  if (y < 0) y = 0;
    TextOutA(hdc, x, y, "HDR", 3);
    SelectObject(hdc, old);
    DeleteObject(hf);

    HBITMAP hMask = CreateBitmap(SZ, SZ, 1, 1, nullptr);
    HDC hdcM = CreateCompatibleDC(hScr);
    SelectObject(hdcM, hMask);
    PatBlt(hdcM, 0, 0, SZ, SZ, BLACKNESS);
    DeleteDC(hdcM);
    DeleteDC(hdc);
    ReleaseDC(nullptr, hScr);

    ICONINFO ii = {};
    ii.fIcon    = TRUE;
    ii.hbmMask  = hMask;
    ii.hbmColor = hbm;
    HICON ico = CreateIconIndirect(&ii);
    DeleteObject(hbm);
    DeleteObject(hMask);
    return ico;
}

// =============================================================================
// Monitor thread  (game detection)
// =============================================================================
#define WM_TRAYICON   (WM_APP + 1)
#define WM_HDRSTATUS  (WM_APP + 2)   // wParam: 1=game HDR on, 0=off

static HANDLE g_stopEvent = nullptr;
static HWND   g_trayWnd   = nullptr;
static char   g_hdrSource[MAX_PATH] = {};  // who activated HDR (game exe name or "Browser")

static DWORD WINAPI MonitorThread(LPVOID)
{
    Log("Monitor started");
    std::map<DWORD, HANDLE> games;     // HDR games (whitelist/folders)
    std::map<DWORD, HANDLE> sdrGames;  // SDR/blacklisted games (KTC dimming only)
    bool hdrActive        = false;
    bool sdrDimmingActive = false;

    std::set<DWORD> seen;
    {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32 pe = {};  pe.dwSize = sizeof(pe);
            if (Process32First(snap, &pe))
                do { seen.insert(pe.th32ProcessID); } while (Process32Next(snap, &pe));
            CloseHandle(snap);
        }
    }

    while (WaitForSingleObject(g_stopEvent, 100) == WAIT_TIMEOUT)
    {
        // --- Check for exited HDR games ---
        for (auto it = games.begin(); it != games.end(); ) {
            if (WaitForSingleObject(it->second, 0) == WAIT_OBJECT_0) {
                char name[MAX_PATH] = {};  DWORD sz = MAX_PATH;
                QueryFullProcessImageNameA(it->second, 0, name, &sz);
                const char* base = strrchr(name, '\\');
                Log("Game exited: %s (PID %lu)", base ? base + 1 : name, it->first);
                CloseHandle(it->second);
                it = games.erase(it);
            } else ++it;
        }

        // --- Check for exited SDR games ---
        for (auto it = sdrGames.begin(); it != sdrGames.end(); ) {
            if (WaitForSingleObject(it->second, 0) == WAIT_OBJECT_0) {
                char name[MAX_PATH] = {};  DWORD sz = MAX_PATH;
                QueryFullProcessImageNameA(it->second, 0, name, &sz);
                const char* base = strrchr(name, '\\');
                Log("SDR game exited: %s (PID %lu)", base ? base + 1 : name, it->first);
                CloseHandle(it->second);
                it = sdrGames.erase(it);
            } else ++it;
        }

        // --- All HDR games closed ---
        if (games.empty() && hdrActive) {
            Log("All HDR games closed — disabling HDR");
            SetHDR(false);
            int hDim, sDim;
            EnterCriticalSection(&g_cfgLock);
            hDim = g_cfg.ktcLocalDimming;
            sDim = g_cfg.ktcSdrLocalDimming;
            g_hdrSource[0] = '\0';
            LeaveCriticalSection(&g_cfgLock);
            if (hDim > 0) {
                if (!sdrGames.empty() && sDim > 0) {
                    // SDR game still running — restore SDR dimming
                    SetKTCLocalDimming(sDim);
                    sdrDimmingActive = true;
                } else {
                    SetKTCLocalDimming(1);  // reset to Auto
                }
            }
            hdrActive = false;
            if (g_trayWnd) PostMessage(g_trayWnd, WM_HDRSTATUS, 0, 0);
        }

        // --- All SDR games closed ---
        if (sdrGames.empty() && sdrDimmingActive && !hdrActive) {
            int sDim;
            EnterCriticalSection(&g_cfgLock);
            sDim = g_cfg.ktcSdrLocalDimming;
            LeaveCriticalSection(&g_cfgLock);
            if (sDim > 0) { Log("All SDR games closed — reset dimming"); SetKTCLocalDimming(1); }
            sdrDimmingActive = false;
        }

        // --- Snapshot new processes ---
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) continue;

        PROCESSENTRY32 pe = {};  pe.dwSize = sizeof(pe);
        if (Process32First(snap, &pe)) {
            do {
                DWORD pid = pe.th32ProcessID;
                if (seen.count(pid) || games.count(pid) || sdrGames.count(pid)) continue;
                seen.insert(pid);

                std::string path = GetProcessPath(pid);
                if (path.empty()) continue;

                int cls = ClassifyProcess(path);
                if (cls == 0) continue;

                const char* base = strrchr(path.c_str(), '\\');

                if (cls == 1) {
                    // HDR game
                    Log("Game detected: %s (PID %lu)", base ? base + 1 : path.c_str(), pid);
                    HANDLE hProc = OpenProcess(
                        SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
                    if (!hProc) { Log("  (cannot open process)"); continue; }

                    if (games.empty()) {
                        Log("Enabling HDR...");
                        bool ok = false;
                        for (int r = 0; r < 3 && !ok; r++) {
                            if (r) Sleep(300);
                            ok = SetHDR(true);
                        }
                        Log("HDR %s", ok ? "ENABLED" : "enable FAILED after retries");
                        int dimming;
                        EnterCriticalSection(&g_cfgLock);
                        dimming = g_cfg.ktcLocalDimming;
                        strncpy_s(g_hdrSource, base ? base + 1 : path.c_str(), _TRUNCATE);
                        LeaveCriticalSection(&g_cfgLock);
                        SetKTCLocalDimming(dimming);
                        sdrDimmingActive = false;  // HDR takes precedence
                        hdrActive = true;
                        if (g_trayWnd) PostMessage(g_trayWnd, WM_HDRSTATUS, 1, 0);
                    }
                    games[pid] = hProc;

                } else if (cls == -1) {
                    // Blacklisted / SDR game
                    int sDim;
                    EnterCriticalSection(&g_cfgLock);
                    sDim = g_cfg.ktcSdrLocalDimming;
                    LeaveCriticalSection(&g_cfgLock);
                    if (sDim == 0) continue;  // SDR dimming disabled — ignore

                    Log("SDR game detected: %s (PID %lu)", base ? base + 1 : path.c_str(), pid);
                    HANDLE hProc = OpenProcess(
                        SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
                    if (!hProc) { Log("  (cannot open SDR process)"); continue; }

                    if (sdrGames.empty() && !hdrActive) {
                        Log("SDR dimming -> %d", sDim);
                        SetKTCLocalDimming(sDim);
                        sdrDimmingActive = true;
                    }
                    sdrGames[pid] = hProc;
                }

            } while (Process32Next(snap, &pe));
        }
        CloseHandle(snap);
    }

    // Shutdown cleanup
    if (hdrActive) {
        SetHDR(false);
        { int d; EnterCriticalSection(&g_cfgLock); d=g_cfg.ktcLocalDimming; LeaveCriticalSection(&g_cfgLock); if(d>0) SetKTCLocalDimming(1); }
        if (g_trayWnd) PostMessage(g_trayWnd, WM_HDRSTATUS, 0, 0);
    }
    if (sdrDimmingActive) {
        int d; EnterCriticalSection(&g_cfgLock); d=g_cfg.ktcSdrLocalDimming; LeaveCriticalSection(&g_cfgLock);
        if (d > 0) SetKTCLocalDimming(1);
    }
    for (auto& kv : games)    CloseHandle(kv.second);
    for (auto& kv : sdrGames) CloseHandle(kv.second);
    Log("Monitor stopped");
    return 0;
}

// =============================================================================
// List-management dialog  (generic: folder list or exe list)
// =============================================================================
#define IDC_LBOX       100
#define IDC_ADD        101
#define IDC_DEL        102
#define IDC_CLOSE2     103
#define IDC_ADD_FOLDER 104

// mode: 0=files only  1=folders only  2=both (files + folders, used by Exclude dialog)
struct ListDlgData { std::vector<std::string>* items; int mode; HFONT hFont = nullptr; };

static void Populate(HWND lb, std::vector<std::string>* items)
{
    SendMessageA(lb, LB_RESETCONTENT, 0, 0);
    HDC hdc = GetDC(lb);
    HFONT hf = (HFONT)SendMessageA(lb, WM_GETFONT, 0, 0);
    HGDIOBJ old = SelectObject(hdc, hf ? hf : GetStockObject(DEFAULT_GUI_FONT));
    int maxW = 0;
    EnterCriticalSection(&g_cfgLock);
    for (auto& s : *items) {
        SendMessageA(lb, LB_ADDSTRING, 0, (LPARAM)s.c_str());
        SIZE sz = {};
        GetTextExtentPoint32A(hdc, s.c_str(), (int)s.size(), &sz);
        if (sz.cx > maxW) maxW = sz.cx;
    }
    LeaveCriticalSection(&g_cfgLock);
    SelectObject(hdc, old);
    ReleaseDC(lb, hdc);
    SendMessageA(lb, LB_SETHORIZONTALEXTENT, maxW + 8, 0);
}

static LRESULT CALLBACK ListDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    ListDlgData* d = (ListDlgData*)GetWindowLongPtrA(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        d = (ListDlgData*)((CREATESTRUCTA*)lp)->lpCreateParams;
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)d);

        // DPI-aware dimensions
        typedef UINT(WINAPI* PFN_GetDpiForWindow)(HWND);
        static auto pfnDpi = (PFN_GetDpiForWindow)GetProcAddress(GetModuleHandleA("user32.dll"), "GetDpiForWindow");
        UINT dpi = pfnDpi ? pfnDpi(hwnd) : 96;
        auto S = [&](int v){ return MulDiv(v, (int)dpi, 96); };

        // System message font (DPI-aware)
        NONCLIENTMETRICSA ncm = {}; ncm.cbSize = sizeof(ncm);
        SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        d->hFont = CreateFontIndirectA(&ncm.lfMessageFont);

        RECT rc;  GetClientRect(hwnd, &rc);
        int gap = S(8), bw = S(90), bh = S(28);
        int lbH = rc.bottom - bh - gap * 3;

        HWND lb = CreateWindowExA(WS_EX_CLIENTEDGE, "LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
            gap, gap, rc.right - gap * 2, lbH,
            hwnd, (HMENU)IDC_LBOX, nullptr, nullptr);
        SendMessageA(lb, WM_SETFONT, (WPARAM)d->hFont, FALSE);

        int y = lbH + gap * 2;
        if (d->mode == 2) {
            // Exclude dialog: Add folder + Add file + Remove + Close
            HWND bAddF = CreateWindowA("BUTTON", L->btnAddFolder, WS_CHILD | WS_VISIBLE,
                gap,                     y, bw, bh, hwnd, (HMENU)IDC_ADD_FOLDER, nullptr, nullptr);
            HWND bAddE = CreateWindowA("BUTTON", L->btnAddFile,   WS_CHILD | WS_VISIBLE,
                gap + bw + gap,          y, bw, bh, hwnd, (HMENU)IDC_ADD,        nullptr, nullptr);
            HWND bDel  = CreateWindowA("BUTTON", L->btnRemove,    WS_CHILD | WS_VISIBLE,
                gap + (bw + gap) * 2,    y, bw, bh, hwnd, (HMENU)IDC_DEL,        nullptr, nullptr);
            HWND bCls  = CreateWindowA("BUTTON", L->btnClose,     WS_CHILD | WS_VISIBLE,
                rc.right - bw - gap,     y, bw, bh, hwnd, (HMENU)IDC_CLOSE2,     nullptr, nullptr);
            SendMessageA(bAddF, WM_SETFONT, (WPARAM)d->hFont, FALSE);
            SendMessageA(bAddE, WM_SETFONT, (WPARAM)d->hFont, FALSE);
            SendMessageA(bDel,  WM_SETFONT, (WPARAM)d->hFont, FALSE);
            SendMessageA(bCls,  WM_SETFONT, (WPARAM)d->hFont, FALSE);
        } else {
            HWND bAdd = CreateWindowA("BUTTON", L->btnAdd,    WS_CHILD | WS_VISIBLE,
                gap,                 y, bw, bh, hwnd, (HMENU)IDC_ADD,    nullptr, nullptr);
            HWND bDel = CreateWindowA("BUTTON", L->btnRemove, WS_CHILD | WS_VISIBLE,
                gap + bw + gap,      y, bw, bh, hwnd, (HMENU)IDC_DEL,    nullptr, nullptr);
            HWND bCls = CreateWindowA("BUTTON", L->btnClose,  WS_CHILD | WS_VISIBLE,
                rc.right - bw - gap, y, bw, bh, hwnd, (HMENU)IDC_CLOSE2, nullptr, nullptr);
            SendMessageA(bAdd, WM_SETFONT, (WPARAM)d->hFont, FALSE);
            SendMessageA(bDel, WM_SETFONT, (WPARAM)d->hFont, FALSE);
            SendMessageA(bCls, WM_SETFONT, (WPARAM)d->hFont, FALSE);
        }

        Populate(lb, d->items);
        return 0;
    }

    case WM_COMMAND: {
        HWND lb = GetDlgItem(hwnd, IDC_LBOX);

        auto browseFolder = [&]() {
            BROWSEINFOA bi = {};
            bi.hwndOwner = hwnd;
            bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;
            LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
            if (pidl) {
                char path[MAX_PATH] = {};
                if (SHGetPathFromIDListA(pidl, path)) {
                    std::string s(path);
                    if (s.back() != '\\') s += '\\';
                    EnterCriticalSection(&g_cfgLock);
                    d->items->push_back(s);
                    LeaveCriticalSection(&g_cfgLock);
                    SaveConfig();
                    Populate(lb, d->items);
                }
                CoTaskMemFree(pidl);
            }
        };
        auto browseFile = [&]() {
            char path[MAX_PATH] = {};
            OPENFILENAMEA ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner   = hwnd;
            ofn.lpstrFilter = "Executables\0*.exe\0All Files\0*.*\0";
            ofn.lpstrFile   = path;
            ofn.nMaxFile    = MAX_PATH;
            ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameA(&ofn)) {
                EnterCriticalSection(&g_cfgLock);
                d->items->push_back(path);
                LeaveCriticalSection(&g_cfgLock);
                SaveConfig();
                Populate(lb, d->items);
            }
        };

        if (LOWORD(wp) == IDC_ADD_FOLDER) { browseFolder(); }

        if (LOWORD(wp) == IDC_ADD) {
            if (d->mode == 1) browseFolder();
            else              browseFile();   // mode 0 = files only, mode 2 = add file
        }

        if (LOWORD(wp) == IDC_DEL) {
            int sel = (int)SendMessageA(lb, LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR) {
                EnterCriticalSection(&g_cfgLock);
                if (sel < (int)d->items->size())
                    d->items->erase(d->items->begin() + sel);
                LeaveCriticalSection(&g_cfgLock);
                SaveConfig();
                Populate(lb, d->items);
            }
        }

        if (LOWORD(wp) == IDC_CLOSE2) DestroyWindow(hwnd);
        return 0;
    }

    case WM_CLOSE:   DestroyWindow(hwnd); return 0;
    case WM_DESTROY:
        if (d && d->hFont) DeleteObject(d->hFont);
        delete d;
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static void ShowListDialog(const char* title,
                           std::vector<std::string>* items,
                           int mode)
{
    ListDlgData* data = new ListDlgData{items, mode};
    HINSTANCE hInst   = (HINSTANCE)GetModuleHandleA(nullptr);

    // Get system DPI to size the window properly
    typedef UINT(WINAPI* PFN_GetDpiForSystem)();
    static auto pfnDpiSys = (PFN_GetDpiForSystem)GetProcAddress(GetModuleHandleA("user32.dll"), "GetDpiForSystem");
    UINT dpi = pfnDpiSys ? pfnDpiSys() : 96;
    int W = MulDiv(640, (int)dpi, 96);
    int H = MulDiv(460, (int)dpi, 96);

    HWND hw = CreateWindowExA(
        WS_EX_TOPMOST,
        "HDRAutostartListDlg", title,
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, W, H,
        nullptr, nullptr, hInst, data);
    if (hw) SetForegroundWindow(hw);
    else    delete data;
}

// =============================================================================
// Auto-update  (background thread → WinHTTP + URLDownloadToFile)
// =============================================================================
#define WM_UPDATE_AVAILABLE (WM_APP + 3)

struct UpdateInfo { char tag[64]; char dlUrl[512]; };

static bool IsNewerVersion(const char* remote)
{
    const char* r = (*remote == 'v' || *remote == 'V') ? remote + 1 : remote;
    int lMaj=0, lMin=0, lPat=0, rMaj=0, rMin=0, rPat=0;
    sscanf(APP_VERSION, "%d.%d.%d", &lMaj, &lMin, &lPat);
    sscanf(r,           "%d.%d.%d", &rMaj, &rMin, &rPat);
    if (rMaj != lMaj) return rMaj > lMaj;
    if (rMin != lMin) return rMin > lMin;
    return rPat > lPat;
}

struct DownloadArgs { char url[512]; char path[MAX_PATH]; };

static DWORD WINAPI DoSilentUpdate(LPVOID p)
{
    DownloadArgs* a = (DownloadArgs*)p;
    HRESULT hr = URLDownloadToFileA(nullptr, a->url, a->path, 0, nullptr);
    if (SUCCEEDED(hr)) {
        // Run installer silently — it will kill us, install, then relaunch the exe
        SHELLEXECUTEINFOA sei = {};
        sei.cbSize = sizeof(sei);
        sei.fMask  = SEE_MASK_NOCLOSEPROCESS;
        sei.lpVerb = "open";
        sei.lpFile = a->path;
        sei.lpParameters = "/S";
        sei.nShow  = SW_HIDE;
        ShellExecuteExA(&sei);
    }
    delete a;
    return 0;
}

static DWORD WINAPI UpdateCheckThread(LPVOID)
{
    Sleep(8000);  // let the app settle before checking

    HINTERNET hSes = WinHttpOpen(L"HDRAutostart-Update/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSes) return 0;

    HINTERNET hCon = WinHttpConnect(hSes, L"api.github.com",
        INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hCon) { WinHttpCloseHandle(hSes); return 0; }

    HINTERNET hReq = WinHttpOpenRequest(hCon, L"GET",
        L"/repos/conecta6/HDRAutostart-W11/releases/latest",
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (!hReq) { WinHttpCloseHandle(hCon); WinHttpCloseHandle(hSes); return 0; }

    WinHttpAddRequestHeaders(hReq,
        L"Accept: application/vnd.github+json\r\n",
        (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD);

    if (!WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) goto cleanup;
    if (!WinHttpReceiveResponse(hReq, nullptr)) goto cleanup;

    {
        std::string body;
        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
            std::string chunk(avail, '\0');
            DWORD got = 0;
            if (WinHttpReadData(hReq, &chunk[0], avail, &got))
                body.append(chunk, 0, got);
        }

        // Extract "tag_name":"..."
        const char* p = strstr(body.c_str(), "\"tag_name\"");
        if (!p) goto cleanup;
        p = strchr(p, ':');  if (!p) goto cleanup;
        p = strchr(p, '"');  if (!p) goto cleanup;
        ++p;
        const char* e = strchr(p, '"');  if (!e) goto cleanup;
        size_t tlen = (size_t)(e - p);
        if (tlen == 0 || tlen >= 64) goto cleanup;

        char tag[64] = {};
        memcpy(tag, p, tlen);

        Log("Update check: latest=%s current=" APP_VERSION, tag);
        if (!IsNewerVersion(tag)) goto cleanup;

        UpdateInfo* info = new UpdateInfo;
        strncpy_s(info->tag, tag, _TRUNCATE);
        snprintf(info->dlUrl, sizeof(info->dlUrl),
            "https://github.com/conecta6/HDRAutostart-W11/releases/download/%s/HDRAutostartSetup.exe",
            tag);

        if (g_trayWnd) PostMessageA(g_trayWnd, WM_UPDATE_AVAILABLE, 0, (LPARAM)info);
    }

cleanup:
    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hCon);
    WinHttpCloseHandle(hSes);
    return 0;
}

// =============================================================================
// Tray window
// =============================================================================
#define ID_TRAY_FOLDERS   200
#define ID_TRAY_WHITELIST 201
#define ID_TRAY_BLACKLIST 202
#define ID_TRAY_EXCLUDE   206
#define ID_TRAY_ABOUT     199
#define ID_TRAY_STARTUP   203
#define ID_TRAY_EXIT      204
#define ID_TRAY_GITHUB    205
#define ID_KTC_OFF          299
#define ID_KTC_AUTO         300
#define ID_KTC_LOW          301
#define ID_KTC_STANDARD     302
#define ID_KTC_HIGH         303
#define ID_KTC_SDR_OFF      309
#define ID_KTC_SDR_AUTO     310
#define ID_KTC_SDR_LOW      311
#define ID_KTC_SDR_STANDARD 312
#define ID_KTC_SDR_HIGH     313

#define TIMER_BROWSER     1   // 500ms browser fullscreen check
#define TIMER_TRAY_RETRY  2   // 1s retry when Shell_NotifyIcon(NIM_ADD) fails at logon

static NOTIFYICONDATAA g_nid          = {};
static HICON           g_icoOff       = nullptr;
static HICON           g_icoOn        = nullptr;
static UINT            WM_TASKBARCREATED = 0;
static int             g_trayRetry    = 0;  // retry counter for NIM_ADD

// These are only accessed on the main (tray) thread — no lock needed
static bool g_gameHdrOn      = false;  // updated by WM_HDRSTATUS from monitor thread
static bool g_browserHdrOn   = false;  // managed by TIMER_BROWSER
static int  g_browserSkipTicks = 20;   // skip first 10s of browser checks at startup

static void UpdateTray(bool on)
{
    if (on && g_hdrSource[0])
        snprintf(g_nid.szTip, sizeof(g_nid.szTip), "%s [%s]  v" APP_VERSION, L->tipOn, g_hdrSource);
    else
        snprintf(g_nid.szTip, sizeof(g_nid.szTip), "%s  v" APP_VERSION, on ? L->tipOn : L->tipOff);
    g_nid.hIcon = on ? g_icoOn : g_icoOff;
    Shell_NotifyIconA(NIM_MODIFY, &g_nid);
}

static void CheckBrowserHDR()
{
    // Skip the first ~10 seconds after startup to let the shell settle
    if (g_browserSkipTicks > 0) { --g_browserSkipTicks; return; }

    // Game controls HDR while running — don't interfere
    if (g_gameHdrOn) {
        g_browserHdrOn = false;
        return;
    }

    bool isFS = CheckBrowserFullscreen();

    if (isFS && !g_browserHdrOn) {
        Log("Browser fullscreen — enabling HDR");
        SetHDR(true);
        int dimming;
        EnterCriticalSection(&g_cfgLock);
        dimming = g_cfg.ktcLocalDimming;
        LeaveCriticalSection(&g_cfgLock);
        SetKTCLocalDimming(dimming);
        strncpy_s(g_hdrSource, "Browser", _TRUNCATE);
        g_browserHdrOn = true;
        UpdateTray(true);
    } else if (!isFS && g_browserHdrOn) {
        Log("Browser left fullscreen — disabling HDR");
        SetHDR(false);
        { int d; EnterCriticalSection(&g_cfgLock); d=g_cfg.ktcLocalDimming; LeaveCriticalSection(&g_cfgLock); if(d>0) SetKTCLocalDimming(1); }
        g_hdrSource[0] = '\0';
        g_browserHdrOn = false;
        UpdateTray(false);
    }
}

static LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_UPDATE_AVAILABLE) {
        UpdateInfo* info = (UpdateInfo*)lp;
        Log("Update available: %s — downloading silently", info->tag);

        // Balloon notification
        g_nid.uFlags |= NIF_INFO;
        snprintf(g_nid.szInfo,      sizeof(g_nid.szInfo),
            L == &kES ? "Actualizando a %s..." : "Updating to %s...", info->tag);
        snprintf(g_nid.szInfoTitle, sizeof(g_nid.szInfoTitle), "HDRAutostart");
        g_nid.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
        Shell_NotifyIconA(NIM_MODIFY, &g_nid);
        g_nid.uFlags &= ~NIF_INFO;

        // Download and install in background thread
        char tmpDir[MAX_PATH];
        GetTempPathA(MAX_PATH, tmpDir);
        DownloadArgs* da = new DownloadArgs;
        strncpy_s(da->url,  info->dlUrl, _TRUNCATE);
        snprintf(da->path, sizeof(da->path),
            "%sHDRAutostartSetup_%s.exe", tmpDir, info->tag);
        CloseHandle(CreateThread(nullptr, 0, DoSilentUpdate, da, 0, nullptr));

        delete info;
        return 0;
    }

    if (msg == WM_TASKBARCREATED) {
        KillTimer(hwnd, TIMER_TRAY_RETRY);
        g_trayRetry = 0;
        Shell_NotifyIconA(NIM_ADD, &g_nid);
        return 0;
    }

    switch (msg) {
    case WM_TIMER:
        if (wp == TIMER_BROWSER) {
            CheckBrowserHDR();
        } else if (wp == TIMER_TRAY_RETRY) {
            if (Shell_NotifyIconA(NIM_ADD, &g_nid) || ++g_trayRetry >= 30)
                KillTimer(hwnd, TIMER_TRAY_RETRY);
        }
        return 0;

    case WM_HDRSTATUS:
        g_gameHdrOn = (wp != 0);
        UpdateTray(g_gameHdrOn || g_browserHdrOn);
        return 0;

    case WM_TRAYICON:
        if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) {
            HMENU m = CreatePopupMenu();
            AppendMenuA(m, MF_STRING | MF_DISABLED | MF_GRAYED, ID_TRAY_ABOUT, "HDRAutostart v" APP_VERSION);
            AppendMenuA(m, MF_SEPARATOR, 0, nullptr);
            AppendMenuA(m, MF_STRING, ID_TRAY_FOLDERS,   L->menuFolders);
            AppendMenuA(m, MF_STRING, ID_TRAY_WHITELIST, L->menuWhitelist);
            AppendMenuA(m, MF_STRING, ID_TRAY_BLACKLIST, L->menuBlacklist);
            AppendMenuA(m, MF_STRING, ID_TRAY_EXCLUDE,   L->menuExclude);
            AppendMenuA(m, MF_SEPARATOR, 0, nullptr);

            // Local Dimming parent menu
            int d, ds;
            EnterCriticalSection(&g_cfgLock);
            d  = g_cfg.ktcLocalDimming;
            ds = g_cfg.ktcSdrLocalDimming;
            LeaveCriticalSection(&g_cfgLock);

            HMENU sub = CreatePopupMenu();
            AppendMenuA(sub, MF_STRING | (d==0?MF_CHECKED:0u), ID_KTC_OFF,      L->ktcOff);
            AppendMenuA(sub, MF_SEPARATOR, 0, nullptr);
            AppendMenuA(sub, MF_STRING | (d==1?MF_CHECKED:0u), ID_KTC_AUTO,     L->ktcAuto);
            AppendMenuA(sub, MF_STRING | (d==2?MF_CHECKED:0u), ID_KTC_LOW,      L->ktcLow);
            AppendMenuA(sub, MF_STRING | (d==3?MF_CHECKED:0u), ID_KTC_STANDARD, L->ktcStd);
            AppendMenuA(sub, MF_STRING | (d==4?MF_CHECKED:0u), ID_KTC_HIGH,     L->ktcHigh);

            HMENU subSDR = CreatePopupMenu();
            AppendMenuA(subSDR, MF_STRING | (ds==0?MF_CHECKED:0u), ID_KTC_SDR_OFF,      L->ktcOff);
            AppendMenuA(subSDR, MF_SEPARATOR, 0, nullptr);
            AppendMenuA(subSDR, MF_STRING | (ds==1?MF_CHECKED:0u), ID_KTC_SDR_AUTO,     L->ktcAuto);
            AppendMenuA(subSDR, MF_STRING | (ds==2?MF_CHECKED:0u), ID_KTC_SDR_LOW,      L->ktcLow);
            AppendMenuA(subSDR, MF_STRING | (ds==3?MF_CHECKED:0u), ID_KTC_SDR_STANDARD, L->ktcStd);
            AppendMenuA(subSDR, MF_STRING | (ds==4?MF_CHECKED:0u), ID_KTC_SDR_HIGH,     L->ktcHigh);

            HMENU dimMenu = CreatePopupMenu();
            AppendMenuA(dimMenu, MF_POPUP, (UINT_PTR)sub,    L->menuKTC);
            AppendMenuA(dimMenu, MF_POPUP, (UINT_PTR)subSDR, L->menuKTCSDR);
            AppendMenuA(m, MF_POPUP, (UINT_PTR)dimMenu, L->menuLocalDimming);
            AppendMenuA(m, MF_SEPARATOR, 0, nullptr);

            UINT startFlag = MF_STRING | (IsInStartup() ? MF_CHECKED : 0u);
            AppendMenuA(m, startFlag, ID_TRAY_STARTUP, L->menuStartup);
            AppendMenuA(m, MF_SEPARATOR, 0, nullptr);
            AppendMenuA(m, MF_STRING, ID_TRAY_GITHUB, L->menuGithub);
            AppendMenuA(m, MF_SEPARATOR, 0, nullptr);
            AppendMenuA(m, MF_STRING, ID_TRAY_EXIT, L->menuExit);

            POINT pt;  GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
            PostMessageA(hwnd, WM_NULL, 0, 0);
            DestroyMenu(m);   // also destroys sub
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_TRAY_FOLDERS:
            ShowListDialog(L->dlgFolders,   &g_cfg.folders,   1); break;
        case ID_TRAY_WHITELIST:
            ShowListDialog(L->dlgWhitelist, &g_cfg.whitelist, 0); break;
        case ID_TRAY_BLACKLIST:
            ShowListDialog(L->dlgBlacklist, &g_cfg.blacklist, 0); break;
        case ID_TRAY_EXCLUDE:
            ShowListDialog(L->dlgExclude,   &g_cfg.exclude,   2); break;
        case ID_TRAY_STARTUP:
            SetStartup(!IsInStartup());  break;
        case ID_KTC_OFF:          { EnterCriticalSection(&g_cfgLock); g_cfg.ktcLocalDimming=0;    LeaveCriticalSection(&g_cfgLock); SaveConfig(); } break;
        case ID_KTC_AUTO:         { EnterCriticalSection(&g_cfgLock); g_cfg.ktcLocalDimming=1;    LeaveCriticalSection(&g_cfgLock); SaveConfig(); } break;
        case ID_KTC_LOW:          { EnterCriticalSection(&g_cfgLock); g_cfg.ktcLocalDimming=2;    LeaveCriticalSection(&g_cfgLock); SaveConfig(); } break;
        case ID_KTC_STANDARD:     { EnterCriticalSection(&g_cfgLock); g_cfg.ktcLocalDimming=3;    LeaveCriticalSection(&g_cfgLock); SaveConfig(); } break;
        case ID_KTC_HIGH:         { EnterCriticalSection(&g_cfgLock); g_cfg.ktcLocalDimming=4;    LeaveCriticalSection(&g_cfgLock); SaveConfig(); } break;
        case ID_KTC_SDR_OFF:      { EnterCriticalSection(&g_cfgLock); g_cfg.ktcSdrLocalDimming=0; LeaveCriticalSection(&g_cfgLock); SaveConfig(); } break;
        case ID_KTC_SDR_AUTO:     { EnterCriticalSection(&g_cfgLock); g_cfg.ktcSdrLocalDimming=1; LeaveCriticalSection(&g_cfgLock); SaveConfig(); } break;
        case ID_KTC_SDR_LOW:      { EnterCriticalSection(&g_cfgLock); g_cfg.ktcSdrLocalDimming=2; LeaveCriticalSection(&g_cfgLock); SaveConfig(); } break;
        case ID_KTC_SDR_STANDARD: { EnterCriticalSection(&g_cfgLock); g_cfg.ktcSdrLocalDimming=3; LeaveCriticalSection(&g_cfgLock); SaveConfig(); } break;
        case ID_KTC_SDR_HIGH:     { EnterCriticalSection(&g_cfgLock); g_cfg.ktcSdrLocalDimming=4; LeaveCriticalSection(&g_cfgLock); SaveConfig(); } break;
        case ID_TRAY_GITHUB:
            ShellExecuteA(nullptr, "open", "https://github.com/conecta6/HDRAutostart-W11", nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case ID_TRAY_EXIT:
            KillTimer(hwnd, TIMER_BROWSER);
            SetEvent(g_stopEvent);
            DestroyWindow(hwnd);
            break;
        }
        return 0;

    case WM_DESTROY:
        Shell_NotifyIconA(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

// =============================================================================
// WinMain
// =============================================================================
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    // DPI awareness — prevents OS scaling (blurry menus/UI)
    {
        typedef BOOL(WINAPI* PFN)(void*);
        HMODULE u = GetModuleHandleA("user32.dll");
        PFN fn = u ? (PFN)GetProcAddress(u, "SetProcessDpiAwarenessContext") : nullptr;
        if (fn) fn((void*)(LONG_PTR)-4);  // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
        else    SetProcessDPIAware();     // fallback (Vista+)
    }

    HANDLE mutex = CreateMutexA(nullptr, TRUE, "HDRAutostart_Singleton_v2");
    if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(mutex); return 0; }

    if (!IsElevated()) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        RelaunchElevated();
        return 0;
    }

    CoInitialize(nullptr);
    DetectLang();
    InitializeCriticalSection(&g_cfgLock);
    LoadConfig();
    OpenLog();
    Log("=== HDRAutostart started ===");

    WM_TASKBARCREATED = RegisterWindowMessageA("TaskbarCreated");

    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(wc);  wc.hInstance = hInst;
    wc.lpszClassName = "HDRAutostartTray";  wc.lpfnWndProc = TrayWndProc;
    RegisterClassExA(&wc);

    WNDCLASSEXA wc2 = {};
    wc2.cbSize = sizeof(wc2);  wc2.hInstance = hInst;
    wc2.lpszClassName = "HDRAutostartListDlg";  wc2.lpfnWndProc = ListDlgProc;
    wc2.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassExA(&wc2);

    g_trayWnd = CreateWindowExA(0, "HDRAutostartTray", "HDRAutostart", 0,
        0, 0, 0, 0, HWND_MESSAGE, nullptr, hInst, nullptr);

    g_icoOff = CreateHDRIcon(false);
    g_icoOn  = CreateHDRIcon(true);

    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = g_trayWnd;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon            = g_icoOff;
    snprintf(g_nid.szTip, sizeof(g_nid.szTip), "%s  v" APP_VERSION, L->tipOff);
    if (!Shell_NotifyIconA(NIM_ADD, &g_nid))
        SetTimer(g_trayWnd, TIMER_TRAY_RETRY, 1000, nullptr);  // shell not ready yet

    SetTimer(g_trayWnd, TIMER_BROWSER, 500, nullptr);

    g_stopEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    HANDLE hThread = CreateThread(nullptr, 0, MonitorThread,     nullptr, 0, nullptr);
    CloseHandle(CreateThread(           nullptr, 0, UpdateCheckThread, nullptr, 0, nullptr));

    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    SetEvent(g_stopEvent);
    WaitForSingleObject(hThread, 5000);
    CloseHandle(hThread);
    CloseHandle(g_stopEvent);

    if (g_icoOff) DestroyIcon(g_icoOff);
    if (g_icoOn)  DestroyIcon(g_icoOn);
    DeleteCriticalSection(&g_cfgLock);
    if (g_log) fclose(g_log);
    CoUninitialize();
    CloseHandle(mutex);
    return 0;
}
