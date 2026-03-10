@echo off
setlocal enabledelayedexpansion

:: Project root: %~dp0  (directory where this build.bat lives — no need to change)
:: All source files and output are relative to %~dp0

call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
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

:: Bundle ControlMyMonitor helper for installer/runtime use
copy /Y "%~dp0third_party\ControlMyMonitor\ControlMyMonitor.exe" "%~dp0dist\ControlMyMonitor.exe" >nul
if errorlevel 1 ( echo [ERROR] ControlMyMonitor copy failed & pause & exit /b 1 )

echo [BUILD] HDRAutostart.exe OK

:: ── Build installer ──────────────────────────────────────────────────────────
if exist "C:\Program Files (x86)\NSIS\makensis.exe" (
    "C:\Program Files (x86)\NSIS\makensis.exe" "%~dp0installer.nsi"
) else (
    "C:\Program Files\NSIS\makensis.exe" "%~dp0installer.nsi"
)
if errorlevel 1 ( echo [ERROR] NSIS installer failed & pause & exit /b 1 )

echo [BUILD] All done.
