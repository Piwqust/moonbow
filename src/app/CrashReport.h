#pragma once
// Crash reports for testers, kept on this computer. An unhandled exception (any thread, mpv's
// too), std::terminate, abort, a pure virtual call or a CRT invalid parameter writes
// crash-<time>.dmp (a minidump: thread stacks and loaded modules, no heap) and crash-<time>.txt
// (version, exception, module + offset) to LogDir(), then ends the process without Windows'
// "stopped working" dialog. Nothing is sent anywhere. Read a dump with WinDbg and the matching
// Lumen.pdb (tools\package-beta.ps1 keeps one per build).

#include <string>

namespace app {

// After --data-dir is applied. The LUMEN_CRASH_DIR environment variable (an existing folder)
// redirects the reports, for test runs.
void InstallCrashHandler(const std::wstring& version);

// The .txt of a crash in an earlier session, once, or empty. Also keeps only the 5 newest reports.
std::wstring TakeCrashNotice();

// --crash-test (--automation only): crash on purpose, through std::terminate when `terminate`.
void CrashForTest(bool terminate);

}  // namespace app
