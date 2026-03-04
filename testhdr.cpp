// =============================================================================
// testhdr.cpp
// Windows HDR Timing Experiment
//
// PURPOSE
//   Determine whether enabling HDR *before* a game's graphics swapchain
//   initialization causes the game (and features like RTX HDR / Auto HDR)
//   to correctly detect an HDR display.
//
// TWO MODES
//   Launch mode  : the tool enables HDR, waits, then launches the game itself.
//   Monitor mode : the tool polls for an already-starting process and enables
//                  HDR the instant it is detected.
//
// COMPILE (MSVC Developer Command Prompt / Visual Studio 2019+)
//   cl /EHsc /W3 /O2 testhdr.cpp /link user32.lib
//
// USAGE
//   Launch mode  :  testhdr.exe [options] "C:\path\to\game.exe" [game args...]
//   Monitor mode :  testhdr.exe --monitor game.exe [options]
//
// OPTIONS
//   --no-hdr           Skip HDR activation (baseline comparison run)
//   --delay <ms>       Wait N ms after HDR enable before launching (default 500)
//   --watch-window     Log when the game window first appears on screen
//   --monitor <exe>    Monitor mode: enable HDR the moment process is detected
//   --help             Show usage
//
// QUICK EXAMPLES
//   testhdr.exe "C:\Games\MyGame\game.exe"
//   testhdr.exe --delay 200 --watch-window "C:\Games\MyGame\game.exe"
//   testhdr.exe --no-hdr "C:\Games\MyGame\game.exe"      <- baseline
//   testhdr.exe --monitor MyGame.exe
//
// NOTE ON DXGI SWAPCHAIN INTERCEPTION (future work)
//   To pinpoint the exact moment the game creates its DXGI swapchain you would:
//     1. Inject a DLL into the game process (e.g. via CreateRemoteThread).
//     2. Hook IDXGIFactory::CreateSwapChain / CreateSwapChainForHwnd using
//        MinHook or Microsoft Detours.
//     3. In the hook, record a QueryPerformanceCounter timestamp and compare
//        it against the HDR-activation timestamp captured by this tool.
//   This prototype does NOT implement injection or hooking. Window appearance
//   (--watch-window) serves as a conservative proxy for graphics init timing.
// =============================================================================

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "userenv.lib")

#include <set>
#include <ctime>

// =============================================================================
// High-precision timer
// =============================================================================

static LARGE_INTEGER g_timerStart;
static LARGE_INTEGER g_timerFreq;

static void TimerInit()
{
    QueryPerformanceFrequency(&g_timerFreq);
    QueryPerformanceCounter(&g_timerStart);
}

static double ElapsedMs()
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart - g_timerStart.QuadPart)
           * 1000.0
           / static_cast<double>(g_timerFreq.QuadPart);
}

static FILE* g_logFile = nullptr;

// Open a log file next to the exe: testhdr_YYYYMMDD_HHMMSS.log
static void OpenLogFile()
{
    char exeDir[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exeDir, MAX_PATH);
    // Trim to directory
    char* last = strrchr(exeDir, '\\');
    if (last) *(last + 1) = '\0';

    time_t now = time(nullptr);
    struct tm t = {};
    localtime_s(&t, &now);

    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%stesthdr_%04d%02d%02d_%02d%02d%02d.log",
             exeDir,
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec);

    g_logFile = fopen(path, "w");
    if (g_logFile)
        printf("[log] Writing to: %s\n\n", path);
}

static void Log(const char* fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    printf("[%8.2f ms] %s\n", ElapsedMs(), buf);
    if (g_logFile) {
        fprintf(g_logFile, "[%8.2f ms] %s\n", ElapsedMs(), buf);
        fflush(g_logFile);
    }
    fflush(stdout);
}

// Print + log without timestamp prefix (for headers/labels)
static void LogRaw(const char* fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    printf("%s\n", buf);
    if (g_logFile) {
        fprintf(g_logFile, "%s\n", buf);
        fflush(g_logFile);
    }
}

// =============================================================================
// DisplayConfig HDR toggle
//
// Uses the CCD (Connected Displays Configuration) API to enable or disable
// Windows HDR (Advanced Color) on every connected display that supports it.
//
// Key API calls:
//   GetDisplayConfigBufferSizes  – determine array sizes needed
//   QueryDisplayConfig           – enumerate active display paths
//   DisplayConfigGetDeviceInfo   – read per-display HDR capability / state
//   DisplayConfigSetDeviceInfo   – write new HDR state
//
// All four functions are exported from user32.dll (Windows 7+).
// The Advanced Color info/state structures require Windows 10 version 1703+.
// =============================================================================

