@echo off
rem Builds build\Moonbow.exe with MSVC. Usage: build.bat [release|debug]
rem Set MOONBOW_OUT to build into another folder (parallel builds in worktrees).
setlocal enabledelayedexpansion
set "ROOT=%~dp0"
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=release"

if not exist "%ROOT%third_party\mpv\include\mpv\client.h" (
    echo libmpv not found. Run: powershell -ExecutionPolicy Bypass -File tools\fetch-libmpv.ps1
    exit /b 1
)

if not defined VCINSTALLDIR (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
    if not defined VSINSTALL (
        echo Visual Studio with C++ tools not found.
        exit /b 1
    )
    call "!VSINSTALL!\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 || exit /b 1
)

set "OUT=%ROOT%build"
if defined MOONBOW_OUT set "OUT=%MOONBOW_OUT%"
set "OBJ=%OUT%\obj\%CONFIG%"
if not exist "%OBJ%" mkdir "%OBJ%"

rem Every .cpp under src (object files share one folder, so basenames must be unique).
rem They go to a response file: on the command line the list outgrows cmd's 8191-character
rem limit when the checkout path is long (worktrees).
set "RSP=%OBJ%\sources.rsp"
if exist "%RSP%" del "%RSP%"
rem src\thumbs is MoonbowThumbs.dll, built below.
for /r "%ROOT%src" %%f in (*.cpp) do (
    set "F=%%f"
    if "!F:\src\thumbs\=!"=="!F!" echo "%%f">>"%RSP%"
)

set "CFLAGS=/nologo /std:c++20 /utf-8 /EHsc /W4 /permissive- /Zc:__cplusplus /MP /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_WIN32_WINNT=0x0A00 /I"%ROOT%src" /I"%ROOT%third_party\mpv\include""
set "LFLAGS=/SUBSYSTEM:WINDOWS /MANIFEST:NO /DYNAMICBASE /NXCOMPAT"
rem Graphics, codecs, shell, network and UI Automation DLLs load on first use, not before wWinMain (faster first paint).
set "LFLAGS=%LFLAGS% /DELAYLOAD:d3d11.dll /DELAYLOAD:d2d1.dll /DELAYLOAD:dwrite.dll /DELAYLOAD:dcomp.dll /DELAYLOAD:propsys.dll /DELAYLOAD:oleaut32.dll /DELAYLOAD:imm32.dll /DELAYLOAD:shlwapi.dll /DELAYLOAD:winhttp.dll /DELAYLOAD:crypt32.dll /DELAYLOAD:uiautomationcore.dll /DELAYLOAD:dbghelp.dll /DELAYLOAD:cabinet.dll /DELAYLOAD:version.dll /DELAYLOAD:bcrypt.dll"
if /i "%CONFIG%"=="debug" (
    set "CFLAGS=%CFLAGS% /Od /Zi /MTd /D_DEBUG"
    set "LFLAGS=%LFLAGS% /DEBUG"
    set "DLLFLAGS=/DEBUG"
) else if /i "%CONFIG%"=="asan" (
    rem AddressSanitizer, for test runs; needs the C++ AddressSanitizer component of Visual Studio.
    set "CFLAGS=%CFLAGS% /O1 /Zi /MT /DNDEBUG /fsanitize=address"
    set "LFLAGS=%LFLAGS% /DEBUG /INCREMENTAL:NO"
    set "DLLFLAGS=/DEBUG /INCREMENTAL:NO"
) else (
    rem /Zi and /DEBUG only add Moonbow.pdb, which crash dumps need; /OPT:REF,ICF keep the exe as small.
    set "CFLAGS=%CFLAGS% /O2 /GL /Gw /MT /DNDEBUG /Zi"
    set "LFLAGS=%LFLAGS% /LTCG /OPT:REF /OPT:ICF /DEBUG"
    set "DLLFLAGS=/LTCG /OPT:REF /OPT:ICF /DEBUG"
)
rem uiautomationcore.lib comes before windowsapp.lib, whose API-set imports of it would load at startup.
set "LIBS=user32.lib gdi32.lib advapi32.lib shell32.lib shlwapi.lib ole32.lib oleaut32.lib uuid.lib dwmapi.lib imm32.lib windowscodecs.lib propsys.lib d3d11.lib dxgi.lib d2d1.lib dwrite.lib dcomp.lib uiautomationcore.lib windowsapp.lib winhttp.lib crypt32.lib bcrypt.lib dbghelp.lib delayimp.lib"

rc /nologo /i "%ROOT%res" /fo "%OBJ%\app.res" "%ROOT%res\app.rc" || exit /b 1
cl %CFLAGS% /Fo"%OBJ%\\" /Fd"%OBJ%\\" @"%RSP%" "%OBJ%\app.res" /Fe"%OUT%\Moonbow.exe" /link %LFLAGS% %LIBS% || exit /b 1

rem Explorer's thumbnail provider for audio (src\thumbs); it reuses the exe's tag reader objects.
if not exist "%OBJ%\thumbs" mkdir "%OBJ%\thumbs"
cl %CFLAGS% /LD /Fo"%OBJ%\thumbs\\" /Fd"%OBJ%\thumbs\\" "%ROOT%src\thumbs\ThumbProvider.cpp" "%OBJ%\TagReader.obj" "%OBJ%\Json.obj" "%OBJ%\Paths.obj" /Fe"%OUT%\MoonbowThumbs.dll" /link %DLLFLAGS% /NOIMPLIB /NOEXP user32.lib gdi32.lib advapi32.lib shell32.lib shlwapi.lib ole32.lib uuid.lib windowscodecs.lib d2d1.lib || exit /b 1

copy /y "%ROOT%third_party\mpv\libmpv-2.dll" "%OUT%\" >nul || exit /b 1
if /i "%CONFIG%"=="asan" copy /y "%VCToolsInstallDir%bin\Hostx64\x64\clang_rt.asan_dynamic-x86_64.dll" "%OUT%\" >nul
echo Built %OUT%\Moonbow.exe (%CONFIG%)
