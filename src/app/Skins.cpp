#include "app/Skins.h"

#include "Trace.h"
#include "core/Config.h"
#include "core/Json.h"
#include "core/Paths.h"
#include "ui/Host.h"
#include "ui/Theme.h"

namespace app {

namespace {

constexpr UINT kMsgChanged = WM_APP + 1;
constexpr wchar_t kNotifyClass[] = L"LumenSkinWatch";

// Built-in skins: res/skins/*.json, embedded by res/app.rc. "lumen" is the brand itself.
struct BuiltinDef {
    const char* id;
    const wchar_t* resource;
};
constexpr BuiltinDef kBuiltins[] = {
    {"lumen", nullptr},
    {"oled", L"LUMEN_SKIN_OLED"},
    {"contrast", L"LUMEN_SKIN_CONTRAST"},
    {"sepia", L"LUMEN_SKIN_SEPIA"},
};

std::string Resource(const wchar_t* name) {
    HRSRC res = FindResourceW(nullptr, name, RT_RCDATA);
    HGLOBAL mem = res ? LoadResource(nullptr, res) : nullptr;
    const char* data = mem ? static_cast<const char*>(LockResource(mem)) : nullptr;
    return data ? std::string(data, SizeofResource(nullptr, res)) : std::string();
}

struct Stamp {
    FILETIME write{};
    ULARGE_INTEGER size{};
    bool exists = false;
    bool operator==(const Stamp& o) const {
        return exists == o.exists && write.dwLowDateTime == o.write.dwLowDateTime &&
               write.dwHighDateTime == o.write.dwHighDateTime && size.QuadPart == o.size.QuadPart;
    }
};

Stamp StampOf(const std::wstring& path) {
    Stamp s;
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa)) {
        s.exists = true;
        s.write = fa.ftLastWriteTime;
        s.size.LowPart = fa.nFileSizeLow;
        s.size.HighPart = fa.nFileSizeHigh;
    }
    return s;
}

// Worker: waits for changes in the skin's folder. Editors save in steps (truncate, write, rename
// over), so it waits until the folder has been quiet for 150 ms, then posts once if the file
// itself changed. Blocks in the kernel between changes: no polling.
void WatchLoop(std::wstring dir, std::wstring file, HANDLE stop, HWND notify) {
    HANDLE change = FindFirstChangeNotificationW(
        dir.c_str(), FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE);
    if (change == INVALID_HANDLE_VALUE) return;
    Stamp last = StampOf(file);
    const HANDLE handles[2] = {stop, change};
    for (;;) {
        DWORD r = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        if (r != WAIT_OBJECT_0 + 1) break;
        FindNextChangeNotification(change);
        while ((r = WaitForMultipleObjects(2, handles, FALSE, 150)) == WAIT_OBJECT_0 + 1)
            FindNextChangeNotification(change);
        if (r != WAIT_TIMEOUT) break;
        const Stamp now = StampOf(file);
        if (now == last) continue;
        last = now;
        PostMessageW(notify, kMsgChanged, 0, 0);
    }
    FindCloseChangeNotification(change);
}

}  // namespace

Skins& Skins::Get() {
    static Skins skins;
    return skins;
}

Skins::~Skins() {
    StopWatch();
}

const std::vector<BuiltinSkin>& Skins::Builtins() {
    if (builtinsLoaded_) return builtins_;
    builtinsLoaded_ = true;
    for (const BuiltinDef& d : kBuiltins) {
        BuiltinSkin b;
        b.id = d.id;
        if (d.resource) {
            skin::ParseResult r = skin::Parse(Resource(d.resource));
            b.name = r.skin.name;
            b.nameRu = r.skin.nameRu;
            if (r.ok) b.skin = std::make_shared<const skin::Skin>(std::move(r.skin));
        } else {
            b.name = L"Lumen";
        }
        if (b.name.empty()) b.name = json::ToWide(b.id);
        builtins_.push_back(std::move(b));
    }
    return builtins_;
}

