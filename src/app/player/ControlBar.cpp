#include "app/player/ControlBar.h"
#include "app/player/LiveState.h"
#include "app/player/NowPlaying.h"
#include "app/player/PanelKit.h"
#include "app/player/PanelTabs.h"
#include "app/player/SeekPreview.h"

#include "Util.h"
#include "app/Actions.h"
#include "app/App.h"
#include "app/I18n.h"
#include "app/MainWindow.h"
#include "gfx/Text.h"

#include <cmath>
#include <sstream>

namespace app {

using namespace ui;

namespace {

constexpr float kBarH = 56;

// At the end of a file mpv's last time-pos is a frame short of the duration ("0:07 / 0:08"):
// the time and the seek bar show the end itself.
double ShownPosition(const PlayerState& s) {
    return s.eof && s.duration > 0 ? s.duration : s.position;
}

const std::vector<ControlInfo> kControls = {
    {ControlId::Previous, "prev", L"Previous", "previous", true},
    {ControlId::PlayPause, "play", L"Play/Pause", "play", false},
    {ControlId::Next, "next", L"Next", "next", true},
    {ControlId::Time, "time", L"Current Time", "recent", true},
    {ControlId::Duration, "duration", L"Duration", "recent", true},
    {ControlId::Volume, "volume", L"Volume", "volume_value-high", true},
    {ControlId::Audio, "audio", L"Audio Tracks", "audio-tracks", true},
    {ControlId::Subtitles, "subtitles", L"Subtitles", "subtitles", true},
    {ControlId::Settings, "settings", L"Playback Settings", "playback-settings", true},
    {ControlId::Playlist, "playlist", L"Queue", "list", true},
    {ControlId::Spacer, "spacer", L"Spacer", "", true},
    {ControlId::Divider, "divider", L"Divider", "divider", true},
    {ControlId::Speed, "speed", L"Playback Speed", "speed", true},
    {ControlId::Forward10, "fwd10", L"Forward 10s", "forward-10-sec", true},
    {ControlId::Backward10, "back10", L"Backward 10s", "backward-10-sec", true},
    {ControlId::Forward15, "fwd15", L"Forward 15s", "forward-15-sec", true},
    {ControlId::Backward15, "back15", L"Backward 15s", "backward-15-sec", true},
    {ControlId::Stop, "stop", L"Stop", "stop", true},
    {ControlId::Repeat, "repeat", L"Repeat", "repeat_right", true},
    {ControlId::Shuffle, "shuffle", L"Shuffle", "shuffle_right", true},
    {ControlId::Record, "record", L"Record", "record", true},
    {ControlId::Snapshot, "snapshot", L"Snapshot", "snapshot", true},
    {ControlId::PictureInPicture, "pip", L"Picture in Picture", "pip", true},
    {ControlId::Hdr, "hdr", L"HDR", "hdr", true},
    {ControlId::Zoom, "zoom", L"Zoom", "zoom", true},
    {ControlId::RightSidebar, "sidebar", L"Right Sidebar", "sidebar_right", true},
    {ControlId::Quit, "quit", L"Quit Lumen", "quit", true},
    {ControlId::Preferences, "prefs", L"Lumen Settings", "settings", true},
    {ControlId::GoHome, "home", L"Go Home", "home", true},
    {ControlId::Thumbnail, "thumbnail", L"Thumbnail", "tv", true},
    {ControlId::FileName, "filename", L"File Name", "file", true},
    {ControlId::ArtistName, "artist", L"Artist Name", "artist", true},
    {ControlId::FolderName, "folder", L"Folder Name", "folder", true},
    {ControlId::PlaylistName, "playlistname", L"Playlist Name", "lists", true},
    {ControlId::Properties, "properties", L"Properties", "info", true},
    {ControlId::Replay, "replay", L"Replay", "replay", true},
    {ControlId::Chapters, "chapters", L"Chapters", "chapters", true},
    {ControlId::Favorite, "favorite", L"Favorite", "favorite", true},
    {ControlId::Bookmark, "bookmark", L"Bookmark", "bookmark", true},
    {ControlId::Bookmarks, "bookmarks", L"Bookmarks", "bookmarks", true},
    {ControlId::Playlists, "playlists", L"Playlists", "playlist", true},
};

ControlLayout g_layout = ControlLayout::Default();

Signal<>& LayoutSignal() {
    static Signal<> s;
    return s;
}

// Re-renders when the player state changes in a way `mask` covers.
class PlayerBound {
protected:
    void Bind(Widget* self, unsigned mask) {
        conn_ = Connect(TheApp().playerChanged, [self, mask](unsigned m) {
            if (m & mask) self->Invalidate();
        });
    }
    ScopedConnection conn_;
};

const PlayerState& PS() {
    return TheApp().player().State();
}

// Icon button that runs an action and names its key in the tooltip.
class ActionButton : public IconButton {
public:
    ActionButton(const char* icon, const wchar_t* tip, Action a) : IconButton(icon), tip_(tip), action_(a) {
        onClick = [a] { Run(a); };
        tooltip = WithKey(Tr(tip_), action_);
    }
    void Paint(Painter& p) override {
        tooltip = WithKey(Tr(tip_), action_);
        IconButton::Paint(p);
    }

private:
    const wchar_t* tip_;
    Action action_;
};

// --- items ----------------------------------------------------------------------------------

class PlayButton : public Widget, PlayerBound {
public:
    PlayButton() {
        interactive = true;
        focusable = true;
        width = Length::Px(54);
        height = Length::Px(28);
        onClick = [] { Run(Action::PlayPause); };
        Bind(this, kChangeState | kChangeFile);
    }
    void Paint(Painter& p) override {
        const bool loaded = PS().fileLoaded;
        Color bg = loaded ? P().accent : P().bgCard;
        if (loaded && IsPressed()) bg = Color::Mix(bg, P().bgBase, 0.22f);
        else if (loaded && IsHot()) bg = Color::Mix(bg, P().bgBase, 0.1f);
        p.Fill(rect, bg, 4);
        PaintFocusRing(p, rect, 4);
        const bool playing = loaded && !PS().paused;
        p.Icon(playing ? "pause" : "play", rect.Center(22, 22), loaded ? P().onAccent : P().textTertiary);
        tooltip = WithKey(playing ? Tr(L"Pause") : Tr(L"Play"), Action::PlayPause);
    }
};

class TimeLabel : public Widget, PlayerBound {
public:
    explicit TimeLabel(bool duration) : duration_(duration) {
        Bind(this, kChangeTime | kChangeFile);
        if (duration_) {
            interactive = true;
            tooltip = Tr(L"Show remaining time");
            remaining_ = TheApp().UiValue("player.remainingTime") == "1";
            onClick = [this] {
                remaining_ = !remaining_;
                TheApp().SetUiValue("player.remainingTime", remaining_ ? "1" : "0");
                Invalidate();
            };
        }
    }
    Size MeasureContent(float, float) override {
        return {gfx::Text::Get().Measure(L"-00:00:00", TextStyle::Mono).width + 8, 24};
    }
    void Paint(Painter& p) override {
        const PlayerState& s = PS();
        std::wstring text = L"--:--:--";
        if (s.fileLoaded) {
            const double pos = ShownPosition(s);
            const bool hours = s.duration >= 3600 || pos >= 3600;
            if (!duration_)
                text = FormatTime(pos, hours);
            else if (s.duration > 0)
                // Rounded up: "-10:00" at the start of a 10:00 file, "-0:00" only at the end.
                text = remaining_ ? L"-" + FormatTime(std::ceil(std::max(0.0, s.duration - pos) - 1e-3), hours)
                                  : FormatTime(s.duration, hours);
        }
        p.Text(text, rect, TextStyle::Mono, IsHot() ? P().textPrimary : P().textSecondary, HAlign::Center);
    }

private:
    bool duration_;
    bool remaining_ = false;
};

// The volume as a vertical slider above the speaker, for a bar that has no room for the wedge
// (the minimal bar, windows under 900 DIP): the level shows and can be set, not only stepped by
// the wheel (VLC, PotPlayer, IINA). Opened by hovering or clicking the speaker; it closes when
// the pointer has left both, on Esc or on a click elsewhere.
class VolumePopup : public Widget, PlayerBound {
public:
    explicit VolumePopup(Rect anchor) : anchor_(anchor) {
        interactive = true;
        width = Length::Px(kW);
        height = Length::Px(kH);
        Bind(this, kChangeVolume);
    }
    ~VolumePopup() override {
        if (Host* h = GetHost(); h && timer_) h->CancelTimer(timer_);
    }
    static constexpr float kW = 44, kH = 176;
    void Paint(Painter& p) override {
        const PlayerState& s = PS();
        p.Shadow(rect, 8, 12, 4, 0.35f);
        p.Fill(rect, P().bgMenu, 8);
        p.Stroke(rect, P().line, 8);
        p.Text(std::to_wstring(static_cast<int>(std::lround(s.volume))) + L"%", {rect.x, rect.y + 6, rect.w, 22},
               TextStyle::Small, P().textPrimary, HAlign::Center);
        const Rect t = Track();
        const Color empty = Color::Mix(P().textTertiary, P().textSecondary, 0.15f);
        p.Fill(t, empty, 2);
        const float k = static_cast<float>(std::clamp(s.volume / 100.0, 0.0, 1.0));
        const Color fill = s.mute ? Color::Mix(empty, P().accentMid, 0.45f) : P().accentMid;
        if (k > 0) p.Fill({t.x, t.Bottom() - t.h * k, t.w, t.h * k}, fill, 2);
        p.FillCircle({t.Cx(), t.Bottom() - t.h * k}, dragging_ || hotTrack_ ? 7.0f : 6.0f, P().accent);
        const Rect m = MuteRect();
        if (hotMute_) p.Fill(m, P().hover, 4);
        p.Icon(VolumeIcon(s.volume, s.mute), m.Center(22, 22), hotMute_ ? P().textPrimary : P().textSecondary);
    }
    bool OnMouseMove(const MouseEvent& e) override {
        const bool m = MuteRect().Contains(e.x, e.y), t = !m && e.y < MuteRect().y;
        if (m != hotMute_ || t != hotTrack_) {
            hotMute_ = m;
            hotTrack_ = t;
            Invalidate();
        }
        if (dragging_) SetFromY(e.y);
        return true;
    }
    void OnMouseLeave() override {
        hotMute_ = hotTrack_ = false;
        Invalidate();
        CloseWhenLeft();
    }
    bool OnMouseDown(const MouseEvent& e) override {
        if (MuteRect().Contains(e.x, e.y)) {
            Run(Action::Mute);
            return true;
        }
        dragging_ = true;
        SetFromY(e.y);
        return true;
    }
    bool OnMouseUp(const MouseEvent&) override {
        dragging_ = false;
        Invalidate();
        return true;
    }
    bool OnWheel(const WheelEvent& e) override {
        WheelVolume(e.delta, false);
        return true;
    }
    // After the pointer left the speaker or the popup: closes unless it is back on one of them.
    void CloseWhenLeft() {
        Host* h = GetHost();
        if (!h || timer_) return;
        timer_ = h->StartTimer(350, [this, h] {
            timer_ = 0;
            const Point m = h->MousePos();
            if (dragging_ || (h->MouseInside() && (rect.Contains(m.x, m.y) || anchor_.Inset(-4).Contains(m.x, m.y))))
                return;
            h->ClosePopup(this);
        });
    }

private:
    Rect Track() const { return {rect.Cx() - 2, rect.y + 36, 4, rect.h - 36 - 50}; }
    Rect MuteRect() const { return {rect.Cx() - 16, rect.Bottom() - 40, 32, 32}; }
    void SetFromY(float y) {
        const Rect t = Track();
        const double v = std::clamp((t.Bottom() - y) / std::max(1.0f, t.h), 0.0f, 1.0f) * 100.0;
        if (PS().mute) TheApp().player().SetMute(false);
        TheApp().player().SetVolume(std::round(v));
        Invalidate();
    }
    Rect anchor_;
    bool hotMute_ = false, hotTrack_ = false, dragging_ = false;
    UINT_PTR timer_ = 0;
};

// Speaker button + wedge slider, as in the design.
class VolumeControl : public Widget, PlayerBound {
public:
    VolumeControl() {
        interactive = true;
        focusable = true;
        width = Length::Px(126);
        height = Length::Px(32);
        Bind(this, kChangeVolume);
    }
    ~VolumeControl() override {
        if (Host* h = GetHost()) {
            if (hoverTimer_) h->CancelTimer(hoverTimer_);
            if (popup_ && h->IsPopupOpen(popup_)) h->ClosePopup(popup_);
        }
    }
    Rect Wedge() const { return collapsed_ ? Rect{} : Rect{rect.x + 36, rect.Cy() - 9, 86, 18}; }
    // Narrow windows keep only the speaker (wheel still changes the volume), so the
    // seek bar keeps its room.
    void SetCollapsed(bool c) {
        if (c == collapsed_) return;
        collapsed_ = c;
        width = Length::Px(c ? 32.0f : 126.0f);
        // Collapsed, the speaker opens the slider popup (it shows the level); right click mutes.
        tooltip.clear();
    }
    void Paint(Painter& p) override {
        const PlayerState& s = PS();
        const Rect speaker{rect.x, rect.y, 32, rect.h};
        if (hotSpeaker_) p.Fill(speaker, P().hover, 4);
        const char* icon = VolumeIcon(s.volume, s.mute);
        p.Icon(icon, speaker.Center(24, 24), hotSpeaker_ ? P().textPrimary : P().textSecondary);
        if (collapsed_) return;

        const Rect w = Wedge();
        // Geometries are in window DIPs (the DPI is the target's transform): rebuilt only when
        // the bar moves or the level changes, not on every frame the bar repaints.
        const float t = static_cast<float>(std::clamp(s.volume / 100.0, 0.0, 1.0));
        if (!SameRect(w, wedgeRect_)) {
            wedgeRect_ = w;
            track_ = MakeWedge(w, 1.0f);
            level_.Reset();
        }
        if (!level_ || t != levelT_) {
            levelT_ = t;
            level_ = t > 0 ? MakeWedge(w, t) : nullptr;
        }
        // The empty track needs ~3:1 against the bar in both themes (bgDeep vanishes in light).
        const Color empty = Color::Mix(P().textTertiary, P().textSecondary, 0.15f);
        if (track_) p.FillGeometry(track_.Get(), empty);
        if (level_) p.FillGeometry(level_.Get(), s.mute ? Color::Mix(empty, P().accentMid, 0.45f) : P().accentMid);
        PaintFocusRing(p, w, 2);
    }
    // The level while the pointer is on the wedge: the seek tip's bubble (26 DIP, shadow) at the
    // seek tip's height, so the two read as one kind of thing.
    void PaintOver(Painter& p) override {
        if (collapsed_ || !(hotWedge_ || dragging_)) return;
        const Rect w = Wedge();
        const std::wstring label = std::to_wstring(static_cast<int>(std::lround(PS().volume))) + L"%";
        const float tw = gfx::Text::Get().Measure(label, TextStyle::Small).width + 20;
        const float barCy = Parent() ? Parent()->rect.Cy() : rect.Cy();
        const Rect tip{std::round(w.Cx() - tw / 2), barCy - 47, tw, 26};
        p.Shadow(tip, 4, 8, 3, 0.3f);
        p.Fill(tip, P().bgMenu, 4);
        p.Text(label, tip, TextStyle::Small, P().textPrimary, HAlign::Center);
    }
    bool OnMouseMove(const MouseEvent& e) override {
        const bool sp = e.x < rect.x + 34, wd = Wedge().Inset(-4).Contains(e.x, e.y);
        if (sp != hotSpeaker_ || wd != hotWedge_) {
            hotSpeaker_ = sp;
            hotWedge_ = wd;
            Invalidate();
        }
        // Collapsed: resting on the speaker for a moment opens the slider.
        if (collapsed_ && hoverArmed_ && !PopupOpen() && !hoverTimer_)
            if (Host* h = GetHost())
                hoverTimer_ = h->StartTimer(300, [this] {
                    hoverTimer_ = 0;
                    if (IsHot()) OpenPopup();
                });
        if (dragging_) SetFromX(e.x);
        return true;
    }
    void OnMouseLeave() override {
        hotSpeaker_ = hotWedge_ = false;
        Invalidate();
        if (Host* h = GetHost(); h && hoverTimer_) {
            h->CancelTimer(hoverTimer_);
            hoverTimer_ = 0;
        }
        hoverArmed_ = true;
        if (PopupOpen()) popup_->CloseWhenLeft();
    }
    bool OnMouseDown(const MouseEvent& e) override {
        if (e.button == MouseButton::Right) {  // not onContextMenu: the Menu key must not mute
            Run(Action::Mute);
            return true;
        }
        if (collapsed_) {
            // A click while it is open closes it (the click outside the popup does that).
            if (!PopupOpen()) OpenPopup();
            return true;
        }
        if (e.x < rect.x + 34) {
            Run(Action::Mute);
            return true;
        }
        dragging_ = true;
        SetFromX(e.x);
        return true;
    }
    bool OnMouseUp(const MouseEvent&) override {
        dragging_ = false;
        Invalidate();
        return true;
    }
    bool OnWheel(const WheelEvent& e) override {
        // The same rule as the wheel over the video; the toast only when the slider is hidden.
        WheelVolume(e.delta, collapsed_);
        Invalidate();
        return true;
    }
    void Describe(A11y& a) override {
        a.role = Role::Slider;
        a.name = Tr(L"Volume");
        a.max = 100;
        a.now = PS().volume;
        a.step = 5;
        a.setRange = [this](double v) {
            if (PS().mute) TheApp().player().SetMute(false);
            TheApp().player().SetVolume(std::round(v));
            Invalidate();
        };
    }
    bool OnKeyDown(const KeyEvent& e) override {
        if (e.key == VK_LEFT || e.key == VK_DOWN) Run(Action::VolumeDown);
        else if (e.key == VK_RIGHT || e.key == VK_UP) Run(Action::VolumeUp);
        // Collapsed, the level shows only in the slider: Enter or Space opens it from the keyboard
        // (the arrows keep stepping the volume, and the popup shows the new level).
        else if (collapsed_ && (e.key == VK_RETURN || e.key == VK_SPACE)) {
            if (PopupOpen()) GetHost()->ClosePopup(popup_);
            else OpenPopup();
        } else return false;
        return true;
    }
    // The popup is placed once, above the speaker. When the bar moves (fullscreen, a resize, another
    // bar style) or the wedge comes back, it closes instead of floating away from its speaker.
    void OnLayout() override {
        Widget::OnLayout();
        if (PopupOpen() && (!collapsed_ || !SameRect(rect, popupFor_))) GetHost()->ClosePopup(popup_);
    }

private:
    bool PopupOpen() const { return popup_ && GetHost() && GetHost()->IsPopupOpen(popup_); }
    void OpenPopup() {
        Host* h = GetHost();
        if (!h || PopupOpen()) return;
        const Rect speaker{rect.x, rect.y, 32, rect.h};
        PopupOptions o;
        // Above the bar, like the popovers, centered on the speaker.
        const float barTop = Parent() ? Parent()->rect.y : rect.y;
        o.anchor = {speaker.Cx() - VolumePopup::kW / 2, barTop, VolumePopup::kW, 0};
        o.place = Place::Above;
        o.offset = 8;
        o.focusContent = false;  // the arrow keys keep changing the volume through the player
        popup_ = static_cast<VolumePopup*>(h->ShowPopup(std::make_unique<VolumePopup>(speaker), o));
        popupFor_ = rect;
        hoverArmed_ = false;  // closed while the pointer stays here (a click): no reopening by itself
    }
    bool collapsed_ = false;
    bool hoverArmed_ = true;
    VolumePopup* popup_ = nullptr;
    UINT_PTR hoverTimer_ = 0;
    void SetFromX(float x) {
        const Rect w = Wedge();
        if (w.w <= 0) return;
        const double v = std::clamp((x - w.x) / w.w, 0.0f, 1.0f) * 100.0;
        if (PS().mute) TheApp().player().SetMute(false);
        TheApp().player().SetVolume(std::round(v));
        Invalidate();
    }
    static bool SameRect(const Rect& a, const Rect& b) { return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h; }
    // The wedge filled up to t (0..1): its top edge rises from 28 % of the height to the full one.
    static gfx::ComPtr<ID2D1PathGeometry> MakeWedge(const Rect& w, float t) {
        gfx::ComPtr<ID2D1PathGeometry> g;
        gfx::ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(gfx::Gpu::Get().D2DFactory()->CreatePathGeometry(&g)) || FAILED(g->Open(&sink))) return nullptr;
        const float x2 = w.x + w.w * t;
        const float h2 = w.h * (0.28f + 0.72f * t);
        sink->BeginFigure({w.x, w.Bottom()}, D2D1_FIGURE_BEGIN_FILLED);
        const D2D1_POINT_2F pts[] = {{x2, w.Bottom()}, {x2, w.Bottom() - h2}, {w.x, w.Bottom() - w.h * 0.28f}};
        sink->AddLines(pts, 3);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        if (FAILED(sink->Close())) return nullptr;
        return g;
    }
    bool hotSpeaker_ = false, hotWedge_ = false, dragging_ = false;
    gfx::ComPtr<ID2D1PathGeometry> track_, level_;
    Rect wedgeRect_{}, popupFor_{};
    float levelT_ = -1;
};

// Icon button whose active state follows the player (repeat, shuffle, record...).
class StateButton : public IconButton, PlayerBound {
public:
    StateButton(const char* icon, const wchar_t* tip, std::function<void()> click, std::function<bool()> active,
                std::function<const char*()> iconFn = nullptr)
        : IconButton(icon, Tr(tip)), active_(std::move(active)), iconFn_(std::move(iconFn)) {
        onClick = std::move(click);
        Bind(this, kChangeState | kChangeFile | kChangeTracks);
        queueConn_ = Connect(TheApp().queue().changed, [this] { Invalidate(); });
    }
    void Paint(Painter& p) override {
        if (active_) active = active_();
        if (iconFn_) icon = iconFn_();
        IconButton::Paint(p);
    }

private:
    std::function<bool()> active_;
    std::function<const char*()> iconFn_;
    ScopedConnection queueConn_;
};

// Dimmed while Next has nothing to play (no next item, no next episode found).
class NextButton : public ActionButton, PlayerBound {
public:
    NextButton() : ActionButton("next", L"Next", Action::Next) {
        // kChangeTime: the folder search for the next episode ends a moment after the file loads.
        Bind(this, kChangeState | kChangeFile | kChangeTime);
        queueConn_ = Connect(TheApp().queue().changed, [this] { Invalidate(); });
    }
    void Paint(Painter& p) override {
        enabled = Enabled(Action::Next);
        ActionButton::Paint(p);
    }

private:
    ScopedConnection queueConn_;
};

// `key`: the action whose key the tooltip names (V cycles what the Subtitles button opens).
class PanelButton : public IconButton {
public:
    PanelButton(const char* icon, const wchar_t* tip, PanelKind kind, Action key = Action::None)
        : IconButton(icon, Tr(tip)), kind_(kind), tip_(tip), key_(key) {
        onClick = [this] { TogglePlayerPanel(kind_, rect); };
    }
    void Paint(Painter& p) override {
        active = PlayerPanelOpen(kind_);
        if (key_ != Action::None) tooltip = WithKey(Tr(tip_), key_);
        IconButton::Paint(p);
    }

private:
    PanelKind kind_;
    const wchar_t* tip_;
    Action key_;
};

// Last on the right of every bar style, where YouTube, VLC and MPC-HC have it.
class FullscreenButton : public IconButton {
public:
    FullscreenButton() : IconButton("fullscreen") { onClick = [] { Run(Action::Fullscreen); }; }
    void Paint(Painter& p) override {
        const MainWindow* w = TheApp().main();
        const bool full = w && w->IsFullscreen();
        icon = full ? "fullscreen_active" : "fullscreen";
        tooltip = full ? WithKey(Tr(L"Exit fullscreen"), Action::ExitFullscreen) : WithKey(Tr(L"Fullscreen"), Action::Fullscreen);
        IconButton::Paint(p);
    }
};

// The speed popover button; the wheel steps the speed and a right click goes back to 1×.
// Away from 1× it shows the speed ("1,5×") instead of the icon, so it is never a surprise.
class SpeedButton : public PanelButton, PlayerBound {
public:
    SpeedButton() : PanelButton("speed", L"Playback speed", PanelKind::Speed) {
        Bind(this, kChangeState);
    }
    bool OnMouseDown(const MouseEvent& e) override {
        if (e.button != MouseButton::Right) return false;
        Run(Action::SpeedReset);  // not onContextMenu: the Menu key must not reset the speed
        return true;
    }
    bool OnWheel(const WheelEvent& e) override {
        if (!PS().fileLoaded || e.delta == 0) return false;
        Run(e.delta > 0 ? Action::SpeedUp : Action::SpeedDown);
        return true;
    }
    void Paint(Painter& p) override {
        wchar_t buf[96];
        swprintf_s(buf, L"%s %g× · %s", Tr(L"Speed"), PS().speed, Tr(L"scroll to change, right-click for 1×"));
        tooltip = buf;
        const double speed = PS().speed;
        if (!PS().fileLoaded || std::abs(speed - 1.0) < 1e-3) {
            icon = "speed";
            PanelButton::Paint(p);
            return;
        }
        icon.clear();
        PanelButton::Paint(p);  // hover, press and focus states without the icon
        swprintf_s(buf, L"%g×", std::round(speed * 100) / 100);
        const std::wstring text = panel::LocalDecimal(buf);
        const bool on = PlayerPanelOpen(PanelKind::Speed);
        p.Text(text, rect, text.size() > 4 ? TextStyle::Small : TextStyle::SmallMedium,
               on ? P().accent : IsHot() ? P().textPrimary : ToneColor(tone), HAlign::Center, VAlign::Center);
    }
};

class InfoLabel : public Widget, PlayerBound {
public:
    explicit InfoLabel(ControlId id) : id_(id) {
        Bind(this, kChangeFile | kChangeMeta);
        maxW = 240;
    }
    std::wstring Text() const {
        const QueueItem* item = TheApp().queue().CurrentItem();
        switch (id_) {
            case ControlId::FileName: return item ? FileNameOf(item->path) : L"";
            case ControlId::ArtistName: return CurrentTags().artist;
            case ControlId::FolderName: {
                if (!item) return L"";
                const size_t slash = item->path.find_last_of(L"\\/");
                return slash == std::wstring::npos ? L"" : FileNameOf(item->path.substr(0, slash));
            }
            case ControlId::PlaylistName: return TheApp().queue().Name();
            default: return L"";
        }
    }
    Size MeasureContent(float availW, float) override {
        return {std::min(availW, gfx::Text::Get().Measure(Text(), TextStyle::Body).width + 8), 24};
    }
    void Paint(Painter& p) override { p.Text(Text(), rect, TextStyle::Body, P().textSecondary, HAlign::Center); }

private:
    ControlId id_;
};

}  // namespace

