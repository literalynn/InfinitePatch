@echo off
rem Builds Binaries\Win32\dinput8.dll (32-bit). Needs Visual Studio with the C++ workload.
setlocal
cd /d "%~dp0"
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do set "VS=%%i"
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"&call "%VS%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul || exit /b 1
if not exist build mkdir build
if not exist Binaries\Win32 mkdir Binaries\Win32
rc /nologo /fo build\version.res src\version.rc || exit /b 1
cl /nologo /std:c++17 /O2 /MT /W4 /EHsc /LD %* src\dllmain.cpp build\version.res /Fobuild\ /Fdbuild\ /Fe:Binaries\Win32\dinput8.dll /link /DEF:src\dinput8.def /IMPLIB:build\dinput8.lib dxguid.lib user32.lib || exit /b 1