// Numeric values for DISPLAYCONFIG_DEVICE_INFO_TYPE entries that may not be
// present in older Platform SDK headers.  Cast to the enum type so the structs
// below are compatible with the header-defined type.
static const DISPLAYCONFIG_DEVICE_INFO_TYPE
    kGetAdvancedColorInfo  = static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(9);
static const DISPLAYCONFIG_DEVICE_INFO_TYPE
    kSetAdvancedColorState = static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(10);

// Mirrors DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO (wingdi.h, Windows 10 1703+).
struct AdvancedColorInfo
{
    DISPLAYCONFIG_DEVICE_INFO_HEADER header;
    union {
        struct {
            UINT32 advancedColorSupported    : 1;
            UINT32 advancedColorEnabled      : 1;
            UINT32 wideColorEnforced         : 1;
            UINT32 advancedColorForceDisabled: 1;
            UINT32 reserved                  : 28;
        };
        UINT32 value;
    };
    UINT32 colorEncoding;        // DISPLAYCONFIG_COLOR_ENCODING (not used here)
    UINT32 bitsPerColorChannel;
};

// Mirrors DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE (wingdi.h, Windows 10 1703+).
struct AdvancedColorState
{
    DISPLAYCONFIG_DEVICE_INFO_HEADER header;
    union {
        struct {
            UINT32 enableAdvancedColor : 1;
            UINT32 reserved            : 31;
        };
        UINT32 value;
    };
};

// Toggle HDR on every display that supports Advanced Color.
// Returns true if at least one display was successfully changed (or was
// already in the target state).
static bool SetHDR(bool enable)
{
    UINT32 pathCount = 0, modeCount = 0;
    LONG rc = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,
                                          &pathCount, &modeCount);
    if (rc != ERROR_SUCCESS) {
        printf("    GetDisplayConfigBufferSizes failed (error %ld)\n", rc);
        return false;
    }

    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);

    rc = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,
                            &pathCount, paths.data(),
                            &modeCount, modes.data(),
                            nullptr);
    if (rc != ERROR_SUCCESS) {
        printf("    QueryDisplayConfig failed (error %ld)\n", rc);
        return false;
    }

    bool anySuccess = false;

    for (UINT32 i = 0; i < pathCount; ++i)
    {
        const LUID    adapterId = paths[i].targetInfo.adapterId;
        const UINT32  targetId  = paths[i].targetInfo.id;

        // --- Query current state ---
        AdvancedColorInfo getInfo  = {};
        getInfo.header.type        = kGetAdvancedColorInfo;
        getInfo.header.size        = sizeof(getInfo);
        getInfo.header.adapterId   = adapterId;
        getInfo.header.id          = targetId;

        rc = DisplayConfigGetDeviceInfo(&getInfo.header);
        if (rc != ERROR_SUCCESS) {
            printf("    Display %u: DisplayConfigGetDeviceInfo failed (error %ld), skipping\n",
                   i, rc);
            continue;
        }

        printf("    Display %u: HDR supported=%-3s  currently=%s\n",
               i,
               getInfo.advancedColorSupported ? "yes" : "no",
               getInfo.advancedColorEnabled    ? "ON"  : "OFF");

        if (!getInfo.advancedColorSupported) {
            printf("    Display %u: HDR not supported on this display\n", i);
            continue;
        }

        // Always push the Set even if Get reports the target state already —
        // the Get can return stale/incorrect data in some driver configurations.

        // --- Apply new state ---
        AdvancedColorState setState  = {};
        setState.header.type         = kSetAdvancedColorState;
        setState.header.size         = sizeof(setState);
        setState.header.adapterId    = adapterId;
        setState.header.id           = targetId;
        setState.enableAdvancedColor = enable ? 1u : 0u;

        rc = DisplayConfigSetDeviceInfo(&setState.header);
        if (rc == ERROR_SUCCESS) {
            printf("    Display %u: HDR %s\n", i, enable ? "ENABLED" : "DISABLED");
            anySuccess = true;
        } else {
            printf("    Display %u: DisplayConfigSetDeviceInfo failed (error %ld)\n", i, rc);
        }
    }

    return anySuccess;
}

