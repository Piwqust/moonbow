@echo off
rem Builds build-tests\core_tests.exe with MSVC and runs it.
rem Usage: tests\build-tests.bat [release^|debug] [test-name-filter]
setlocal enabledelayedexpansion
set "ROOT=%~dp0..\"
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=release"

if not defined VCINSTALLDIR (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
    if not defined VSINSTALL (
        echo Visual Studio with C++ tools not found.
        exit /b 1
    )
    call "!VSINSTALL!\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 || exit /b 1
)

set "OUT=%ROOT%build-tests"
set "OBJ=%OUT%\obj\%CONFIG%"
if not exist "%OBJ%" mkdir "%OBJ%"
rem Keep build output out of git without touching the repository's .gitignore.
if not exist "%OUT%\.gitignore" (echo *)> "%OUT%\.gitignore"

rem Same flags as build.bat, plus /WX so any /W4 warning fails the build.
set "CFLAGS=/nologo /std:c++20 /utf-8 /EHsc /W4 /WX /permissive- /Zc:__cplusplus /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_WIN32_WINNT=0x0A00"
set "LFLAGS=/SUBSYSTEM:CONSOLE /DYNAMICBASE /NXCOMPAT"
if /i "%CONFIG%"=="debug" (
    set "CFLAGS=%CFLAGS% /Od /Zi /MTd /D_DEBUG"
    set "LFLAGS=%LFLAGS% /DEBUG"
) else (
    set "CFLAGS=%CFLAGS% /O2 /MT /DNDEBUG"
)
set "LIBS=user32.lib gdi32.lib shell32.lib shlwapi.lib ole32.lib propsys.lib uuid.lib"

cl %CFLAGS% /Fo"%OBJ%\\" /Fd"%OBJ%\\" "%ROOT%tests\core_tests.cpp" "%ROOT%src\core\*.cpp" "%ROOT%src\library\*.cpp" "%ROOT%src\app\player\Lyrics.cpp" "%ROOT%src\app\PlayQueue.cpp" "%ROOT%src\net\Cache.cpp" "%ROOT%src\net\Http.cpp" "%ROOT%src\net\OnlineData.cpp" "%ROOT%src\net\Secret.cpp" /Fe"%OUT%\core_tests.exe" /link %LFLAGS% %LIBS% || exit /b 1

"%OUT%\core_tests.exe" %2
exit /b %errorlevel%
