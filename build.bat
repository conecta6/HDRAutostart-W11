@echo off
setlocal enabledelayedexpansion

:: Project root: %~dp0  (directory where this build.bat lives — no need to change)
:: All source files and output are relative to %~dp0

:: Locate the MSVC environment with vswhere (any VS / Build Tools version).
:: Paths with "(x86)" are kept in variables and expanded with !var! (no parentheses
:: issues); gotos are used instead of ( ... ) blocks for the same reason.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSINSTALL="
if not exist "!VSWHERE!" goto :no_vs
for /f "usebackq delims=" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL goto :no_vs
if not exist "!VSINSTALL!\VC\Auxiliary\Build\vcvarsall.bat" goto :no_vs
call "!VSINSTALL!\VC\Auxiliary\Build\vcvarsall.bat" x64
where cl >nul 2>&1
if errorlevel 1 goto :no_vs
goto :vs_ok

:no_vs
echo [ERROR] Visual Studio Build Tools with the C++ workload not found (vswhere / vcvarsall.bat / cl)
pause
exit /b 1

:vs_ok
if not exist "%~dp0dist" mkdir "%~dp0dist"

:: Check if HDRAutostart is running and kill it before building
tasklist /FI "IMAGENAME eq HDRAutostart.exe" 2>nul | find /I "HDRAutostart.exe" >nul
if not errorlevel 1 (
    echo [BUILD] HDRAutostart.exe is running - closing it...
    taskkill /IM HDRAutostart.exe /F >nul 2>&1
    timeout /t 1 /nobreak >nul
)

:: Generate icon.ico from create_icon.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0create_icon.ps1"
if errorlevel 1 ( echo [ERROR] create_icon.ps1 failed & pause & exit /b 1 )

:: Compile icon resource
rc /nologo /fo "%~dp0dist\hdrautostart.res" "%~dp0hdrautostart.rc"
if errorlevel 1 ( echo [ERROR] rc failed & pause & exit /b 1 )

:: Build testhdr (helper tool)
cl /EHsc /O2 /W3 "%~dp0testhdr.cpp" /Fe:"%~dp0testhdr.exe" /Fo:"%~dp0testhdr.obj" /link user32.lib
if errorlevel 1 ( echo [ERROR] testhdr build failed & pause & exit /b 1 )

:: Build HDRAutostart (with embedded icon)
cl /EHsc /O2 /W3 "%~dp0hdrautostart.cpp" /Fe:"%~dp0dist\HDRAutostart.exe" /Fo:"%~dp0dist\HDRAutostart.obj" /link user32.lib advapi32.lib shell32.lib "%~dp0dist\hdrautostart.res"
if errorlevel 1 ( echo [ERROR] HDRAutostart build failed & pause & exit /b 1 )

echo [BUILD] HDRAutostart.exe OK

:: ── Build installer ──────────────────────────────────────────────────────────
:: Look for makensis in PATH first, then in the default install folders
set "MAKENSIS="
for /f "usebackq delims=" %%i in (`where makensis 2^>nul`) do if not defined MAKENSIS set "MAKENSIS=%%i"
if not defined MAKENSIS if exist "C:\Program Files (x86)\NSIS\makensis.exe" set "MAKENSIS=C:\Program Files (x86)\NSIS\makensis.exe"
if not defined MAKENSIS if exist "C:\Program Files\NSIS\makensis.exe" set "MAKENSIS=C:\Program Files\NSIS\makensis.exe"
if not defined MAKENSIS ( echo [ERROR] NSIS ^(makensis.exe^) not found - HDRAutostart.exe was built but the installer was not & pause & exit /b 1 )
"!MAKENSIS!" "%~dp0installer.nsi"
if errorlevel 1 ( echo [ERROR] NSIS installer failed & pause & exit /b 1 )

echo [BUILD] All done.