// =============================================================================
// Game window detection  (EnumWindows + GetWindowThreadProcessId)
// =============================================================================

struct WindowSearch { DWORD pid; HWND result; };

static BOOL CALLBACK FindWindowByPid(HWND hwnd, LPARAM lp)
{
    auto* ws = reinterpret_cast<WindowSearch*>(lp);
    DWORD wPid = 0;
    GetWindowThreadProcessId(hwnd, &wPid);
    if (wPid == ws->pid && IsWindowVisible(hwnd)) {
        char title[256] = {};
        GetWindowTextA(hwnd, title, sizeof(title));
        if (title[0] != '\0') {         // ignore untitled system windows
            ws->result = hwnd;
            return FALSE;               // stop enumeration
        }
    }
    return TRUE;
}

// Poll until a visible, titled window belonging to `pid` appears, or until the
// process exits, or until `timeoutMs` elapses.
static void WatchForWindow(DWORD pid, HANDLE hProcess, int timeoutMs = 30000)
{
    Log("Watching for game window (timeout %d ms)...", timeoutMs);
    for (int waited = 0; waited < timeoutMs; waited += 50) {
        WindowSearch ws = { pid, nullptr };
        EnumWindows(FindWindowByPid, reinterpret_cast<LPARAM>(&ws));
        if (ws.result) {
            char title[256] = {};
            GetWindowTextA(ws.result, title, sizeof(title));
            Log("Game window appeared: \"%s\" (HWND %p)", title, (void*)ws.result);
            return;
        }
        if (WaitForSingleObject(hProcess, 0) == WAIT_OBJECT_0) {
            Log("Game process exited before a window was detected");
            return;
        }
        Sleep(50);
    }
    Log("Window watch timed out after %d ms", timeoutMs);
}

// =============================================================================
// Process polling  (CreateToolhelp32Snapshot – no WMI dependency)
// =============================================================================

static DWORD FindProcessByName(const char* exeName)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32 pe = {};
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;

    if (Process32First(snap, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, exeName) == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

// =============================================================================
// Program options
// =============================================================================

struct Options
{
    bool        monitorMode   = false;  // true = wait for process, false = launch
    bool        enableHDR     = true;   // false = --no-hdr (baseline run)
    bool        watchWindow   = false;  // log game window appearance
    int         hdrDelayMs    = 500;    // ms between HDR enable and game launch
    const char* targetExe     = nullptr;// full path (launch) or filename (monitor)
    int         gameArgOffset = 0;      // argv[] index where game args begin
};

static void PrintHelp(const char* prog)
{
    printf(
        "Usage:\n"
        "  Launch mode  :  %s [options] \"C:\\path\\to\\game.exe\" [game args...]\n"
        "  Monitor mode :  %s --monitor game.exe [options]\n\n"
        "Options:\n"
        "  --no-hdr           Skip HDR activation (baseline comparison run)\n"
        "  --delay <ms>       Wait N ms after HDR enable before launching (default: 500)\n"
        "  --watch-window     Log when the game window first appears\n"
        "  --monitor <exe>    Monitor mode: enable HDR when the process is detected\n"
        "  --help             Show this message\n\n"
        "Examples:\n"
        "  %s \"C:\\Games\\MyGame\\game.exe\"\n"
        "  %s --delay 200 --watch-window \"C:\\Games\\MyGame\\game.exe\"\n"
        "  %s --no-hdr \"C:\\Games\\MyGame\\game.exe\"     <- baseline\n"
        "  %s --monitor MyGame.exe\n",
        prog, prog, prog, prog, prog, prog
    );
}

// =============================================================================
// De-elevated launch
//
// When testhdr.exe runs as Administrator (needed for DisplayConfigSetDeviceInfo)
// and then calls CreateProcess, the child inherits the elevated token.  Games
// launched elevated may get a different DXGI HDR view than when launched as the
// normal user, which can cause HDR detection to fail even if Windows HDR is on.
//
// Solution: borrow the token from explorer.exe (which always runs as the
// standard logged-in user) and use CreateProcessWithTokenW to launch the game
// at user-level elevation even though we are admin.
// =============================================================================

static HANDLE GetCurrentUserToken()
{
    // Find explorer.exe — it reliably runs as the non-elevated desktop user.
    DWORD explorerPid = FindProcessByName("explorer.exe");
    if (explorerPid == 0) { Log("WARNING: explorer.exe not found, will launch elevated"); return nullptr; }

    HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, explorerPid);
    if (!hProc) { Log("WARNING: OpenProcess(explorer) failed (%lu)", GetLastError()); return nullptr; }

    HANDLE hToken = nullptr;
    if (!OpenProcessToken(hProc, TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY | TOKEN_QUERY, &hToken)) {
        Log("WARNING: OpenProcessToken(explorer) failed (%lu)", GetLastError());
        CloseHandle(hProc);
        return nullptr;
    }
    CloseHandle(hProc);

    HANDLE hDup = nullptr;
    if (!DuplicateTokenEx(hToken, TOKEN_ALL_ACCESS, nullptr,
                          SecurityImpersonation, TokenPrimary, &hDup)) {
        Log("WARNING: DuplicateTokenEx failed (%lu)", GetLastError());
        CloseHandle(hToken);
        return nullptr;
    }
    CloseHandle(hToken);
    return hDup; // caller must CloseHandle
}