// --- registry -----------------------------------------------------------------------

const std::vector<ControlInfo>& AllControls() {
    return kControls;
}

const ControlInfo& ControlInfoFor(ControlId id) {
    return kControls[static_cast<size_t>(id)];
}

ControlLayout ControlLayout::Default() {
    ControlLayout l;
    l.left = {ControlId::Previous, ControlId::PlayPause, ControlId::Next, ControlId::Time};
    l.right = {ControlId::Duration, ControlId::Volume, ControlId::Audio, ControlId::Subtitles, ControlId::Settings,
               ControlId::Playlist};
    return l;
}

std::string ControlLayout::Serialize() const {
    std::string out;
    for (size_t i = 0; i < left.size(); ++i) out += (i ? "," : "") + std::string(ControlInfoFor(left[i]).key);
    out += "|";
    for (size_t i = 0; i < right.size(); ++i) out += (i ? "," : "") + std::string(ControlInfoFor(right[i]).key);
    return out;
}

ControlLayout ControlLayout::Parse(const std::string& s) {
    const size_t bar = s.find('|');
    if (bar == std::string::npos) return Default();
    auto parse = [](const std::string& part) {
        std::vector<ControlId> ids;
        std::stringstream ss(part);
        std::string key;
        while (std::getline(ss, key, ','))
            for (const ControlInfo& c : kControls)
                if (key == c.key) ids.push_back(c.id);
        return ids;
    };
    ControlLayout l{parse(s.substr(0, bar)), parse(s.substr(bar + 1))};
    // Play/pause can't be removed.
    bool hasPlay = false;
    for (ControlId id : l.left) hasPlay |= id == ControlId::PlayPause;
    for (ControlId id : l.right) hasPlay |= id == ControlId::PlayPause;
    if (!hasPlay) l.left.insert(l.left.begin(), ControlId::PlayPause);
    return l;
}

