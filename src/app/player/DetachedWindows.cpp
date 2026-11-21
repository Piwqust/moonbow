#include "app/player/DetachedWindows.h"

#include "Util.h"
#include "app/Actions.h"
#include "app/App.h"
#include "app/I18n.h"
#include "app/MainWindow.h"
#include "app/player/DetachedPanels.h"
#include "app/player/MetersView.h"
#include "app/player/PanelTabs.h"
#include "audio/LoopbackSupport.h"
#include "core/Json.h"
#include "core/Paths.h"
#include "gfx/Gpu.h"
#include "ui/Controls.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>

namespace app {

using namespace ui;

namespace {

Config& Settings() {
    return TheApp().config();
}

constexpr float kCaptionH = 40;

// A part of the player that opens in a window of its own.
struct PanelDef {
    const char* id;
    const wchar_t* name;  // English; Tr() translates
    const char* icon;
    float w, h, minW, minH;  // DIP, client area with the caption row
    float aspect;            // body width / height kept while the edges are dragged; 0: free
    bool audio;              // draws Moonbow's sound: needs process loopback (Windows 10 2004)
    const char* setting;     // the Settings row that opens it, or nullptr
};

const PanelDef kPanels[] = {
    {"controls", L"Player controls", "playback", 560, 140, 380, 140, 0, false, "interface.controlsWindow"},
    {"seekbar", L"Seek bar", "waveform_twotone", 720, 92, 320, 92, 0, false, nullptr},
    {"cover", L"Cover", "album", 420, 460, 160, 200, 1, false, nullptr},
    {"nowplaying", L"Now playing", "music", 460, 560, 280, 220, 0, false, nullptr},
    {"lyrics", L"Song lyrics", "subtitle-2", 460, 600, 260, 240, 0, false, nullptr},
    {"queue", L"Queue", "queue", 420, 640, 320, 320, 0, false, nullptr},
    {"viz", L"Visualization", "spectrum_twotone", 640, 320, 240, 160, 0, true, nullptr},
    {"meters", L"Audio meters", "vu_twotone", 980, 720, 700, 580, 0, true, "interface.metersWindow"},
    {"vu", L"VU meters", "vu_twotone", 720, 360, 360, 220, 0, true, nullptr},
    {"vu.left", L"VU meter, left", "vu_twotone", 420, 340, 240, 200, 0, true, nullptr},
    {"vu.right", L"VU meter, right", "vu_twotone", 420, 340, 240, 200, 0, true, nullptr},
    {"loudness", L"Loudness", "audio", 360, 440, 280, 340, 0, true, nullptr},
    {"levels", L"Levels", "eq", 640, 196, 420, 196, 0, true, nullptr},
    {"stereo", L"Stereo", "scope_twotone", 360, 480, 260, 380, 0, true, nullptr},
    {"spectrum", L"Spectrum", "spectrum_twotone", 640, 320, 360, 220, 0, true, nullptr},
};

const PanelDef* FindDef(const std::string& id) {
    for (const PanelDef& d : kPanels)
        if (id == d.id) return &d;
    return nullptr;
}

bool Available(const PanelDef& d) {
    return !d.audio || audio::ProcessLoopbackSupported();
}

// --- where the windows were (windows.json) ------------------------------------------------------

struct Place {
    int x = 0, y = 0, w = 0, h = 0;  // the window rect in screen pixels; w 0: never placed
    bool onTop = false, frameless = false;
};
std::map<std::string, Place> g_places;  // by panel id, also for closed windows
std::vector<std::string> g_open;        // the windows open now (and at the next start), in order
bool g_quitting = false;

std::wstring LayoutPath() {
    return DataFilePath(L"windows.json");
}

void LoadLayout() {
    std::string text;
    if (!json::ReadFileUtf8(LayoutPath(), &text)) {
        // Before windows.json: the meters' own "keep above" setting.
        g_places["meters"].onTop = Settings().GetBool("interface.metersOnTop");
        return;
    }
    const std::optional<json::Value> doc = json::Parse(text);
    if (!doc || !doc->IsObject()) return;
    if (const json::Value* open = doc->Find("open"))
        for (const json::Value& v : open->AsArray())
            if (FindDef(v.AsString()) && std::find(g_open.begin(), g_open.end(), v.AsString()) == g_open.end())
                g_open.push_back(v.AsString());
    if (const json::Value* places = doc->Find("places"))
        for (const json::Member& m : places->AsObject()) {
            if (!FindDef(m.key)) continue;
            Place pl;
            if (const json::Value* r = m.value.Find("rect"); r && r->size() == 4) {
                pl.x = static_cast<int>((*r)[0].AsInt());
                pl.y = static_cast<int>((*r)[1].AsInt());
                pl.w = static_cast<int>((*r)[2].AsInt());
                pl.h = static_cast<int>((*r)[3].AsInt());
            }
            if (const json::Value* v = m.value.Find("onTop")) pl.onTop = v->AsBool();
            if (const json::Value* v = m.value.Find("frameless")) pl.frameless = v->AsBool();
            g_places[m.key] = pl;
        }
}

void SaveLayout() {
    json::Value doc = json::Value::MakeObject();
    doc.Set("version", 1);
    json::Value open = json::Value::MakeArray();
    for (const std::string& id : g_open) open.Push(id);
    doc.Set("open", std::move(open));
    json::Value places = json::Value::MakeObject();
    for (const auto& [id, pl] : g_places) {
        json::Value p = json::Value::MakeObject();
        if (pl.w > 0) {
            json::Value r = json::Value::MakeArray();
            for (int v : {pl.x, pl.y, pl.w, pl.h}) r.Push(v);
            p.Set("rect", std::move(r));
        }
        p.Set("onTop", pl.onTop);
        p.Set("frameless", pl.frameless);
        places.Set(id, std::move(p));
    }
    doc.Set("places", std::move(places));
    json::WriteFileAtomic(LayoutPath(), json::Serialize(doc, true));
}

// The remembered rect, moved (and if need be shrunk) onto a monitor that is still there.
RECT OnScreen(const Place& pl) {
    RECT r{pl.x, pl.y, pl.x + pl.w, pl.y + pl.h};
    HMONITOR mon = MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(mi)};
    if (!GetMonitorInfoW(mon, &mi)) return r;
    const RECT& work = mi.rcWork;
    RECT seen;
    // Enough of it on some screen to grab: leave it where it was.
    if (IntersectRect(&seen, &r, &work) && seen.right - seen.left >= 120 && seen.bottom - seen.top >= 48) return r;
    const int w = std::min<int>(pl.w, work.right - work.left), h = std::min<int>(pl.h, work.bottom - work.top);
    const int x = std::clamp<int>(pl.x, work.left, work.right - w), y = std::clamp<int>(pl.y, work.top, work.bottom - h);
    return {x, y, x + w, y + h};
}

// --- the window ---------------------------------------------------------------------------------

class PanelWindow;
std::vector<PanelWindow*> g_windows;

// A tool window with one panel: a caption row (name, the panel's tools, close) over it. The whole
// window drags by any part that is not a control, the caption row or not; a double click there
// (outside the caption row) is fullscreen, Esc leaves it.
class PanelWindow : public Host {
public:
    explicit PanelWindow(const PanelDef& def) : def_(def) {}

