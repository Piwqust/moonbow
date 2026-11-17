#include "app/player/DetachedWindows.h"

#include "Util.h"
#include "app/Actions.h"
#include "app/App.h"
#include "app/I18n.h"
#include "app/MainWindow.h"
#include "app/player/ControlBar.h"
#include "app/player/NowPlaying.h"
#include "app/player/VizPaint.h"
#include "audio/LoopbackCapture.h"
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

constexpr unsigned kMetersTap = 2;  // Player::SetSignalTap listener of the meters window

Config& Settings() { return TheApp().config(); }

// A small window owned by the main one: a caption row with the name and a close button over
// `body`. Closing it with X turns its setting off; going away with the main window does not.
class DetachedWindow : public Host {
public:
    DetachedWindow(const char* key, const wchar_t* name) : key_(key), name_(name) {}

    bool Open(float w, float h, float minW, float minH, bool dark) {
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
        root->fill = dark ? std::function<Color()>([] { return P().videoBg; }) : [] { return P().bgBase; };
        root->dir = Dir::Column;
        auto* header = root->Add<Panel>();
        header->fill = [] { return P().bgChrome; };
        header->dir = Dir::Row;
        header->align = Align::Center;
        header->width = Length::Grow();
        header->height = Length::Px(36);
        header->pad = {14, 0, 0, 0};
        header->ncRole = NcRole::Caption;
        caption_ = header->Add<Label>(Tr(name_), TextStyle::Body, Tone::Secondary);
        caption_->width = Length::Grow();
        auto* close = header->Add<IconButton>("close", Tr(L"Close"), 40, 20);
        close->height = Length::Px(36);
        close->onClick = [this] { Close(); };
        body_ = root->Add<Widget>();
        body_->width = Length::Grow();
        body_->height = Length::Grow();
        body_->dir = Dir::Column;
        Build(body_);
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

// --- Audio meters: Moonbow's own sound measured like a studio's desk, on the dark ground of the
// music view: two VU meters, the levels in dBFS, the spectrum and the oscilloscope.
class MetersView : public Widget {
public:
    MetersView() {
        width = Length::Grow();
        height = Length::Grow();
        TheApp().player().SetSignalTap(true, kMetersTap);
        conn_ = Connect(TheApp().playerChanged, [this](unsigned m) {
            if (m & (kChangeTime | kChangeState | kChangeFile | kChangeVolume)) Invalidate();
        });
    }
    ~MetersView() override { TheApp().player().SetSignalTap(false, kMetersTap); }

    void DebugInfo(std::string& out) const override {
        Widget::DebugInfo(out);
        char text[160];
        snprintf(text, sizeof text, " frames=%llu rms=%.3f/%.3f peak=%.3f/%.3f%s",
                 static_cast<unsigned long long>(frame_.serial), frame_.rms[0], frame_.rms[1], frame_.peak[0],
                 frame_.peak[1], capture_ && capture_->Failed() ? " failed" : "");
        out += text;
    }

    void Paint(Painter& p) override {
        const PlayerState& s = TheApp().player().State();
        const bool playing = s.fileLoaded && !s.paused && !s.eof && !s.idle;
        if (playing && !capture_ && GetHost())
            capture_ = std::make_unique<audio::LoopbackCapture>(GetHost()->Hwnd(), Host::kRepaintMessage);
        bool live = false;
        if (capture_) {
            // Windows' "Animation effects" off: the first frame with sound in it stays (WCAG 2.3.3).
            const bool still = AnimFloat::ReducedMotion() && frame_.level > 0.05f;
            capture_->SetGain(TheApp().player().OutputGain());
            capture_->SetRunning(playing && !still);
            if (!still) capture_->Take(frame_);
            live = playing && !capture_->Failed();
        }
        if (s.loadSerial != serial_) {  // a new file: its own loudest moments
            serial_ = s.loadSerial;
            for (Hold& h : max_) h = {};
        }
        const float ref = static_cast<float>(std::atof(Settings().GetString("audio.vuReference").c_str()));
        if (viz_.UpdateNeedles(frame_, live, ref) && !live) Invalidate();  // falling to the stop
        const double now = AnimFloat::Now();
        for (int ch = 0; ch < 2; ++ch) {
            const float db = live ? Db(frame_.peak[ch]) : -200.0f;
            // The peak readout holds the highest value for a moment, like a desk's peak meter.
            if (db >= hold_[ch].db || now - hold_[ch].at > 1500) hold_[ch] = {db, now};
            if (db > max_[ch].db) max_[ch] = {db, now};
        }

        const Color white = Color::Hex(0xFFFFFF);
        const Rect r{rect.x + 20, rect.y + 16, rect.w - 40, rect.h - 32};
        const float readH = 64;
        const float rest = r.h - readH - 32;
        const Rect vu{r.x, r.y, r.w, std::floor(rest * 0.5f)};
        const Rect read{r.x, vu.Bottom() + 12, r.w, readH};
        const Rect spec{r.x, read.Bottom() + 12, r.w, std::floor(rest * 0.32f)};
        const Rect scope{r.x, spec.Bottom() + 8, r.w, r.Bottom() - spec.Bottom() - 8};
        viz_.VuPair(p, vu, false);
        PaintReadouts(p, read, live);
        viz_.Spectrum(p, spec, frame_, white, false, false, !live);
        if (live) viz_.Scope(p, scope, frame_, white.Fade(0.86f), white.Fade(0.4f));
        else p.Fill({scope.x, std::round(scope.Cy()), scope.w, 1}, white.Fade(0.2f));
    }

private:
    struct Hold {
        float db = -200;
        double at = 0;
    };
    static float Db(float v) { return v > 1e-9f ? 20.0f * std::log10(v) : -200.0f; }
    static std::wstring DbText(float db) {
        if (db <= -99) return L"−∞";
        wchar_t t[32];
        swprintf_s(t, L"%+.1f", db);
        if (t[0] == L'-') return L"−" + std::wstring(t + 1);  // a real minus sign
        return t;
    }

    // Per channel: RMS, the held peak and the loudest peak of the file, in dBFS; a peak at
    // 0 dBFS or above (clipping) is red.
    void PaintReadouts(Painter& p, const Rect& r, bool live) {
        const Color white = Color::Hex(0xFFFFFF), red = Color::Hex(0xFF5A4E);
        const float colW = r.w / 4;
        const wchar_t* heads[] = {L"", L"RMS", L"Peak", L"Max"};
        for (int c = 1; c < 4; ++c)
            p.Text(Tr(heads[c]), {r.x + colW * c, r.y, colW, 18}, TextStyle::Micro, white.Fade(0.45f), HAlign::Trailing);
        for (int ch = 0; ch < 2; ++ch) {
            const float y = r.y + 20 + ch * 22;
            p.Text(ch == 0 ? L"L" : L"R", {r.x, y, colW, 20}, TextStyle::BodyMedium, white.Fade(0.6f));
            const float values[] = {live ? Db(frame_.rms[ch]) : -200.0f, hold_[ch].db, max_[ch].db};
            for (int c = 0; c < 3; ++c) {
                const float v = values[c];
                const Color col = c > 0 && v >= 0 ? red : white.Fade(c == 2 ? 0.6f : 0.86f);
                p.Text(DbText(v) + L" dB", {r.x + colW * (c + 1), y, colW, 20}, TextStyle::Mono, col, HAlign::Trailing);
            }
        }
    }

    std::unique_ptr<audio::LoopbackCapture> capture_;
    audio::VisualFrame frame_;
    VizPaint viz_;
    Hold hold_[2], max_[2];
    uint64_t serial_ = 0;
    ScopedConnection conn_;
};

class MetersWindow : public DetachedWindow {
public:
    MetersWindow() : DetachedWindow("interface.metersWindow", L"Audio meters") {}

protected:
    void Build(Widget* body) override { body->Add<MetersView>(); }
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
    if (!(controls ? w->Open(560, 136, 380, 136, false) : w->Open(720, 620, 420, 440, true))) {
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