// Launch exe as the current (non-elevated) user.
// Falls back to regular CreateProcessA if token acquisition fails.
static BOOL LaunchAsUser(const char* exePath, const char* cmdLine,
                         const char* workDir, PROCESS_INFORMATION* pi)
{
    HANDLE hUserToken = GetCurrentUserToken();

    if (hUserToken) {
        // Convert narrow strings to wide for CreateProcessWithTokenW
        wchar_t wExe[MAX_PATH]      = {};
        wchar_t wCmd[32768]         = {};
        wchar_t wDir[MAX_PATH]      = {};

        MultiByteToWideChar(CP_ACP, 0, exePath,  -1, wExe, MAX_PATH);
        MultiByteToWideChar(CP_ACP, 0, cmdLine,  -1, wCmd, 32768);
        if (workDir)
            MultiByteToWideChar(CP_ACP, 0, workDir, -1, wDir, MAX_PATH);

        STARTUPINFOW si  = {};
        si.cb            = sizeof(si);
        si.lpDesktop     = const_cast<LPWSTR>(L"winsta0\\default");

        BOOL ok = CreateProcessWithTokenW(
            hUserToken,
            0,                              // dwLogonFlags
            wExe,                           // lpApplicationName
            wCmd,                           // lpCommandLine
            0,                              // dwCreationFlags
            nullptr,                        // lpEnvironment (inherit)
            workDir ? wDir : nullptr,       // lpCurrentDirectory
            &si, pi
        );
        CloseHandle(hUserToken);
        if (ok) { Log("Game launched as current user (de-elevated)"); return TRUE; }
        Log("CreateProcessWithTokenW failed (%lu), falling back to direct launch", GetLastError());
    }

    // Fallback: regular elevated launch
    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    std::vector<char> buf(cmdLine, cmdLine + strlen(cmdLine) + 1);
    return CreateProcessA(exePath, buf.data(), nullptr, nullptr,
                          FALSE, 0, nullptr, workDir, &si, pi);
}

// Extract directory from a full path (e.g. "C:\Games\foo\game.exe" -> "C:\Games\foo\")
static std::string DirOf(const char* path)
{
    std::string s(path);
    size_t pos = s.find_last_of("\\/");
    return (pos != std::string::npos) ? s.substr(0, pos + 1) : std::string();
}

// =============================================================================
// Launch mode
//   1. Enable HDR
//   2. Wait --delay ms
//   3. Launch game as normal user (de-elevated)
//   4. Optionally watch for the game window
// =============================================================================

