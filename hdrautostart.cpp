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
    BOOL WINAPI GetVCPFeatureAndVCPFeatureReply(HANDLE, BYTE, LPDWORD, LPDWORD, LPDWORD);
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

#define APP_VERSION "0.20"

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
    const char *menuProfiles;
    const char *dlgProfiles;
    const char *menuSharpness;
    const char *profDimLabel, *profSharpLabel, *profExeLabel;
    const char *profDimDefault;
    const char *menuKTCSettings;
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
    "GitHub",
    "Perfiles de juego...", "Perfiles de juego", "Nitidez (KTC)",
    "Local Dimming:", "Nitidez (0-10):", "Ejecutable:", "Usar valor global",
    "Configuraci\xf3n KTC"
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
    "GitHub",
    "Game profiles...", "Game profiles", "Sharpness (KTC)",
    "Local Dimming:", "Sharpness (0-10):", "Executable:", "Global default",
    "KTC Settings"
};
static const Lang* L = &kEN;

static void DetectLang()
{
    if (PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_SPANISH) L = &kES;
}

// =============================================================================
// Config  (hdrautostart.ini next to exe)
// =============================================================================
struct GameProfile {
    std::string exe;   // full path, stored lowercase
    int localDimming;  // -1 = use global, 0 = off, 1-4 = override
    int sharpness;     // -1 = use global, 0-10 = override
};

struct Config {
    std::vector<std::string> folders;    // paths that trigger HDR (prefix match)
    std::vector<std::string> whitelist;  // specific exe paths that always trigger
    std::vector<std::string> blacklist;  // specific exe paths that never trigger HDR but may trigger KTC dimming
    std::vector<std::string> exclude;    // completely ignored — no HDR, no KTC dimming
    int    ktcLocalDimming    = 0;  // 0=Off(default) 1=Auto 2=Low 3=Std 4=High  (juegos HDR)
    int    ktcSdrLocalDimming = 0;  // 0=Off(default) 1=Auto 2=Low 3=Std 4=High  (juegos blacklist/SDR)
    int    ktcSharpnessHdr    = 6;  // -1=off, 0-10 (VCP 0x57)
    int    ktcSharpnessSdr    = 6;  // -1=off, 0-10 (VCP 0x57)
    time_t lastUpdateAttempt  = 0;  // unix timestamp of last auto-update trigger (anti-loop)
    std::vector<GameProfile> profiles;
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
    fprintf(f, "ktc_sharpness_hdr=%d\n", g_cfg.ktcSharpnessHdr);
    fprintf(f, "ktc_sharpness_sdr=%d\n", g_cfg.ktcSharpnessSdr);
    if (g_cfg.lastUpdateAttempt)
        fprintf(f, "last_update_attempt=%lld\n", (long long)g_cfg.lastUpdateAttempt);
    fprintf(f, "[folders]\n");
    for (auto& s : g_cfg.folders)   fprintf(f, "%s\n", s.c_str());
    fprintf(f, "[whitelist]\n");
    for (auto& s : g_cfg.whitelist) fprintf(f, "%s\n", s.c_str());
    fprintf(f, "[blacklist]\n");
    for (auto& s : g_cfg.blacklist) fprintf(f, "%s\n", s.c_str());
    fprintf(f, "[exclude]\n");
    for (auto& s : g_cfg.exclude)   fprintf(f, "%s\n", s.c_str());
    fprintf(f, "[profiles]\n");
    for (auto& p : g_cfg.profiles)
        fprintf(f, "%s|%d|%d\n", p.exe.c_str(), p.localDimming, p.sharpness);
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

