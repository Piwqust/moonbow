@echo off
rem Builds build\Lumen.exe with MSVC. Usage: build.bat [release|debug]
rem Set LUMEN_OUT to build into another folder (parallel builds in worktrees).
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
if defined LUMEN_OUT set "OUT=%LUMEN_OUT%"
set "OBJ=%OUT%\obj\%CONFIG%"
if not exist "%OBJ%" mkdir "%OBJ%"

rem Every .cpp under src (object files share one folder, so basenames must be unique).
set "SOURCES="
for /r "%ROOT%src" %%f in (*.cpp) do set "SOURCES=!SOURCES! "%%f""

set "CFLAGS=/nologo /std:c++20 /utf-8 /EHsc /W4 /permissive- /Zc:__cplusplus /MP /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_WIN32_WINNT=0x0A00 /I"%ROOT%src" /I"%ROOT%third_party\mpv\include""
set "LFLAGS=/SUBSYSTEM:WINDOWS /MANIFEST:NO /DYNAMICBASE /NXCOMPAT"
rem Graphics, codecs and shell DLLs load on first use, not before wWinMain (faster first paint).
set "LFLAGS=%LFLAGS% /DELAYLOAD:d3d11.dll /DELAYLOAD:d2d1.dll /DELAYLOAD:dwrite.dll /DELAYLOAD:dcomp.dll /DELAYLOAD:propsys.dll /DELAYLOAD:oleaut32.dll /DELAYLOAD:imm32.dll /DELAYLOAD:shlwapi.dll"
if /i "%CONFIG%"=="debug" (
    set "CFLAGS=%CFLAGS% /Od /Zi /MTd /D_DEBUG"
    set "LFLAGS=%LFLAGS% /DEBUG"
) else (
    set "CFLAGS=%CFLAGS% /O2 /GL /Gw /MT /DNDEBUG"
    set "LFLAGS=%LFLAGS% /LTCG /OPT:REF /OPT:ICF"
)
set "LIBS=user32.lib gdi32.lib advapi32.lib shell32.lib shlwapi.lib ole32.lib oleaut32.lib uuid.lib dwmapi.lib imm32.lib windowscodecs.lib propsys.lib d3d11.lib dxgi.lib d2d1.lib dwrite.lib dcomp.lib windowsapp.lib delayimp.lib"

rc /nologo /i "%ROOT%res" /fo "%OBJ%\app.res" "%ROOT%res\app.rc" || exit /b 1
cl %CFLAGS% /Fo"%OBJ%\\" /Fd"%OBJ%\\" %SOURCES% "%OBJ%\app.res" /Fe"%OUT%\Lumen.exe" /link %LFLAGS% %LIBS% || exit /b 1

copy /y "%ROOT%third_party\mpv\libmpv-2.dll" "%OUT%\" >nul || exit /b 1
echo Built %OUT%\Lumen.exe (%CONFIG%)
