#pragma once
// Moonbow's own log for bug reports, always on: <data>\logs\moonbow.log, one "hh:mm:ss.mmm text" line
// per event (start, GPU, files, mpv warnings and errors, exit). The previous session is kept as
// moonbow.1.log, so the session that crashed survives the next start. A session stops at 4 MB.
// Crash reports (app/CrashReport.h) go to the same folder: a tester sends one folder.

#include <string>
#include <string_view>

// DataDir() + L"\\logs", created on first use; empty when DataDir() is.
std::wstring LogDir();

// Once, after ClaimDataDir(). Does nothing when another Moonbow owns the data folder: it keeps the log.
void OpenLog(std::wstring_view version);

// Thread-safe. Dropped before OpenLog() and after the size limit.
void Log(std::string_view line);

// At exit; OpenLog() may start a new session after it.
void CloseLog();
