// =============================================================================
// hdrautostart.cpp  v3 — system-tray HDR auto-activator (HDRAutostart)
//   · Per-game profiles decide HDR / SDR handling (matched by exe path, name or folder)
//   · Browser fullscreen triggers HDR (auto-off when leaving fullscreen)
//   · KTC Local Dimming via DDC/CI (VCP 0xF4)
// =============================================================================
#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
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
#include <climits>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <ctime>
#include <winhttp.h>
#include <urlmon.h>
#include <commctrl.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "dxva2.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")

#define APP_VERSION "0.35"

// =============================================================================
// Localisation
// =============================================================================
struct Lang {
    const char *menuStartup, *menuExit;
    const char *btnAdd, *btnRemove, *btnClose, *btnOk, *btnCancel;
    const char *tipOn, *tipOff;
    const char *menuLocalDimming, *menuDesktop;
    const char *ktcKeep, *ktcAuto, *ktcLow, *ktcStd, *ktcHigh;
    const char *menuGithub;
    const char *menuProfiles;
    const char *dlgProfiles;
    const char *menuSharpness;
    const char *profDimLabel;                // profile list: short dimming label
    const char *menuBrightness;
    const char *menuVideo;
    const char *menuVideoBrowser;
    const char *profHdrCheck, *profSharpShort;
    const char *msgFoldersDropped;           // balloon: old config migrated, its monitored folders were discarded
    const char *profTagName, *profTagFolder; // profile list: name-only and folder entries
    const char *dlgProfEdit;                 // profile edit dialog: title and field labels
    const char *profDimField, *profBrightField, *profBrightShort;
    const char *numValueFmt;                 // numeric dialog label, printf(min, max)
};

static const Lang kES = {
    "Ejecutar al inicio", "Salir",
    "Agregar", "Eliminar", "Cerrar", "Aceptar", "Cancelar",
    "HDRAutostart \x97 HDR activo", "HDRAutostart \x97 HDR inactivo",
    "Local Dimming", "Escritorio (KTC)",
    "No tocar", "Auto", "Bajo", "Est\xe1ndar", "Alto",
    "GitHub",
    "Perfiles de juego...", "Perfiles de juego", "Nitidez",
    "Atenuaci\xf3n:",
    "Brillo",
    "V\xed" "deo",
    "HDR en navegador a pantalla completa",
    "Activar HDR con este juego", "Nitidez:",
    "Ahora los juegos se detectan solo por perfil. A\xf1" "ade tus juegos en Perfiles de juego.",
    "(cualquier carpeta)", "[carpeta]",
    "Ajustes del perfil",
    "Atenuaci\xf3n local:", "Brillo (0-100):", "Brillo:",
    "Valor (%d-%d):"
};
static const Lang kEN = {
    "Run at startup", "Exit",
    "Add", "Remove", "Close", "OK", "Cancel",
    "HDRAutostart \x97 HDR active", "HDRAutostart \x97 HDR inactive",
    "Local Dimming", "Desktop (KTC)",
    "Don't change", "Auto", "Low", "Standard", "High",
    "GitHub",
    "Game profiles...", "Game profiles", "Sharpness",
    "Dimming:",
    "Brightness",
    "Video",
    "HDR on browser fullscreen",
    "Enable HDR for this game", "Sharpness:",
    "Games are now detected by profile only. Add your games in Game profiles.",
    "(any folder)", "[folder]",
    "Profile settings",
    "Local Dimming:", "Brightness (0-100):", "Brightness:",
    "Value (%d-%d):"
};
static const Lang* L = &kEN;

static void DetectLang()
{
    if (PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_SPANISH) L = &kES;
}

// =============================================================================
// Config  (hdrautostart.ini next to exe)
// =============================================================================
// A profile is what makes a program a "game". 'exe' is matched like this (lowercase):
//   - ends with a backslash       -> folder prefix
//   - contains a backslash        -> exact full path
//   - otherwise                   -> executable name
struct GameProfile {
    std::string exe;           // see above, stored lowercase
    bool hdr          = true;  // true = enable HDR while running, false = SDR game
    int  localDimming = 1;     // 0 = leave alone, 1=Auto 2=Low 3=Std 4=High
    int  sharpness    = 6;     // -1 = leave alone, 0-10 (VCP 0x87)
    int  brightness   = 100;   // 0-100 (VCP 0x10); only sent to SDR games (-1 only while loading)
};

// General settings = the desktop: what the monitor goes back to when no game (and no
// browser video) is running. Each profile carries its own values.
struct Config {
    int    ktcDimmingDesktop    = 1;   // 0 = leave alone, 1=Auto 2=Low 3=Std 4=High (VCP 0xF4)
    int    ktcSharpnessDesktop  = 6;   // -1=off, 0-10 (VCP 0x87 on KTC)
    int    ktcBrightnessDesktop = 22;  // 0-100 (VCP 0x10)
    int    videoDimming         = 1;   // same scale as ktcDimmingDesktop — HDR video in a browser
    int    videoSharpness       = 6;   // -1=off, 0-10 — HDR video in a browser
    time_t lastUpdateAttempt  = 0;  // unix timestamp of last auto-update trigger (anti-loop)
    bool   browserHdrEnabled  = false;
    std::vector<GameProfile> profiles;
};

static CRITICAL_SECTION g_cfgLock;
static Config           g_cfg;
// Bumped every time the profiles dialog adds, edits or removes a profile (after g_cfg.profiles
// changed): MonitorThread then reclassifies running processes without needing a restart.
static volatile LONG    g_profilesGen = 0;
// Bumped when a desktop setting changes from the menu: MonitorThread then sends the new
// values at once if nothing (no game, no browser video) is holding the monitor.
static volatile LONG    g_desktopGen = 0;
static void DesktopSettingsChanged() { InterlockedIncrement(&g_desktopGen); }
// Set by LoadConfig when the migration of an old config discarded its monitored folders (the
// games they used to detect are no longer detected until they get a profile)
static bool             g_migratedDroppedFolders = false;
// Set by LoadConfig when an old .ini could not be copied to .bak and so was left untouched:
// SaveConfig tries the copy again before it replaces the file
static bool             g_bakPending = false;

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
    // Fallback: try standard per-user AppData locations before ExeDir().
    // Helps when the HKCU registry key was lost (e.g., another user running this exe).
    { char envBuf[MAX_PATH] = {};
      if (ExpandEnvironmentStringsA("%APPDATA%\\HDRAutostart\\", envBuf, MAX_PATH) > 1) {
          DWORD attr = GetFileAttributesA(envBuf);
          if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
              return std::string(envBuf);
      }
    }
    { char envBuf[MAX_PATH] = {};
      if (ExpandEnvironmentStringsA("%LOCALAPPDATA%\\HDRAutostart\\", envBuf, MAX_PATH) > 1) {
          DWORD attr = GetFileAttributesA(envBuf);
          if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
              return std::string(envBuf);
      }
    }
    return ExeDir();
}

static void Log(const char* fmt, ...);  // defined below (used by SaveConfig)