static int RunLaunchMode(const Options& opts, int argc, char* argv[])
{
    printf("--- Launch Mode ---\n");
    printf("  Game      : %s\n", opts.targetExe);
    printf("  HDR       : %s\n", opts.enableHDR ? "ENABLE before launch" : "OFF (baseline)");
    printf("  HDR delay : %d ms\n", opts.hdrDelayMs);
    printf("  Window    : %s\n\n", opts.watchWindow ? "watch" : "ignore");

    // ------------------------------------------------------------------
    // Step 1: Enable HDR
    // ------------------------------------------------------------------
    if (opts.enableHDR) {
        Log("Enabling HDR via DisplayConfig...");
        bool ok = SetHDR(true);
        if (ok)
            Log("HDR activation complete");
        else
            Log("HDR activation failed or no HDR-capable display found");

        if (opts.hdrDelayMs > 0) {
            Log("Waiting %d ms for HDR mode to stabilize...", opts.hdrDelayMs);
            Sleep(opts.hdrDelayMs);
            Log("Stabilization delay complete");
        }
    } else {
        Log("HDR activation skipped (--no-hdr baseline run)");
    }

    // ------------------------------------------------------------------
    // Step 2: Launch the game
    // ------------------------------------------------------------------

    // Build the full command line string.  Enclose the exe path in quotes so
    // paths with spaces are handled correctly.
    std::string cmdLine;
    cmdLine += '"';
    cmdLine += opts.targetExe;
    cmdLine += '"';
    for (int i = opts.gameArgOffset; i < argc; ++i) {
        cmdLine += ' ';
        cmdLine += argv[i];
    }

    Log("Launching as current user: %s", cmdLine.c_str());

    PROCESS_INFORMATION pi  = {};
    std::string gameDir = DirOf(opts.targetExe);

    BOOL launched = LaunchAsUser(
        opts.targetExe,
        cmdLine.c_str(),
        gameDir.empty() ? nullptr : gameDir.c_str(),
        &pi
    );

    if (!launched) {
        printf("Launch failed (error %lu)\n", GetLastError());
        printf("Check that the path is correct and the file exists.\n");
        return 1;
    }

    Log("Game process created  PID=%lu", pi.dwProcessId);

    // ------------------------------------------------------------------
    // Step 3: Optional window watch
    // ------------------------------------------------------------------
    if (opts.watchWindow)
        WatchForWindow(pi.dwProcessId, pi.hProcess);

    // Detach — let the game run independently, do not wait for it to exit.
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    printf("\n--- Done. Game is running, this tool will now exit. ---\n");
    return 0;
}

// =============================================================================
// New-process watcher
//   After the target is detected, poll the process list for `durationMs` and
//   log every NEW exe that appears.  Helps identify child/spawned processes.
// =============================================================================

// Take a snapshot of all current PIDs into a set.
static std::set<DWORD> SnapshotPIDs()
{
    std::set<DWORD> pids;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return pids;
    PROCESSENTRY32 pe = {};
    pe.dwSize = sizeof(pe);
    if (Process32First(snap, &pe)) {
        do { pids.insert(pe.th32ProcessID); }
        while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return pids;
}

static void WatchNewProcesses(int durationMs)
{
    Log("Watching for new processes for %d ms...", durationMs);
    std::set<DWORD> seen = SnapshotPIDs();
    int elapsed = 0;
    while (elapsed < durationMs) {
        Sleep(50);
        elapsed += 50;

        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) continue;
        PROCESSENTRY32 pe = {};
        pe.dwSize = sizeof(pe);
        if (Process32First(snap, &pe)) {
            do {
                if (seen.find(pe.th32ProcessID) == seen.end()) {
                    Log("  NEW process: %-40s PID=%lu  PPID=%lu",
                        pe.szExeFile, pe.th32ProcessID, pe.th32ParentProcessID);
                    seen.insert(pe.th32ProcessID);
                }
            } while (Process32Next(snap, &pe));
        }
        CloseHandle(snap);
    }
    Log("New-process watch complete");
}

// =============================================================================
// Monitor mode
//   Poll process list at ~100 Hz.  The moment the target exe is detected,
//   immediately enable HDR, then wait for the process to exit.
// =============================================================================

static int RunMonitorMode(const Options& opts)
{
    printf("--- Monitor Mode ---\n");
    printf("  Target      : %s\n", opts.targetExe);
    printf("  HDR         : %s\n", opts.enableHDR ? "enable on detection" : "OFF (baseline)");
    printf("  Window      : %s\n\n", opts.watchWindow ? "watch" : "ignore");

    Log("Polling process list (100 Hz) — start your game now...");

    DWORD prevPid = 0;

    while (true)
    {
        DWORD pid = FindProcessByName(opts.targetExe);

        if (pid != 0 && pid != prevPid)
        {
            prevPid = pid;
            Log("Process detected: %s  PID=%lu", opts.targetExe, pid);

            if (opts.enableHDR) {
                Log("Enabling HDR via DisplayConfig...");
                bool ok = SetHDR(true);
                if (ok)
                    Log("HDR activation complete");
                else
                    Log("HDR activation failed or no HDR-capable display found");
            } else {
                Log("HDR activation skipped (--no-hdr)");
            }

            HANDLE hProc = OpenProcess(
                SYNCHRONIZE | PROCESS_QUERY_INFORMATION, FALSE, pid);
            if (!hProc) {
                Log("OpenProcess failed (error %lu) – process may have already exited",
                    GetLastError());
                break;
            }

            if (opts.watchWindow)
                WatchForWindow(pid, hProc);

            // Always watch for child processes spawned by the target.
            // This reveals the real game exe if the target is a launcher.
            WatchNewProcesses(30000);

            Log("Waiting for process to exit...");
            WaitForSingleObject(hProc, INFINITE);
            Log("Process exited");
            CloseHandle(hProc);
            break;
        }

        Sleep(10);  // 10 ms = 100 Hz poll rate
    }

    return 0;
}