void Skins::Start(Config& config) {
    config_ = &config;
    config.Subscribe("interface.", [this](const std::string& key) {
        if (key == "interface.skin" || key == "interface.customSkin" || key == "interface.skinFile") Apply(false);
    });
    Apply(false);
}

void Skins::Stop() {
    StopWatch();
}

void Skins::Apply(bool reload) {
    if (!config_) return;
    const std::wstring file = config_->GetWString("interface.skinFile");
    const bool custom = config_->GetBool("interface.customSkin") && !file.empty();
    const std::string id = config_->GetString("interface.skin");
    const std::string source = custom ? "file:" + json::ToUtf8(file) : "builtin:" + id;
    if (source == source_ && !reload) return;
    const bool first = source_.empty();
    source_ = source;
    custom_ = custom;
    failed_ = false;
    warnings_.clear();

    std::shared_ptr<const skin::Skin> s;
    if (custom) {
        std::string text;
        if (!json::ReadFileUtf8(file, &text)) {
            failed_ = true;
            warnings_.push_back("cannot read the file; the Lumen skin is used");
        } else {
            skin::ParseResult r = skin::Parse(text);
            warnings_ = std::move(r.warnings);
            failed_ = !r.ok;
            if (r.ok) s = std::make_shared<const skin::Skin>(std::move(r.skin));
        }
        StartWatch(file);
    } else {
        StopWatch();
        for (const BuiltinSkin& b : Builtins())
            if (b.id == id) s = b.skin;
    }
    // A first apply of the brand look changes nothing: skip the repaint (startup stays as it was).
    if (first && !s && !custom) return;
    ui::Theme::Get().SetSkin(std::move(s));
    for (ui::Host* h : ui::Host::All()) h->OnThemeChanged();
    if (custom) WriteLog(file);
    Trace(custom ? "skin applied (file)" : "skin applied (built-in)");
    changed.Emit();
}

void Skins::WriteLog(const std::wstring& path) {
    // Skin authors read this: what was ignored and why, and what fails the contrast check.
    std::string log = "Lumen skin log\r\nfile: " + json::ToUtf8(path) + "\r\n";
    if (warnings_.empty()) log += "no warnings\r\n";
    for (const std::string& w : warnings_) log += "warning: " + w + "\r\n";
    if (!failed_) {
        const auto issues = skin::CheckContrast(ui::Theme::Get().Skin(), ui::Theme::Get().Accent());
        if (issues.empty()) log += "contrast: every text and icon token passes\r\n";
        for (const skin::ContrastIssue& i : issues) log += "contrast: " + skin::Describe(i) + "\r\n";
    }
    logPath_ = DataFilePath(L"skin.log");
    if (!logPath_.empty() && !json::WriteFileAtomic(logPath_, log)) logPath_.clear();
    for (const std::string& w : warnings_) OutputDebugStringA(("Lumen skin: " + w + "\n").c_str());
}

LRESULT CALLBACK Skins::NotifyProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == kMsgChanged) {
        Get().OnFileChanged();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void Skins::OnFileChanged() {
    if (custom_) Apply(true);
}

void Skins::StartWatch(const std::wstring& path) {
    if (watcher_.joinable() && watched_ == path) return;
    StopWatch();
    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return;
    if (!notify_) {
        static bool registered = false;
        if (!registered) {
            WNDCLASSW wc{};
            wc.lpfnWndProc = NotifyProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = kNotifyClass;
            registered = RegisterClassW(&wc) != 0;
        }
        notify_ = CreateWindowExW(0, kNotifyClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr),
                                  nullptr);
    }
    stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!notify_ || !stop_) return;
    watched_ = path;
    watcher_ = std::thread(WatchLoop, path.substr(0, slash), path, stop_, notify_);
}

void Skins::StopWatch() {
    if (watcher_.joinable()) {
        SetEvent(stop_);
        watcher_.join();
    }
    if (stop_) CloseHandle(stop_);
    stop_ = nullptr;
    watched_.clear();
    if (notify_) DestroyWindow(notify_);
    notify_ = nullptr;
}

}  // namespace app
