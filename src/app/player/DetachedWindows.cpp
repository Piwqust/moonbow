#include "app/player/DetachedWindows.h"

#include "Util.h"
#include "app/Actions.h"
#include "app/App.h"
#include "app/I18n.h"
#include "app/MainWindow.h"
#include "app/player/ControlBar.h"
#include "app/player/MetersView.h"
#include "app/player/NowPlaying.h"
#include "audio/LoopbackSupport.h"
#include "gfx/Gpu.h"
#include "ui/Anim.h"
#include "ui/Controls.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace app {

using namespace ui;

namespace {

Config& Settings() { return TheApp().config(); }

// A small window owned by the main one: a caption row with the name and a close button over
// `body`. Closing it with X turns its setting off; going away with the main window does not.
class DetachedWindow : public Host {
public:
    DetachedWindow(const char* key, const wchar_t* name) : key_(key), name_(name) {}

    bool Open(float w, float h, float minW, float minH) {
        HostOptions o;
        o.title = std::wstring(Tr(name_)) + L" — Moonbow";
        MainWindow* main = TheApp().main();
        o.owner = main ? main->Hwnd() : nullptr;
        o.showInTaskbar = o.owner == nullptr;
        o.width = w;
        o.height = h;
        o.minWidth = minW;
        o.minHeight = minH;
        if (!Create(o)) return false;

        auto root = std::make_unique<Panel>();
        root->fill = [] { return P().bgBase; };
        root->dir = Dir::Column;
        // The caption row of the main window: the logo, the name, the window's own tools, close.
        auto* header = root->Add<Panel>();
        header->fill = [] { return P().bgChrome; };
        header->dir = Dir::Row;
        header->align = Align::Center;
        header->width = Length::Grow();
        header->height = Length::Px(40);
        header->pad = {16, 0, 0, 0};
        header->gap = 8;
        header->ncRole = NcRole::Caption;
        header->Add<IconView>("moonbow", 16, Tone::Primary);
        caption_ = header->Add<Label>(Tr(name_), TextStyle::Body, Tone::Secondary);
        caption_->width = Length::Grow();
        body_ = root->Add<Widget>();
        body_->width = Length::Grow();
        body_->height = Length::Grow();
        body_->dir = Dir::Column;
        Build(body_);
        BuildTools(header);
        auto* close = header->Add<IconButton>("close", Tr(L"Close"), 40, 20);
        close->height = Length::Px(40);
        close->onClick = [this] { Close(); };
        SetRoot(std::move(root));

        // The player's shortcuts work here too (Space, arrows, M ...).
        onKey = [](const KeyEvent& e) {
            const Action a = ActionForKey(e);
            if (a == Action::None) return false;
            Run(a);
            return true;
        };
        onCloseRequest = [this] {
            closedByUser_ = true;
            return true;
        };
        Show(SW_SHOWNOACTIVATE);
        if (gfx::Gpu::Get().Ready()) OnDeviceReady();
        return true;
    }

