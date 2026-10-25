#pragma once
// Skins (design/SKINS.md): picks the built-in skin or the user's skin file from the
// interface.skin / customSkin / skinFile settings, hands it to ui::Theme and repaints every window.
// A custom file is watched for changes (a directory change notification on a worker thread,
// debounced) and reloaded live. With a built-in skin there is no thread and no timer.
#include "app/Signal.h"
#include "core/Skin.h"

#include <windows.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

class Config;

namespace app {

struct BuiltinSkin {
    std::string id;  // interface.skin value
    std::shared_ptr<const skin::Skin> skin;  // nullptr for "moonbow"
    std::wstring name, nameRu;
};

class Skins {
public:
    static Skins& Get();
    ~Skins();

    void Start(Config& config);  // applies the current settings and follows them
    void Stop();                 // ends the file watcher (before the windows go)

    // Parsed once, on first use (Settings builds its list from them).
    const std::vector<BuiltinSkin>& Builtins();

    bool CustomActive() const { return custom_; }
    // Problems of the custom file: unknown keys, bad values, or why it could not be used.
    const std::vector<std::string>& Warnings() const { return warnings_; }
    bool Failed() const { return failed_; }  // the custom file could not be read or parsed
    const std::wstring& LogPath() const { return logPath_; }

    Signal<> changed;  // after every apply, including a hot reload

private:
    Skins() = default;
    void Apply(bool reload);
    void StartWatch(const std::wstring& path);
    void StopWatch();
    void OnFileChanged();
    void WriteLog(const std::wstring& path);
    static LRESULT CALLBACK NotifyProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    Config* config_ = nullptr;
    std::vector<BuiltinSkin> builtins_;
    bool builtinsLoaded_ = false;
    std::string source_;  // what is applied: "builtin:<id>" or "file:<path>"
    bool custom_ = false;
    bool failed_ = false;
    std::vector<std::string> warnings_;
    std::wstring logPath_;

    // Hot reload: only while a custom skin is active.
    std::wstring watched_;
    HWND notify_ = nullptr;   // message-only window on the UI thread
    HANDLE stop_ = nullptr;   // event: ends the watcher
    std::thread watcher_;
};

}  // namespace app