    enum Section { SEC_NONE, SEC_SETTINGS, SEC_FOLDERS, SEC_WHITELIST, SEC_BLACKLIST, SEC_EXCLUDE, SEC_PROFILES };
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
        if (!strcmp(line, "[profiles]"))  { sec = SEC_PROFILES;  continue; }
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
            if (strncmp(line, "ktc_sharpness_hdr=", 18) == 0) {
                int v = atoi(line + 18);
                if (v > 10 && v <= 100 && (v % 10) == 0) v /= 10;  // migrate old 0-100 configs
                if (v >= -1 && v <= 10) g_cfg.ktcSharpnessHdr = v;
            }
            if (strncmp(line, "ktc_sharpness_sdr=", 18) == 0) {
                int v = atoi(line + 18);
                if (v > 10 && v <= 100 && (v % 10) == 0) v /= 10;  // migrate old 0-100 configs
                if (v >= -1 && v <= 10) g_cfg.ktcSharpnessSdr = v;
            }
            if (strncmp(line, "last_update_attempt=", 20) == 0)
                g_cfg.lastUpdateAttempt = (time_t)atoll(line + 20);
            break;
        case SEC_FOLDERS:   g_cfg.folders.push_back(line);   break;
        case SEC_WHITELIST: g_cfg.whitelist.push_back(line); break;
        case SEC_BLACKLIST: g_cfg.blacklist.push_back(line); break;
        case SEC_EXCLUDE:   g_cfg.exclude.push_back(line);   break;
        case SEC_PROFILES: {
            // format: path|dimming|sharpness
            char* p1 = strchr(line, '|');
            if (p1) {
                char* p2 = strchr(p1 + 1, '|');
                if (p2) {
                    GameProfile gp;
                    gp.exe = std::string(line, p1 - line);
                    gp.localDimming = atoi(p1 + 1);
                    gp.sharpness    = atoi(p2 + 1);
                    if (gp.sharpness > 10 && gp.sharpness <= 100 && (gp.sharpness % 10) == 0)
                        gp.sharpness /= 10;  // migrate old 0-100 profile values
                    g_cfg.profiles.push_back(gp);
                }
            }
            break;
        }
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
// NVAPI raw DDC/CI helpers (used for KTC sharpness VCP 0x57 on NVIDIA)
// =============================================================================
typedef unsigned char NvU8;
typedef unsigned int  NvU32;
typedef int           NvAPI_Status;

struct NvPhysicalGpuHandle__ { int unused; };
struct NvDisplayHandle__     { int unused; };
typedef NvPhysicalGpuHandle__* NvPhysicalGpuHandle;
typedef NvDisplayHandle__*     NvDisplayHandle;

#define NVAPI_MAX_PHYSICAL_GPUS      64
#define NVAPI_OK                     0
#define NVAPI_I2C_SPEED_DEPRECATED   0xFFFF
#define MAKE_NVAPI_VERSION(typeName, ver) ((NvU32)(sizeof(typeName) | ((ver) << 16)))

typedef enum {
    NVAPI_I2C_SPEED_DEFAULT,
    NVAPI_I2C_SPEED_3KHZ,
    NVAPI_I2C_SPEED_10KHZ,
    NVAPI_I2C_SPEED_33KHZ,
    NVAPI_I2C_SPEED_100KHZ,
    NVAPI_I2C_SPEED_200KHZ,
    NVAPI_I2C_SPEED_400KHZ,
} NV_I2C_SPEED;

#pragma pack(push, 8)
typedef struct {
    NvU32        version;
    NvU32        displayMask;
    NvU8         bIsDDCPort;
    NvU8         i2cDevAddress;
    NvU8*        pbI2cRegAddress;
    NvU32        regAddrSize;
    NvU8*        pbData;
    NvU32        cbSize;
    NvU32        i2cSpeed;
    NV_I2C_SPEED i2cSpeedKhz;
    NvU8         portId;
    NvU32        bIsPortIdSet;
} NV_I2C_INFO_V3;
#pragma pack(pop)

typedef NV_I2C_INFO_V3 NV_I2C_INFO;
#define NV_I2C_INFO_VER3 MAKE_NVAPI_VERSION(NV_I2C_INFO_V3, 3)

typedef void* (__cdecl *NvAPI_QueryInterface_t)(NvU32);
typedef NvAPI_Status (__cdecl *NvAPI_Initialize_t)();
typedef NvAPI_Status (__cdecl *NvAPI_Unload_t)();
typedef NvAPI_Status (__cdecl *NvAPI_EnumPhysicalGPUs_t)(NvPhysicalGpuHandle[NVAPI_MAX_PHYSICAL_GPUS], NvU32*);
typedef NvAPI_Status (__cdecl *NvAPI_GetAssociatedNvidiaDisplayHandle_t)(const char*, NvDisplayHandle*);
typedef NvAPI_Status (__cdecl *NvAPI_GetAssociatedDisplayOutputId_t)(NvDisplayHandle, NvU32*);
typedef NvAPI_Status (__cdecl *NvAPI_I2CWrite_t)(NvPhysicalGpuHandle, NV_I2C_INFO*);

static HMODULE                                  g_nvapiDll = nullptr;
static bool                                     g_nvapiInitTried = false;
static bool                                     g_nvapiReady = false;
static NvPhysicalGpuHandle                      g_nvapiGpus[NVAPI_MAX_PHYSICAL_GPUS] = {};
static NvU32                                    g_nvapiGpuCount = 0;
static NvAPI_Unload_t                           g_nvapiUnload = nullptr;
static NvAPI_GetAssociatedNvidiaDisplayHandle_t g_nvapiGetDisplayHandle = nullptr;
static NvAPI_GetAssociatedDisplayOutputId_t     g_nvapiGetOutputId = nullptr;
static NvAPI_I2CWrite_t                         g_nvapiI2CWrite = nullptr;

static bool SetSharpnessViaControlMyMonitor(int level);

static BOOL CALLBACK CollectActiveDisplayNamesProc(HMONITOR hmon, HDC, LPRECT, LPARAM lParam)
{
    auto* names = reinterpret_cast<std::vector<std::string>*>(lParam);
    MONITORINFOEXA mi = {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoA(hmon, (MONITORINFO*)&mi)) return TRUE;
    std::string name = mi.szDevice;
    if (std::find(names->begin(), names->end(), name) == names->end())
        names->push_back(name);
    return TRUE;
}

static bool InitNVAPI()
{
    if (g_nvapiInitTried) return g_nvapiReady;
    g_nvapiInitTried = true;

    g_nvapiDll = LoadLibraryA(sizeof(void*) == 8 ? "nvapi64.dll" : "nvapi.dll");
    if (!g_nvapiDll) {
        Log("NVAPI: library not available");
        return false;
    }

    auto query = reinterpret_cast<NvAPI_QueryInterface_t>(
        GetProcAddress(g_nvapiDll, "nvapi_QueryInterface"));
    if (!query) {
        Log("NVAPI: nvapi_QueryInterface not found");
        FreeLibrary(g_nvapiDll);
        g_nvapiDll = nullptr;
        return false;
    }

    auto nvapiInitialize = reinterpret_cast<NvAPI_Initialize_t>(query(0x0150e828));
    g_nvapiUnload = reinterpret_cast<NvAPI_Unload_t>(query(0xd22bdd7e));
    auto nvapiEnumPhysicalGPUs = reinterpret_cast<NvAPI_EnumPhysicalGPUs_t>(query(0xe5ac921f));
    g_nvapiI2CWrite = reinterpret_cast<NvAPI_I2CWrite_t>(query(0xe812eb07));
    g_nvapiGetDisplayHandle =
        reinterpret_cast<NvAPI_GetAssociatedNvidiaDisplayHandle_t>(query(0x35c29134));
    g_nvapiGetOutputId =
        reinterpret_cast<NvAPI_GetAssociatedDisplayOutputId_t>(query(0xd995937e));

    if (!nvapiInitialize || !g_nvapiUnload || !nvapiEnumPhysicalGPUs ||
        !g_nvapiI2CWrite || !g_nvapiGetDisplayHandle || !g_nvapiGetOutputId) {
        Log("NVAPI: required entry points missing");
        FreeLibrary(g_nvapiDll);
        g_nvapiDll = nullptr;
        g_nvapiUnload = nullptr;
        g_nvapiI2CWrite = nullptr;
        g_nvapiGetDisplayHandle = nullptr;
        g_nvapiGetOutputId = nullptr;
        return false;
    }

    NvAPI_Status st = nvapiInitialize();
    if (st != NVAPI_OK) {
        Log("NVAPI: initialize failed status=%d", st);
        FreeLibrary(g_nvapiDll);
        g_nvapiDll = nullptr;
        g_nvapiUnload = nullptr;
        g_nvapiI2CWrite = nullptr;
        g_nvapiGetDisplayHandle = nullptr;
        g_nvapiGetOutputId = nullptr;
        return false;
    }

    st = nvapiEnumPhysicalGPUs(g_nvapiGpus, &g_nvapiGpuCount);
    if (st != NVAPI_OK || g_nvapiGpuCount == 0) {
        Log("NVAPI: EnumPhysicalGPUs failed status=%d count=%lu", st, (unsigned long)g_nvapiGpuCount);
        g_nvapiUnload();
        FreeLibrary(g_nvapiDll);
        g_nvapiDll = nullptr;
        g_nvapiUnload = nullptr;
        g_nvapiI2CWrite = nullptr;
        g_nvapiGetDisplayHandle = nullptr;
        g_nvapiGetOutputId = nullptr;
        g_nvapiGpuCount = 0;
        return false;
    }

    g_nvapiReady = true;
    Log("NVAPI: ready gpus=%lu", (unsigned long)g_nvapiGpuCount);
    return true;
}

static void ShutdownNVAPI()
{
    if (g_nvapiReady && g_nvapiUnload) {
        NvAPI_Status st = g_nvapiUnload();
        Log("NVAPI: unload status=%d", st);
    }
    g_nvapiReady = false;
    g_nvapiGpuCount = 0;
    g_nvapiUnload = nullptr;
    g_nvapiI2CWrite = nullptr;
    g_nvapiGetDisplayHandle = nullptr;
    g_nvapiGetOutputId = nullptr;
    if (g_nvapiDll) {
        FreeLibrary(g_nvapiDll);
        g_nvapiDll = nullptr;
    }
}

static bool SetNVAPIVCP(BYTE vcp, DWORD value)
{
    if (!InitNVAPI()) return false;

    std::vector<std::string> displayNames;
    EnumDisplayMonitors(nullptr, nullptr, CollectActiveDisplayNamesProc, (LPARAM)&displayNames);
    if (displayNames.empty()) {
        Log("NVAPI: no active displays found");
        return false;
    }

    NvU8 payload[7] = {
        0x51,
        0x84,
        0x03,
        vcp,
        (NvU8)((value >> 8) & 0xFF),
        (NvU8)(value & 0xFF),
        0x00
    };
    NvU8 checksum = 0x6E;
    for (size_t i = 0; i < sizeof(payload) - 1; ++i) checksum ^= payload[i];
    payload[sizeof(payload) - 1] = checksum;

    bool anyAttempt = false;
    bool anySuccess = false;

    for (const auto& displayName : displayNames) {
        NvDisplayHandle nvDisplay = nullptr;
        NvU32 outputId = 0;

        NvAPI_Status st = g_nvapiGetDisplayHandle(displayName.c_str(), &nvDisplay);
        if (st != NVAPI_OK || !nvDisplay) {
            Log("  NVAPI [%s]: no display handle status=%d", displayName.c_str(), st);
            continue;
        }

        st = g_nvapiGetOutputId(nvDisplay, &outputId);
        if (st != NVAPI_OK || outputId == 0) {
            Log("  NVAPI [%s]: no output id status=%d output=0x%08lX",
                displayName.c_str(), st, (unsigned long)outputId);
            continue;
        }

        for (NvU32 i = 0; i < g_nvapiGpuCount; ++i) {
            NV_I2C_INFO info = {};
            info.version         = NV_I2C_INFO_VER3;
            info.displayMask     = outputId;
            info.bIsDDCPort      = 1;
            info.i2cDevAddress   = 0x6E;
            info.pbI2cRegAddress = nullptr;
            info.regAddrSize     = 0;
            info.pbData          = payload;
            info.cbSize          = (NvU32)sizeof(payload);
            info.i2cSpeed        = NVAPI_I2C_SPEED_DEPRECATED;
            info.i2cSpeedKhz     = NVAPI_I2C_SPEED_DEFAULT;
            info.portId          = 0;
            info.bIsPortIdSet    = 0;

            anyAttempt = true;
            st = g_nvapiI2CWrite(g_nvapiGpus[i], &info);
            Log("  NVAPI [%s gpu=%lu]: VCP 0x%02X=%lu mask=0x%08lX status=%d",
                displayName.c_str(), (unsigned long)i, (unsigned)vcp, (unsigned long)value,
                (unsigned long)outputId, st);
            if (st == NVAPI_OK) anySuccess = true;
        }
    }

    if (!anyAttempt) Log("NVAPI: no writable display path for VCP 0x%02X", (unsigned)vcp);
    return anySuccess;
}
// =============================================================================
// KTC DDC/CI VCP helpers
// =============================================================================
// Generic DDC/CI VCP setter — lParam = (vcp << 16) | value
static BOOL CALLBACK KTCSetVCPProc(HMONITOR hmon, HDC, LPRECT, LPARAM lParam)
{
    BYTE  vcp  = (BYTE)((DWORD_PTR)lParam >> 16);
    DWORD val  = (DWORD)((DWORD_PTR)lParam & 0xFFFF);
    DWORD count = 0;
    if (!GetNumberOfPhysicalMonitorsFromHMONITOR(hmon, &count) || count == 0) {
        Log("  KTC DDC: no physical monitors for HMONITOR");
        return TRUE;
    }
    std::vector<PHYSICAL_MONITOR> mons(count);
    if (GetPhysicalMonitorsFromHMONITOR(hmon, count, mons.data())) {
        for (DWORD i = 0; i < count; i++) {
            char desc[256] = {};
            WideCharToMultiByte(CP_UTF8, 0, mons[i].szPhysicalMonitorDescription, -1,
                                desc, sizeof(desc) - 1, nullptr, nullptr);
            BOOL ok = SetVCPFeature(mons[i].hPhysicalMonitor, vcp, val);
            DWORD vcpType = 0, curVal = 0, maxVal = 0;
            BOOL readOk = GetVCPFeatureAndVCPFeatureReply(
                mons[i].hPhysicalMonitor, vcp, &vcpType, &curVal, &maxVal);
            Log("  KTC DDC [%s]: VCP 0x%02X=%lu set=%s readback=%s cur=%lu max=%lu",
                desc, (unsigned)vcp, val,
                ok ? "OK" : "FAIL",
                readOk ? "OK" : "FAIL",
                curVal, maxVal);
        }
        DestroyPhysicalMonitors(count, mons.data());
    }
    return TRUE;
}
static void SetKTCVCP(BYTE vcp, int value)
{
    if (value < 0) return;
    EnumDisplayMonitors(nullptr, nullptr, KTCSetVCPProc,
        (LPARAM)(((DWORD)vcp << 16) | (DWORD)value));
}
static void SetKTCLocalDimming(int level)
{
    if (level == 0) return;
    Log("KTC LocalDimming -> %d (1=Auto,2=Low,3=Std,4=High)", level);
    SetKTCVCP(0xF4, level);
}
static void SetKTCSharpness(int level)
{
    if (level < 0) return;
    Log("KTC Sharpness -> %d", level);
    if (SetSharpnessViaControlMyMonitor(level)) return;
    Log("KTC Sharpness: ControlMyMonitor path failed, trying NVAPI");
    if (SetNVAPIVCP(0x57, (DWORD)level)) return;
    Log("KTC Sharpness: NVAPI path failed, falling back to DXVA2 VCP");
    SetKTCVCP(0x57, level);
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

static bool RunSilentEx(const char* cmd, DWORD timeoutMs, DWORD* exitCode = nullptr)
{
    STARTUPINFOA si = {};  si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    char buf[1024];  strncpy_s(buf, cmd, _TRUNCATE);
    if (!CreateProcessA(nullptr, buf, nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        return false;
    }

    bool ok = (WaitForSingleObject(pi.hProcess, timeoutMs) == WAIT_OBJECT_0);
    DWORD code = STILL_ACTIVE;
    GetExitCodeProcess(pi.hProcess, &code);
    if (exitCode) *exitCode = code;
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return ok;
}

static std::wstring ToWideACP(const char* s)
{
    if (!s) return std::wstring();
    int n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    if (n <= 0) return std::wstring();
    std::vector<wchar_t> buf((size_t)n, L'\0');
    MultiByteToWideChar(CP_ACP, 0, s, -1, buf.data(), n);
    return std::wstring(buf.data());
}

static bool RunAsShellUser(const char* cmd, DWORD timeoutMs, DWORD* exitCode = nullptr)
{
    HWND shell = GetShellWindow();
    if (!shell) return false;

    DWORD shellPid = 0;
    GetWindowThreadProcessId(shell, &shellPid);
    if (!shellPid) return false;

    HANDLE hShell = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, shellPid);
    if (!hShell) return false;

    HANDLE hToken = nullptr;
    bool ok = false;
    if (OpenProcessToken(hShell, TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY | TOKEN_QUERY, &hToken)) {
        HANDLE hDup = nullptr;
        if (DuplicateTokenEx(hToken, TOKEN_ALL_ACCESS, nullptr, SecurityImpersonation,
                             TokenPrimary, &hDup)) {
            STARTUPINFOW si = {};  si.cb = sizeof(si);
            PROCESS_INFORMATION pi = {};
            std::wstring wcmd = ToWideACP(cmd);
            std::vector<wchar_t> cmdBuf(wcmd.begin(), wcmd.end());
            cmdBuf.push_back(L'\0');

            if (CreateProcessWithTokenW(hDup, LOGON_WITH_PROFILE, nullptr, cmdBuf.data(),
                                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                ok = (WaitForSingleObject(pi.hProcess, timeoutMs) == WAIT_OBJECT_0);
                DWORD code = STILL_ACTIVE;
                GetExitCodeProcess(pi.hProcess, &code);
                if (exitCode) *exitCode = code;
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            }
            CloseHandle(hDup);
        }
        CloseHandle(hToken);
    }
    CloseHandle(hShell);
    return ok;
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

static bool FileExists(const std::string& path)
{
    DWORD attrs = GetFileAttributesA(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static std::string g_controlMyMonitorPath;
static bool        g_controlMyMonitorSearched = false;

static std::string FindControlMyMonitorPath()
{
    if (g_controlMyMonitorSearched) return g_controlMyMonitorPath;
    g_controlMyMonitorSearched = true;

    std::vector<std::string> candidates = {
        ExeDir() + "ControlMyMonitor.exe",
        ConfigDir() + "ControlMyMonitor.exe"
    };

    char profile[MAX_PATH] = {};
    if (SHGetFolderPathA(nullptr, CSIDL_PROFILE, nullptr, SHGFP_TYPE_CURRENT, profile) == S_OK) {
        std::string downloads = std::string(profile) + "\\Downloads\\";
        candidates.push_back(downloads + "ControlMyMonitor.exe");

        WIN32_FIND_DATAA fd = {};
        HANDLE hFind = FindFirstFileA((downloads + "controlmymonitor-*").c_str(), &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
                std::string p = downloads + fd.cFileName + "\\ControlMyMonitor.exe";
                candidates.push_back(p);
            } while (FindNextFileA(hFind, &fd));
            FindClose(hFind);
        }
    }

    for (const auto& path : candidates) {
        if (FileExists(path)) {
            g_controlMyMonitorPath = path;
            Log("ControlMyMonitor: found at %s", path.c_str());
            break;
        }
    }

    if (g_controlMyMonitorPath.empty())
        Log("ControlMyMonitor: not found");
    return g_controlMyMonitorPath;
}

static bool SetSharpnessViaControlMyMonitor(int level)
{
    std::string path = FindControlMyMonitorPath();
    if (path.empty()) return false;

    std::vector<std::string> displayNames;
    EnumDisplayMonitors(nullptr, nullptr, CollectActiveDisplayNamesProc, (LPARAM)&displayNames);
    if (displayNames.empty()) {
        Log("  ControlMyMonitor: no active display names found");
        return false;
    }

    for (const auto& displayName : displayNames) {
        const std::string target = displayName + "\\Monitor0";
        bool targetOk = false;
        for (int pass = 0; pass < 3; ++pass) {
            char cmd[1200] = {};
            // ControlMyMonitor CLI expects the decimal VCP code here.
            // Sharpness is 0x57, which is 87 in decimal.
            snprintf(cmd, sizeof(cmd), "\"%s\" /SetValue \"%s\" 87 %d",
                path.c_str(), target.c_str(), level);

            DWORD exitCode = STILL_ACTIVE;
            bool finished = RunAsShellUser(cmd, 5000, &exitCode);
            Log("  ControlMyMonitor: target=%s level=%d pass=%d finished=%s exit=%lu",
                target.c_str(), level, pass + 1, finished ? "yes" : "no", (unsigned long)exitCode);
            if (!(finished && exitCode == 0)) {
                targetOk = false;
                break;
            }

            targetOk = true;
            if (pass == 0) Sleep(150);
            else if (pass == 1) Sleep(350);
        }
        if (targetOk) return true;
    }

    return false;
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
    std::map<DWORD, GameProfile> activeProfiles;  // pid -> profile used
    bool hdrActive        = false;
    bool sdrDimmingActive = false;
    bool dimSentForHdr    = false;  // any dimming command sent this HDR session
    bool sharpSentForHdr  = false;  // any sharpness command sent this HDR session

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
                activeProfiles.erase(it->first);
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
            int sDim, sSharp;
            EnterCriticalSection(&g_cfgLock);
            sDim   = g_cfg.ktcSdrLocalDimming;
            sSharp = g_cfg.ktcSharpnessSdr;
            g_hdrSource[0] = '\0';
            LeaveCriticalSection(&g_cfgLock);
            // Restore dimming if any was sent this session (from global OR from a profile)
            if (dimSentForHdr) {
                if (!sdrGames.empty() && sDim > 0) {
                    SetKTCLocalDimming(sDim);
                    sdrDimmingActive = true;
                } else {
                    SetKTCLocalDimming(1);  // reset to Auto
                }
                dimSentForHdr = false;
            }
            if (sharpSentForHdr) {
                SetKTCSharpness(sSharp);
                sharpSentForHdr = false;
            }
            hdrActive = false;
            if (g_trayWnd) PostMessage(g_trayWnd, WM_HDRSTATUS, 0, 0);
        }

        // --- All SDR games closed ---
        if (sdrGames.empty() && sdrDimmingActive && !hdrActive) {
            int sDim, sSharp;
            EnterCriticalSection(&g_cfgLock);
            sDim   = g_cfg.ktcSdrLocalDimming;
            sSharp = g_cfg.ktcSharpnessSdr;
            LeaveCriticalSection(&g_cfgLock);
            if (sDim > 0) { Log("All SDR games closed — reset dimming"); SetKTCLocalDimming(1); SetKTCSharpness(sSharp); }
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
                        // Check for per-game profile
                        int profileDimming = -1, profileSharpness = -1;
                        std::string loPath = ToLower(path);
                        EnterCriticalSection(&g_cfgLock);
                        for (auto& prof : g_cfg.profiles) {
                            if (prof.exe == loPath) {
                                profileDimming   = prof.localDimming;
                                profileSharpness = prof.sharpness;
                                break;
                            }
                        }
                        int hdrDimming  = g_cfg.ktcLocalDimming;
                        int hdrSharpness = g_cfg.ktcSharpnessHdr;
                        strncpy_s(g_hdrSource, base ? base + 1 : path.c_str(), _TRUNCATE);
                        LeaveCriticalSection(&g_cfgLock);

                        int effectiveDimming   = (profileDimming  >= 0) ? profileDimming  : hdrDimming;
                        int effectiveSharpness = (profileSharpness >= 0) ? profileSharpness : hdrSharpness;

                        // Enable HDR
                        Log("Enabling HDR...");
                        bool ok = false;
                        for (int r = 0; r < 3 && !ok; r++) {
                            if (r) Sleep(300);
                            ok = SetHDR(true);
                        }
                        Log("HDR %s", ok ? "ENABLED" : "enable FAILED after retries");

                        // Local dimming after HDR (KTC proprietary VCP — survives mode switch)
                        if (effectiveDimming > 0) {
                            SetKTCLocalDimming(effectiveDimming);
                            dimSentForHdr = true;
                        }
                        // Sharpness: standard VCP 0x57 gets reset by HDR mode switch.
                        // Wait for monitor to stabilize, then send.
                        if (effectiveSharpness >= 0) {
                            Sleep(500);
                            SetKTCSharpness(effectiveSharpness);
                            sharpSentForHdr = true;
                        }

                        GameProfile usedProfile;
                        usedProfile.exe          = loPath;
                        usedProfile.localDimming = profileDimming;
                        usedProfile.sharpness    = profileSharpness;
                        activeProfiles[pid]      = usedProfile;

                        sdrDimmingActive = false;  // HDR takes precedence
                        hdrActive = true;
                        if (g_trayWnd) PostMessage(g_trayWnd, WM_HDRSTATUS, 1, 0);
                    }
                    games[pid] = hProc;

                } else if (cls == -1) {
                    // Blacklisted / SDR game
                    int sDim, sSharp;
                    EnterCriticalSection(&g_cfgLock);
                    sDim   = g_cfg.ktcSdrLocalDimming;
                    sSharp = g_cfg.ktcSharpnessSdr;
                    LeaveCriticalSection(&g_cfgLock);
                    if (sDim == 0) continue;  // SDR dimming disabled — ignore

                    Log("SDR game detected: %s (PID %lu)", base ? base + 1 : path.c_str(), pid);
                    HANDLE hProc = OpenProcess(
                        SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
                    if (!hProc) { Log("  (cannot open SDR process)"); continue; }

                    if (sdrGames.empty() && !hdrActive) {
                        Log("SDR dimming -> %d", sDim);
                        SetKTCLocalDimming(sDim);
                        SetKTCSharpness(sSharp);
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
        int sh; EnterCriticalSection(&g_cfgLock); sh=g_cfg.ktcSharpnessSdr; LeaveCriticalSection(&g_cfgLock);
        if (dimSentForHdr)   SetKTCLocalDimming(1);
        if (sharpSentForHdr) SetKTCSharpness(sh);
        if (g_trayWnd) PostMessage(g_trayWnd, WM_HDRSTATUS, 0, 0);
    }
    if (sdrDimmingActive) {
        int d, sh; EnterCriticalSection(&g_cfgLock); d=g_cfg.ktcSdrLocalDimming; sh=g_cfg.ktcSharpnessSdr; LeaveCriticalSection(&g_cfgLock);
        if (d > 0) { SetKTCLocalDimming(1); SetKTCSharpness(sh); }
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
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    DownloadArgs* a = (DownloadArgs*)p;
    Log("Downloading update from: %s", a->url);
    Log("Saving to: %s", a->path);
    HRESULT hr = URLDownloadToFileA(nullptr, a->url, a->path, 0, nullptr);
    Log("URLDownloadToFile result: 0x%08X", (unsigned)hr);
    if (SUCCEEDED(hr)) {
        Log("Download OK — removing Zone.Identifier and launching installer silently");
        // Remove the internet-zone mark so SmartScreen doesn't block silent execution
        std::string zoneId = std::string(a->path) + ":Zone.Identifier";
        DeleteFileA(zoneId.c_str());

        // Use CreateProcess — more reliable than ShellExecuteEx from an elevated process
        char cmdLine[MAX_PATH + 8];
        snprintf(cmdLine, sizeof(cmdLine), "\"%s\" /S", a->path);
        STARTUPINFOA si = {};
        si.cb          = sizeof(si);
        si.dwFlags     = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi = {};
        BOOL ok = CreateProcessA(nullptr, cmdLine, nullptr, nullptr,
                                 FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
        Log("CreateProcess result: %d (err=%lu) cmd=%s", ok, GetLastError(), cmdLine);
        if (ok) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
    } else {
        Log("Download FAILED — hr=0x%08X", (unsigned)hr);
    }
    delete a;
    CoUninitialize();
    return 0;
}

static DWORD WINAPI UpdateCheckThread(LPVOID)
{
    Sleep(8000);  // let the app settle before checking

    // Anti-loop: skip if an update was triggered less than 1 hour ago
    EnterCriticalSection(&g_cfgLock);
    time_t lastAttempt = g_cfg.lastUpdateAttempt;
    LeaveCriticalSection(&g_cfgLock);
    if (lastAttempt != 0 && (time(nullptr) - lastAttempt) < 3600) {
        Log("Update check: skipped (triggered %lld s ago)", (long long)(time(nullptr) - lastAttempt));
        return 0;
    }

    Log("Update check: starting");

    HINTERNET hSes = WinHttpOpen(L"HDRAutostart-Update/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSes) { Log("Update: WinHttpOpen failed err=%lu", GetLastError()); return 0; }

    HINTERNET hCon = WinHttpConnect(hSes, L"api.github.com",
        INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hCon) { Log("Update: WinHttpConnect failed err=%lu", GetLastError()); WinHttpCloseHandle(hSes); return 0; }

    HINTERNET hReq = WinHttpOpenRequest(hCon, L"GET",
        L"/repos/conecta6/HDRAutostart-W11/releases/latest",
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (!hReq) { Log("Update: WinHttpOpenRequest failed err=%lu", GetLastError()); WinHttpCloseHandle(hCon); WinHttpCloseHandle(hSes); return 0; }

    WinHttpAddRequestHeaders(hReq,
        L"Accept: application/vnd.github+json\r\n",
        (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD);

    if (!WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        Log("Update: WinHttpSendRequest failed err=%lu", GetLastError()); goto cleanup;
    }
    if (!WinHttpReceiveResponse(hReq, nullptr)) {
        Log("Update: WinHttpReceiveResponse failed err=%lu", GetLastError()); goto cleanup;
    }

    {
        // Check HTTP status code
        DWORD status = 0, statusSz = sizeof(status);
        WinHttpQueryHeaders(hReq,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSz, WINHTTP_NO_HEADER_INDEX);
        Log("Update: HTTP status %lu", status);
        if (status != 200) goto cleanup;

        std::string body;
        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
            std::string chunk(avail, '\0');
            DWORD got = 0;
            if (WinHttpReadData(hReq, &chunk[0], avail, &got))
                body.append(chunk, 0, got);
        }
        Log("Update: response body length=%zu", body.size());

        // Extract "tag_name":"..."
        const char* p = strstr(body.c_str(), "\"tag_name\"");
        if (!p) { Log("Update: tag_name not found in response"); goto cleanup; }
        p = strchr(p, ':');  if (!p) goto cleanup;
        p = strchr(p, '"');  if (!p) goto cleanup;
        ++p;
        const char* e = strchr(p, '"');  if (!e) goto cleanup;
        size_t tlen = (size_t)(e - p);
        if (tlen == 0 || tlen >= 64) goto cleanup;

        char tag[64] = {};
        memcpy(tag, p, tlen);

        Log("Update check: latest=%s current=" APP_VERSION, tag);
        if (!IsNewerVersion(tag)) { Log("Update: already up to date"); goto cleanup; }

        Log("Update: newer version found — preparing download");
        UpdateInfo* info = new UpdateInfo;
        strncpy_s(info->tag, tag, _TRUNCATE);
        snprintf(info->dlUrl, sizeof(info->dlUrl),
            "https://github.com/conecta6/HDRAutostart-W11/releases/download/%s/HDRAutostartSetup.exe",
            tag);

        if (g_trayWnd) PostMessageA(g_trayWnd, WM_UPDATE_AVAILABLE, 0, (LPARAM)info);
        else { Log("Update: g_trayWnd is null, cannot post message"); delete info; }
    }

cleanup:
    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hCon);
    WinHttpCloseHandle(hSes);
    Log("Update check: done");
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
#define ID_TRAY_PROFILES  207
#define ID_KTC_SHARP_HDR  320
#define ID_KTC_SHARP_SDR  321
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
        int dimming, sharpHdr;
        EnterCriticalSection(&g_cfgLock);
        dimming  = g_cfg.ktcLocalDimming;
        sharpHdr = g_cfg.ktcSharpnessHdr;
        LeaveCriticalSection(&g_cfgLock);
        SetHDR(true);
        // Local dimming after HDR (KTC proprietary VCP — survives mode switch)
        SetKTCLocalDimming(dimming);
        // Sharpness: standard VCP 0x57 gets reset by HDR mode switch; wait then send
        if (sharpHdr >= 0) { Sleep(500); SetKTCSharpness(sharpHdr); }
        strncpy_s(g_hdrSource, "Browser", _TRUNCATE);
        g_browserHdrOn = true;
        UpdateTray(true);
    } else if (!isFS && g_browserHdrOn) {
        Log("Browser left fullscreen — disabling HDR");
        SetHDR(false);
        { int d, sh; EnterCriticalSection(&g_cfgLock); d=g_cfg.ktcLocalDimming; sh=g_cfg.ktcSharpnessSdr; LeaveCriticalSection(&g_cfgLock); if(d>0) SetKTCLocalDimming(1); SetKTCSharpness(sh); }
        g_hdrSource[0] = '\0';
        g_browserHdrOn = false;
        UpdateTray(false);
    }
}

// =============================================================================
// Sharpness combobox dialog
// =============================================================================
#define IDC_SHARP_COMBO  400
#define IDC_SHARP_OK     401
#define IDC_SHARP_CANCEL 402

struct SharpDlgData {
    int*  value;
    HFONT hFont;
};

static LRESULT CALLBACK SharpDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    SharpDlgData* d = (SharpDlgData*)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        d = (SharpDlgData*)((CREATESTRUCTA*)lp)->lpCreateParams;
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)d);

        typedef UINT(WINAPI* PFN_GetDpiForWindow)(HWND);
        static auto pfnDpi = (PFN_GetDpiForWindow)GetProcAddress(GetModuleHandleA("user32.dll"), "GetDpiForWindow");
        UINT dpi = pfnDpi ? pfnDpi(hwnd) : 96;
        auto S = [&](int v){ return MulDiv(v, (int)dpi, 96); };

        NONCLIENTMETRICSA ncm = {}; ncm.cbSize = sizeof(ncm);
        SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        d->hFont = CreateFontIndirectA(&ncm.lfMessageFont);

        int gap = S(8), bw = S(80), bh = S(26), lw = S(110), cw = S(110);

        HWND hLbl = CreateWindowA("STATIC", "Sharpness:",
            WS_CHILD | WS_VISIBLE, gap, gap + S(4), lw, S(20), hwnd, nullptr, nullptr, nullptr);
        SendMessageA(hLbl, WM_SETFONT, (WPARAM)d->hFont, FALSE);

        HWND hCbo = CreateWindowExA(0, "COMBOBOX", nullptr,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            gap + lw + gap, gap, cw, S(220), hwnd, (HMENU)IDC_SHARP_COMBO, nullptr, nullptr);
        SendMessageA(hCbo, WM_SETFONT, (WPARAM)d->hFont, FALSE);

        // Off (-1), then 0, 1, 2 ... 10
        { int idx = (int)SendMessageA(hCbo, CB_ADDSTRING, 0, (LPARAM)"Off");
          SendMessageA(hCbo, CB_SETITEMDATA, idx, (LPARAM)(DWORD)-1); }
        int selIdx = (*d->value == -1) ? 0 : 0;
        for (int v = 0; v <= 10; v++) {
            char buf[8]; snprintf(buf, sizeof(buf), "%d", v);
            int idx = (int)SendMessageA(hCbo, CB_ADDSTRING, 0, (LPARAM)buf);
            SendMessageA(hCbo, CB_SETITEMDATA, idx, (LPARAM)(DWORD)v);
            if (v == *d->value) selIdx = idx;
        }
        SendMessageA(hCbo, CB_SETCURSEL, selIdx, 0);

        int y2 = gap + S(36);
        HWND hOk  = CreateWindowA("BUTTON", "OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            gap,              y2, bw, bh, hwnd, (HMENU)IDC_SHARP_OK,     nullptr, nullptr);
        HWND hCan = CreateWindowA("BUTTON", "Cancel", WS_CHILD | WS_VISIBLE,
            gap + bw + gap,   y2, bw, bh, hwnd, (HMENU)IDC_SHARP_CANCEL, nullptr, nullptr);
        SendMessageA(hOk,  WM_SETFONT, (WPARAM)d->hFont, FALSE);
        SendMessageA(hCan, WM_SETFONT, (WPARAM)d->hFont, FALSE);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_SHARP_OK) {
            HWND hCbo = GetDlgItem(hwnd, IDC_SHARP_COMBO);
            int sel = (int)SendMessageA(hCbo, CB_GETCURSEL, 0, 0);
            if (sel != CB_ERR) {
                *d->value = (int)(DWORD)SendMessageA(hCbo, CB_GETITEMDATA, sel, 0);
                SaveConfig();
            }
            DestroyWindow(hwnd);
        } else if (LOWORD(wp) == IDC_SHARP_CANCEL) {
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_CLOSE:   DestroyWindow(hwnd); return 0;
    case WM_DESTROY:
        if (d && d->hFont) DeleteObject(d->hFont);
        delete d;
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static void ShowSharpnessDialog(const char* title, int* value)
{
    SharpDlgData* data = new SharpDlgData{value, nullptr};
    HINSTANCE hInst = (HINSTANCE)GetModuleHandleA(nullptr);

    typedef UINT(WINAPI* PFN_GetDpiForSystem)();
    static auto pfnDpiSys = (PFN_GetDpiForSystem)GetProcAddress(GetModuleHandleA("user32.dll"), "GetDpiForSystem");
    UINT dpi = pfnDpiSys ? pfnDpiSys() : 96;
    int W = MulDiv(290, (int)dpi, 96);
    int H = MulDiv(110, (int)dpi, 96);

    HWND hw = CreateWindowExA(
        WS_EX_TOPMOST,
        "HDRAutostartSharpDlg", title,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, W, H,
        nullptr, nullptr, hInst, data);
    if (hw) SetForegroundWindow(hw);
    else    delete data;
}

// =============================================================================
// Per-profile edit dialog
// =============================================================================
#define IDC_PROF_DIM_COMBO   410
#define IDC_PROF_SHARP_COMBO 411
#define IDC_PROF_OK          412
#define IDC_PROF_CANCEL      413

struct ProfEditData {
    int  dimming;
    int  sharpness;
    bool ok;
    HFONT hFont;
};

static LRESULT CALLBACK ProfEditDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    ProfEditData* d = (ProfEditData*)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        d = (ProfEditData*)((CREATESTRUCTA*)lp)->lpCreateParams;
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)d);

        typedef UINT(WINAPI* PFN_GetDpiForWindow)(HWND);
        static auto pfnDpi = (PFN_GetDpiForWindow)GetProcAddress(GetModuleHandleA("user32.dll"), "GetDpiForWindow");
        UINT dpi = pfnDpi ? pfnDpi(hwnd) : 96;
        auto S = [&](int v){ return MulDiv(v, (int)dpi, 96); };

        NONCLIENTMETRICSA ncm = {}; ncm.cbSize = sizeof(ncm);
        SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        d->hFont = CreateFontIndirectA(&ncm.lfMessageFont);

        int gap = S(8), bw = S(80), bh = S(26), lw = S(120), cw = S(160);

        // Row 1 — Local Dimming
        HWND hL1 = CreateWindowA("STATIC", "Local Dimming:",
            WS_CHILD | WS_VISIBLE, gap, gap + S(4), lw, S(20), hwnd, nullptr, nullptr, nullptr);
        SendMessageA(hL1, WM_SETFONT, (WPARAM)d->hFont, FALSE);
        HWND hC1 = CreateWindowExA(0, "COMBOBOX", nullptr,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            gap + lw + gap, gap, cw, S(160), hwnd, (HMENU)IDC_PROF_DIM_COMBO, nullptr, nullptr);
        SendMessageA(hC1, WM_SETFONT, (WPARAM)d->hFont, FALSE);
        {
            struct { const char* label; int val; } dimItems[] = {
                {"Global default", -1}, {"Off", 0}, {"Auto", 1},
                {"Low", 2}, {"Standard", 3}, {"High", 4}
            };
            int selDim = 0;
            for (int i = 0; i < 6; i++) {
                int idx = (int)SendMessageA(hC1, CB_ADDSTRING, 0, (LPARAM)dimItems[i].label);
                SendMessageA(hC1, CB_SETITEMDATA, idx, (LPARAM)(DWORD)dimItems[i].val);
                if (dimItems[i].val == d->dimming) selDim = idx;
            }
            SendMessageA(hC1, CB_SETCURSEL, selDim, 0);
        }

        // Row 2 — Sharpness
        int row2 = gap + S(36);
        HWND hL2 = CreateWindowA("STATIC", "Sharpness:",
            WS_CHILD | WS_VISIBLE, gap, row2 + S(4), lw, S(20), hwnd, nullptr, nullptr, nullptr);
        SendMessageA(hL2, WM_SETFONT, (WPARAM)d->hFont, FALSE);
        HWND hC2 = CreateWindowExA(0, "COMBOBOX", nullptr,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            gap + lw + gap, row2, cw, S(220), hwnd, (HMENU)IDC_PROF_SHARP_COMBO, nullptr, nullptr);
        SendMessageA(hC2, WM_SETFONT, (WPARAM)d->hFont, FALSE);
        {
            int idxG = (int)SendMessageA(hC2, CB_ADDSTRING, 0, (LPARAM)"Global default");
            SendMessageA(hC2, CB_SETITEMDATA, idxG, (LPARAM)(DWORD)-1);
            int selSharp = (d->sharpness == -1) ? 0 : 0;
            for (int v = 0; v <= 10; v++) {
                char buf[8]; snprintf(buf, sizeof(buf), "%d", v);
                int idx = (int)SendMessageA(hC2, CB_ADDSTRING, 0, (LPARAM)buf);
                SendMessageA(hC2, CB_SETITEMDATA, idx, (LPARAM)(DWORD)v);
                if (v == d->sharpness) selSharp = idx;
            }
            SendMessageA(hC2, CB_SETCURSEL, selSharp, 0);
        }

        int y3 = row2 + S(36);
        HWND hOk  = CreateWindowA("BUTTON", "OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            gap,              y3, bw, bh, hwnd, (HMENU)IDC_PROF_OK,     nullptr, nullptr);
        HWND hCan = CreateWindowA("BUTTON", "Cancel", WS_CHILD | WS_VISIBLE,
            gap + bw + gap,   y3, bw, bh, hwnd, (HMENU)IDC_PROF_CANCEL, nullptr, nullptr);
        SendMessageA(hOk,  WM_SETFONT, (WPARAM)d->hFont, FALSE);
        SendMessageA(hCan, WM_SETFONT, (WPARAM)d->hFont, FALSE);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_PROF_OK) {
            HWND hC1 = GetDlgItem(hwnd, IDC_PROF_DIM_COMBO);
            HWND hC2 = GetDlgItem(hwnd, IDC_PROF_SHARP_COMBO);
            int s1 = (int)SendMessageA(hC1, CB_GETCURSEL, 0, 0);
            int s2 = (int)SendMessageA(hC2, CB_GETCURSEL, 0, 0);
            if (s1 != CB_ERR && s2 != CB_ERR) {
                d->dimming   = (int)(DWORD)SendMessageA(hC1, CB_GETITEMDATA, s1, 0);
                d->sharpness = (int)(DWORD)SendMessageA(hC2, CB_GETITEMDATA, s2, 0);
                d->ok        = true;
            }
            DestroyWindow(hwnd);
        } else if (LOWORD(wp) == IDC_PROF_CANCEL) {
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_CLOSE:   DestroyWindow(hwnd); return 0;
    case WM_DESTROY:
        if (d && d->hFont) DeleteObject(d->hFont);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

// Run ProfEditDlg modally (spin message loop until destroyed)
static bool RunProfEditDialog(HWND parent, int& dimming, int& sharpness)
{
    ProfEditData data;
    data.dimming   = dimming;
    data.sharpness = sharpness;
    data.ok        = false;
    data.hFont     = nullptr;

    HINSTANCE hInst = (HINSTANCE)GetModuleHandleA(nullptr);
    typedef UINT(WINAPI* PFN_GetDpiForSystem)();
    static auto pfnDpiSys = (PFN_GetDpiForSystem)GetProcAddress(GetModuleHandleA("user32.dll"), "GetDpiForSystem");
    UINT dpi = pfnDpiSys ? pfnDpiSys() : 96;
    int W = MulDiv(320, (int)dpi, 96);
    int H = MulDiv(155, (int)dpi, 96);

    HWND hw = CreateWindowExA(
        WS_EX_TOPMOST,
        "HDRAutostartProfEditDlg", "Profile settings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, W, H,
        parent, nullptr, hInst, &data);
    if (!hw) return false;
    SetForegroundWindow(hw);
    EnableWindow(parent, FALSE);
    MSG m;
    while (IsWindow(hw) && GetMessageA(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    if (data.ok) { dimming = data.dimming; sharpness = data.sharpness; }
    return data.ok;
}

// =============================================================================
// Game Profiles dialog
// =============================================================================
#define IDC_PROF_LIST   420
#define IDC_PROF_ADD    421
#define IDC_PROF_REMOVE 422
#define IDC_PROF_CLOSE  423
#define IDC_PROF_EDIT   424

struct ProfilesDlgData { HFONT hFont; };

static void PopulateProfilesList(HWND lb)
{
    SendMessageA(lb, LB_RESETCONTENT, 0, 0);
    EnterCriticalSection(&g_cfgLock);
    for (auto& p : g_cfg.profiles) {
        // Show basename for readability
        const char* base = strrchr(p.exe.c_str(), '\\');
        const char* name = base ? base + 1 : p.exe.c_str();
        char dim_str[16], sharp_str[16];
        if (p.localDimming < 0) snprintf(dim_str,   sizeof(dim_str),   "Global");
        else                     snprintf(dim_str,   sizeof(dim_str),   "%d", p.localDimming);
        if (p.sharpness < 0)    snprintf(sharp_str, sizeof(sharp_str), "Global");
        else                     snprintf(sharp_str, sizeof(sharp_str), "%d", p.sharpness);
        char entry[MAX_PATH + 64];
        snprintf(entry, sizeof(entry), "%s  [Dim: %s | Sharp: %s]", name, dim_str, sharp_str);
        SendMessageA(lb, LB_ADDSTRING, 0, (LPARAM)entry);
    }
    LeaveCriticalSection(&g_cfgLock);
}

static LRESULT CALLBACK ProfilesDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    ProfilesDlgData* d = (ProfilesDlgData*)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        d = (ProfilesDlgData*)((CREATESTRUCTA*)lp)->lpCreateParams;
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)d);

        typedef UINT(WINAPI* PFN_GetDpiForWindow)(HWND);
        static auto pfnDpi = (PFN_GetDpiForWindow)GetProcAddress(GetModuleHandleA("user32.dll"), "GetDpiForWindow");
        UINT dpi = pfnDpi ? pfnDpi(hwnd) : 96;
        auto S = [&](int v){ return MulDiv(v, (int)dpi, 96); };

        NONCLIENTMETRICSA ncm = {}; ncm.cbSize = sizeof(ncm);
        SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        d->hFont = CreateFontIndirectA(&ncm.lfMessageFont);

        RECT rc; GetClientRect(hwnd, &rc);
        int gap = S(8), bw = S(90), bh = S(28);
        int lbH = rc.bottom - bh - gap * 3;

        HWND lb = CreateWindowExA(WS_EX_CLIENTEDGE, "LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
            gap, gap, rc.right - gap * 2, lbH,
            hwnd, (HMENU)IDC_PROF_LIST, nullptr, nullptr);
        SendMessageA(lb, WM_SETFONT, (WPARAM)d->hFont, FALSE);

        int y = lbH + gap * 2;
        HWND bAdd  = CreateWindowA("BUTTON", L->btnAdd,    WS_CHILD | WS_VISIBLE,
            gap,                       y, bw, bh, hwnd, (HMENU)IDC_PROF_ADD,    nullptr, nullptr);
        HWND bEdit = CreateWindowA("BUTTON", L == &kES ? "Editar" : "Edit", WS_CHILD | WS_VISIBLE,
            gap + (bw + gap),          y, bw, bh, hwnd, (HMENU)IDC_PROF_EDIT,   nullptr, nullptr);
        HWND bDel  = CreateWindowA("BUTTON", L->btnRemove, WS_CHILD | WS_VISIBLE,
            gap + (bw + gap) * 2,      y, bw, bh, hwnd, (HMENU)IDC_PROF_REMOVE, nullptr, nullptr);
        HWND bCls  = CreateWindowA("BUTTON", L->btnClose,  WS_CHILD | WS_VISIBLE,
            rc.right - bw - gap,       y, bw, bh, hwnd, (HMENU)IDC_PROF_CLOSE,  nullptr, nullptr);
        SendMessageA(bAdd,  WM_SETFONT, (WPARAM)d->hFont, FALSE);
        SendMessageA(bEdit, WM_SETFONT, (WPARAM)d->hFont, FALSE);
        SendMessageA(bDel,  WM_SETFONT, (WPARAM)d->hFont, FALSE);
        SendMessageA(bCls,  WM_SETFONT, (WPARAM)d->hFont, FALSE);

        PopulateProfilesList(lb);
        return 0;
    }
    case WM_COMMAND: {
        HWND lb = GetDlgItem(hwnd, IDC_PROF_LIST);
        if (LOWORD(wp) == IDC_PROF_ADD) {
            // Pick an exe
            char path[MAX_PATH] = {};
            OPENFILENAMEA ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner   = hwnd;
            ofn.lpstrFilter = "Executables\0*.exe\0All Files\0*.*\0";
            ofn.lpstrFile   = path;
            ofn.nMaxFile    = MAX_PATH;
            ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameA(&ofn)) {
                int dimming = -1, sharpness = -1;
                if (RunProfEditDialog(hwnd, dimming, sharpness)) {
                    GameProfile gp;
                    gp.exe          = ToLower(std::string(path));
                    gp.localDimming = dimming;
                    gp.sharpness    = sharpness;
                    EnterCriticalSection(&g_cfgLock);
                    g_cfg.profiles.push_back(gp);
                    LeaveCriticalSection(&g_cfgLock);
                    SaveConfig();
                    PopulateProfilesList(lb);
                }
            }
        } else if (LOWORD(wp) == IDC_PROF_EDIT ||
                   (LOWORD(wp) == IDC_PROF_LIST && HIWORD(wp) == LBN_DBLCLK)) {
            int sel = (int)SendMessageA(lb, LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR) {
                EnterCriticalSection(&g_cfgLock);
                bool valid = sel < (int)g_cfg.profiles.size();
                int dimming  = valid ? g_cfg.profiles[sel].localDimming : -1;
                int sharpness = valid ? g_cfg.profiles[sel].sharpness   : -1;
                LeaveCriticalSection(&g_cfgLock);
                if (valid && RunProfEditDialog(hwnd, dimming, sharpness)) {
                    EnterCriticalSection(&g_cfgLock);
                    g_cfg.profiles[sel].localDimming = dimming;
                    g_cfg.profiles[sel].sharpness    = sharpness;
                    LeaveCriticalSection(&g_cfgLock);
                    SaveConfig();
                    PopulateProfilesList(lb);
                    SendMessageA(lb, LB_SETCURSEL, sel, 0);
                }
            }
        } else if (LOWORD(wp) == IDC_PROF_REMOVE) {
            int sel = (int)SendMessageA(lb, LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR) {
                EnterCriticalSection(&g_cfgLock);
                if (sel < (int)g_cfg.profiles.size())
                    g_cfg.profiles.erase(g_cfg.profiles.begin() + sel);
                LeaveCriticalSection(&g_cfgLock);
                SaveConfig();
                PopulateProfilesList(lb);
            }
        } else if (LOWORD(wp) == IDC_PROF_CLOSE) {
            DestroyWindow(hwnd);
        }
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

static void ShowProfilesDialog()
{
    ProfilesDlgData* data = new ProfilesDlgData{nullptr};
    HINSTANCE hInst = (HINSTANCE)GetModuleHandleA(nullptr);

    typedef UINT(WINAPI* PFN_GetDpiForSystem)();
    static auto pfnDpiSys = (PFN_GetDpiForSystem)GetProcAddress(GetModuleHandleA("user32.dll"), "GetDpiForSystem");
    UINT dpi = pfnDpiSys ? pfnDpiSys() : 96;
    int W = MulDiv(640, (int)dpi, 96);
    int H = MulDiv(460, (int)dpi, 96);

    HWND hw = CreateWindowExA(
        WS_EX_TOPMOST,
        "HDRAutostartProfilesDlg", L->dlgProfiles,
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, W, H,
        nullptr, nullptr, hInst, data);
    if (hw) SetForegroundWindow(hw);
    else    delete data;
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

        // Save timestamp to prevent re-triggering on the next launch (anti-loop)
        EnterCriticalSection(&g_cfgLock);
        g_cfg.lastUpdateAttempt = time(nullptr);
        LeaveCriticalSection(&g_cfgLock);
        SaveConfig();

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
            int d, ds, shdrv, ssdv;
            EnterCriticalSection(&g_cfgLock);
            d     = g_cfg.ktcLocalDimming;
            ds    = g_cfg.ktcSdrLocalDimming;
            shdrv = g_cfg.ktcSharpnessHdr;
            ssdv  = g_cfg.ktcSharpnessSdr;
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

            // Local Dimming submenu
            HMENU dimMenu = CreatePopupMenu();
            AppendMenuA(dimMenu, MF_POPUP, (UINT_PTR)sub,    L->menuKTC);
            AppendMenuA(dimMenu, MF_POPUP, (UINT_PTR)subSDR, L->menuKTCSDR);

            // Sharpness submenu
            char shdrLabel[64], ssdLabel[64];
            snprintf(shdrLabel, sizeof(shdrLabel), "HDR (KTC): %d", shdrv);
            snprintf(ssdLabel,  sizeof(ssdLabel),  "SDR (KTC): %d", ssdv);
            HMENU sharpMenu = CreatePopupMenu();
            AppendMenuA(sharpMenu, MF_STRING, ID_KTC_SHARP_HDR, shdrLabel);
            AppendMenuA(sharpMenu, MF_STRING, ID_KTC_SHARP_SDR, ssdLabel);

            // KTC Settings root submenu
            HMENU ktcMenu = CreatePopupMenu();
            AppendMenuA(ktcMenu, MF_POPUP,     (UINT_PTR)dimMenu,       L->menuLocalDimming);
            AppendMenuA(ktcMenu, MF_POPUP,     (UINT_PTR)sharpMenu,     L->menuSharpness);
            AppendMenuA(ktcMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuA(ktcMenu, MF_STRING,    ID_TRAY_PROFILES,        L->menuProfiles);
            AppendMenuA(m, MF_POPUP, (UINT_PTR)ktcMenu, L->menuKTCSettings);
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
        case ID_KTC_SHARP_HDR:
            ShowSharpnessDialog("Sharpness HDR (KTC)", &g_cfg.ktcSharpnessHdr); break;
        case ID_KTC_SHARP_SDR:
            ShowSharpnessDialog("Sharpness SDR (KTC)", &g_cfg.ktcSharpnessSdr); break;
        case ID_TRAY_PROFILES:
            ShowProfilesDialog(); break;
        case ID_TRAY_GITHUB:
            ShellExecuteA(nullptr, "open", "https://github.com/conecta6/HDRAutostart-W11", nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case ID_TRAY_EXIT:
            KillTimer(hwnd, TIMER_BROWSER);
            SetEvent(g_stopEvent);
            DestroyWindow(hwnd);
            break;
        default:
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
    g_cfg.lastUpdateAttempt = 0;  // reset on fresh start so update is always attempted
    OpenLog();
    Log("=== HDRAutostart started ===");
    InitNVAPI();

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

    WNDCLASSEXA wc3 = {};
    wc3.cbSize = sizeof(wc3);  wc3.hInstance = hInst;
    wc3.lpszClassName = "HDRAutostartSharpDlg";  wc3.lpfnWndProc = SharpDlgProc;
    wc3.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassExA(&wc3);

    WNDCLASSEXA wc4 = {};
    wc4.cbSize = sizeof(wc4);  wc4.hInstance = hInst;
    wc4.lpszClassName = "HDRAutostartProfilesDlg";  wc4.lpfnWndProc = ProfilesDlgProc;
    wc4.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassExA(&wc4);

    WNDCLASSEXA wc5 = {};
    wc5.cbSize = sizeof(wc5);  wc5.hInstance = hInst;
    wc5.lpszClassName = "HDRAutostartProfEditDlg";  wc5.lpfnWndProc = ProfEditDlgProc;
    wc5.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassExA(&wc5);

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
    ShutdownNVAPI();
    DeleteCriticalSection(&g_cfgLock);
    if (g_log) fclose(g_log);
    CoUninitialize();
    CloseHandle(mutex);
    return 0;
}