const ControlLayout& CurrentControlLayout() {
    return g_layout;
}

void SetControlLayout(const ControlLayout& layout) {
    g_layout = layout;
    LayoutSignal().Emit();
}

Signal<>& ControlLayoutChanged() {
    return LayoutSignal();
}

std::unique_ptr<Widget> CreateControl(ControlId id) {
    App& app = TheApp();
    auto button = [](const char* icon, const wchar_t* tip, Action a) { return std::make_unique<ActionButton>(icon, tip, a); };
    auto skip = [](const char* icon, const wchar_t* tip, double seconds) {
        auto b = std::make_unique<IconButton>(icon, Tr(tip));
        b->onClick = [seconds] { SeekBy(seconds); };  // with the position message, like the keys
        return b;
    };
    switch (id) {
        case ControlId::Previous: return button("previous", L"Previous", Action::Previous);
        case ControlId::PlayPause: return std::make_unique<PlayButton>();
        case ControlId::Next: return std::make_unique<NextButton>();
        case ControlId::Time: return std::make_unique<TimeLabel>(false);
        case ControlId::Duration: return std::make_unique<TimeLabel>(true);
        case ControlId::Volume: return std::make_unique<VolumeControl>();
        case ControlId::Audio:
            return std::make_unique<PanelButton>("audio-tracks", L"Audio tracks", PanelKind::Tracks, Action::CycleAudio);
        case ControlId::Subtitles:
            return std::make_unique<PanelButton>("subtitles", L"Subtitles", PanelKind::Subtitles, Action::CycleSubtitles);
        case ControlId::Settings:
            return std::make_unique<PanelButton>("playback-settings", L"Playback settings", PanelKind::Settings);
        case ControlId::Playlist:
            return std::make_unique<PanelButton>("list", L"Queue", PanelKind::Queue, Action::TogglePlaylist);
        case ControlId::Spacer: {
            auto w = std::make_unique<Widget>();
            w->width = Length::Px(24);
            return w;
        }
        case ControlId::Divider: {
            auto d = std::make_unique<Divider>(true);
            d->height = Length::Px(20);
            d->pad = {6, 0, 6, 0};
            return d;
        }
        case ControlId::Speed: return std::make_unique<SpeedButton>();
        // The icons say the step: not the configurable arrow-key skip.
        case ControlId::Forward10: return skip("forward-10-sec", L"Forward 10s", 10);
        case ControlId::Backward10: return skip("backward-10-sec", L"Backward 10s", -10);
        case ControlId::Forward15: return skip("forward-15-sec", L"Forward 15s", 15);
        case ControlId::Backward15: return skip("backward-15-sec", L"Backward 15s", -15);
        case ControlId::Stop: return button("stop", L"Stop", Action::Stop);
        case ControlId::Repeat: return panel::MakeRepeatButton();  // the queue's: it shows One apart from All
        case ControlId::Shuffle:
            return std::make_unique<StateButton>(
                "shuffle_right", L"Shuffle", [] { Run(Action::ToggleShuffle); }, [&app] { return app.queue().Shuffle(); });
        case ControlId::Record: return button("record", L"Record", Action::Record);
        case ControlId::Snapshot: return button("snapshot", L"Snapshot", Action::Snapshot);
        case ControlId::PictureInPicture: return button("pip", L"Picture in Picture", Action::PictureInPicture);
        case ControlId::Hdr:
            return std::make_unique<StateButton>(
                "hdr", L"HDR", [] { TogglePlayerPanel(PanelKind::Settings, {}); }, [] { return PS().hdr; });
        case ControlId::Zoom: return button("zoom", L"Zoom", Action::ZoomIn);
        case ControlId::RightSidebar: return button("sidebar_right", L"Sidebar", Action::ToggleSidebar);
        case ControlId::Quit: return button("quit", L"Quit", Action::Quit);
        case ControlId::Preferences: return button("settings", L"Settings", Action::Settings);
        case ControlId::GoHome: return button("home", L"Go home", Action::GoHome);
        case ControlId::Thumbnail: return button("tv", L"Thumbnail", Action::ShowInfo);
        case ControlId::FileName:
        case ControlId::ArtistName:
        case ControlId::FolderName:
        case ControlId::PlaylistName: return std::make_unique<InfoLabel>(id);
        case ControlId::Properties: return button("info", L"Properties", Action::ShowInfo);
        case ControlId::Replay: return button("replay", L"Replay", Action::Replay);
        case ControlId::Chapters: return std::make_unique<PanelButton>("chapters", L"Chapters", PanelKind::Chapters);
        case ControlId::Favorite: return button("favorite", L"Favorite", Action::ToggleFavorite);
        case ControlId::Bookmark: return button("bookmark", L"Add bookmark", Action::AddBookmark);
        case ControlId::Bookmarks: return std::make_unique<PanelButton>("bookmarks", L"Bookmarks", PanelKind::Bookmarks);
        case ControlId::Playlists: {
            auto b = std::make_unique<IconButton>("playlist", Tr(L"Playlists"));
            b->onClick = [] {
                TheApp().ShowPlayer(false);
                TheApp().Navigate(Route{Section::Playlists, {}});
            };
            return b;
        }
        case ControlId::Count: break;
    }
    return std::make_unique<Widget>();
}