    const PanelDef& Def() const { return def_; }
    bool ClosedByUser() const { return closedByUser_; }
    Place& Where() { return g_places[def_.id]; }

    bool Open() {
        const Place pl = Where();
        HostOptions o;
        o.title = std::wstring(Tr(def_.name)) + L" — Moonbow";
        o.toolWindow = true;
        o.showInTaskbar = false;
        o.width = def_.w;
        o.height = def_.h;
        o.minWidth = def_.minW;
        o.minHeight = pl.frameless ? std::max(60.0f, def_.minH - kCaptionH) : def_.minH;
        o.center = pl.w <= 0;
        if (!Create(o)) return false;

        auto root = std::make_unique<Panel>();
        root->fill = [] { return P().bgBase; };
        root->dir = Dir::Column;
        root->ncRole = NcRole::Caption;  // drag by anything that is not a control
        root->onContextMenu = [this](const MouseEvent& e) {
            std::vector<MenuItem> items;
            AppendPanelWindowMenu(this, items);
            ShowContextMenu(this, {e.x, e.y}, std::move(items));
        };
        header_ = root->Add<Panel>();
        header_->fill = [] { return P().bgChrome; };
        header_->dir = Dir::Row;
        header_->align = Align::Center;
        header_->width = Length::Grow();
        header_->height = Length::Px(kCaptionH);
        header_->pad = {16, 0, 0, 0};
        header_->gap = 8;
        header_->Add<IconView>("moonbow", 16, Tone::Primary);
        caption_ = header_->Add<Label>(Tr(def_.name), TextStyle::Body, Tone::Secondary);
        caption_->width = Length::Grow();
        const std::string id = def_.id;
        if (id == "meters" || id == "loudness" || id == "levels") AddMetersTools(header_);
        auto* close = header_->Add<IconButton>("close", Tr(L"Close"), 40, 20);
        close->height = Length::Px(kCaptionH);
        close->onClick = [this] { Close(); };
        header_->visible = !pl.frameless;
        auto* body = root->Add<Widget>();
        body->width = Length::Grow();
        body->height = Length::Grow();
        body->dir = Dir::Column;
        Build(body);
        SetRoot(std::move(root));

        // The player's shortcuts work here too (Space, arrows, M ...); Esc leaves fullscreen.
        onKey = [this](const KeyEvent& e) {
            if (e.key == VK_ESCAPE && IsFullscreen()) {
                SetFullscreen(false);
                return true;
            }
            const Action a = ActionForKey(e);
            if (a == Action::None) return false;
            Run(a);
            return true;
        };
        onCloseRequest = [this] {
            closedByUser_ = true;
            Remember();
            return true;
        };
        if (pl.w > 0) {
            // Twice: the first move may cross to a monitor of another scale, whose WM_DPICHANGED
            // resizes the window; the second gives it back the size it had.
            const RECT r = OnScreen(pl);
            for (int i = 0; i < 2; ++i)
                SetWindowPos(Hwnd(), nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
        }
        ApplyOnTop();
        Show(SW_SHOWNOACTIVATE);
        if (gfx::Gpu::Get().Ready()) OnDeviceReady();
        return true;
    }

    // The window's rect for the next time; not while maximized, fullscreen or minimized.
    void Remember() {
        HWND h = Hwnd();
        if (!h || IsFullscreen() || IsZoomed(h) || IsIconic(h)) return;
        RECT r;
        if (!GetWindowRect(h, &r)) return;
        Place& pl = Where();
        pl.x = r.left;
        pl.y = r.top;
        pl.w = r.right - r.left;
        pl.h = r.bottom - r.top;
    }

    bool OnTop() { return Where().onTop; }
    void SetOnTop(bool on) {
        Where().onTop = on;
        ApplyOnTop();
        SaveLayout();
    }
    bool Frameless() { return Where().frameless; }
    void SetFrameless(bool on) {
        Where().frameless = on;
        header_->visible = !on;
        // The panel keeps its size: the window grows or shrinks by the caption row.
        HWND h = Hwnd();
        RECT r;
        if (h && !IsFullscreen() && !IsZoomed(h) && GetWindowRect(h, &r)) {
            const int cap = static_cast<int>(std::lround(kCaptionH * Scale()));
            SetWindowPos(h, nullptr, r.left, r.top + (on ? cap : -cap), r.right - r.left,
                         r.bottom - r.top + (on ? -cap : cap), SWP_NOZORDER | SWP_NOACTIVATE);
        }
        RequestLayout();
        Remember();
        SaveLayout();
    }

protected:
    bool HandleMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT* result) override {
        switch (msg) {
            case WM_SIZING: {
                // The cover keeps its square while the edges are dragged.
                if (def_.aspect <= 0 || IsFullscreen()) return false;
                RECT& r = *reinterpret_cast<RECT*>(lp);
                const SIZE band = FrameBand();
                const double cap = Frameless() ? 0.0 : kCaptionH * Scale();
                if (wp == WMSZ_TOP || wp == WMSZ_BOTTOM) {
                    const double h = r.bottom - r.top - band.cy - cap;
                    r.right = r.left + static_cast<LONG>(std::lround(h * def_.aspect)) + 2 * band.cx;
                } else {
                    const double w = r.right - r.left - 2 * band.cx;
                    const LONG h = static_cast<LONG>(std::lround(w / def_.aspect + cap)) + band.cy;
                    if (wp == WMSZ_TOPLEFT || wp == WMSZ_TOPRIGHT) r.top = r.bottom - h;
                    else r.bottom = r.top + h;
                }
                *result = TRUE;
                return true;
            }
            case WM_EXITSIZEMOVE:
                Remember();
                SaveLayout();
                return false;
            case WM_NCRBUTTONDOWN:
                if (wp != HTCAPTION) return false;
                *result = 0;
                return true;
            case WM_NCRBUTTONUP: {
                // The panel is the caption (drag to move): its menu, not the Windows system menu.
                if (wp != HTCAPTION) return false;
                const Point at = ClientPoint(lp);
                for (Widget* w = Root() ? Root()->HitTest(at.x, at.y) : nullptr; w; w = w->Parent())
                    if (w->onContextMenu) {
                        MouseEvent e;
                        e.x = at.x;
                        e.y = at.y;
                        e.button = MouseButton::Right;
                        w->onContextMenu(e);
                        break;
                    }
                *result = 0;
                return true;
            }
            case WM_NCLBUTTONDBLCLK: {
                // On the panel: fullscreen. On the caption row: maximize, as usual.
                if (wp != HTCAPTION) return false;
                const Point at = ClientPoint(lp);
                if (header_->visible && at.y < kCaptionH && !IsFullscreen()) return false;
                Post([this] { SetFullscreen(!IsFullscreen()); });
                *result = 0;
                return true;
            }
            default: return false;
        }
    }

private:
    Point ClientPoint(LPARAM lp) const {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(Hwnd(), &pt);
        return {pt.x / Scale(), pt.y / Scale()};
    }