// =============================================================================
// Elevation helpers
// DisplayConfigSetDeviceInfo requires administrator privileges.
// If not already elevated, re-launch self with "runas" so Windows prompts UAC.
// =============================================================================

static bool IsElevated()
{
    BOOL elevated = FALSE;
    HANDLE token  = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION te = {};
        DWORD           sz = sizeof(te);
        if (GetTokenInformation(token, TokenElevation, &te, sz, &sz))
            elevated = te.TokenIsElevated;
        CloseHandle(token);
    }
    return elevated != FALSE;
}

// Re-launch this exe with the same arguments under "runas" (UAC prompt).
static void RelaunchElevated(int argc, char* argv[])
{
    char selfPath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, selfPath, MAX_PATH);

    // Rebuild the argument string (skip argv[0])
    std::string params;
    for (int i = 1; i < argc; ++i) {
        if (i > 1) params += ' ';
        // Quote each argument in case it contains spaces
        params += '"';
        params += argv[i];
        params += '"';
    }

    SHELLEXECUTEINFOA sei = {};
    sei.cbSize      = sizeof(sei);
    sei.lpVerb      = "runas";
    sei.lpFile      = selfPath;
    sei.lpParameters = params.empty() ? nullptr : params.c_str();
    sei.nShow       = SW_NORMAL;

    if (!ShellExecuteExA(&sei))
        printf("Failed to relaunch as administrator (error %lu)\n", GetLastError());
}

// =============================================================================
// main
// =============================================================================

int main(int argc, char* argv[])
{
    TimerInit();
    OpenLogFile();

    // DisplayConfigSetDeviceInfo needs admin rights.
    // If we are not elevated, ask Windows to re-launch us with UAC.
    if (!IsElevated()) {
        printf("Not running as administrator — requesting elevation...\n");
        RelaunchElevated(argc, argv);
        return 0;
    }

    printf("testhdr v1.0 -- Windows HDR Timing Experiment\n");
    printf("==============================================\n\n");

    if (argc < 2) {
        PrintHelp(argv[0]);
        return 0;
    }

    Options opts;
    int i = 1;

    // Parse flags / options.  Stop at the first non-flag token, which is the
    // game executable path in launch mode.
    while (i < argc)
    {
        const char* arg = argv[i];

        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            PrintHelp(argv[0]);
            return 0;
        }
        else if (strcmp(arg, "--no-hdr") == 0) {
            opts.enableHDR = false;
            ++i;
        }
        else if (strcmp(arg, "--watch-window") == 0) {
            opts.watchWindow = true;
            ++i;
        }
        else if (strcmp(arg, "--delay") == 0) {
            if (i + 1 >= argc) {
                printf("--delay requires a value in milliseconds\n");
                return 1;
            }
            opts.hdrDelayMs = atoi(argv[i + 1]);
            i += 2;
        }
        else if (strcmp(arg, "--monitor") == 0) {
            if (i + 1 >= argc) {
                printf("--monitor requires an executable name\n");
                return 1;
            }
            opts.monitorMode = true;
            opts.targetExe   = argv[i + 1];
            i += 2;
        }
        else {
            // First non-option token = game executable (launch mode)
            opts.targetExe    = argv[i];
            opts.gameArgOffset = i + 1;
            break;
        }
    }

    if (!opts.targetExe) {
        printf("Error: no target executable specified.\n\n");
        PrintHelp(argv[0]);
        return 1;
    }

    return opts.monitorMode
        ? RunMonitorMode(opts)
        : RunLaunchMode(opts, argc, argv);
}