static void SaveConfig()
{
    std::string path = ConfigDir() + "hdrautostart.ini";
    // Write to a temp file and swap it in at the end, so a crash or power loss
    // mid-save can never leave an empty/truncated .ini behind.
    std::string tmp  = path + ".tmp";
    EnterCriticalSection(&g_cfgLock);
    FILE* f = fopen(tmp.c_str(), "w");
    if (!f) { LeaveCriticalSection(&g_cfgLock); return; }
    fprintf(f, "[settings]\n");
    fprintf(f, "ktc_dimming_desktop=%d\n",    g_cfg.ktcDimmingDesktop);
    fprintf(f, "ktc_sharpness_desktop=%d\n",   g_cfg.ktcSharpnessDesktop);
    fprintf(f, "ktc_brightness_desktop=%d\n",  g_cfg.ktcBrightnessDesktop);
    fprintf(f, "video_dimming=%d\n",           g_cfg.videoDimming);
    fprintf(f, "video_sharpness=%d\n",         g_cfg.videoSharpness);
    fprintf(f, "browser_hdr=%d\n",          g_cfg.browserHdrEnabled ? 1 : 0);
    if (g_cfg.lastUpdateAttempt)
        fprintf(f, "last_update_attempt=%lld\n", (long long)g_cfg.lastUpdateAttempt);
    fprintf(f, "[profiles]\n");  // exe|dimming|sharpness|hdr|brightness
    for (auto& p : g_cfg.profiles)
        fprintf(f, "%s|%d|%d|%d|%d\n", p.exe.c_str(), p.localDimming, p.sharpness, p.hdr ? 1 : 0, p.brightness);
    bool ok = !ferror(f);
    if (fclose(f) != 0) ok = false;
    // An old .ini that could not be backed up when it was loaded: try the copy again before
    // the file is replaced. If it keeps failing the user's change is saved anyway.
    if (ok && g_bakPending) {
        std::string bak = path + ".bak";
        if (CopyFileA(path.c_str(), bak.c_str(), TRUE)) {
            Log("SaveConfig: old file saved as %s", bak.c_str());
            g_bakPending = false;
        } else {
            DWORD bakErr = GetLastError();
            if (bakErr == ERROR_FILE_EXISTS) {
                Log("SaveConfig: %s already exists, kept as is", bak.c_str());
                g_bakPending = false;
            } else if (bakErr == ERROR_FILE_NOT_FOUND || bakErr == ERROR_PATH_NOT_FOUND) {
                g_bakPending = false;  // nothing to back up any more
            } else {
                Log("SaveConfig: could not save %s (error %lu) — the old file is overwritten WITHOUT a backup",
                    bak.c_str(), bakErr);
            }
        }
    }
    // Swap inside the lock: two threads saving at once must not share the temp file
    // The swap can fail transiently (antivirus / indexer holding the target): retry a few times
    bool  moved   = false;
    DWORD moveErr = 0;
    if (ok) {
        for (int attempt = 0; attempt < 4 && !moved; attempt++) {
            if (attempt) Sleep(50);
            moved = MoveFileExA(tmp.c_str(), path.c_str(),
                                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
            if (!moved) moveErr = GetLastError();
        }
    }
    if (!moved) {
        DeleteFileA(tmp.c_str());
        if (ok) Log("SaveConfig: could not replace %s (error %lu)", path.c_str(), moveErr);
    }
    LeaveCriticalSection(&g_cfgLock);
}

// A numeric .ini field is valid only if, after optional blanks, it starts with a digit or with
// '-' and a digit (atoi would turn "abc" into 0). Text after the number is ignored.
static bool ParseInt64Field(const char* text, long long& value)
{
    while (*text == ' ' || *text == '\t') text++;
    const char* d = (*text == '-') ? text + 1 : text;
    if (*d < '0' || *d > '9') return false;
    value = atoll(text);
    return true;
}

static bool ParseIntField(const char* text, int& value)
{
    long long v = 0;
    if (!ParseInt64Field(text, v) || v < INT_MIN || v > INT_MAX) return false;
    value = (int)v;
    return true;
}

// Numeric field within [lo, hi]
static bool ParseIntRange(const char* text, int lo, int hi, int& value)
{
    int v = 0;
    if (!ParseIntField(text, v) || v < lo || v > hi) return false;
    value = v;
    return true;
}

static bool ParseSharpnessValue(const char* text, int& value)
{
    int v = 0;
    if (!ParseIntField(text, v)) return false;
    if (v > 10 && v <= 100 && (v % 10) == 0) v /= 10;  // migrate old 0-100 configs
    if (v < -1 || v > 10) return false;
    value = v;
    return true;
}

static std::string ToLower(std::string s);  // defined below (used by LoadConfig)

// Executable name of a path (text after the last backslash, or the whole string)
static std::string PathBase(const std::string& s)
{
    size_t p = s.rfind('\\');
    return p == std::string::npos ? s : s.substr(p + 1);
}

// Match a profile entry against a process path (all three arguments lowercase):
//   - entry ending in a backslash    -> folder prefix match
//   - entry containing a backslash   -> exact full-path match
//   - entry with no backslash (name) -> match by executable name
// Returns 0 = no match, otherwise the specificity: 1 = folder, 2 = name, 3 = full path.
// The name form lets one profile cover a program wherever it is installed.
static int MatchProfileEntry(const std::string& entryLo,
                             const std::string& pathLo, const std::string& baseLo)
{
    if (entryLo.empty()) return 0;
    if (entryLo.back() == '\\')
        return pathLo.compare(0, entryLo.size(), entryLo) == 0 ? 1 : 0;
    if (entryLo.find('\\') != std::string::npos)
        return entryLo == pathLo ? 3 : 0;
    return entryLo == baseLo ? 2 : 0;
}

// Value of an "key=value" .ini line (pointer just after the '='), nullptr if the line is another key
static const char* IniValue(const char* line, const char* key)
{
    size_t n = strlen(key);
    return (strncmp(line, key, n) == 0 && line[n] == '=') ? line + n + 1 : nullptr;
}

static void LoadConfig()
{
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
    // Installs whose ConfigPath only became visible after the installer switched to the
    // 64-bit registry view used one of these per-user folders before: copy it over (origin kept).
    {
        static const char* const kOldIni[] = {
            "%APPDATA%\\HDRAutostart\\hdrautostart.ini",
            "%LOCALAPPDATA%\\HDRAutostart\\hdrautostart.ini"
        };
        for (size_t i = 0; !f && i < sizeof(kOldIni) / sizeof(kOldIni[0]); i++) {
            char o[MAX_PATH] = {};
            DWORD n = ExpandEnvironmentStringsA(kOldIni[i], o, MAX_PATH);
            if (n == 0 || n > MAX_PATH) continue;
            if (_stricmp(o, path.c_str()) == 0) continue;
            if (CopyFileA(o, path.c_str(), FALSE)) f = fopen(path.c_str(), "r");
        }
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
          if (CopyFileA(o.c_str(), path.c_str(), FALSE)) { DeleteFileA(o.c_str()); f = fopen(path.c_str(), "r"); } }
    }
    if (!f) {
        // Try: parent directory of exe (common case: old exe in hdr2\, new in hdr2\dist\)
        std::string exeD = ExeDir();
        if (!exeD.empty() && exeD.back() == '\\') exeD.pop_back();
        size_t p2 = exeD.rfind('\\');
        if (p2 != std::string::npos) {
            std::string o = exeD.substr(0, p2 + 1) + "steamhdr.ini";
            if (CopyFileA(o.c_str(), path.c_str(), FALSE)) { DeleteFileA(o.c_str()); f = fopen(path.c_str(), "r"); }
        }
    }
    if (!f) {
        SaveConfig();  // fresh install: no profiles yet
        return;
    }

    enum Section { SEC_NONE, SEC_SETTINGS, SEC_FOLDERS, SEC_WHITELIST, SEC_BLACKLIST, SEC_EXCLUDE, SEC_PROFILES };
    Section sec = SEC_NONE;
    bool sawDimDesktop = false, sawDesktopSharpness = false, sawBrightnessDesktop = false;
    bool sawVideoDim = false, sawVideoSharp = false;
    // Settings of the pre-"desktop" format (general HDR / SDR values): read only to migrate them
    // into the profiles and the video values below. Defaults are the old ones.
    bool sawOldKey = false;
    int  oldDimHdr = 0, oldDimSdr = 0, oldSharpHdr = 6, oldSharpSdr = 6, oldBrightSdr = 100;
    // Legacy lists (pre-profile .ini): read only to migrate them into profiles below
    std::vector<std::string> oldFolders, oldWhitelist, oldBlacklist, oldExclude;
    std::vector<size_t> legacyProfiles;  // indexes into g_cfg.profiles of 3-field lines (no hdr yet)
    // The pre-profile versions always wrote these four headers: seeing any of them is what
    // marks the file as an old one (only then is a 3-field profile line a legacy line)
    bool sawOldHeader = false;
    char line[4096];  // MAX_PATH(260) is not enough; Windows supports paths up to 32767 chars
    while (fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = '\0';
        if (!n) continue;
        if (!strcmp(line, "[settings]"))  { sec = SEC_SETTINGS;  continue; }
        if (!strcmp(line, "[folders]"))   { sec = SEC_FOLDERS;   sawOldHeader = true; continue; }
        if (!strcmp(line, "[whitelist]")) { sec = SEC_WHITELIST; sawOldHeader = true; continue; }
        if (!strcmp(line, "[blacklist]")) { sec = SEC_BLACKLIST; sawOldHeader = true; continue; }
        if (!strcmp(line, "[exclude]"))   { sec = SEC_EXCLUDE;   sawOldHeader = true; continue; }
        if (!strcmp(line, "[profiles]"))  { sec = SEC_PROFILES;  continue; }
        switch (sec) {
        case SEC_SETTINGS: {
            const char* v;
            int i = 0;
            // A value that is not a valid number (or is out of range) is ignored: the key keeps its default
            if ((v = IniValue(line, "ktc_dimming_desktop"))) {
                if (ParseIntRange(v, 0, 4, i)) { g_cfg.ktcDimmingDesktop = i; sawDimDesktop = true; }
            } else if ((v = IniValue(line, "ktc_sharpness_desktop"))) {
                if (ParseSharpnessValue(v, i)) { g_cfg.ktcSharpnessDesktop = i; sawDesktopSharpness = true; }
            } else if ((v = IniValue(line, "ktc_brightness_desktop"))) {
                if (ParseIntRange(v, 0, 100, i)) { g_cfg.ktcBrightnessDesktop = i; sawBrightnessDesktop = true; }
            } else if ((v = IniValue(line, "video_dimming"))) {
                if (ParseIntRange(v, 0, 4, i)) { g_cfg.videoDimming = i; sawVideoDim = true; }
            } else if ((v = IniValue(line, "video_sharpness"))) {
                if (ParseSharpnessValue(v, i)) { g_cfg.videoSharpness = i; sawVideoSharp = true; }
            } else if ((v = IniValue(line, "last_update_attempt"))) {
                long long ts = 0;
                if (ParseInt64Field(v, ts)) g_cfg.lastUpdateAttempt = (time_t)ts;
            } else if ((v = IniValue(line, "browser_hdr"))) {
                if (ParseIntField(v, i)) g_cfg.browserHdrEnabled = i != 0;
            } else if ((v = IniValue(line, "ktc_local_dimming"))) {
                sawOldKey = true;
                if (ParseIntRange(v, 0, 4, i)) oldDimHdr = i;
            } else if ((v = IniValue(line, "ktc_sdr_local_dimming"))) {
                sawOldKey = true;
                if (ParseIntRange(v, 0, 4, i)) oldDimSdr = i;
            } else if ((v = IniValue(line, "ktc_sharpness_hdr"))) {
                sawOldKey = true;
                if (ParseSharpnessValue(v, i)) oldSharpHdr = i;
            } else if ((v = IniValue(line, "ktc_sharpness_sdr"))) {
                sawOldKey = true;
                if (ParseSharpnessValue(v, i)) oldSharpSdr = i;
            } else if ((v = IniValue(line, "ktc_brightness_sdr"))) {
                sawOldKey = true;
                if (ParseIntRange(v, 0, 100, i)) oldBrightSdr = i;
            }
            break;
        }
        case SEC_FOLDERS:   oldFolders.push_back(ToLower(line));   break;
        case SEC_WHITELIST: oldWhitelist.push_back(ToLower(line)); break;
        case SEC_BLACKLIST: oldBlacklist.push_back(ToLower(line)); break;
        case SEC_EXCLUDE:   oldExclude.push_back(ToLower(line));   break;
        case SEC_PROFILES: {
            // format: exe|dimming|sharpness|hdr|brightness
            // (older forms: 3 fields have no hdr; without brightness it is filled in below.
            //  In the pre-"desktop" format -1 in dimming / sharpness meant "use the general value")
            // Every line that is dropped is logged with its text
            char* p1 = strchr(line, '|');
            char* p2 = p1 ? strchr(p1 + 1, '|') : nullptr;
            if (!p2) { Log("Config: profile line ignored (not exe|dimming|sharpness...): %s", line); break; }
            GameProfile gp;
            gp.brightness = -1;  // "missing" until a valid fifth field says otherwise
            // Lowercase: MonitorThread compares against the lowercased process path
            gp.exe = ToLower(std::string(line, p1 - line));
            int sharp = 0, hdrVal = 1;
            // Dimming must be -1..4, sharpness -1..10 (old 0-100 migrated), hdr 0 or 1:
            // a missing, non-numeric or out-of-range value drops the line
            char* p3 = strchr(p2 + 1, '|');
            char* p4 = p3 ? strchr(p3 + 1, '|') : nullptr;
            if (gp.exe.empty() || !ParseIntRange(p1 + 1, -1, 4, gp.localDimming) ||
                !ParseSharpnessValue(p2 + 1, sharp) || (p3 && !ParseIntRange(p3 + 1, 0, 1, hdrVal))) {
                Log("Config: profile line ignored (invalid or out of range): %s", line);
                break;
            }
            gp.sharpness = sharp;
            if (p3) gp.hdr = hdrVal != 0;
            // Brightness: an invalid or missing value stays -1 and gets its default below
            if (p4) {
                int b = 0;
                if (ParseIntRange(p4 + 1, 0, 100, b)) gp.brightness = b;
            }
            // Ignore a second profile for the same entry (first one wins)
            bool dup = false;
            for (auto& q : g_cfg.profiles) if (q.exe == gp.exe) { dup = true; break; }
            if (dup) { Log("Config: profile line ignored (duplicate entry): %s", line); break; }
            if (!p3) legacyProfiles.push_back(g_cfg.profiles.size());
            g_cfg.profiles.push_back(gp);
            break;
        }
        default: break;
        }
    }
    fclose(f);
    // No old-style header (headers may come after [profiles], hence the check here):
    // 3-field lines keep the default hdr = true and nothing needs migrating
    if (!sawOldHeader) legacyProfiles.clear();
    // Only a removed general key (sawOldKey) means the file has old general values to move into
    // the profiles and the video values. A file that merely lacks ktc_dimming_desktop (e.g. a new
    // .ini edited by hand) is completed with defaults and its profiles are left as written.
    // Either way the file is about to be rewritten, so it gets a backup first.
    const bool oldFormat = sawOldKey || !sawDimDesktop;
    bool needSave = !sawDimDesktop || !sawDesktopSharpness || !sawBrightnessDesktop ||
                    !sawVideoDim || !sawVideoSharp;
    bool skipSave = false;  // old file that could not be backed up: leave it untouched
    if (sawOldKey) {
        if (!sawDesktopSharpness) g_cfg.ktcSharpnessDesktop = oldSharpSdr;
        if (!sawVideoDim)   g_cfg.videoDimming   = oldDimHdr;
        if (!sawVideoSharp) g_cfg.videoSharpness = oldSharpHdr;
    }

    const bool listMigration = !legacyProfiles.empty() || !oldFolders.empty() ||
                               !oldWhitelist.empty() || !oldBlacklist.empty() || !oldExclude.empty();

    // --- Migration: folders / whitelist / blacklist / exclude lists -> profiles ---
    if (listMigration) {
        // Entries that matched the old exclusion list were ignored by the old versions: they
        // are not migrated. A candidate entry is compared as if it were a process path:
        // a name matches an equal name or the final name of an excluded path; a path matches
        // an equal path, an excluded folder prefix or an excluded name equal to its file name.
        auto excludedByOld = [&](const std::string& cand) {
            std::string base = PathBase(cand);
            for (auto& e : oldExclude) {
                if (MatchProfileEntry(e, cand, base)) return true;
                if (cand.find('\\') == std::string::npos && e.find('\\') != std::string::npos &&
                    e.back() != '\\' && PathBase(e) == cand) return true;
            }
            return false;
        };
        int excludedProfiles = 0, excludedEntries = 0;
        {
            // Drop excluded 3-field profiles (indexes in 'legacyProfiles' are ascending)
            std::vector<size_t> kept;
            std::vector<GameProfile> rest;
            size_t li = 0;
            for (size_t i = 0; i < g_cfg.profiles.size(); i++) {
                bool legacy = li < legacyProfiles.size() && legacyProfiles[li] == i;
                if (legacy) li++;
                if (legacy && excludedByOld(g_cfg.profiles[i].exe)) { excludedProfiles++; continue; }
                if (legacy) kept.push_back(rest.size());
                rest.push_back(g_cfg.profiles[i]);
            }
            g_cfg.profiles.swap(rest);
            legacyProfiles.swap(kept);
        }
        // 3-field profiles get their mode from the old lists: blacklist -> SDR;
        // whitelist or monitored folder -> HDR; no match -> SDR
        for (size_t idx : legacyProfiles) {
            GameProfile& gp = g_cfg.profiles[idx];
            std::string base = PathBase(gp.exe);
            bool blocked = false, hdr = false;
            for (auto& e : oldBlacklist)
                if (MatchProfileEntry(e, gp.exe, base)) { blocked = true; break; }
            if (!blocked) {
                for (auto& e : oldWhitelist)
                    if (MatchProfileEntry(e, gp.exe, base)) { hdr = true; break; }
                for (auto& e : oldFolders)
                    if (!e.empty() && gp.exe.compare(0, e.size(), e) == 0) { hdr = true; break; }
            }
            gp.hdr = hdr;
        }
        // List entries without a profile become one (blacklist first: it used to win over the whitelist)
        int migratedBl = 0, migratedWl = 0;
        for (int pass = 0; pass < 2; pass++) {
            const std::vector<std::string>& src = pass == 0 ? oldBlacklist : oldWhitelist;
            for (auto& e : src) {
                if (e.empty()) continue;
                if (excludedByOld(e)) { excludedEntries++; continue; }
                bool have = false;
                for (auto& q : g_cfg.profiles) if (q.exe == e) { have = true; break; }
                if (have) continue;
                GameProfile gp;
                gp.exe = e;
                gp.hdr = (pass == 1);
                // No values of its own: it takes the old general ones of its mode (the defaults
                // of those if the file had none)
                gp.localDimming = gp.hdr ? oldDimHdr   : oldDimSdr;
                gp.sharpness    = gp.hdr ? oldSharpHdr : oldSharpSdr;
                gp.brightness   = oldBrightSdr;
                g_cfg.profiles.push_back(gp);
                if (pass == 0) migratedBl++; else migratedWl++;
            }
        }
        Log("Config migration: %zu profile(s) got a mode, %d whitelist and %d blacklist entries "
            "became profiles, %zu folder(s) and %zu exclusion(s) discarded",
            legacyProfiles.size(), migratedWl, migratedBl, oldFolders.size(), oldExclude.size());
        if (excludedProfiles || excludedEntries)
            Log("Config migration: %d profile(s) and %d list entries matched the exclusion list and were not migrated",
                excludedProfiles, excludedEntries);
        // The games those folders used to detect are no longer detected: tell the user once
        if (!oldFolders.empty()) g_migratedDroppedFolders = true;
    }

    // --- Profiles: fill in the values a line (or a list entry) did not carry ---
    // Old general keys seen: -1 in dimming / sharpness meant "use the general value of its mode",
    // and a missing brightness is the old general SDR brightness. Otherwise -1 sharpness is
    // "don't change" (kept); a stray -1 dimming is read as "don't change" (0).
    int filledProfiles = 0;
    for (auto& gp : g_cfg.profiles) {
        bool filled = false;
        if (gp.localDimming < 0) {
            gp.localDimming = sawOldKey ? (gp.hdr ? oldDimHdr : oldDimSdr) : 0;
            filled = true;
        }
        if (sawOldKey && gp.sharpness < 0) {
            gp.sharpness = gp.hdr ? oldSharpHdr : oldSharpSdr;  // may stay -1: "don't change"
            filled = true;
        }
        if (gp.brightness < 0) {
            gp.brightness = sawOldKey ? oldBrightSdr : 100;
            filled = true;
        }
        if (filled) filledProfiles++;
    }

    if (listMigration || oldFormat) {
        if (sawOldKey)
            Log("Config migration: general settings became desktop / video values "
                "(old dimming HDR=%d SDR=%d, sharpness HDR=%d SDR=%d, brightness SDR=%d); "
                "%d profile(s) got their own values",
                oldDimHdr, oldDimSdr, oldSharpHdr, oldSharpSdr, oldBrightSdr, filledProfiles);
        // Keep the old file once (never overwrite an existing backup) before rewriting it
        std::string bak = path + ".bak";
        if (CopyFileA(path.c_str(), bak.c_str(), TRUE)) {
            Log("Config migration: old file saved as %s", bak.c_str());
        } else {
            DWORD bakErr = GetLastError();
            if (bakErr == ERROR_FILE_EXISTS) {
                Log("Config migration: %s already exists, kept as is", bak.c_str());
            } else {
                // No backup: do not rewrite the old file on this load (the migration stays applied
                // in memory and is repeated on the next start). The first SaveConfig after a
                // user change tries the copy again (g_bakPending) before replacing the file.
                Log("Config migration: could not save %s (error %lu) — old file left untouched",
                    bak.c_str(), bakErr);
                skipSave = true;
                g_bakPending = true;
            }
        }
        needSave = true;
    }
    if (needSave && !skipSave) SaveConfig();
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
    char msg[2048];
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
    // The display configuration can change between the size query and the
    // actual query (ERROR_INSUFFICIENT_BUFFER): retry a few times.
    for (int attempt = 0; attempt < 3; attempt++) {
        UINT32 np = 0, nm = 0;
        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &np, &nm) != ERROR_SUCCESS) return false;
        p.resize(np); m.resize(nm);
        LONG r = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &np, p.data(),
                                    &nm, m.data(), nullptr);
        if (r == ERROR_SUCCESS) return true;
        if (r != ERROR_INSUFFICIENT_BUFFER) return false;
    }
    return false;
}