// --- SeekBar -----------------------------------------------------------------------

SeekBar::SeekBar() {
    interactive = true;
    width = Length::Grow();
    height = Length::Px(32);
    conn_ = Connect(TheApp().playerChanged, [this](unsigned m) {
        if (m & (kChangeTime | kChangeFile | kChangeChapters)) Invalidate();
    });
    liveConn_ = Connect(LiveState::Get().changed, [this] { Invalidate(); });
}

SeekBar::~SeekBar() = default;

Rect SeekBar::Track() const {
    const float h = compact ? 6.0f : slim ? (hoverX_ >= 0 || dragging_ ? 8.0f : 4.0f) : 26.0f;
    return {rect.x + pad.l, rect.Cy() - h / 2, rect.w - pad.H(), h};
}

double SeekBar::TimeAt(float x) const {
    const Rect t = Track();
    const double d = TheApp().player().State().duration;
    return std::clamp((x - t.x) / std::max(1.0f, t.w), 0.0f, 1.0f) * d;
}

bool SeekBar::OnMouseDown(const MouseEvent& e) {
    const PlayerState& s = TheApp().player().State();
    if (!s.fileLoaded || s.duration <= 0 || !s.seekable) return true;
    dragging_ = true;
    dragFrom_ = s.position;
    dragTime_ = TimeAt(e.x);
    TheApp().player().SeekAbsolute(dragTime_, false);
    Invalidate();
    return true;
}

