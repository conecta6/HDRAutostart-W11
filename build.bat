@echo off
setlocal enabledelayedexpansion
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
if not exist "C:\Users\migue\Desktop\hdr2\dist" mkdir "C:\Users\migue\Desktop\hdr2\dist"

:: Check if HDRAutostart is running and kill it before building
tasklist /FI "IMAGENAME eq HDRAutostart.exe" 2>nul | find /I "HDRAutostart.exe" >nul
if not errorlevel 1 (
    echo [BUILD] HDRAutostart.exe is running - closing it...
    taskkill /IM HDRAutostart.exe /F >nul 2>&1
    timeout /t 1 /nobreak >nul
)

:: Generate icon.ico from create_icon.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Users\migue\Desktop\hdr2\create_icon.ps1"
if errorlevel 1 ( echo [ERROR] create_icon.ps1 failed & pause & exit /b 1 )

:: Compile icon resource
rc /nologo /fo "C:\Users\migue\Desktop\hdr2\dist\hdrautostart.res" "C:\Users\migue\Desktop\hdr2\hdrautostart.rc"
if errorlevel 1 ( echo [ERROR] rc failed & pause & exit /b 1 )

:: Build testhdr (helper tool)
cl /EHsc /O2 /W3 "C:\Users\migue\Desktop\hdr2\testhdr.cpp" /Fe:"C:\Users\migue\Desktop\hdr2\testhdr.exe" /Fo:"C:\Users\migue\Desktop\hdr2\testhdr.obj" /link user32.lib
if errorlevel 1 ( echo [ERROR] testhdr build failed & pause & exit /b 1 )

:: Build HDRAutostart (with embedded icon)
cl /EHsc /O2 /W3 "C:\Users\migue\Desktop\hdr2\hdrautostart.cpp" /Fe:"C:\Users\migue\Desktop\hdr2\dist\HDRAutostart.exe" /Fo:"C:\Users\migue\Desktop\hdr2\dist\HDRAutostart.obj" /link user32.lib advapi32.lib shell32.lib "C:\Users\migue\Desktop\hdr2\dist\hdrautostart.res"
if errorlevel 1 ( echo [ERROR] HDRAutostart build failed & pause & exit /b 1 )

echo [BUILD] HDRAutostart.exe OK

:: ── Build installer ──────────────────────────────────────────────────────────
if exist "C:\Program Files (x86)\NSIS\makensis.exe" (
    "C:\Program Files (x86)\NSIS\makensis.exe" "C:\Users\migue\Desktop\hdr2\installer.nsi"
) else (
    "C:\Program Files\NSIS\makensis.exe" "C:\Users\migue\Desktop\hdr2\installer.nsi"
)
if errorlevel 1 ( echo [ERROR] NSIS installer failed & pause & exit /b 1 )

echo [BUILD] All done.