    const char* Key() const { return key_; }
    bool ClosedByUser() const { return closedByUser_; }

protected:
    virtual void Build(Widget* body) = 0;
    virtual void BuildTools(Widget* /*header*/) {}  // between the name and the close button
    Label* caption_ = nullptr;

private:
    const char* key_;
    const wchar_t* name_;
    Widget* body_ = nullptr;
    bool closedByUser_ = false;
};

// --- Player controls: what plays, the seek bar (the song's wave for music), the buttons and the
// volume.
class ControlsWindow : public DetachedWindow {
public:
    ControlsWindow() : DetachedWindow("interface.controlsWindow", L"Player controls") {}

protected:
    void Build(Widget* body) override {
        body->pad = {12, 6, 12, 8};
        body->gap = 2;
        body->Add<SeekBar>();
        auto* row = body->Add<Widget>();
        row->dir = Dir::Row;
        row->align = Align::Center;
        row->width = Length::Grow();
        row->gap = 4;
        time_ = row->Add<Label>(L"", TextStyle::MonoSmall, Tone::Secondary);
        time_->width = Length::Px(120);
        row->Add<Widget>()->width = Length::Grow();
        auto* prev = row->Add<IconButton>("previous", Tr(L"Previous"), 36, 22);
        prev->onClick = [] { Run(Action::Previous); };
        play_ = row->Add<IconButton>("play", Tr(L"Play"), 44, 26);
        play_->tone = Tone::Primary;
        play_->onClick = [] { Run(Action::PlayPause); };
        auto* next = row->Add<IconButton>("next", Tr(L"Next"), 36, 22);
        next->onClick = [] { Run(Action::Next); };
        row->Add<Widget>()->width = Length::Grow();
        mute_ = row->Add<IconButton>("volume_value-high", Tr(L"Mute"), 32, 20);
        mute_->onClick = [] { TheApp().player().ToggleMute(); };
        // shortcut: 0-100 %; a volume above 100 (Settings > Audio > Maximum volume) shows as full.
        volume_ = row->Add<Slider>(0.0f, 100.0f, 100.0f, 1.0f);
        volume_->width = Length::Px(96);
        volume_->format = [](float v) { return std::to_wstring(static_cast<int>(std::lround(v))) + L" %"; };
        volume_->onChange = [](float v) { TheApp().player().SetVolume(v); };
        conn_ = Connect(TheApp().playerChanged, [this](unsigned) { Refresh(); });
        Refresh();
    }

private:
    void Refresh() {
        const PlayerState& s = TheApp().player().State();
        const PlayingTags& tags = CurrentTags();
        std::wstring name = tags.title.empty() ? (s.path.empty() ? std::wstring(Tr(L"Player controls")) : FileNameOf(s.path))
                                               : tags.title;
        if (!tags.artist.empty()) name += L" · " + tags.artist;
        if (caption_->Text() != name) caption_->SetText(name);
        const bool hours = s.duration >= 3600;
        const double pos = s.eof && s.duration > 0 ? s.duration : s.position;
        time_->SetText(s.fileLoaded ? FormatTime(pos, hours) + L" / " + FormatTime(s.duration, hours) : L"");
        const bool playing = s.fileLoaded && !s.paused && !s.eof;
        play_->icon = playing ? "pause" : "play";
        play_->tooltip = WithKey(Tr(playing ? L"Pause" : L"Play"), Action::PlayPause);
        mute_->icon = VolumeIcon(s.volume, s.mute);
        if (!volume_->IsPressed()) volume_->SetValue(static_cast<float>(std::min(s.volume, 100.0)));
        play_->Invalidate();
        mute_->Invalidate();
    }

    Label* time_ = nullptr;
    IconButton* play_ = nullptr;
    IconButton* mute_ = nullptr;
    Slider* volume_ = nullptr;
    ScopedConnection conn_;
};

class MetersWindow : public DetachedWindow {
public:
    MetersWindow() : DetachedWindow("interface.metersWindow", L"Audio meters") {}

protected:
    void Build(Widget* body) override { view_ = AddMetersView(body); }
    void BuildTools(Widget* header) override { AddMetersTools(header, view_); }

private:
    Widget* view_ = nullptr;
};

DetachedWindow* g_controls = nullptr;
DetachedWindow* g_meters = nullptr;

template <typename W>
void Follow(DetachedWindow*& slot, bool want) {
    if (!want) {
        if (slot && slot->Hwnd()) slot->Close();
        return;
    }
    if (slot && slot->Hwnd()) return;
    auto* w = new W();
    w->onDestroyed = [w, &slot] {
        if (slot == w) slot = nullptr;
        // Closed with X: the setting follows. With the main window (quitting) it stays on, so
        // the window comes back next time.
        if (w->ClosedByUser()) Settings().SetBool(w->Key(), false);
        // Deleting a Host from inside its own WM_DESTROY is not safe; do it after the message.
        MainWindow* main = TheApp().main();
        if (main && main->Hwnd()) main->Post([w] { delete w; });
    };
    slot = w;
    const bool controls = std::is_same_v<W, ControlsWindow>;
    if (!(controls ? w->Open(560, 140, 380, 140) : w->Open(980, 720, 700, 580))) {
        slot = nullptr;
        delete w;
    }
}

void Sync() {
    Follow<ControlsWindow>(g_controls, Settings().GetBool("interface.controlsWindow"));
    Follow<MetersWindow>(g_meters, Settings().GetBool("interface.metersWindow") && audio::ProcessLoopbackSupported());
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
    Sync();
}

}  // namespace app
