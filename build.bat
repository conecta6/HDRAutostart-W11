@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
if not exist "C:\Users\migue\Desktop\hdr2\dist" mkdir "C:\Users\migue\Desktop\hdr2\dist"

:: Generate icon.ico from create_icon.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Users\migue\Desktop\hdr2\create_icon.ps1"

:: Compile icon resource
rc /nologo /fo "C:\Users\migue\Desktop\hdr2\dist\hdrautostart.res" "C:\Users\migue\Desktop\hdr2\hdrautostart.rc"

:: Build testhdr (helper tool)
cl /EHsc /O2 /W3 "C:\Users\migue\Desktop\hdr2\testhdr.cpp" /Fe:"C:\Users\migue\Desktop\hdr2\testhdr.exe" /Fo:"C:\Users\migue\Desktop\hdr2\testhdr.obj" /link user32.lib

:: Build HDRAutostart (with embedded icon)
cl /EHsc /O2 /W3 "C:\Users\migue\Desktop\hdr2\hdrautostart.cpp" /Fe:"C:\Users\migue\Desktop\hdr2\dist\HDRAutostart.exe" /Fo:"C:\Users\migue\Desktop\hdr2\dist\HDRAutostart.obj" /link user32.lib advapi32.lib shell32.lib "C:\Users\migue\Desktop\hdr2\dist\hdrautostart.res"

:: ── Build installer ──────────────────────────────────────────────────────────
if exist "C:\Program Files (x86)\NSIS\makensis.exe" (
    "C:\Program Files (x86)\NSIS\makensis.exe" "C:\Users\migue\Desktop\hdr2\installer.nsi"
) else (
    "C:\Program Files\NSIS\makensis.exe" "C:\Users\migue\Desktop\hdr2\installer.nsi"
)
