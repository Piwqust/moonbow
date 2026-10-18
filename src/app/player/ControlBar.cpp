#include "app/player/ControlBar.h"
#include "app/player/LiveState.h"
#include "app/player/PanelKit.h"
#include "app/player/PanelTabs.h"
#include "app/player/SeekPreview.h"

#include "Util.h"
#include "app/Actions.h"
#include "app/App.h"
#include "app/I18n.h"
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
    {ControlId::Audio, "audio", L"Audio Tracks", "audio", true},
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
    {ControlId::FileName, "filename", L"File Name", "info", true},
    {ControlId::ArtistName, "artist", L"Artist Name", "info", true},
    {ControlId::FolderName, "folder", L"Folder Name", "info", true},
    {ControlId::PlaylistName, "playlistname", L"Playlist Name", "info", true},
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
        tooltip = playing ? Tr(L"Pause") : Tr(L"Play");
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

// Speaker button + wedge slider, as in the design.
class VolumeControl : public Widget, PlayerBound {
public:
    VolumeControl() {
        interactive = true;
        focusable = true;
        width = Length::Px(126);
        height = Length::Px(32);
        Bind(this, kChangeVolume);
        onContextMenu = [](const MouseEvent&) { Run(Action::Mute); };
    }
    Rect Wedge() const { return collapsed_ ? Rect{} : Rect{rect.x + 36, rect.Cy() - 9, 86, 18}; }
    // Narrow windows keep only the speaker (wheel still changes the volume), so the
    // seek bar keeps its room.
    void SetCollapsed(bool c) {
        if (c == collapsed_) return;
        collapsed_ = c;
        width = Length::Px(c ? 32.0f : 126.0f);
        tooltip = c ? Tr(L"Volume: scroll to change, click to mute") : std::wstring();
    }
    void Paint(Painter& p) override {
        const PlayerState& s = PS();
        const Rect speaker{rect.x, rect.y, 32, rect.h};
        if (hotSpeaker_) p.Fill(speaker, P().hover, 4);
        const char* icon = VolumeIcon(s.volume, s.mute);
        p.Icon(icon, speaker.Center(24, 24), hotSpeaker_ ? P().textPrimary : P().textSecondary);
        if (collapsed_) return;

        const Rect w = Wedge();
        auto wedge = [&](float t, Color c) {
            gfx::ComPtr<ID2D1PathGeometry> g;
            gfx::ComPtr<ID2D1GeometrySink> sink;
            gfx::Gpu::Get().D2DFactory()->CreatePathGeometry(&g);
            g->Open(&sink);
            const float x2 = w.x + w.w * t;
            const float h2 = w.h * (0.28f + 0.72f * t);
            sink->BeginFigure({w.x, w.Bottom()}, D2D1_FIGURE_BEGIN_FILLED);
            const D2D1_POINT_2F pts[] = {{x2, w.Bottom()}, {x2, w.Bottom() - h2}, {w.x, w.Bottom() - w.h * 0.28f}};
            sink->AddLines(pts, 3);
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            p.FillGeometry(g.Get(), c);
        };
        // The empty track needs ~3:1 against the bar in both themes (bgDeep vanishes in light).
        const Color empty = Color::Mix(P().textTertiary, P().textSecondary, 0.15f);
        wedge(1.0f, empty);
        const float t = static_cast<float>(std::clamp(s.volume / 100.0, 0.0, 1.0));
        if (t > 0) wedge(t, s.mute ? Color::Mix(empty, P().accentMid, 0.45f) : P().accentMid);
        PaintFocusRing(p, w, 2);
        if (hotWedge_ || dragging_) {
            const std::wstring label = std::to_wstring(static_cast<int>(std::lround(s.volume))) + L"%";
            const Rect tip{w.Cx() - 24, rect.y - 30, 48, 24};
            p.Fill(tip, P().bgMenu, 4);
            p.Text(label, tip, TextStyle::Small, P().textPrimary, HAlign::Center);
        }
    }
    bool OnMouseMove(const MouseEvent& e) override {
        const bool sp = e.x < rect.x + 34, wd = Wedge().Inset(-4).Contains(e.x, e.y);
        if (sp != hotSpeaker_ || wd != hotWedge_) {
            hotSpeaker_ = sp;
            hotWedge_ = wd;
            Invalidate();
        }
        if (dragging_) SetFromX(e.x);
        return true;
    }
    void OnMouseLeave() override {
        hotSpeaker_ = hotWedge_ = false;
        Invalidate();
    }
    bool OnMouseDown(const MouseEvent& e) override {
        if (collapsed_ || e.x < rect.x + 34) {
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
    bool OnKeyDown(const KeyEvent& e) override {
        if (e.key == VK_LEFT || e.key == VK_DOWN) Run(Action::VolumeDown);
        else if (e.key == VK_RIGHT || e.key == VK_UP) Run(Action::VolumeUp);
        else return false;
        return true;
    }

private:
    bool collapsed_ = false;
    void SetFromX(float x) {
        const Rect w = Wedge();
        if (w.w <= 0) return;
        const double v = std::clamp((x - w.x) / w.w, 0.0f, 1.0f) * 100.0;
        if (PS().mute) TheApp().player().SetMute(false);
        TheApp().player().SetVolume(std::round(v));
        Invalidate();
    }
    bool hotSpeaker_ = false, hotWedge_ = false, dragging_ = false;
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

class PanelButton : public IconButton {
public:
    PanelButton(const char* icon, const wchar_t* tip, PanelKind kind) : IconButton(icon, Tr(tip)), kind_(kind) {
        onClick = [this] { TogglePlayerPanel(kind_, rect); };
    }
    void Paint(Painter& p) override {
        active = PlayerPanelOpen(kind_);
        IconButton::Paint(p);
    }

private:
    PanelKind kind_;
};

// The speed popover button; the wheel steps the speed and a right click goes back to 1×.
// Away from 1× it shows the speed ("1,5×") instead of the icon, so it is never a surprise.
class SpeedButton : public PanelButton, PlayerBound {
public:
    SpeedButton() : PanelButton("speed", L"Playback speed", PanelKind::Speed) {
        onContextMenu = [](const MouseEvent&) { Run(Action::SpeedReset); };
        Bind(this, kChangeState);
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
        const PlayerState& s = PS();
        const QueueItem* item = TheApp().queue().CurrentItem();
        switch (id_) {
            case ControlId::FileName: return item ? FileNameOf(item->path) : L"";
            case ControlId::ArtistName: return s.artist;
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
    auto button = [](const char* icon, const wchar_t* tip, Action a) {
        auto b = std::make_unique<IconButton>(icon, Tr(tip));
        b->onClick = [a] { Run(a); };
        return b;
    };
    switch (id) {
        case ControlId::Previous: return button("previous", L"Previous", Action::Previous);
        case ControlId::PlayPause: return std::make_unique<PlayButton>();
        case ControlId::Next: return button("next", L"Next", Action::Next);
        case ControlId::Time: return std::make_unique<TimeLabel>(false);
        case ControlId::Duration: return std::make_unique<TimeLabel>(true);
        case ControlId::Volume: return std::make_unique<VolumeControl>();
        case ControlId::Audio: return std::make_unique<PanelButton>("audio", L"Audio tracks", PanelKind::Tracks);
        case ControlId::Subtitles: return std::make_unique<PanelButton>("subtitles", L"Subtitles", PanelKind::Subtitles);
        case ControlId::Settings:
            return std::make_unique<PanelButton>("playback-settings", L"Playback settings", PanelKind::Settings);
        case ControlId::Playlist: return std::make_unique<PanelButton>("list", L"Queue", PanelKind::Queue);
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
        case ControlId::Forward10: return button("forward-10-sec", L"Forward 10s", Action::SeekForward);
        case ControlId::Backward10: return button("backward-10-sec", L"Backward 10s", Action::SeekBackward);
        case ControlId::Forward15: {
            auto b = std::make_unique<IconButton>("forward-15-sec", Tr(L"Forward 15s"));
            b->onClick = [] { TheApp().player().SeekRelative(15); };
            return b;
        }
        case ControlId::Backward15: {
            auto b = std::make_unique<IconButton>("backward-15-sec", Tr(L"Backward 15s"));
            b->onClick = [] { TheApp().player().SeekRelative(-15); };
            return b;
        }
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

bool SeekBar::OnMouseUp(const MouseEvent&) {
    if (dragging_) TheApp().player().SeekAbsolute(dragTime_, true);
    dragging_ = false;
    Invalidate();
    return true;
}

bool SeekBar::OnWheel(const WheelEvent& e) {
    if (!TheApp().player().State().fileLoaded) return false;
    SeekBy(5.0 * e.delta);  // with the position message, like the arrow keys
    return true;
}

void SeekBar::OnMouseLeave() {
    hoverX_ = -1;
    Invalidate();
}

void SeekBar::Paint(Painter& p) {
    const PlayerState& s = TheApp().player().State();
    const Rect t = Track();
    p.Fill(t, P().bgDeep, compact ? 3.0f : 3.0f);
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
            p.Fill({std::round(bx), std::round(t.Cy() - bhc / 2), 1.5f, bhc}, bx < playX ? P().accentMid : P().lineStrong,
                   0.75f);
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
    p.Fill({t.x, t.y, t.w * k, t.h}, Color::Mix(P().bgDeep, P().accent, 0.32f), 3);
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
        const float cx = t.x + t.w * static_cast<float>(c.time / s.duration);
        p.Fill({std::round(cx), t.y + (compact ? 0.0f : 6.0f), 1, compact ? t.h : t.h - 12}, P().lineStrong);
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
    if ((hoverX_ < 0 && !dragging_) || !s.fileLoaded || s.duration <= 0 || compact) return;
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
        InvalidateLayout();
        return;
    }
    const ControlLayout& l = CurrentControlLayout();
    for (ControlId cid : l.left) AddChild(CreateControl(cid));
    auto* seek = Add<SeekBar>();
    seek->pad = {10, 0, 10, 0};
    for (ControlId cid : l.right) AddChild(CreateControl(cid));
    InvalidateLayout();
}

void ControlBar::OnLayout() {
    for (auto& c : Children())
        if (auto* v = dynamic_cast<VolumeControl*>(c.get())) v->SetCollapsed(minimal_ || rect.w < 900);
    Widget::OnLayout();
}

void ControlBar::Paint(Painter& p) {
    if (floating) {
        // Over the picture: a rounded, nearly opaque panel with a soft shadow, inset from the edges.
        const float r = minimal_ ? 20.0f : 14.0f;
        p.Shadow(rect, r, 18, 6, 0.40f * opacity);
        p.Fill(rect, P().bgChrome.Fade(0.96f * opacity), r);
        p.Stroke(rect, P().line.Fade(opacity), r);
        return;
    }
    p.Fill(rect, P().bgChrome.Fade(opacity));
}

}  // namespace app