bool SeekBar::OnMouseMove(const MouseEvent& e) {
    hoverX_ = e.x;
    if (dragging_) {
        dragTime_ = TimeAt(e.x);
        TheApp().player().SeekAbsolute(dragTime_, false);
    }
    Invalidate();
    return true;
}

bool SeekBar::OnMouseUp(const MouseEvent& e) {
    // A dropped drag (Esc, a lost capture) goes back to where it began, like the sliders.
    if (dragging_) TheApp().player().SeekAbsolute(e.cancelled ? dragFrom_ : dragTime_, true);
    dragging_ = false;
    Invalidate();
    return true;
}

bool SeekBar::OnWheel(const WheelEvent& e) {
    if (!TheApp().player().State().fileLoaded) return false;
    SeekBy(5.0 * e.delta);  // with the position message, like the arrow keys
    return true;
}

void SeekBar::Describe(A11y& a) {
    const PlayerState& s = TheApp().player().State();
    a.role = Role::Slider;
    a.name = Tr(L"Seek bar");
    a.max = s.fileLoaded ? s.duration : 0;
    a.now = std::clamp(s.position, 0.0, a.max);
    a.step = 5;  // seconds, like the wheel
    if (s.fileLoaded) a.setRange = [](double v) { TheApp().player().SeekAbsolute(v, true); };
}