// True if the monitor answers the KTC-proprietary local dimming code (0xF4)
// with a non-zero max. Used to recognise KTC panels whose PnP vendor ID is unknown.
static bool RespondsToKTCDimming(HANDLE hPhysicalMonitor)
{
    DWORD dimCur = 0, dimMax = 0;
    return GetVCPFeatureAndVCPFeatureReply(hPhysicalMonitor, 0xF4,
                                           nullptr, &dimCur, &dimMax) && dimMax != 0;
}

// True if any physical monitor behind this HMONITOR answers the KTC dimming code.
// Slow (DDC/CI round trip per physical monitor, ~60 ms).
static bool AnyPhysicalMonitorRespondsToKTCDimming(HMONITOR hmon)
{
    DWORD count = 0;
    if (!GetNumberOfPhysicalMonitorsFromHMONITOR(hmon, &count) || count == 0) return false;
    std::vector<PHYSICAL_MONITOR> mons(count);
    if (!GetPhysicalMonitorsFromHMONITOR(hmon, count, mons.data())) return false;
    bool isKTC = false;
    for (DWORD i = 0; i < count && !isKTC; i++)
        isKTC = RespondsToKTCDimming(mons[i].hPhysicalMonitor);
    DestroyPhysicalMonitors(count, mons.data());
    return isKTC;
}

struct FindMonitorCtx {
    const WCHAR* gdiName;   // e.g. L"\\\\.\\DISPLAY1"
    HMONITOR     hmon;
};

static BOOL CALLBACK FindMonitorByGdiNameProc(HMONITOR hmon, HDC, LPRECT, LPARAM lParam)
{
    FindMonitorCtx* ctx = (FindMonitorCtx*)lParam;
    MONITORINFOEXW mi = {};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(hmon, (MONITORINFO*)&mi) && _wcsicmp(mi.szDevice, ctx->gdiName) == 0) {
        ctx->hmon = hmon;
        return FALSE;
    }
    return TRUE;
}

static bool IsKTCMonitor(HMONITOR hmon);  // defined below (shared cached verdict)

// True if the target of this display path is a KTC monitor: PnP vendor ID "KTC"
// or "SKG" (the M27P6 reports "SKG") in the target device path, or, failing that,
// the cached verdict of IsKTCMonitor (vendor ID / DDC/CI dimming probe).
static bool IsKTCDisplayPath(const DISPLAYCONFIG_PATH_INFO& pi)
{
    DISPLAYCONFIG_TARGET_DEVICE_NAME tn = {};
    tn.header.type      = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
    tn.header.size      = sizeof(tn);
    tn.header.adapterId = pi.targetInfo.adapterId;
    tn.header.id        = pi.targetInfo.id;
    if (DisplayConfigGetDeviceInfo(&tn.header) == ERROR_SUCCESS) {
        // monitorDevicePath looks like "\\?\DISPLAY#SKG2774#5&...#{guid}"
        std::wstring path = tn.monitorDevicePath;
        CharUpperW(&path[0]);
        if (path.find(L"DISPLAY#KTC") != std::wstring::npos ||
            path.find(L"DISPLAY#SKG") != std::wstring::npos) return true;
    }

    // Fallback: DDC/CI probe on the monitor attached to this path's source.
    DISPLAYCONFIG_SOURCE_DEVICE_NAME sn = {};
    sn.header.type      = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
    sn.header.size      = sizeof(sn);
    sn.header.adapterId = pi.sourceInfo.adapterId;
    sn.header.id        = pi.sourceInfo.id;
    if (DisplayConfigGetDeviceInfo(&sn.header) != ERROR_SUCCESS) return false;

    FindMonitorCtx ctx = { sn.viewGdiDeviceName, nullptr };
    EnumDisplayMonitors(nullptr, nullptr, FindMonitorByGdiNameProc, (LPARAM)&ctx);
    if (!ctx.hmon) return false;

    return IsKTCMonitor(ctx.hmon);
}

// Turns HDR on/off on KTC displays only; other monitors are never touched.
// Returns true if at least one KTC HDR-capable display was switched.
// *noDisplay (optional) is set to true when no active KTC display with HDR support
// was found at all (as opposed to a switch that failed); callers do the logging.
static bool SetHDR(bool on, bool* noDisplay = nullptr)
{
    if (noDisplay) *noDisplay = false;
    std::vector<DISPLAYCONFIG_PATH_INFO> p;
    std::vector<DISPLAYCONFIG_MODE_INFO> m;
    if (!QPaths(p, m)) return false;
    bool any = false;
    bool ktcHdrSeen = false;
    for (auto& pi : p) {
        ACI info = {};
        info.h.type      = kGetACI;
        info.h.size      = sizeof(info);
        info.h.adapterId = pi.targetInfo.adapterId;
        info.h.id        = pi.targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&info.h) != ERROR_SUCCESS || !info.sup) continue;
        if (!IsKTCDisplayPath(pi)) continue;
        ktcHdrSeen = true;
        ACS st = {};
        st.h.type      = kSetACS;
        st.h.size      = sizeof(st);
        st.h.adapterId = pi.targetInfo.adapterId;
        st.h.id        = pi.targetInfo.id;
        st.on          = on ? 1u : 0u;
        if (DisplayConfigSetDeviceInfo(&st.h) == ERROR_SUCCESS) any = true;
    }
    if (noDisplay) *noDisplay = !ktcHdrSeen;
    return any;
}

// Returns true if any HDR-capable active KTC display currently has HDR enabled
// (non-KTC displays are ignored).
// Returns false on query failure (never act blindly); 'queried' tells both apart.
static bool IsHDROn(bool* queried = nullptr)
{
    std::vector<DISPLAYCONFIG_PATH_INFO> p;
    std::vector<DISPLAYCONFIG_MODE_INFO> m;
    if (queried) *queried = false;
    if (!QPaths(p, m)) return false;
    if (queried) *queried = true;
    for (auto& pi : p) {
        ACI info = {};
        info.h.type      = kGetACI;
        info.h.size      = sizeof(info);
        info.h.adapterId = pi.targetInfo.adapterId;
        info.h.id        = pi.targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&info.h) != ERROR_SUCCESS || !info.sup) continue;
        if (!IsKTCDisplayPath(pi)) continue;
        if (info.en) return true;
    }
    return false;
}

// Up to 3 attempts of SetHDR(on), 300 ms apart. When turning HDR off, a failed
// attempt with HDR confirmed off (or no HDR display) counts as success.
// When turning HDR on and there is no KTC HDR display at all, it gives up at once
// (*noDisplay = true): retrying cannot help.
static bool SetHDRRetry(bool on, bool* noDisplay = nullptr)
{
    if (noDisplay) *noDisplay = false;
    for (int r = 0; r < 3; r++) {
        bool nd = false;
        if (SetHDR(on, &nd)) return true;
        if (noDisplay) *noDisplay = nd;
        if (on && nd) return false;
        if (r == 2) break;
        bool queried = false;
        if (!on && !IsHDROn(&queried) && queried) return true;
        Sleep(300);
    }
    return false;
}

// =============================================================================
// NVAPI raw DDC/CI helpers
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

    // DDC/CI Set VCP Feature (MCCS standard, section 7.4)
    NvU8 payload[7] = {
        0x51,              // source address: host (DDC/CI header)
        0x84,              // length: 4 bytes of data follow (0x80 | 4)
        0x03,              // command: Set VCP Feature (DDC/CI opcode)
        vcp,               // VCP feature code (e.g. 0xF4=local dimming, 0x87=sharpness, 0x10=brightness)
        (NvU8)((value >> 8) & 0xFF),  // value high byte
        (NvU8)(value & 0xFF),          // value low byte
        0x00               // checksum placeholder (filled below)
    };
    NvU8 checksum = 0x6E;  // XOR seed: destination address (monitor = 0x6E)
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
// True if the display behind this HMONITOR reports a known KTC PnP vendor ID.
// KTC panels do not always use "KTC" (e.g. M27P6 reports "SKG").
static bool IsKTCDeviceId(HMONITOR hmon)
{
    MONITORINFOEXA mi = {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoA(hmon, (MONITORINFO*)&mi)) return false;
    DISPLAY_DEVICEA dd = {};
    dd.cb = sizeof(dd);
    for (DWORD i = 0; EnumDisplayDevicesA(mi.szDevice, i, &dd, 0); i++) {
        // DeviceID looks like "MONITOR\SKG2774\{guid}\0001"
        if (_strnicmp(dd.DeviceID, "MONITOR\\KTC", 11) == 0 ||
            _strnicmp(dd.DeviceID, "MONITOR\\SKG", 11) == 0) return true;
    }
    return false;
}

// Shared "is this monitor a KTC panel" verdict: known PnP vendor ID, or failing that the
// DDC/CI dimming probe (slow, ~60 ms per physical monitor). Cached by GDI name + DeviceID
// (not HMONITOR, which changes across display reconfigurations). Positives live until the
// cache is invalidated (WM_DISPLAYCHANGE); negatives expire after 30 s because a DDC probe
// can fail occasionally. Called from MonitorThread and from the UI thread.
struct KTCCacheEntry { bool isKTC; ULONGLONG tick; };
static CRITICAL_SECTION                     g_ktcCacheLock;
static std::map<std::string, KTCCacheEntry> g_ktcCache;
static unsigned                             g_ktcCacheGen = 0;  // bumped on invalidation
static const ULONGLONG                      kKtcNegativeTtlMs = 30000;

static void InvalidateKTCMonitorCache()
{
    EnterCriticalSection(&g_ktcCacheLock);
    g_ktcCache.clear();
    g_ktcCacheGen++;
    LeaveCriticalSection(&g_ktcCacheLock);
}

static bool IsKTCMonitor(HMONITOR hmon)
{
    MONITORINFOEXA mi = {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoA(hmon, (MONITORINFO*)&mi)) return false;  // stale handle: not cached
    DISPLAY_DEVICEA dd = {};
    dd.cb = sizeof(dd);
    EnumDisplayDevicesA(mi.szDevice, 0, &dd, 0);
    std::string key = std::string(mi.szDevice) + "|" + dd.DeviceID;

    unsigned gen = 0;
    EnterCriticalSection(&g_ktcCacheLock);
    gen = g_ktcCacheGen;
    auto it = g_ktcCache.find(key);
    if (it != g_ktcCache.end() &&
        (it->second.isKTC || GetTickCount64() - it->second.tick < kKtcNegativeTtlMs)) {
        bool cached = it->second.isKTC;
        LeaveCriticalSection(&g_ktcCacheLock);
        return cached;
    }
    LeaveCriticalSection(&g_ktcCacheLock);

    // Probe outside the lock: DDC/CI is slow
    bool isKTC = IsKTCDeviceId(hmon) || AnyPhysicalMonitorRespondsToKTCDimming(hmon);

    EnterCriticalSection(&g_ktcCacheLock);
    if (gen == g_ktcCacheGen) {  // skip the store if the cache was invalidated meanwhile
        KTCCacheEntry e = { isKTC, GetTickCount64() };
        g_ktcCache[key] = e;
    }
    LeaveCriticalSection(&g_ktcCacheLock);
    Log("KTC monitor check [%s]: %s", key.c_str(), isKTC ? "KTC" : "not KTC");
    return isKTC;
}