    void ApplyOnTop() {
        if (Hwnd())
            SetWindowPos(Hwnd(), Where().onTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    void Build(Widget* body) {
        const std::string id = def_.id;
        auto card = [body](MeterCard c) {
            body->pad = {8, 8, 8, 8};
            AddMeterCard(body, c);
        };
        if (id == "controls") {
            AddControlsPanel(body, [this](const std::wstring& text) {
                if (caption_->Text() != text) caption_->SetText(text);
            });
        } else if (id == "seekbar") {
            AddSeekPanel(body);
        } else if (id == "cover" || id == "nowplaying") {
            AddCoverPanel(body, id == "nowplaying");
        } else if (id == "lyrics") {
            AddLyricsPanel(body);
        } else if (id == "queue") {
            body->AddChild(panel::BuildQueueTab(false, true, false));
        } else if (id == "viz") {
            AddVizPanel(body);
        } else if (id == "meters") {
            AddMetersView(body);
        } else if (id == "vu") {
            card(MeterCard::Vu);
        } else if (id == "vu.left") {
            card(MeterCard::VuLeft);
        } else if (id == "vu.right") {
            card(MeterCard::VuRight);
        } else if (id == "loudness") {
            card(MeterCard::Loudness);
        } else if (id == "levels") {
            card(MeterCard::Levels);
        } else if (id == "stereo") {
            card(MeterCard::Stereo);
        } else if (id == "spectrum") {
            card(MeterCard::Spectrum);
        }
    }

    const PanelDef& def_;
    Panel* header_ = nullptr;
    Label* caption_ = nullptr;
    bool closedByUser_ = false;
};

PanelWindow* FindWindow(const std::string& id) {
    for (PanelWindow* w : g_windows)
        if (id == w->Def().id && w->Hwnd()) return w;
    return nullptr;
}

PanelWindow* WindowOf(const Host* host) {
    for (PanelWindow* w : g_windows)
        if (w == host) return w;
    return nullptr;
}

bool OpenWindow(const PanelDef& def) {
    if (FindWindow(def.id)) return true;
    if (!Available(def) || g_quitting) return false;
    auto* w = new PanelWindow(def);
    g_windows.push_back(w);
    w->onDestroyed = [w] {
        std::erase(g_windows, w);
        // Closed with X (or from a menu): gone for the next start too, and its Settings row follows.
        // Going away with the app keeps it for the next start.
        if (w->ClosedByUser() && !g_quitting) {
            std::erase(g_open, std::string(w->Def().id));
            if (w->Def().setting) Settings().SetBool(w->Def().setting, false);
            SaveLayout();
        }
        // Deleting a Host from inside its own WM_DESTROY is not safe; do it after the message.
        MainWindow* main = TheApp().main();
        if (main && main->Hwnd()) main->Post([w] { delete w; });
    };
    if (!w->Open()) {
        std::erase(g_windows, w);
        delete w;
        return false;
    }
    if (std::find(g_open.begin(), g_open.end(), def.id) == g_open.end()) g_open.push_back(def.id);
    w->Remember();
    SaveLayout();
    return true;
}

void CloseWindow(const PanelDef& def) {
    if (PanelWindow* w = FindWindow(def.id)) w->Close();
}

// The windows that follow a Settings row.
void Sync() {
    for (const PanelDef& d : kPanels) {
        if (!d.setting) continue;
        if (Settings().GetBool(d.setting) && Available(d)) OpenWindow(d);
        else CloseWindow(d);
    }
}

}  // namespace

void InitDetachedWindows() {
    static Config::Token token = 0;
    if (token) return;
    // Posted: a setting may change from inside a window that is about to close.
    token = Settings().Subscribe("interface.", [](const std::string& key) {
        if (key == "interface.controlsWindow" || key == "interface.metersWindow")
            if (MainWindow* main = TheApp().main()) main->Post(Sync);
    });
    LoadLayout();
    // Last time's windows, in the order they were opened; the controls and the meters follow
    // their Settings rows instead.
    const std::vector<std::string> open = g_open;
    for (const std::string& id : open)
        if (const PanelDef* d = FindDef(id); d && !d->setting) OpenWindow(*d);
    Sync();
}

void CloseDetachedWindows() {
    if (g_quitting) return;
    g_quitting = true;
    for (PanelWindow* w : g_windows) w->Remember();
    SaveLayout();
    const std::vector<PanelWindow*> windows = g_windows;
    for (PanelWindow* w : windows) w->Destroy();
}

bool PanelWindowOpen(const std::string& id) {
    return FindWindow(id) != nullptr;
}

bool SetPanelWindow(const std::string& id, bool open) {
    const PanelDef* d = FindDef(id);
    if (!d || !Available(*d)) return false;
    if (d->setting) Settings().SetBool(d->setting, open);
    if (open) return OpenWindow(*d);
    CloseWindow(*d);
    return true;
}

void TogglePanelWindow(const std::string& id) {
    SetPanelWindow(id, !PanelWindowOpen(id));
}

std::string PanelWindowsState() {
    std::string out;
    for (const PanelDef& d : kPanels) {
        out += d.id;
        out += PanelWindowOpen(d.id) ? " open\n" : Available(d) ? " closed\n" : " unavailable\n";
    }
    return out;
}

std::vector<MenuItem> PanelWindowItems() {
    std::vector<MenuItem> items;
    bool audioPart = false;
    for (const PanelDef& d : kPanels) {
        if (!Available(d)) continue;
        // The player's parts, then Moonbow's sound.
        if (d.audio && !audioPart) {
            audioPart = true;
            if (!items.empty()) items.push_back(MenuItem::Separator());
        }
        MenuItem m{Tr(d.name), L"", d.icon};
        if (std::string(d.id) == "meters") m.shortcut = ShortcutText(Action::AudioMeters);
        m.checked = PanelWindowOpen(d.id);
        const std::string id = d.id;
        m.action = [id] { TogglePanelWindow(id); };
        items.push_back(std::move(m));
    }
    return items;
}

void AppendPanelWindowMenu(Host* host, std::vector<MenuItem>& items) {
    PanelWindow* w = WindowOf(host);
    if (!w) return;
    if (!items.empty() && !items.back().separator) items.push_back(MenuItem::Separator());
    MenuItem top{Tr(L"Keep above other windows")};
    top.checked = w->OnTop();
    top.action = [w, on = !w->OnTop()] { w->SetOnTop(on); };
    items.push_back(std::move(top));
    MenuItem bar{Tr(L"Show the title bar")};
    bar.checked = !w->Frameless();
    // Posted: the window resizes, and the menu that runs this belongs to it.
    bar.action = [w, on = !w->Frameless()] { w->Post([w, on] { w->SetFrameless(on); }); };
    items.push_back(std::move(bar));
    MenuItem close{Tr(L"Close window"), L"", "close"};
    close.action = [w] { w->Post([w] { w->Close(); }); };
    items.push_back(std::move(close));
}

}  // namespace app