void SeekBar::OnMouseLeave() {
    hoverX_ = -1;
    Invalidate();
}

void SeekBar::Paint(Painter& p) {
    const PlayerState& s = TheApp().player().State();
    const Rect t = Track();
    // The unplayed track needs ~3:1 against the bar (WCAG 1.4.11). bgDeep does that only in the
    // dark theme; in light it is #E6E6E6 on #EBEBEB, so the thin tracks take the volume wedge's
    // "empty" tone and the tall one a groove with an outline of it.
    const bool light = Theme::Get().Mode() == ThemeMode::Light;
    const Color empty = Color::Mix(P().textTertiary, P().textSecondary, 0.15f);
    const bool thin = slim || compact;
    const Color track = slim ? (light ? empty : P().lineStrong) : compact ? (light ? empty : P().bgDeep)
                        : light ? P().lineStrong : P().bgDeep;
    // Thin tracks show the played part in the accent itself: a dimmed accent would be darker than
    // the grey of the rest.
    const bool strongPlayed = slim || (compact && light);
    p.Fill(t, track, 3.0f);
    if (!thin && light) p.Stroke(t, empty, 3.0f);
    if (!s.fileLoaded || s.duration <= 0) return;
    const double pos = dragging_ ? dragTime_ : ShownPosition(s);
    const float k = static_cast<float>(std::clamp(pos / s.duration, 0.0, 1.0));
    if (!compact && !s.hasVideo) {
        // Music: the design's waveform-style bar (decorative bars seeded by the file name).
        uint32_t h = 2166136261u;
        for (wchar_t ch : s.path) h = (h ^ static_cast<uint32_t>(ch)) * 16777619u;
        const float pitch = 5, playX = t.x + t.w * k;
        float smooth = 0.5f;
        for (float bx = t.x + 6; bx < t.Right() - 6; bx += pitch) {
            h = h * 1664525u + 1013904223u;
            const float r = static_cast<float>((h >> 8) & 0xFFFF) / 65535.0f;
            smooth = smooth * 0.55f + r * 0.45f;
            const float bh = std::round(4 + (t.h - 10) * smooth * smooth * 1.4f);
            const float bhc = std::min(bh, t.h - 6);
            p.Fill({std::round(bx), std::round(t.Cy() - bhc / 2), 1.5f, bhc},
                   bx < playX ? P().accentMid : light ? empty : P().lineStrong, 0.75f);
        }
        const float x = std::clamp(playX, t.x + 2, t.Right() - 2);
        p.Fill({x - 2, t.Cy() - 10, 4, 20}, P().accent, 2);
        return;
    }
    // Buffered range ahead of the playhead (network streams).
    if (s.cacheTime > pos) {
        const float kc = static_cast<float>(std::clamp(s.cacheTime / s.duration, 0.0, 1.0));
        p.Fill({t.x + t.w * k, t.Bottom() - 3, t.w * (kc - k), 3}, P().line, 1);
    }
    p.Fill({t.x, t.y, t.w * k, t.h}, strongPlayed ? P().accent : Color::Mix(track, P().accent, 0.32f), 3);
    // A-B loop range.
    const LiveValues& lv = LiveState::Get().V();
    if (!std::isnan(lv.abA)) {
        const float ax = t.x + t.w * static_cast<float>(std::clamp(lv.abA / s.duration, 0.0, 1.0));
        const float bx = std::isnan(lv.abB) ? ax : t.x + t.w * static_cast<float>(std::clamp(lv.abB / s.duration, 0.0, 1.0));
        if (bx > ax) p.Fill({ax, t.Bottom() - 3, bx - ax, 3}, P().accent, 1);
        p.Fill({std::round(ax) - 1, t.y, 2, t.h}, P().accent);
        if (bx > ax) p.Fill({std::round(bx) - 1, t.y, 2, t.h}, P().accent);
    }
    for (const Chapter& c : s.chapters) {
        if (c.time <= 0 || c.time >= s.duration) continue;  // a mark past the end would sit beyond the bar
        const float cx = std::round(t.x + t.w * static_cast<float>(c.time / s.duration));
        if (slim)
            // A 4-8 DIP track has no room for a tick inside it: a 2 DIP gap in the track (uosc).
            p.Fill({cx - 1, t.y, 2, t.h}, P().bgChrome);
        else if (compact)
            p.Fill({cx, t.y, 1, t.h}, light ? P().bgPanel : P().lineStrong);
        else
            p.Fill({cx, t.y + 6, 1, t.h - 12}, light ? P().bgChrome : P().lineStrong);
    }
    const float x = std::clamp(t.x + t.w * k, t.x + 2, t.Right() - 2);
    if (slim)
        p.FillCircle({x, t.Cy()}, hoverX_ >= 0 || dragging_ ? 7.0f : 5.0f, P().accent);
    else if (compact)
        p.Fill({x - 2, t.y - 2, 4, t.h + 4}, P().accent, 2);
    else
        p.Fill({x - 2, t.Cy() - 10, 4, 20}, P().accent, 2);
}