// Generic DDC/CI VCP setter — lParam = (vcp << 16) | value
// Only KTC monitors are written to: brightness/sharpness are standard VCP codes
// that any other brand would obey too.
static BOOL CALLBACK KTCSetVCPProc(HMONITOR hmon, HDC, LPRECT, LPARAM lParam)
{
    BYTE  vcp  = (BYTE)((DWORD_PTR)lParam >> 16);
    DWORD val  = (DWORD)((DWORD_PTR)lParam & 0xFFFF);
    if (!IsKTCMonitor(hmon)) return TRUE;
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
            // The monitor silently drops a command sent right after another one (seen on the
            // KTC M27P6: dimming + brightness back-to-back left brightness unchanged).
            // DDC/CI asks for ~50 ms after a write; 120 ms was reliable in testing.
            Sleep(120);
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
    Log("KTC Sharpness -> %d (VCP 0x87)", level);
    SetKTCVCP(0x87, level);
}

static void SetKTCBrightness(int level)
{
    if (level < 0) return;
    Log("KTC Brightness -> %d (VCP 0x10)", level);
    SetKTCVCP(0x10, level);
}

static void RestoreKTCSharpnessAfterHdrTransition(int level)
{
    if (level < 0) return;
    // Some monitors ignore the first DDC write while finishing the HDR -> SDR switch.
    for (int attempt = 0; attempt < 4; ++attempt) {
        if (attempt) Sleep(300);
        SetKTCSharpness(level);
    }
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

// Starts 'cmd' with the Explorer (non-elevated) token. Returns true as soon as the
// process was created; the result does not depend on how long it runs. It still waits
// up to timeoutMs (0 = don't wait) so that *exitCode, if requested, is meaningful when
// the process ends in time (STILL_ACTIVE otherwise).
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
                ok = true;
                WaitForSingleObject(pi.hProcess, timeoutMs);
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

// Returns true if installed for all users (HKLM has ConfigPath).
// Returns false for per-user install or portable/dev mode.
static bool IsAllUsersInstall()
{
    char buf[MAX_PATH] = {};  DWORD sz = sizeof(buf);
    return RegGetValueA(HKEY_LOCAL_MACHINE, "Software\\HDRAutostart", "ConfigPath",
                        RRF_RT_REG_SZ, nullptr, buf, &sz) == ERROR_SUCCESS && buf[0];
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
        char cmd[1024] = {};  // needs MAX_PATH(260) + UNLEN(256) + ~80 literals in per-user branch
        if (IsAllUsersInstall()) {
            // All-users install: use PowerShell Register-ScheduledTask with -GroupId so the
            // task fires for EVERY user who logs on. schtasks without /ru, even when called
            // from an elevated token, stores the current user as principal — not "all users".
            // Users group by SID (S-1-5-32-545): its name is localized (BUILTIN\\Usuarios...).
            snprintf(cmd, sizeof(cmd),
                "powershell -NonInteractive -NoProfile -ExecutionPolicy Bypass -Command "
                "\"Register-ScheduledTask -TaskName 'HDRAutostart' "
                "-Action (New-ScheduledTaskAction -Execute '%s') "
                "-Trigger (New-ScheduledTaskTrigger -AtLogOn) "
                "-Principal (New-ScheduledTaskPrincipal -GroupId 'S-1-5-32-545' -RunLevel Highest) "
                "-Force\"",
                self);
        } else {
            // Per-user install: restrict task to this user only to avoid running
            // the task for other users who cannot access this user's AppData folders.
            char userName[256] = {};  DWORD userNameSize = sizeof(userName);
            GetUserNameA(userName, &userNameSize);
            snprintf(cmd, sizeof(cmd),
                "schtasks /create /tn HDRAutostart /tr \"\\\"%s\\\"\" /sc onlogon /ru \"%s\" /rl highest /f",
                self, userName);
        }
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

// Best profile for a process path: full path > executable name > folder prefix
// (the longest folder wins among folders). Returns false when no profile applies;
// 'out' is then left untouched.
static bool FindProfile(const std::string& path, GameProfile& out)
{
    std::string lo   = ToLower(path);
    std::string base = PathBase(lo);
    int    bestRank = 0;
    size_t bestLen  = 0;
    EnterCriticalSection(&g_cfgLock);
    for (auto& p : g_cfg.profiles) {
        int rank = MatchProfileEntry(p.exe, lo, base);
        if (!rank) continue;
        if (rank > bestRank || (rank == 1 && bestRank == 1 && p.exe.size() > bestLen)) {
            bestRank = rank;
            bestLen  = p.exe.size();
            out      = p;
        }
    }
    LeaveCriticalSection(&g_cfgLock);
    return bestRank > 0;
}

// Returns  1 = HDR game (profile with HDR enabled)
//          0 = no profile: ignore
//         -1 = SDR game (profile with HDR disabled: KTC dimming/sharpness/brightness only)
static int ClassifyProcess(const std::string& path)
{
    GameProfile prof;
    if (!FindProfile(path, prof)) return 0;
    return prof.hdr ? 1 : -1;
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
// of a KTC display (fullscreen video on any other monitor is ignored)
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

    if (!(wrc.left  <= mi.rcMonitor.left  &&
          wrc.top   <= mi.rcMonitor.top   &&
          wrc.right >= mi.rcMonitor.right &&
          wrc.bottom>= mi.rcMonitor.bottom)) return false;

    // Only a KTC monitor counts (checked last: it may need a slow DDC/CI probe)
    return IsKTCMonitor(hmon);
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
// 1 while HDR was switched on for a fullscreen browser video. Written by the tray thread
// (TIMER_BROWSER), read by MonitorThread: it must not touch the monitor in that state.
static volatile LONG g_browserHdrOn = 0;

// Grace period before turning HDR off once the last HDR game exits. A launcher
// closing and handing off to the real game would otherwise toggle HDR off/on.
static const DWORD kHdrOffGraceMs = 2000;

// Full image path of a process through a handle MonitorThread already holds ("" if unavailable)
static std::string HandlePath(HANDLE h)
{
    char path[MAX_PATH] = {};  DWORD sz = MAX_PATH;
    if (!QueryFullProcessImageNameA(h, 0, path, &sz)) return "";
    return path;
}

// The desktop values as a profile-shaped set (what the monitor returns to)
static GameProfile DesktopProfile()
{
    GameProfile p;
    EnterCriticalSection(&g_cfgLock);
    p.localDimming = g_cfg.ktcDimmingDesktop;
    p.sharpness    = g_cfg.ktcSharpnessDesktop;
    p.brightness   = g_cfg.ktcBrightnessDesktop;
    LeaveCriticalSection(&g_cfgLock);
    return p;
}

// Profile of a running game. If it was removed since the game was classified, the desktop
// values stand in for it (nothing game-specific is applied).
static GameProfile ProfileOrDesktop(const std::string& path)
{
    GameProfile p;
    if (!path.empty() && FindProfile(path, p)) return p;
    return DesktopProfile();
}

// Sends a set of KTC values: dimming (0 = leave alone), brightness (only when asked: HDR
// games are driven by the monitor itself) and sharpness (-1 = leave alone).
// afterHdrOff: an HDR -> SDR switch just happened, so wait for the monitor to settle and
// send sharpness with retries (the switch resets VCP 0x87).
static void ApplyProfileValues(const GameProfile& p, bool withBrightness, bool afterHdrOff)
{
    if (afterHdrOff) Sleep(500);
    SetKTCLocalDimming(p.localDimming);
    if (withBrightness) SetKTCBrightness(p.brightness);
    if (afterHdrOff) RestoreKTCSharpnessAfterHdrTransition(p.sharpness);
    else             SetKTCSharpness(p.sharpness);
}

// Back to the desktop: dimming, sharpness and brightness, always all three
static void ApplyDesktopValues(bool afterHdrOff)
{
    ApplyProfileValues(DesktopProfile(), true, afterHdrOff);
}

// HDR is off now and no HDR game is left: an SDR game still running (e.g. its profile was
// switched from HDR to SDR) gets its own values, otherwise the monitor goes back to the desktop.
// Returns true when an SDR game's values were applied.
static bool ApplyAfterHdrOff(const std::map<DWORD, HANDLE>& sdrGames)
{
    if (!sdrGames.empty()) {
        ApplyProfileValues(ProfileOrDesktop(HandlePath(sdrGames.begin()->second)), true, true);
        return true;
    }
    ApplyDesktopValues(true);
    return false;
}

static DWORD WINAPI MonitorThread(LPVOID)
{
    Log("Monitor started");
    std::map<DWORD, HANDLE> games;     // HDR games (profile with hdr=1)
    std::map<DWORD, HANDLE> sdrGames;  // SDR games (profile with hdr=0: KTC values only)
    bool hdrActive        = false;
    bool sdrDimmingActive = false;
    ULONGLONG hdrIdleSince = 0;     // tick when 'games' became empty (0 = not idle)
    bool hdrEnablePending = false;  // game HDR enable failed; main loop keeps retrying
    ULONGLONG hdrEnableLastTry = 0; // tick of the last enable attempt
    int hdrEnableTries = 0;         // late-retry attempts used so far
    const int kMaxEnableTries = 15; // ~30 s at one attempt per 2 s
    int hdrSessionSharpness = -1;   // effective HDR sharpness of this session (-1 = none)

    // Starts empty on purpose: the first scan also classifies processes that were
    // already running at startup (e.g. a game opened before the app launched).
    std::set<DWORD> seen;
    std::map<DWORD, int> openFails;  // pid -> consecutive OpenProcess failures
    const int kMaxOpenFails = 20;    // give up on a PID after this many failed opens
    bool startupChecked = false;  // one-time "HDR left on" check after the first full scan
    LONG profilesSeen = g_profilesGen;  // last profile-list generation this thread acted on
    LONG desktopSeen  = g_desktopGen;   // last desktop-settings generation this thread acted on

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

        // --- Profiles changed in the dialog: apply them to programs already running ---
        {
            LONG gen = g_profilesGen;
            if (gen != profilesSeen) {
                profilesSeen = gen;
                // Programs that had no profile get classified again by this iteration's scan
                seen.clear();
                openFails.clear();
                // Tracked games whose class changed (to 0, or between HDR and SDR) are dropped
                // WITHOUT going into 'seen': the scan below detects them again with their new
                // class, and the "closed" blocks below do the HDR/SDR transitions.
                // (A game that keeps its class gets its edited values re-sent below,
                // but only when it is the only game running.)
                for (int pass = 0; pass < 2; pass++) {
                    std::map<DWORD, HANDLE>& tracked = (pass == 0) ? games : sdrGames;
                    const int ownClass = (pass == 0) ? 1 : -1;
                    for (auto it = tracked.begin(); it != tracked.end(); ) {
                        std::string tp = HandlePath(it->second);
                        if (!tp.empty() && ClassifyProcess(tp) != ownClass) {
                            const char* tb = strrchr(tp.c_str(), '\\');
                            Log("Profile changed: %s (PID %lu) is no longer an %s game — classifying again",
                                tb ? tb + 1 : tp.c_str(), it->first, pass == 0 ? "HDR" : "SDR");
                            CloseHandle(it->second);
                            it = tracked.erase(it);
                        } else ++it;
                    }
                }
                // A profile edited while its game runs (same mode): send its values again.
                // Only for a single open game; with several, the edit applies from the next launch.
                // Not while an HDR browser video holds the monitor (patch: browser video and games
                // are not unified yet, see the pending item in CLAUDE.md).
                if (games.size() + sdrGames.size() == 1) {
                    bool hdrGame = !games.empty();
                    std::string tp = HandlePath(hdrGame ? games.begin()->second : sdrGames.begin()->second);
                    if (!tp.empty() && hdrGame == hdrActive && !g_browserHdrOn) {
                        GameProfile prof = ProfileOrDesktop(tp);
                        Log("Profile changed: sending the values of the running %s game again",
                            hdrGame ? "HDR" : "SDR");
                        ApplyProfileValues(prof, !hdrGame, false);
                        if (hdrGame) hdrSessionSharpness = prof.sharpness;
                    }
                }
            }
        }

        // --- All HDR games closed ---
        bool hdrOffDue = false;
        if (games.empty() && hdrActive) {
            if (hdrIdleSince == 0) {
                hdrIdleSince = GetTickCount64();
                Log("No HDR games running — waiting %lu ms before disabling HDR", kHdrOffGraceMs);
            }
            hdrOffDue = (GetTickCount64() - hdrIdleSince) >= kHdrOffGraceMs;
        } else {
            hdrIdleSince = 0;
        }
        if (hdrOffDue) {
            Log("All HDR games closed — disabling HDR");
            if (!SetHDRRetry(false)) Log("HDR disable FAILED after retries");
            EnterCriticalSection(&g_cfgLock);
            g_hdrSource[0] = '\0';
            LeaveCriticalSection(&g_cfgLock);
            // The monitor settles, then the remaining SDR game's values (or the desktop's) go out
            if (ApplyAfterHdrOff(sdrGames)) sdrDimmingActive = true;
            hdrActive = false;
            hdrEnablePending = false;
            hdrEnableTries = 0;
            hdrIdleSince = 0;
            if (g_trayWnd) PostMessage(g_trayWnd, WM_HDRSTATUS, 0, 0);
        }

        // --- Late retry of a failed HDR enable (single non-blocking attempt) ---
        if (hdrEnablePending && hdrActive && !games.empty() &&
            (GetTickCount64() - hdrEnableLastTry) >= 2000) {
            hdrEnableLastTry = GetTickCount64();
            bool noDisplay = false;
            if (SetHDR(true, &noDisplay)) {
                Log("HDR ENABLED (late retry)");
                hdrEnablePending = false;
                hdrEnableTries = 0;
                if (g_trayWnd) PostMessage(g_trayWnd, WM_HDRSTATUS, 1, 0);
                // The mode switch resets sharpness (VCP 0x87): re-apply it
                if (hdrSessionSharpness >= 0) {
                    Sleep(500);
                    SetKTCSharpness(hdrSessionSharpness);
                }
            } else if (++hdrEnableTries >= kMaxEnableTries) {
                // Keep hdrActive so closing the game still restores the KTC values that were sent
                Log("HDR enable still failing — giving up for this session");
                hdrEnablePending = false;
                hdrEnableTries = 0;
            }
        }

        // --- Snapshot new processes ---
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) continue;

        std::set<DWORD> alive;  // PIDs alive this scan — used to purge dead PIDs from 'seen'
        PROCESSENTRY32 pe = {};  pe.dwSize = sizeof(pe);
        if (Process32First(snap, &pe)) {
            do {
                DWORD pid = pe.th32ProcessID;
                alive.insert(pid);
                if (seen.count(pid) || games.count(pid) || sdrGames.count(pid)) continue;

                std::string path = GetProcessPath(pid);
                if (path.empty()) { seen.insert(pid); continue; }  // no accessible path — remember

                int cls = ClassifyProcess(path);
                if (cls == 0) { seen.insert(pid); continue; }      // not a game — remember

                const char* base = strrchr(path.c_str(), '\\');

                if (cls == 1) {
                    // HDR game
                    HANDLE hProc = OpenProcess(
                        SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
                    DWORD openErr = GetLastError();
                    if (!hProc) {
                        // Retry next tick (don't burn into 'seen'), but give up if it never opens
                        int fails = ++openFails[pid];
                        if (fails == 1)
                            Log("Cannot open process %s (PID %lu, err=%lu) — will retry",
                                base ? base + 1 : path.c_str(), pid, openErr);
                        if (fails >= kMaxOpenFails) {
                            Log("Giving up on %s (PID %lu) after %d failed open attempts",
                                base ? base + 1 : path.c_str(), pid, fails);
                            seen.insert(pid);
                            openFails.erase(pid);
                        }
                        continue;
                    }
                    openFails.erase(pid);
                    Log("Game detected: %s (PID %lu)", base ? base + 1 : path.c_str(), pid);

                    if (games.empty()) {
                        // The game's own values (the desktop's if its profile was removed since classification)
                        GameProfile prof = ProfileOrDesktop(path);
                        EnterCriticalSection(&g_cfgLock);
                        strncpy_s(g_hdrSource, base ? base + 1 : path.c_str(), _TRUNCATE);
                        LeaveCriticalSection(&g_cfgLock);

                        // Enable HDR
                        Log("Enabling HDR...");
                        bool noDisplay = false;
                        bool ok = SetHDRRetry(true, &noDisplay);
                        if (noDisplay) {
                            // KTC off or in a non-HDR mode: nothing to enable, nothing to restore later.
                            // The game still goes into 'games' below so it is not classified again.
                            Log("No KTC HDR display active — HDR not enabled for this game");
                        } else {
                            Log("HDR %s", ok ? "ENABLED" : "enable FAILED after retries");
                            // On failure keep retrying from the main loop (see "late retry")
                            hdrEnablePending     = !ok;
                            hdrEnableLastTry     = GetTickCount64();
                            hdrEnableTries       = 0;
                            hdrSessionSharpness  = prof.sharpness;

                            // Local dimming after HDR (KTC proprietary VCP — survives mode switch).
                            // No brightness: in HDR mode the monitor drives it.
                            SetKTCLocalDimming(prof.localDimming);
                            // Sharpness: VCP 0x87 gets reset by HDR mode switch.
                            // Wait for monitor to stabilize, then send.
                            if (prof.sharpness >= 0) {
                                Sleep(500);
                                SetKTCSharpness(prof.sharpness);
                            }

                            sdrDimmingActive = false;  // HDR takes precedence
                            hdrActive = true;
                            // Orange icon only once HDR is really on; a pending enable sends it on late success
                            if (ok && g_trayWnd) PostMessage(g_trayWnd, WM_HDRSTATUS, 1, 0);
                        }
                    }
                    games[pid] = hProc;

                } else if (cls == -1) {
                    // SDR game
                    HANDLE hProc = OpenProcess(
                        SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
                    DWORD openErr = GetLastError();
                    if (!hProc) {
                        // Retry next tick (don't burn into 'seen'), but give up if it never opens
                        int fails = ++openFails[pid];
                        if (fails == 1)
                            Log("Cannot open process %s (PID %lu, err=%lu) — will retry",
                                base ? base + 1 : path.c_str(), pid, openErr);
                        if (fails >= kMaxOpenFails) {
                            Log("Giving up on %s (PID %lu) after %d failed open attempts",
                                base ? base + 1 : path.c_str(), pid, fails);
                            seen.insert(pid);
                            openFails.erase(pid);
                        }
                        continue;
                    }
                    openFails.erase(pid);
                    Log("SDR game detected: %s (PID %lu)", base ? base + 1 : path.c_str(), pid);

                    if (sdrGames.empty() && !hdrActive) {
                        // Browser HDR video on: track the game but leave the monitor to the video
                        // (patch until browser video and games are unified)
                        if (!g_browserHdrOn) ApplyProfileValues(ProfileOrDesktop(path), true, false);
                        sdrDimmingActive = true;
                    }
                    sdrGames[pid] = hProc;
                }

            } while (Process32Next(snap, &pe));
        }
        CloseHandle(snap);

        // --- All SDR games closed ---
        // After the scan on purpose: a game whose profile just changed to HDR is detected above
        // (which clears sdrDimmingActive) before this runs, so no desktop values go out in between.
        // With a browser HDR video on, the state is updated but nothing is sent: the desktop values
        // go out when the video ends (patch until browser video and games are unified).
        if (sdrGames.empty() && sdrDimmingActive && !hdrActive) {
            if (g_browserHdrOn) {
                Log("All SDR games closed — browser HDR video active, desktop settings go out when it ends");
            } else {
                Log("All SDR games closed — restoring desktop settings");
                ApplyDesktopValues(false);
            }
            sdrDimmingActive = false;
        }

        // First completed scan: if HDR was left on by a previous run (app killed,
        // shutdown) and no game is running, turn it off once. The KTC values of the SDR game that
        // is open (or the desktop's) are sent afterwards, since the switch resets sharpness.
        if (!startupChecked) {
            startupChecked = true;
            if (games.empty() && !hdrActive && IsHDROn()) {
                Log("Startup: HDR was left on with no game running — disabling");
                if (!SetHDRRetry(false)) {
                    Log("HDR disable FAILED after retries");
                } else {
                    // The HDR -> SDR switch resets the monitor sharpness (VCP 0x87): send the
                    // values of the SDR game that is running, or the desktop's
                    ApplyAfterHdrOff(sdrGames);
                }
            }
        }

        // --- Desktop settings changed in the menu: send them now if nothing holds the monitor ---
        {
            LONG dg = g_desktopGen;
            if (dg != desktopSeen) {
                if (startupChecked && games.empty() && sdrGames.empty() && !hdrActive && !g_browserHdrOn) {
                    desktopSeen = dg;
                    Log("Desktop settings changed — applying them");
                    ApplyDesktopValues(false);
                } else if (hdrActive || sdrDimmingActive || g_browserHdrOn) {
                    // Something holds the monitor and sends the desktop values when it lets go
                    // (HDR off, last SDR game closed, browser video ended): nothing to do now
                    desktopSeen = dg;
                }
                // Otherwise (e.g. an HDR game is tracked but HDR never came on because there is no
                // KTC HDR display) the change stays pending and goes out as soon as nothing is
                // tracked. No DDC traffic meanwhile: the send condition above is simply false.
            }
        }

        // Purge dead PIDs from 'seen' so a recycled PID (e.g. a relaunched game)
        // is classified again instead of being skipped forever.
        for (auto it = seen.begin(); it != seen.end(); )
            if (!alive.count(*it)) it = seen.erase(it); else ++it;
        for (auto it = openFails.begin(); it != openFails.end(); )
            if (!alive.count(it->first)) it = openFails.erase(it); else ++it;
    }

    // Shutdown cleanup: back to the desktop values
    if (hdrActive) {
        if (!SetHDRRetry(false)) Log("HDR disable FAILED after retries");
        ApplyDesktopValues(true);
        if (g_trayWnd) PostMessage(g_trayWnd, WM_HDRSTATUS, 0, 0);
    } else if (sdrDimmingActive) {
        ApplyDesktopValues(false);
    }
    for (auto& kv : games)    CloseHandle(kv.second);
    for (auto& kv : sdrGames) CloseHandle(kv.second);
    Log("Monitor stopped");
    return 0;
}

// =============================================================================
// Auto-update  (background thread → WinHTTP + URLDownloadToFile)
// =============================================================================
#define WM_UPDATE_AVAILABLE (WM_APP + 3)
#define WM_MIGRATION_NOTICE (WM_APP + 4)   // startup balloon: old config migrated, folders discarded

struct UpdateInfo { char tag[64]; char dlUrl[512]; };

static bool IsNewerVersion(const char* remote)
{
    const char* r = (*remote == 'v' || *remote == 'V') ? remote + 1 : remote;
    int lMaj=0, lMin=0, lPat=0, rMaj=0, rMin=0, rPat=0;
    if (sscanf(APP_VERSION, "%d.%d.%d", &lMaj, &lMin, &lPat) < 2) return false;
    if (sscanf(r,           "%d.%d.%d", &rMaj, &rMin, &rPat) < 2) return false;
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

// True if this exe is the copy registered by the installer (InstallLocation in
// Add/Remove Programs). Portable copies must not auto-update: the silent installer
// would create a separate per-user install and leave the portable config behind.
static bool IsInstalledCopy()
{
    static const char* kUninst =
        "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\HDRAutostart";
    std::string self = ToLower(ExeDir());
    if (!self.empty() && self.back() == '\\') self.pop_back();
    // The NSIS installer is 32-bit, so its HKLM writes land in the 32-bit registry view.
    const struct { HKEY root; REGSAM view; } keys[] = {
        { HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY },
        { HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY },
        { HKEY_CURRENT_USER,  0 },
    };
    for (auto& k : keys) {
        HKEY h;
        if (RegOpenKeyExA(k.root, kUninst, 0, KEY_QUERY_VALUE | k.view, &h) != ERROR_SUCCESS) continue;
        char buf[MAX_PATH] = {};  DWORD sz = sizeof(buf) - 1, type = 0;
        LONG r = RegQueryValueExA(h, "InstallLocation", nullptr, &type, (BYTE*)buf, &sz);
        RegCloseKey(h);
        if (r != ERROR_SUCCESS || type != REG_SZ) continue;
        std::string loc = ToLower(buf);
        if (!loc.empty() && loc.back() == '\\') loc.pop_back();
        if (loc == self) return true;
    }
    return false;
}

static DWORD WINAPI UpdateCheckThread(LPVOID)
{
    if (!IsInstalledCopy()) {
        Log("Update check: skipped (portable copy, not registered by the installer)");
        return 0;
    }

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
#define ID_TRAY_ABOUT     199
#define ID_TRAY_STARTUP   203
#define ID_TRAY_EXIT      204
#define ID_TRAY_GITHUB    205
#define ID_TRAY_PROFILES  207
#define ID_DESK_SHARP     322
#define ID_DESK_BRIGHT    323
#define ID_VIDEO_SHARP    325
#define ID_DESK_DIM_0     330   // 330-334: desktop Local Dimming Don't change/Auto/Low/Std/High
#define ID_VIDEO_DIM_0    340   // 340-344: same for HDR video in a browser

#define ID_VIDEO_BROWSER  314

#define TIMER_BROWSER     1   // 500ms browser fullscreen check
#define TIMER_TRAY_RETRY  2   // 1s retry when Shell_NotifyIcon(NIM_ADD) fails at logon

static NOTIFYICONDATAA g_nid          = {};
static HICON           g_icoOff       = nullptr;
static HICON           g_icoOn        = nullptr;
static UINT            WM_TASKBARCREATED = 0;
static int             g_trayRetry    = 0;  // retry counter for NIM_ADD

// These are only accessed on the main (tray) thread — no lock needed
static bool g_gameHdrOn      = false;  // updated by WM_HDRSTATUS from monitor thread
static int  g_browserSkipTicks = 20;   // skip first 10s of browser checks at startup

static void UpdateTray(bool on)
{
    char src[MAX_PATH] = {};
    EnterCriticalSection(&g_cfgLock);
    strncpy_s(src, g_hdrSource, _TRUNCATE);
    LeaveCriticalSection(&g_cfgLock);
    if (on && src[0])
        snprintf(g_nid.szTip, sizeof(g_nid.szTip), "%s [%s]  v" APP_VERSION, L->tipOn, src);
    else
        snprintf(g_nid.szTip, sizeof(g_nid.szTip), "%s  v" APP_VERSION, on ? L->tipOn : L->tipOff);
    g_nid.hIcon = on ? g_icoOn : g_icoOff;
    Shell_NotifyIconA(NIM_MODIFY, &g_nid);
}

static void CheckBrowserHDR()
{
    // Skip the first ~10 seconds after startup to let the shell settle
    if (g_browserSkipTicks > 0) { --g_browserSkipTicks; return; }

    // Feature disabled — turn off browser HDR if it was on and bail
    {
        bool enabled;
        EnterCriticalSection(&g_cfgLock);
        enabled = g_cfg.browserHdrEnabled;
        LeaveCriticalSection(&g_cfgLock);
        if (!enabled) {
            if (g_browserHdrOn) {
                Log("Browser HDR disabled — disabling HDR");
                if (!SetHDRRetry(false)) Log("HDR disable FAILED after retries");
                EnterCriticalSection(&g_cfgLock);
                g_hdrSource[0] = '\0';
                LeaveCriticalSection(&g_cfgLock);
                ApplyDesktopValues(true);
                g_browserHdrOn = 0;
                UpdateTray(false);
            }
            return;
        }
    }

    // Game controls HDR while running — don't interfere
    if (g_gameHdrOn) {
        g_browserHdrOn = 0;
        return;
    }

    bool isFS = CheckBrowserFullscreen();

    // UI thread only: remembers a failed enable until this fullscreen session ends
    static bool s_enableFailedThisFS = false;
    if (!isFS) s_enableFailedThisFS = false;

    if (isFS && !g_browserHdrOn) {
        // Already failed during this fullscreen session: don't retry (or log) every 500 ms
        if (s_enableFailedThisFS) return;
        Log("Browser fullscreen — enabling HDR");
        int dimming, sharpVideo;
        EnterCriticalSection(&g_cfgLock);
        dimming    = g_cfg.videoDimming;
        sharpVideo = g_cfg.videoSharpness;
        LeaveCriticalSection(&g_cfgLock);
        bool noDisplay = false;
        // Raised BEFORE the switch: MonitorThread must not send desktop values in the middle of
        // this sequence (HDR on -> dimming -> sharpness). Lowered again if the enable fails.
        g_browserHdrOn = 1;
        if (!SetHDR(true, &noDisplay)) {
            Log(noDisplay ? "Browser HDR: no KTC HDR display active — not enabled"
                          : "Browser HDR: enable FAILED — not retrying until fullscreen ends");
            g_browserHdrOn = 0;
            s_enableFailedThisFS = true;
            return;
        }
        // Local dimming after HDR (KTC proprietary VCP — survives mode switch)
        SetKTCLocalDimming(dimming);
        // Sharpness: VCP 0x87 gets reset by HDR mode switch; wait then send
        if (sharpVideo >= 0) { Sleep(500); SetKTCSharpness(sharpVideo); }
        EnterCriticalSection(&g_cfgLock);
        strncpy_s(g_hdrSource, "Browser", _TRUNCATE);
        LeaveCriticalSection(&g_cfgLock);
        UpdateTray(true);
    } else if (!isFS && g_browserHdrOn) {
        Log("Browser left fullscreen — disabling HDR");
        if (!SetHDRRetry(false)) Log("HDR disable FAILED after retries");
        // Same path as the games: the desktop values go out after the monitor
        // finishes the HDR -> SDR transition.
        ApplyDesktopValues(true);
        EnterCriticalSection(&g_cfgLock);
        g_hdrSource[0] = '\0';
        LeaveCriticalSection(&g_cfgLock);
        g_browserHdrOn = 0;
        UpdateTray(false);
    }
}

// Turn off browser-activated HDR when the app exits (the window is destroyed
// right after, so no tray update here).
static void StopBrowserHDROnExit()
{
    if (!g_browserHdrOn) return;

    Log("Exit with browser HDR active — disabling HDR");
    if (!SetHDRRetry(false)) Log("HDR disable FAILED after retries");
    ApplyDesktopValues(true);
    EnterCriticalSection(&g_cfgLock);
    g_hdrSource[0] = '\0';
    LeaveCriticalSection(&g_cfgLock);
    g_browserHdrOn = 0;
}

// =============================================================================
// Sharpness combobox dialog
// =============================================================================
#define IDC_SHARP_COMBO  400
#define IDC_SHARP_OK     401
#define IDC_SHARP_CANCEL 402

struct SharpDlgData {
    int*  value;
    bool  desktop;       // a desktop setting: MonitorThread is told to apply it
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

        HWND hLbl = CreateWindowA("STATIC", L->profSharpShort,
            WS_CHILD | WS_VISIBLE, gap, gap + S(4), lw, S(20), hwnd, nullptr, nullptr, nullptr);
        SendMessageA(hLbl, WM_SETFONT, (WPARAM)d->hFont, FALSE);

        HWND hCbo = CreateWindowExA(0, "COMBOBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            gap + lw + gap, gap, cw, S(220), hwnd, (HMENU)IDC_SHARP_COMBO, nullptr, nullptr);
        SendMessageA(hCbo, WM_SETFONT, (WPARAM)d->hFont, FALSE);

        // "Don't change" (-1), then 0, 1, 2 ... 10
        { int idx = (int)SendMessageA(hCbo, CB_ADDSTRING, 0, (LPARAM)L->ktcKeep);
          SendMessageA(hCbo, CB_SETITEMDATA, idx, (LPARAM)(DWORD)-1); }
        int selIdx = 0;  // default to "Don't change" (index 0); updated below if value matches
        for (int v = 0; v <= 10; v++) {
            char buf[8]; snprintf(buf, sizeof(buf), "%d", v);
            int idx = (int)SendMessageA(hCbo, CB_ADDSTRING, 0, (LPARAM)buf);
            SendMessageA(hCbo, CB_SETITEMDATA, idx, (LPARAM)(DWORD)v);
            if (v == *d->value) selIdx = idx;
        }
        SendMessageA(hCbo, CB_SETCURSEL, selIdx, 0);

        int y2 = gap + S(36);
        HWND hOk  = CreateWindowA("BUTTON", L->btnOk, WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            gap,              y2, bw, bh, hwnd, (HMENU)IDC_SHARP_OK,     nullptr, nullptr);
        HWND hCan = CreateWindowA("BUTTON", L->btnCancel, WS_CHILD | WS_VISIBLE,
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
                int v = (int)(DWORD)SendMessageA(hCbo, CB_GETITEMDATA, sel, 0);
                EnterCriticalSection(&g_cfgLock);
                *d->value = v;
                LeaveCriticalSection(&g_cfgLock);
                SaveConfig();
                if (d->desktop) DesktopSettingsChanged();
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

static void ShowSharpnessDialog(const char* title, int* value, bool desktop = false)
{
    SharpDlgData* data = new SharpDlgData{value, desktop, nullptr};
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
// Numeric input dialog (generic, e.g. brightness 0-100)
// =============================================================================
#define IDC_NUM_EDIT   430
#define IDC_NUM_SPIN   431
#define IDC_NUM_OK     432
#define IDC_NUM_CANCEL 433

struct NumDlgData { int* value; int minV; int maxV; bool desktop; HFONT hFont; };

static LRESULT CALLBACK NumDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    NumDlgData* d = (NumDlgData*)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        d = (NumDlgData*)((CREATESTRUCTA*)lp)->lpCreateParams;
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)d);

        typedef UINT(WINAPI* PFN_GetDpiForWindow)(HWND);
        static auto pfnDpi = (PFN_GetDpiForWindow)GetProcAddress(GetModuleHandleA("user32.dll"), "GetDpiForWindow");
        UINT dpi = pfnDpi ? pfnDpi(hwnd) : 96;
        auto S = [&](int v){ return MulDiv(v, (int)dpi, 96); };

        NONCLIENTMETRICSA ncm = {}; ncm.cbSize = sizeof(ncm);
        SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        d->hFont = CreateFontIndirectA(&ncm.lfMessageFont);

        int gap = S(8), bw = S(80), bh = S(26), lw = S(120), ew = S(60), sw = S(18);

        char rangeLabel[64];
        snprintf(rangeLabel, sizeof(rangeLabel), L->numValueFmt, d->minV, d->maxV);
        HWND hLbl = CreateWindowA("STATIC", rangeLabel,
            WS_CHILD | WS_VISIBLE, gap, gap + S(4), lw, S(20), hwnd, nullptr, nullptr, nullptr);
        SendMessageA(hLbl, WM_SETFONT, (WPARAM)d->hFont, FALSE);

        HWND hEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER,
            gap + lw + gap, gap, ew, S(22), hwnd, (HMENU)IDC_NUM_EDIT, nullptr, nullptr);
        SendMessageA(hEdit, WM_SETFONT, (WPARAM)d->hFont, FALSE);
        char buf[8]; snprintf(buf, sizeof(buf), "%d", *d->value);
        SetWindowTextA(hEdit, buf);

        HWND hSpin = CreateWindowExA(0, UPDOWN_CLASSA, nullptr,
            WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS,
            0, 0, sw, S(22), hwnd, (HMENU)IDC_NUM_SPIN, nullptr, nullptr);
        SendMessageA(hSpin, UDM_SETBUDDY,  (WPARAM)hEdit, 0);
        SendMessageA(hSpin, UDM_SETRANGE32, d->minV, d->maxV);
        SendMessageA(hSpin, UDM_SETPOS32,   0, *d->value);

        int y2 = gap + S(36);
        HWND hOk  = CreateWindowA("BUTTON", L->btnOk, WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            gap,            y2, bw, bh, hwnd, (HMENU)IDC_NUM_OK,     nullptr, nullptr);
        HWND hCan = CreateWindowA("BUTTON", L->btnCancel, WS_CHILD | WS_VISIBLE,
            gap + bw + gap, y2, bw, bh, hwnd, (HMENU)IDC_NUM_CANCEL, nullptr, nullptr);
        SendMessageA(hOk,  WM_SETFONT, (WPARAM)d->hFont, FALSE);
        SendMessageA(hCan, WM_SETFONT, (WPARAM)d->hFont, FALSE);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_NUM_OK) {
            char buf[16] = {};
            GetWindowTextA(GetDlgItem(hwnd, IDC_NUM_EDIT), buf, sizeof(buf));
            if (buf[0]) {  // empty field: keep the current value (atoi("") would store 0)
                int v = atoi(buf);
                if (v < d->minV) v = d->minV;
                if (v > d->maxV) v = d->maxV;
                EnterCriticalSection(&g_cfgLock);
                *d->value = v;
                LeaveCriticalSection(&g_cfgLock);
                SaveConfig();
                if (d->desktop) DesktopSettingsChanged();
            }
            DestroyWindow(hwnd);
        } else if (LOWORD(wp) == IDC_NUM_CANCEL) {
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

static void ShowNumDialog(const char* title, int* value, int minV, int maxV, bool desktop = false)
{
    NumDlgData* data = new NumDlgData{value, minV, maxV, desktop, nullptr};
    HINSTANCE hInst = (HINSTANCE)GetModuleHandleA(nullptr);

    typedef UINT(WINAPI* PFN_GetDpiForSystem)();
    static auto pfnDpiSys = (PFN_GetDpiForSystem)GetProcAddress(GetModuleHandleA("user32.dll"), "GetDpiForSystem");
    UINT dpi = pfnDpiSys ? pfnDpiSys() : 96;
    int W = MulDiv(280, (int)dpi, 96);
    int H = MulDiv(110, (int)dpi, 96);

    HWND hw = CreateWindowExA(
        WS_EX_TOPMOST,
        "HDRAutostartNumDlg", title,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, W, H,
        nullptr, nullptr, hInst, data);
    if (hw) SetForegroundWindow(hw);
    else    delete data;
}

// =============================================================================
// Per-profile edit dialog
// =============================================================================
#define IDC_PROF_DIM_COMBO     410
#define IDC_PROF_SHARP_COMBO   411
#define IDC_PROF_OK            412
#define IDC_PROF_CANCEL        413
#define IDC_PROF_HDR_CHECK     414
#define IDC_PROF_BRIGHT_LABEL  415
#define IDC_PROF_BRIGHT_EDIT   416
#define IDC_PROF_BRIGHT_SPIN   417

struct ProfEditData {
    int  dimming;
    int  sharpness;
    int  brightness;
    bool hdr;
    bool ok;
    HFONT hFont;
};

// Brightness only matters to SDR games: grey the field out while the HDR box is checked
static void ProfEditUpdateBrightness(HWND hwnd)
{
    BOOL on = SendDlgItemMessageA(hwnd, IDC_PROF_HDR_CHECK, BM_GETCHECK, 0, 0) != BST_CHECKED;
    EnableWindow(GetDlgItem(hwnd, IDC_PROF_BRIGHT_LABEL), on);
    EnableWindow(GetDlgItem(hwnd, IDC_PROF_BRIGHT_EDIT),  on);
    EnableWindow(GetDlgItem(hwnd, IDC_PROF_BRIGHT_SPIN),  on);
}

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

        int gap = S(8), bw = S(80), bh = S(26), lw = S(130), cw = S(150), ew = S(60), sw = S(18);

        // Row 0 — HDR on/off for this game
        RECT rc; GetClientRect(hwnd, &rc);
        HWND hHdr = CreateWindowA("BUTTON", L->profHdrCheck,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            gap, gap, rc.right - gap * 2, S(22), hwnd, (HMENU)IDC_PROF_HDR_CHECK, nullptr, nullptr);
        SendMessageA(hHdr, WM_SETFONT, (WPARAM)d->hFont, FALSE);
        SendMessageA(hHdr, BM_SETCHECK, d->hdr ? BST_CHECKED : BST_UNCHECKED, 0);

        // Row 1 — Local Dimming
        int row1 = gap + S(30);
        HWND hL1 = CreateWindowA("STATIC", L->profDimField,
            WS_CHILD | WS_VISIBLE, gap, row1 + S(4), lw, S(20), hwnd, nullptr, nullptr, nullptr);
        SendMessageA(hL1, WM_SETFONT, (WPARAM)d->hFont, FALSE);
        HWND hC1 = CreateWindowExA(0, "COMBOBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            gap + lw + gap, row1, cw, S(160), hwnd, (HMENU)IDC_PROF_DIM_COMBO, nullptr, nullptr);
        SendMessageA(hC1, WM_SETFONT, (WPARAM)d->hFont, FALSE);
        {
            const char* const dimLabels[] = { L->ktcKeep, L->ktcAuto, L->ktcLow, L->ktcStd, L->ktcHigh };
            int selDim = 1;  // Auto
            for (int i = 0; i < 5; i++) {
                int idx = (int)SendMessageA(hC1, CB_ADDSTRING, 0, (LPARAM)dimLabels[i]);
                SendMessageA(hC1, CB_SETITEMDATA, idx, (LPARAM)(DWORD)i);
                if (i == d->dimming) selDim = idx;
            }
            SendMessageA(hC1, CB_SETCURSEL, selDim, 0);
        }

        // Row 2 — Sharpness
        int row2 = row1 + S(36);
        HWND hL2 = CreateWindowA("STATIC", L->profSharpShort,
            WS_CHILD | WS_VISIBLE, gap, row2 + S(4), lw, S(20), hwnd, nullptr, nullptr, nullptr);
        SendMessageA(hL2, WM_SETFONT, (WPARAM)d->hFont, FALSE);
        HWND hC2 = CreateWindowExA(0, "COMBOBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            gap + lw + gap, row2, cw, S(220), hwnd, (HMENU)IDC_PROF_SHARP_COMBO, nullptr, nullptr);
        SendMessageA(hC2, WM_SETFONT, (WPARAM)d->hFont, FALSE);
        {
            // "Don't change" (-1: send nothing to the monitor), then 0 .. 10
            int idxOff = (int)SendMessageA(hC2, CB_ADDSTRING, 0, (LPARAM)L->ktcKeep);
            SendMessageA(hC2, CB_SETITEMDATA, idxOff, (LPARAM)(DWORD)-1);
            int selSharp = idxOff;
            for (int v = 0; v <= 10; v++) {
                char buf[8]; snprintf(buf, sizeof(buf), "%d", v);
                int idx = (int)SendMessageA(hC2, CB_ADDSTRING, 0, (LPARAM)buf);
                SendMessageA(hC2, CB_SETITEMDATA, idx, (LPARAM)(DWORD)v);
                if (v == d->sharpness) selSharp = idx;
            }
            SendMessageA(hC2, CB_SETCURSEL, selSharp, 0);
        }

        // Row 3 — Brightness (SDR games only)
        int row3 = row2 + S(36);
        HWND hL3 = CreateWindowA("STATIC", L->profBrightField,
            WS_CHILD | WS_VISIBLE, gap, row3 + S(4), lw, S(20), hwnd, (HMENU)IDC_PROF_BRIGHT_LABEL, nullptr, nullptr);
        SendMessageA(hL3, WM_SETFONT, (WPARAM)d->hFont, FALSE);
        HWND hE3 = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER,
            gap + lw + gap, row3, ew, S(22), hwnd, (HMENU)IDC_PROF_BRIGHT_EDIT, nullptr, nullptr);
        SendMessageA(hE3, WM_SETFONT, (WPARAM)d->hFont, FALSE);
        SendMessageA(hE3, EM_LIMITTEXT, 3, 0);
        HWND hSp = CreateWindowExA(0, UPDOWN_CLASSA, nullptr,
            WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS,
            0, 0, sw, S(22), hwnd, (HMENU)IDC_PROF_BRIGHT_SPIN, nullptr, nullptr);
        SendMessageA(hSp, UDM_SETBUDDY,   (WPARAM)hE3, 0);
        SendMessageA(hSp, UDM_SETRANGE32, 0, 100);
        SendMessageA(hSp, UDM_SETPOS32,   0, d->brightness);

        int y4 = row3 + S(36);
        HWND hOk  = CreateWindowA("BUTTON", L->btnOk, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            gap,              y4, bw, bh, hwnd, (HMENU)IDC_PROF_OK,     nullptr, nullptr);
        HWND hCan = CreateWindowA("BUTTON", L->btnCancel, WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            gap + bw + gap,   y4, bw, bh, hwnd, (HMENU)IDC_PROF_CANCEL, nullptr, nullptr);
        SendMessageA(hOk,  WM_SETFONT, (WPARAM)d->hFont, FALSE);
        SendMessageA(hCan, WM_SETFONT, (WPARAM)d->hFont, FALSE);

        ProfEditUpdateBrightness(hwnd);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_PROF_HDR_CHECK) {
            ProfEditUpdateBrightness(hwnd);
        } else if (LOWORD(wp) == IDC_PROF_OK || LOWORD(wp) == IDOK) {  // IDOK: Enter via IsDialogMessage
            HWND hC1 = GetDlgItem(hwnd, IDC_PROF_DIM_COMBO);
            HWND hC2 = GetDlgItem(hwnd, IDC_PROF_SHARP_COMBO);
            int s1 = (int)SendMessageA(hC1, CB_GETCURSEL, 0, 0);
            int s2 = (int)SendMessageA(hC2, CB_GETCURSEL, 0, 0);
            bool hdr = SendDlgItemMessageA(hwnd, IDC_PROF_HDR_CHECK, BM_GETCHECK, 0, 0) == BST_CHECKED;
            int bright = d->brightness;  // HDR profile: brightness is not used, keep what it had
            if (!hdr) {
                char buf[16] = {};
                GetWindowTextA(GetDlgItem(hwnd, IDC_PROF_BRIGHT_EDIT), buf, sizeof(buf));
                if (!buf[0]) {  // empty is not 0: ask for a value instead of saving one
                    MessageBeep(MB_ICONWARNING);
                    SetFocus(GetDlgItem(hwnd, IDC_PROF_BRIGHT_EDIT));
                    return 0;
                }
                bright = atoi(buf);
                if (bright < 0)   bright = 0;
                if (bright > 100) bright = 100;
            }
            if (s1 != CB_ERR && s2 != CB_ERR) {
                d->dimming    = (int)(DWORD)SendMessageA(hC1, CB_GETITEMDATA, s1, 0);
                d->sharpness  = (int)(DWORD)SendMessageA(hC2, CB_GETITEMDATA, s2, 0);
                d->brightness = bright;
                d->hdr        = hdr;
                d->ok         = true;
            }
            DestroyWindow(hwnd);
        } else if (LOWORD(wp) == IDC_PROF_CANCEL || LOWORD(wp) == IDCANCEL) {
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
static bool RunProfEditDialog(HWND parent, int& dimming, int& sharpness, int& brightness, bool& hdr)
{
    ProfEditData data;
    data.dimming    = dimming;
    data.sharpness  = sharpness;
    data.brightness = brightness;
    data.hdr        = hdr;
    data.ok         = false;
    data.hFont      = nullptr;

    HINSTANCE hInst = (HINSTANCE)GetModuleHandleA(nullptr);
    typedef UINT(WINAPI* PFN_GetDpiForSystem)();
    static auto pfnDpiSys = (PFN_GetDpiForSystem)GetProcAddress(GetModuleHandleA("user32.dll"), "GetDpiForSystem");
    UINT dpi = pfnDpiSys ? pfnDpiSys() : 96;
    int W = MulDiv(330, (int)dpi, 96);
    int H = MulDiv(225, (int)dpi, 96);

    HWND hw = CreateWindowExA(
        WS_EX_TOPMOST,
        "HDRAutostartProfEditDlg", L->dlgProfEdit,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, W, H,
        parent, nullptr, hInst, &data);
    if (!hw) return false;
    SetForegroundWindow(hw);
    SetFocus(GetDlgItem(hw, IDC_PROF_HDR_CHECK));
    EnableWindow(parent, FALSE);
    MSG m = {};
    while (IsWindow(hw) && GetMessageA(&m, nullptr, 0, 0) > 0) {
        // Tab / Shift+Tab / Enter / Esc between the controls
        if (!IsDialogMessageA(hw, &m)) {
            TranslateMessage(&m);
            DispatchMessageA(&m);
        }
    }
    if (IsWindow(hw)) {
        // The app is quitting (tray Exit) while this dialog is open: this nested loop
        // just consumed WM_QUIT. Close the dialog while 'data' is still alive and
        // re-post the quit so the main loop in WinMain also ends.
        DestroyWindow(hw);
        PostQuitMessage((int)m.wParam);
    }
    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    if (data.ok) { dimming = data.dimming; sharpness = data.sharpness; brightness = data.brightness; hdr = data.hdr; }
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

// Shortens a folder path (with trailing backslash) for display: more than 4 components
// become "drive\...\parent\folder\"; short paths are shown whole
static std::string AbbrevFolder(const std::string& folder)
{
    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t i = 0; i < folder.size(); i++)
        if (folder[i] == '\\') { parts.push_back(folder.substr(start, i - start)); start = i + 1; }
    if (parts.size() <= 4) return folder;
    size_t n = parts.size();
    return parts[0] + "\\...\\" + parts[n - 2] + "\\" + parts[n - 1] + "\\";
}

static void PopulateProfilesList(HWND lb)
{
    SendMessageA(lb, LB_RESETCONTENT, 0, 0);
    const char* const dimNames[] = { L->ktcKeep, L->ktcAuto, L->ktcLow, L->ktcStd, L->ktcHigh };
    EnterCriticalSection(&g_cfgLock);
    for (auto& p : g_cfg.profiles) {
        // Three entry kinds, each shown differently so two profiles never look alike:
        //   full path -> "name.exe  (c:\...\folder\)"   name only -> "name.exe  (any folder)"
        //   folder    -> "[folder] c:\games\"
        std::string name;
        if (!p.exe.empty() && p.exe.back() == '\\') {
            name = std::string(L->profTagFolder) + " " + p.exe;
        } else if (p.exe.find('\\') == std::string::npos) {
            name = p.exe + "  " + L->profTagName;
        } else {
            name = PathBase(p.exe) + "  (" + AbbrevFolder(p.exe.substr(0, p.exe.size() - PathBase(p.exe).size())) + ")";
        }
        char sharp_str[16];
        if (p.sharpness < 0) snprintf(sharp_str, sizeof(sharp_str), "%s", L->ktcKeep);
        else                 snprintf(sharp_str, sizeof(sharp_str), "%d", p.sharpness);
        char entry[MAX_PATH + 256];
        int len = snprintf(entry, sizeof(entry), "%s  [%s]  %s %s  %s %s", name.c_str(),
                           p.hdr ? "HDR" : "SDR", L->profDimLabel,
                           dimNames[p.localDimming >= 0 && p.localDimming <= 4 ? p.localDimming : 0],
                           L->profSharpShort, sharp_str);
        // Brightness only applies to SDR games
        if (!p.hdr && len > 0 && len < (int)sizeof(entry))
            snprintf(entry + len, sizeof(entry) - len, "  %s %d", L->profBrightShort, p.brightness);
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
                // A profile for this exe already exists: edit it instead of adding a duplicate
                std::string exe = ToLower(std::string(path));
                // New profiles start with HDR on and inherit the desktop (general) monitor
                // values as a base: dimming, sharpness and brightness. The user edits from there.
                int dimming = 1, sharpness = 6, brightness = 100;
                bool hdr = true;
                bool sameExe = false;
                EnterCriticalSection(&g_cfgLock);
                dimming    = g_cfg.ktcDimmingDesktop;
                sharpness  = g_cfg.ktcSharpnessDesktop;
                brightness = g_cfg.ktcBrightnessDesktop;
                for (auto& q : g_cfg.profiles)
                    if (q.exe == exe) {
                        dimming = q.localDimming; sharpness = q.sharpness; brightness = q.brightness;
                        hdr = q.hdr; sameExe = true; break;
                    }
                LeaveCriticalSection(&g_cfgLock);
                if (!sameExe) {
                    // No profile for this exact path, but a name or folder profile already covers it:
                    // start from its values (OK creates a new, more specific full-path profile)
                    GameProfile cover;
                    if (FindProfile(exe, cover)) {
                        dimming = cover.localDimming; sharpness = cover.sharpness;
                        brightness = cover.brightness; hdr = cover.hdr;
                    }
                }
                if (RunProfEditDialog(hwnd, dimming, sharpness, brightness, hdr)) {
                    int idx = -1;
                    EnterCriticalSection(&g_cfgLock);
                    // Look again: the list may have changed while the dialog was open
                    for (size_t i = 0; i < g_cfg.profiles.size(); i++)
                        if (g_cfg.profiles[i].exe == exe) { idx = (int)i; break; }
                    if (idx < 0) {
                        GameProfile gp;
                        gp.exe = exe;
                        g_cfg.profiles.push_back(gp);
                        idx = (int)g_cfg.profiles.size() - 1;
                    }
                    g_cfg.profiles[idx].localDimming = dimming;
                    g_cfg.profiles[idx].sharpness    = sharpness;
                    g_cfg.profiles[idx].brightness   = brightness;
                    g_cfg.profiles[idx].hdr          = hdr;
                    LeaveCriticalSection(&g_cfgLock);
                    InterlockedIncrement(&g_profilesGen);
                    SaveConfig();
                    PopulateProfilesList(lb);
                    SendMessageA(lb, LB_SETCURSEL, idx, 0);
                }
            }
        } else if (LOWORD(wp) == IDC_PROF_EDIT ||
                   (LOWORD(wp) == IDC_PROF_LIST && HIWORD(wp) == LBN_DBLCLK)) {
            int sel = (int)SendMessageA(lb, LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR) {
                EnterCriticalSection(&g_cfgLock);
                bool valid = sel < (int)g_cfg.profiles.size();
                int dimming   = valid ? g_cfg.profiles[sel].localDimming : 1;
                int sharpness = valid ? g_cfg.profiles[sel].sharpness    : -1;
                int brightness = valid ? g_cfg.profiles[sel].brightness  : 100;
                bool hdr      = valid ? g_cfg.profiles[sel].hdr          : true;
                std::string exe = valid ? g_cfg.profiles[sel].exe        : std::string();
                LeaveCriticalSection(&g_cfgLock);
                if (valid && RunProfEditDialog(hwnd, dimming, sharpness, brightness, hdr)) {
                    EnterCriticalSection(&g_cfgLock);
                    // The list may have changed while the dialog was open (second
                    // Profiles window): only write if this is still the same entry.
                    if (sel < (int)g_cfg.profiles.size() && g_cfg.profiles[sel].exe == exe) {
                        g_cfg.profiles[sel].localDimming = dimming;
                        g_cfg.profiles[sel].sharpness    = sharpness;
                        g_cfg.profiles[sel].brightness   = brightness;
                        g_cfg.profiles[sel].hdr          = hdr;
                    }
                    LeaveCriticalSection(&g_cfgLock);
                    InterlockedIncrement(&g_profilesGen);
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
                InterlockedIncrement(&g_profilesGen);
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
    // One profiles window at a time: bring the existing one forward
    HWND existing = FindWindowA("HDRAutostartProfilesDlg", nullptr);
    if (existing) {
        ShowWindow(existing, SW_RESTORE);
        SetForegroundWindow(existing);
        return;
    }
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

// Local Dimming submenu: Don't change, Auto, Low, Standard, High (command ids baseId + 0..4, current one checked)
static HMENU BuildDimMenu(int current, int baseId)
{
    HMENU sub = CreatePopupMenu();
    AppendMenuA(sub, MF_STRING | (current == 0 ? MF_CHECKED : 0u), baseId + 0, L->ktcKeep);
    AppendMenuA(sub, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(sub, MF_STRING | (current == 1 ? MF_CHECKED : 0u), baseId + 1, L->ktcAuto);
    AppendMenuA(sub, MF_STRING | (current == 2 ? MF_CHECKED : 0u), baseId + 2, L->ktcLow);
    AppendMenuA(sub, MF_STRING | (current == 3 ? MF_CHECKED : 0u), baseId + 3, L->ktcStd);
    AppendMenuA(sub, MF_STRING | (current == 4 ? MF_CHECKED : 0u), baseId + 4, L->ktcHigh);
    return sub;
}

// Sharpness value for a menu label: the number, or "Don't change" for -1 (nothing is sent)
static void SharpnessText(char* out, size_t n, int v)
{
    if (v < 0) snprintf(out, n, "%s", L->ktcKeep);
    else       snprintf(out, n, "%d", v);
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
        HANDLE ht = CreateThread(nullptr, 0, DoSilentUpdate, da, 0, nullptr);
        if (ht) CloseHandle(ht);
        else    delete da;

        delete info;
        return 0;
    }

    if (msg == WM_MIGRATION_NOTICE) {
        g_migratedDroppedFolders = false;  // once
        g_nid.uFlags |= NIF_INFO;
        snprintf(g_nid.szInfo,      sizeof(g_nid.szInfo),      "%s", L->msgFoldersDropped);
        snprintf(g_nid.szInfoTitle, sizeof(g_nid.szInfoTitle), "HDRAutostart");
        g_nid.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
        Shell_NotifyIconA(NIM_MODIFY, &g_nid);
        g_nid.uFlags &= ~NIF_INFO;
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
            if (Shell_NotifyIconA(NIM_ADD, &g_nid)) {
                KillTimer(hwnd, TIMER_TRAY_RETRY);
                if (g_migratedDroppedFolders) PostMessageA(hwnd, WM_MIGRATION_NOTICE, 0, 0);
            } else if (++g_trayRetry >= 30) {
                KillTimer(hwnd, TIMER_TRAY_RETRY);
            }
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
            // Game profiles are the main entry point: a game is only watched once it has a profile
            AppendMenuA(m, MF_STRING, ID_TRAY_PROFILES, L->menuProfiles);
            AppendMenuA(m, MF_SEPARATOR, 0, nullptr);

            int deskDim, deskSharp, deskBright, vidDim, vidSharp;
            bool browserHdrOn;
            EnterCriticalSection(&g_cfgLock);
            deskDim      = g_cfg.ktcDimmingDesktop;
            deskSharp    = g_cfg.ktcSharpnessDesktop;
            deskBright   = g_cfg.ktcBrightnessDesktop;
            vidDim       = g_cfg.videoDimming;
            vidSharp     = g_cfg.videoSharpness;
            browserHdrOn = g_cfg.browserHdrEnabled;
            LeaveCriticalSection(&g_cfgLock);

            char sharpTxt[16], lbl[96];

            // Desktop submenu: what the monitor returns to when no game is running
            HMENU deskMenu = CreatePopupMenu();
            AppendMenuA(deskMenu, MF_POPUP, (UINT_PTR)BuildDimMenu(deskDim, ID_DESK_DIM_0), L->menuLocalDimming);
            SharpnessText(sharpTxt, sizeof(sharpTxt), deskSharp);
            snprintf(lbl, sizeof(lbl), "%s: %s...", L->menuSharpness, sharpTxt);
            AppendMenuA(deskMenu, MF_STRING, ID_DESK_SHARP, lbl);
            snprintf(lbl, sizeof(lbl), "%s: %d...", L->menuBrightness, deskBright);
            AppendMenuA(deskMenu, MF_STRING, ID_DESK_BRIGHT, lbl);
            AppendMenuA(m, MF_POPUP, (UINT_PTR)deskMenu, L->menuDesktop);

            // Video submenu: HDR for fullscreen browser video and the values it uses
            HMENU videoMenu = CreatePopupMenu();
            AppendMenuA(videoMenu, MF_STRING | (browserHdrOn ? MF_CHECKED : 0u),
                        ID_VIDEO_BROWSER, L->menuVideoBrowser);
            AppendMenuA(videoMenu, MF_POPUP, (UINT_PTR)BuildDimMenu(vidDim, ID_VIDEO_DIM_0), L->menuLocalDimming);
            SharpnessText(sharpTxt, sizeof(sharpTxt), vidSharp);
            snprintf(lbl, sizeof(lbl), "%s: %s...", L->menuSharpness, sharpTxt);
            AppendMenuA(videoMenu, MF_STRING, ID_VIDEO_SHARP, lbl);
            AppendMenuA(m, MF_POPUP, (UINT_PTR)videoMenu, L->menuVideo);
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
        case ID_TRAY_STARTUP:
            SetStartup(!IsInStartup());  break;
        case ID_DESK_SHARP: {
            char title[96];
            snprintf(title, sizeof(title), "%s - %s", L->menuSharpness, L->menuDesktop);
            ShowSharpnessDialog(title, &g_cfg.ktcSharpnessDesktop, true);
            break;
        }
        case ID_DESK_BRIGHT: {
            char title[96];
            snprintf(title, sizeof(title), "%s - %s", L->menuBrightness, L->menuDesktop);
            ShowNumDialog(title, &g_cfg.ktcBrightnessDesktop, 0, 100, true);
            break;
        }
        case ID_VIDEO_SHARP: {
            char title[96];
            snprintf(title, sizeof(title), "%s - %s", L->menuSharpness, L->menuVideo);
            ShowSharpnessDialog(title, &g_cfg.videoSharpness);
            break;
        }
        case ID_TRAY_PROFILES:
            ShowProfilesDialog(); break;
        case ID_VIDEO_BROWSER:
            { EnterCriticalSection(&g_cfgLock); g_cfg.browserHdrEnabled = !g_cfg.browserHdrEnabled; LeaveCriticalSection(&g_cfgLock); SaveConfig(); } break;
        case ID_TRAY_GITHUB: {
            // Open the URL with the Explorer (non-elevated) token so a browser started by
            // this click doesn't run as administrator. Fall back to ShellExecute on failure.
            const char* url = "https://github.com/conecta6/HDRAutostart-W11";
            char sysDir[MAX_PATH] = {};
            char cmd[MAX_PATH + 128] = {};
            bool launched = false;
            if (GetSystemDirectoryA(sysDir, MAX_PATH) > 0) {
                snprintf(cmd, sizeof(cmd), "\"%s\\rundll32.exe\" url.dll,FileProtocolHandler %s", sysDir, url);
                launched = RunAsShellUser(cmd, 0);  // don't block the UI thread
            }
            if (!launched)
                ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
            break;
        }
        case ID_TRAY_EXIT:
            KillTimer(hwnd, TIMER_BROWSER);
            StopBrowserHDROnExit();
            SetEvent(g_stopEvent);
            DestroyWindow(hwnd);
            break;
        default: {
            int id = LOWORD(wp);
            if (id >= ID_DESK_DIM_0 && id <= ID_DESK_DIM_0 + 4) {
                EnterCriticalSection(&g_cfgLock);
                g_cfg.ktcDimmingDesktop = id - ID_DESK_DIM_0;
                LeaveCriticalSection(&g_cfgLock);
                SaveConfig();
                DesktopSettingsChanged();
            } else if (id >= ID_VIDEO_DIM_0 && id <= ID_VIDEO_DIM_0 + 4) {
                EnterCriticalSection(&g_cfgLock);
                g_cfg.videoDimming = id - ID_VIDEO_DIM_0;
                LeaveCriticalSection(&g_cfgLock);
                SaveConfig();
            }
            break;
        }
        }
        return 0;

    case WM_DISPLAYCHANGE:
        // Monitors added/removed/reconfigured: re-evaluate which ones are KTC
        InvalidateKTCMonitorCache();
        break;  // fall through to DefWindowProcA below

    case WM_DESTROY:
        // Also covers exits that bypass ID_TRAY_EXIT: don't leave browser HDR on
        KillTimer(hwnd, TIMER_BROWSER);
        StopBrowserHDROnExit();
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
    InitializeCriticalSection(&g_ktcCacheLock);
    OpenLog();  // before LoadConfig so the config migration can be logged
    Log("=== HDRAutostart started ===");
    LoadConfig();
    // Reset update timestamp only if it's older than 24h (prevents install-loop:
    // installer relaunches the app which would re-trigger the update immediately)
    if (g_cfg.lastUpdateAttempt != 0 &&
        (time(nullptr) - g_cfg.lastUpdateAttempt) > 86400)
        g_cfg.lastUpdateAttempt = 0;
    // NVAPI is not loaded at startup: SetNVAPIVCP is unused, and loading nvapi64.dll
    // by name from an elevated process searches the exe folder first.

    WM_TASKBARCREATED = RegisterWindowMessageA("TaskbarCreated");

    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(wc);  wc.hInstance = hInst;
    wc.lpszClassName = "HDRAutostartTray";  wc.lpfnWndProc = TrayWndProc;
    RegisterClassExA(&wc);

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

    WNDCLASSEXA wc6 = {};
    wc6.cbSize = sizeof(wc6);  wc6.hInstance = hInst;
    wc6.lpszClassName = "HDRAutostartNumDlg";  wc6.lpfnWndProc = NumDlgProc;
    wc6.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassExA(&wc6);

    // Hidden top-level window (not HWND_MESSAGE): message-only windows never receive
    // the "TaskbarCreated" broadcast, so the icon was lost when Explorer restarted.
    g_trayWnd = CreateWindowExA(0, "HDRAutostartTray", "HDRAutostart", 0,
        0, 0, 0, 0, nullptr, nullptr, hInst, nullptr);
    // We run elevated: let the (non-elevated) shell deliver that broadcast to us.
    ChangeWindowMessageFilterEx(g_trayWnd, WM_TASKBARCREATED, MSGFLT_ALLOW, nullptr);

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
    else if (g_migratedDroppedFolders)
        PostMessageA(g_trayWnd, WM_MIGRATION_NOTICE, 0, 0);    // icon is in: show the one-time notice

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
    DeleteCriticalSection(&g_ktcCacheLock);
    DeleteCriticalSection(&g_cfgLock);
    if (g_log) fclose(g_log);
    CoUninitialize();
    CloseHandle(mutex);
    return 0;
}
