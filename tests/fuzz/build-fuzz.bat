@echo off
rem Builds the parser fuzzer and replays every seed against it (a regression check).
rem Usage: tests\fuzz\build-fuzz.bat [debug^|libfuzzer]
rem   debug (default): optimized code with the debug CRT's index, iterator and heap checks;
rem     build-tests\fuzz.exe mutates the seeds itself, for example:
rem     build-tests\fuzz.exe tags tests\fuzz\seeds\tags 300 %TEMP%
rem   libfuzzer: coverage-guided with AddressSanitizer (needs the C++ AddressSanitizer component of
rem     Visual Studio), build-tests\fuzz-lf.exe, run as:
rem     set LUMEN_FUZZ_TARGET=tags
rem     build-tests\fuzz-lf.exe -max_total_time=300 %TEMP%\corpus-tags tests\fuzz\seeds\tags
setlocal enabledelayedexpansion
set "ROOT=%~dp0..\..\"
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=debug"

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
set "OBJ=%OUT%\obj\fuzz-%CONFIG%"
if not exist "%OBJ%" mkdir "%OBJ%"
if not exist "%OUT%\.gitignore" (echo *)> "%OUT%\.gitignore"

set "CFLAGS=/nologo /std:c++20 /utf-8 /EHsc /W4 /WX /permissive- /Zc:__cplusplus /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_WIN32_WINNT=0x0A00 /Zi"
set "LFLAGS=/SUBSYSTEM:CONSOLE /DYNAMICBASE /NXCOMPAT /DEBUG"
set "EXE=fuzz.exe"
if /i "%CONFIG%"=="libfuzzer" (
    set "CFLAGS=%CFLAGS% /O1 /MT /DNDEBUG /fsanitize=address,fuzzer /DLUMEN_LIBFUZZER"
    set "LFLAGS=%LFLAGS% /INCREMENTAL:NO"
    set "EXE=fuzz-lf.exe"
) else (
    set "CFLAGS=%CFLAGS% /O2 /MTd /D_DEBUG"
)
set "LIBS=user32.lib gdi32.lib shell32.lib shlwapi.lib ole32.lib propsys.lib uuid.lib dbghelp.lib"

cl %CFLAGS% /Fo"%OBJ%\\" /Fd"%OBJ%\\" "%ROOT%tests\fuzz\fuzz.cpp" "%ROOT%src\core\*.cpp" "%ROOT%src\library\*.cpp" "%ROOT%src\app\player\Lyrics.cpp" "%ROOT%src\net\Cache.cpp" "%ROOT%src\net\Http.cpp" "%ROOT%src\net\OnlineData.cpp" "%ROOT%src\net\Secret.cpp" /Fe"%OUT%\%EXE%" /link %LFLAGS% %LIBS% || exit /b 1
if /i "%CONFIG%"=="libfuzzer" (
    copy /y "%VCToolsInstallDir%bin\Hostx64\x64\clang_rt.asan_dynamic-x86_64.dll" "%OUT%\" >nul
    exit /b 0
)

rem Seeds, then the inputs that once crashed (regressions\<target>\*.bin, kept after each fix).
for /d %%t in ("%ROOT%tests\fuzz\seeds\*" "%ROOT%tests\fuzz\regressions\*") do (
    "%OUT%\fuzz.exe" %%~nxt "%%t" || exit /b 1
)
exit /b 0