void SeekBar::PaintOver(Painter& p) {
    const PlayerState& s = TheApp().player().State();
    if (compact) return;
    if ((hoverX_ < 0 && !dragging_) || !s.fileLoaded || s.duration <= 0) {
        HideSeekPreview();
        return;
    }
    const double t = dragging_ ? dragTime_ : TimeAt(hoverX_);
    std::wstring label = FormatTime(t, s.duration >= 3600);
    for (auto it = s.chapters.rbegin(); it != s.chapters.rend(); ++it)
        if (it->time <= t && !it->title.empty()) {
            label = it->title + L"  ·  " + label;
            break;
        }
    const float w = gfx::Text::Get().Measure(label, TextStyle::Small).width + 20;
    const Rect tr = Track();
    const float at = dragging_ ? tr.x + tr.w * static_cast<float>(t / s.duration) : hoverX_;
    const Size client = GetHost() ? GetHost()->ClientSize() : Size{rect.Right(), rect.Bottom()};
    const Rect bounds{0, 0, client.w, client.h};
    if (!dragging_) {
        // Where a click would land, under the tip (uosc, YouTube): easier to aim on a wide bar.
        const float hx = std::round(std::clamp(hoverX_, tr.x, tr.Right()));
        const float grow = slim ? 4.0f : 0.0f;
        p.Fill({hx - 1, tr.y - grow, 2, tr.h + 2 * grow}, P().textPrimary.Fade(0.7f), 1);
    }
    PaintSeekPreview(p, tr, at, t, bounds);
    const Rect tip{ClampedLeft(at, w, tr, bounds), tr.y - 34, w, 26};
    p.Shadow(tip, 4, 8, 3, 0.3f);
    p.Fill(tip, P().bgMenu, 4);
    p.Text(label, tip, TextStyle::Small, P().textPrimary, HAlign::Center);
}

// --- ControlBar ----------------------------------------------------------------------

ControlBar::ControlBar() {
    width = Length::Grow();
    height = Length::Px(kBarH);
    dir = Dir::Row;
    align = Align::Center;
    pad = {20, 0, 20, 0};
    gap = 6;
    interactive = true;  // swallow clicks between items (don't pause the video)
    layoutConn_ = Connect(ControlLayoutChanged(), [this] { Rebuild(); });
    // Some items take their tooltip when built.
    langConn_ = Connect(TheApp().languageChanged, [this] { Rebuild(); });
    configToken_ = TheApp().config().Subscribe("interface.barStyle", [this](const std::string&) {
        Rebuild();
        if (Parent()) Parent()->InvalidateLayout();  // the bar height and the video area change
    });
    Rebuild();
}

ControlBar::~ControlBar() {
    TheApp().config().Unsubscribe(configToken_);
}

void ControlBar::ReadStyle() {
    const std::string style = TheApp().config().GetString("interface.barStyle");
    minimal_ = style == "minimal";
    classic_ = style == "classic";
    height = Length::Px(BarHeight());
    pad = minimal_ ? Insets{14, 0, 14, 0} : Insets{20, 0, 20, 0};
}

void ControlBar::Rebuild() {
    ClearChildren();
    ReadStyle();
    if (minimal_) {
        // Play, the time and a slim seek bar; volume and the queue stay one click away.
        AddChild(CreateControl(ControlId::PlayPause));
        AddChild(CreateControl(ControlId::Time));
        auto* seek = Add<SeekBar>();
        seek->slim = true;
        seek->pad = {10, 0, 10, 0};
        for (ControlId cid : {ControlId::Duration, ControlId::Volume, ControlId::Playlist}) AddChild(CreateControl(cid));
        Add<FullscreenButton>();
        InvalidateLayout();
        return;
    }
    const ControlLayout& l = CurrentControlLayout();
    for (ControlId cid : l.left) AddChild(CreateControl(cid));
    auto* seek = Add<SeekBar>();
    seek->pad = {10, 0, 10, 0};
    for (ControlId cid : l.right) AddChild(CreateControl(cid));
    // Not in the customizable layout: it stays last, like the seek bar stays in the middle.
    Add<FullscreenButton>();
    InvalidateLayout();
}

void ControlBar::OnLayout() {
    for (auto& c : Children())
        if (auto* v = dynamic_cast<VolumeControl*>(c.get())) v->SetCollapsed(minimal_ || rect.w < 900);
    Widget::OnLayout();
}

void ControlBar::Paint(Painter& p) {
    // The bar and everything on it (buttons, seek bar, times, the seek tip in PaintOver) fade as
    // one: the opacity multiplies every primitive until PaintOver pops it, no offscreen layer.
    p.PushOpacity(opacity);
    if (floating) {
        // Over the picture: a rounded, opaque panel with a soft shadow, inset from the edges. Opaque,
        // so the picture's colours never show through it (no hue seam where the video changes).
        // A skin may ask for a see-through panel (controlBar.opacity); the shadow fades with it so it
        // does not darken the picture under the bar.
        const float r = minimal_ ? 20.0f : 14.0f;
        const float k = Theme::Get().BarOpacity();
        p.Shadow(rect, r, 18, 6, 0.40f * k);
        p.Fill(rect, P().bgChrome.Fade(k), r);
        p.Stroke(rect, P().line, r);
        return;
    }
    p.Fill(rect, P().bgChrome);
}

void ControlBar::PaintOver(Painter& p) {
    p.PopOpacity();
}

}  // namespace app
