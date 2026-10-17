#include "app/player/PlayerView.h"

#include "Util.h"
#include "app/Actions.h"
#include "app/App.h"
#include "app/I18n.h"
#include "app/MainWindow.h"
#include "app/Shell.h"
#include "app/player/ControlBar.h"
#include "app/player/PanelTabs.h"

#include <cmath>

namespace app {

using namespace ui;

namespace {

constexpr float kSideRail = 56;
constexpr float kSidePanel = 360;
constexpr float kVideoPad = 10;

const PlayerState& PS() {
    return TheApp().player().State();
}

MenuItem Item(const wchar_t* label, Action a, const char* icon = "") {
    MenuItem m{Tr(label), ShortcutText(a), icon};
    m.enabled = Enabled(a);
    m.action = [a] { Run(a); };
    return m;
}

std::vector<MenuItem> TrackItems(const wchar_t* type, const char* prop, bool allowOff) {
    std::vector<MenuItem> items;
    bool any = false;
    for (const Track& t : PS().tracks) {
        if (t.type != type) continue;
        MenuItem m{t.Label()};
        m.checked = t.selected;
        any |= t.selected;
        const int64_t id = t.id;
        m.action = [prop, id] { TheApp().player().SetTrack(prop, id); };
        items.push_back(std::move(m));
    }
    if (allowOff) {
        MenuItem off{Tr(L"Disable")};
        off.checked = !any;
        off.action = [prop] { TheApp().player().SetTrack(prop, 0); };
        items.insert(items.begin(), std::move(off));
    }
    if (items.empty()) {
        MenuItem none{Tr(L"No tracks")};
        none.enabled = false;
        items.push_back(none);
    }
    return items;
}

void ShowVideoContextMenu(Host* host, Point at) {
    App& app = TheApp();
    const PlayerState& s = PS();
    std::vector<MenuItem> items;
    MenuItem play = Item(s.paused || !s.fileLoaded ? L"Play" : L"Pause", Action::PlayPause,
                         s.paused || !s.fileLoaded ? "play" : "pause");
    items.push_back(play);
    items.push_back(Item(L"Stop", Action::Stop, "stop"));
    items.push_back(Item(L"Previous Item", Action::Previous, "previous"));
    items.push_back(Item(L"Next Item", Action::Next, "next"));
    items.push_back(Item(L"Record", Action::Record, "record"));
    items.push_back(Item(L"Hide UI", Action::HideUI, "ui"));

    MenuItem playback{Tr(L"Playback"), L"", "playback"};
    for (double sp : {0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0, 3.0}) {
        wchar_t buf[32];
        swprintf_s(buf, L"%g×", sp);
        MenuItem m{buf};
        m.radio = true;
        m.checked = std::abs(s.speed - sp) < 0.01;
        m.action = [sp] { TheApp().player().SetSpeed(sp); };
        playback.submenu.push_back(std::move(m));
    }
    playback.submenu.push_back(MenuItem::Separator());
    playback.submenu.push_back(Item(L"Next chapter", Action::NextChapter, "chapters"));
    playback.submenu.push_back(Item(L"Previous chapter", Action::PreviousChapter));
    MenuItem repeat = Item(L"Repeat", Action::ToggleRepeat, "repeat_right");
    repeat.checked = app.queue().RepeatMode() != Repeat::Off;
    playback.submenu.push_back(repeat);
    MenuItem shuffle = Item(L"Shuffle", Action::ToggleShuffle, "shuffle_right");
    shuffle.checked = app.queue().Shuffle();
    playback.submenu.push_back(shuffle);
    playback.submenu.push_back(Item(L"A-B loop", Action::ABLoop));
    items.push_back(playback);

    MenuItem video{Tr(L"Video"), L"", "video"};
    video.submenu = TrackItems(L"video", "vid", true);
    video.submenu.push_back(MenuItem::Separator());
    MenuItem aspect{Tr(L"Aspect ratio")};
    for (const auto& [label, value] : std::vector<std::pair<const wchar_t*, const char*>>{
             {L"Default", "-1"}, {L"16:9", "16:9"}, {L"4:3", "4:3"}, {L"21:9", "2.33"}, {L"1:1", "1"}}) {
        MenuItem m{Tr(label)};
        const std::string v = value;
        m.action = [v] { TheApp().player().SetProperty("video-aspect-override", v); };
        aspect.submenu.push_back(std::move(m));
    }
    video.submenu.push_back(aspect);
    video.submenu.push_back(Item(L"Zoom in", Action::ZoomIn, "zoom"));
    video.submenu.push_back(Item(L"Zoom out", Action::ZoomOut));
    video.submenu.push_back(Item(L"Reset zoom", Action::ZoomReset));
    video.submenu.push_back(Item(L"Rotate", Action::Rotate, "refresh"));
    video.submenu.push_back(Item(L"Deinterlace", Action::Deinterlace));
    video.submenu.push_back(MenuItem::Separator());
    video.submenu.push_back(Item(L"Fullscreen", Action::Fullscreen, "fullscreen"));
    video.submenu.push_back(Item(L"Picture in Picture", Action::PictureInPicture, "pip"));
    video.submenu.push_back(Item(L"Snapshot", Action::Snapshot, "snapshot"));
    items.push_back(video);

    MenuItem audio{Tr(L"Audio"), L"", "audio"};
    audio.submenu = TrackItems(L"audio", "aid", true);
    audio.submenu.push_back(MenuItem::Separator());
    MenuItem mute = Item(L"Mute", Action::Mute);
    mute.checked = s.mute;
    audio.submenu.push_back(mute);
    items.push_back(audio);

    MenuItem subs{Tr(L"Subtitle"), L"", "subtitles"};
    subs.submenu = TrackItems(L"sub", "sid", true);
    subs.submenu.push_back(MenuItem::Separator());
    subs.submenu.push_back(Item(L"Add subtitle file…", Action::OpenSubtitles, "add"));
    items.push_back(subs);

    MenuItem more{Tr(L"More Settings"), L"", "settings"};
    more.submenu.push_back({Tr(L"Playback settings"), L"", "playback-settings", false, true, false, false,
                            [] { TogglePlayerPanel(PanelKind::Settings, {}); }});
    more.submenu.push_back(Item(L"Customize player", Action::CustomizePlayer, "edit"));
    more.submenu.push_back(Item(L"Settings", Action::Settings, "preferences"));
    items.push_back(more);

    MenuItem playlist{Tr(L"Playlist"), L"", "playlist"};
    const auto& q = app.queue().Items();
    for (size_t i = 0; i < q.size() && i < 30; ++i) {
        MenuItem m{q[i].title.empty() ? FileNameOf(q[i].path) : q[i].title};
        m.radio = true;
        m.checked = static_cast<int>(i) == app.queue().Current();
        m.action = [i] { TheApp().PlayIndex(static_cast<int>(i)); };
        playlist.submenu.push_back(std::move(m));
    }
    if (playlist.submenu.empty()) playlist.submenu.push_back({Tr(L"Queue is empty"), L"", "", false, false});
    items.push_back(playlist);

    MenuItem open{Tr(L"Open Media"), L"", "open"};
    open.submenu.push_back(Item(L"Open file", Action::OpenFile, "file"));
    open.submenu.push_back(Item(L"Open folder", Action::OpenFolder, "folder"));
    open.submenu.push_back(Item(L"Open network stream", Action::OpenNetwork, "streaming"));
    items.push_back(open);
    items.push_back(Item(L"Quit", Action::Quit, "quit"));
    ShowContextMenu(host, at, std::move(items));
}

}  // namespace

// The video rectangle: a transparent hole the mpv child window shows through,
// framed by the chrome color. Handles clicks, double clicks, wheel, menu.
class PlayerView::VideoArea : public Widget {
public:
    explicit VideoArea(MainWindow* window) : window_(window) {
        interactive = true;
        onContextMenu = [this](const MouseEvent& e) { ShowVideoContextMenu(GetHost(), {e.x, e.y}); };
    }
    bool OnMouseDown(const MouseEvent& e) override {
        // The click that closed a player popover doesn't also pause.
        if (e.button == MouseButton::Left && panel::PanelJustClosed()) return true;
        if (e.button == MouseButton::Left && PS().fileLoaded) {
            Run(Action::PlayPause);  // like MPC-HC; a double click undoes it
            pausedByClick_ = true;
        } else if (e.button == MouseButton::Middle) {
            Run(Action::Fullscreen);
        } else if (e.button == MouseButton::X1) {
            Run(Action::Previous);
        } else if (e.button == MouseButton::X2) {
            Run(Action::Next);
        }
        return true;
    }
    bool OnDoubleClick(const MouseEvent&) override {
        if (window_->PictureInPicture()) return true;
        if (pausedByClick_) Run(Action::PlayPause);
        pausedByClick_ = false;
        Run(Action::Fullscreen);
        return true;
    }
    bool OnMouseMove(const MouseEvent&) override {
        window_->PokeChrome();
        return false;
    }
    bool OnWheel(const WheelEvent& e) override {
        if (e.horizontal) {
            TheApp().player().SeekRelative(5.0 * e.delta);
        } else {
            TheApp().player().SetVolume(std::round(PS().volume + 5 * e.delta));
            ShowToast(GetHost(), Fmt(Tr(L"Volume {0}%"), static_cast<long long>(std::lround(PS().volume))),
                      VolumeIcon(PS().volume, PS().mute), 900);
        }
        return true;
    }
    Rect Inner(bool immersive) const { return immersive ? rect : Rect{rect.x + kVideoPad, rect.y, rect.w - 2 * kVideoPad, rect.h}; }

private:
    MainWindow* window_;
    bool pausedByClick_ = false;
};

// Shown instead of a black player when a file can't be opened.
class PlayerView::ErrorCard : public Widget {
public:
    ErrorCard() {
        dir = Dir::Column;
        align = Align::Center;
        gap = 8;
        pad = Insets::All(28);
        width = Length::Px(440);
        icon_ = Add<IconView>("info", 32, Tone::Secondary);
        title_ = Add<Label>(L"", TextStyle::Heading, Tone::Primary);
        title_->halign = HAlign::Center;
        title_->wrap = true;
        title_->maxLines = 2;
        title_->width = Length::Grow();
        body_ = Add<Label>(L"", TextStyle::Body, Tone::Secondary);
        body_->halign = HAlign::Center;
        body_->wrap = true;
        body_->width = Length::Grow();
        detail_ = Add<Label>(L"", TextStyle::Small, Tone::Tertiary);
        detail_->halign = HAlign::Center;
        detail_->width = Length::Grow();
        auto* row = Add<Widget>();
        row->dir = Dir::Row;
        row->gap = 8;
        row->pad.t = 12;
        next_ = row->Add<Button>(Tr(L"Next file"), ButtonKind::Primary, "next");
        next_->onClick = [] { Run(Action::Next); };
        close_ = row->Add<Button>(Tr(L"Close"), ButtonKind::Secondary);
        close_->onClick = [] { Run(Action::CloseMedia); };
    }
    void Refresh() {
        App& app = TheApp();
        title_->SetText(Fmt(Tr(L"Can't open “{0}”"), {FileNameOf(app.FailedPath())}));
        const std::wstring& err = app.FailedError();
        const bool missing = GetFileAttributesW(app.FailedPath().c_str()) == INVALID_FILE_ATTRIBUTES &&
                             app.FailedPath().find(L"://") == std::wstring::npos;
        body_->SetText(missing ? Tr(L"The file was moved or deleted.")
                       : app.FailedPath().find(L"://") != std::wstring::npos
                           ? Tr(L"The stream is unavailable. Check the link and your connection.")
                           : Tr(L"The file is damaged or its format isn't supported."));
        detail_->SetText(err.empty() ? std::wstring() : L"mpv: " + err);
        detail_->visible = !err.empty();
        const int nextIndex = app.queue().NextIndex(true);
        next_->visible = nextIndex >= 0 && nextIndex != app.queue().Current();
        InvalidateLayout();
    }
    void Paint(Painter& p) override { p.Fill(rect, P().bgPanel, 12); }

private:
    IconView* icon_;
    Label *title_, *body_, *detail_;
    Button *next_, *close_;
};

PlayerView::PlayerView(MainWindow* window) : window_(window) {
    dir = Dir::None;
    width = Length::Grow();
    height = Length::Grow();

    side_ = Add<Widget>();
    side_->dir = Dir::Row;
    side_->visible = false;
    auto* rail = side_->Add<NavSidebar>();
    rail->SetCollapsed(true);
    side_->Add<LibraryPane>(true)->width = Length::Px(kSidePanel);

    video_ = Add<VideoArea>(window);
    music_ = AddChild(CreateMusicView());
    music_->visible = false;
    pip_ = AddChild(CreatePipOverlay(window));
    pip_->visible = false;
    error_ = Add<ErrorCard>();
    error_->visible = false;
    bar_ = Add<ControlBar>();

    playerConn_ = Connect(TheApp().playerChanged, [this](unsigned m) {
        if (m & (kChangeError | kChangeFile)) error_->Refresh();
        if (m & (kChangeTracks | kChangeFile | kChangeState | kChangeError)) InvalidateLayout();
    });
}

PlayerView::~PlayerView() = default;

void PlayerView::SetSidePanel(bool shown) {
    side_->visible = shown;
    InvalidateLayout();
}

Rect PlayerView::VideoRect() const {
    const bool immersive = window_->IsFullscreen() || window_->UiHidden();
    if (!visible || music_->visible) return {};
    return video_->Inner(immersive);
}

void PlayerView::OnLayout() {
    const bool immersive = window_->IsFullscreen() || window_->UiHidden();
    const PlayerState& s = PS();
    const bool audioOnly = s.fileLoaded && !s.hasVideo;
    const float barH = 56;
    const bool pip = window_->PictureInPicture();

    float x = rect.x;
    if (side_->visible && !immersive) {
        side_->Arrange({rect.x, rect.y, kSideRail + kSidePanel, rect.h - barH});
        x += kSideRail + kSidePanel;
    }
    const Rect area = immersive ? rect : Rect{x, rect.y, rect.Right() - x, rect.h - barH};
    video_->Arrange(area);
    music_->visible = audioOnly;
    if (audioOnly) music_->Arrange(immersive ? area : video_->Inner(false));

    bar_->opacity = immersive ? window_->ChromeOpacity() : 1.0f;
    bar_->visible = !pip && (!immersive || window_->ChromeOpacity() > 0.01f);
    bar_->Arrange({rect.x, rect.Bottom() - barH, rect.w, barH});
    pip_->visible = pip;
    if (pip) pip_->Arrange(rect);

    error_->visible = !s.fileLoaded && !TheApp().FailedPath().empty() && !pip;
    if (error_->visible) {
        const Rect v = video_->Inner(immersive);
        const Size sz = error_->Measure(std::min(440.0f, v.w - 32), v.h);
        error_->Arrange(v.Center(sz.w, sz.h));
    }
}

void PlayerView::Paint(Painter& p) {
    const bool immersive = window_->IsFullscreen() || window_->UiHidden();
    if (immersive) return;
    // Chrome around the video hole (never paint over the video itself).
    const Rect a = video_->rect;
    p.Fill({a.x, a.y, kVideoPad, a.h}, P().bgChrome);
    p.Fill({a.Right() - kVideoPad, a.y, kVideoPad, a.h}, P().bgChrome);
    if (!PS().fileLoaded) p.Fill(video_->Inner(false), P().videoBg);
}

void PlayerView::PaintOver(Painter& p) {
    const PlayerState& s = PS();
    if (s.buffering || (s.fileLoaded && s.bufferPercent > 0 && s.bufferPercent < 100 && s.paused && !s.eof)) {
        const Rect v = video_->Inner(window_->IsFullscreen() || window_->UiHidden());
        const std::wstring text = Fmt(Tr(L"Buffering {0}%"), static_cast<long long>(s.bufferPercent));
        const Rect box = v.Center(gfx::Text::Get().Measure(text, TextStyle::Body).width + 32, 36);
        p.Fill(box, P().bgMenu.Fade(0.92f), 6);
        p.Text(text, box, TextStyle::Body, P().textPrimary, HAlign::Center);
    }
}

// --- MiniPlayer -------------------------------------------------------------------

namespace {

constexpr float kMiniW = 280;
constexpr float kMiniVideoH = 148;

class MiniOverlayButton : public IconButton {
public:
    MiniOverlayButton(const char* icon, const wchar_t* tip, int box, int isz)
        : IconButton(icon, Tr(tip), box, isz) {
        round = true;
    }
};

}  // namespace

MiniPlayer::MiniPlayer(MainWindow* window) : window_(window) {
    interactive = true;
    width = Length::Px(kMiniW);
    height = Length::Px(kMiniVideoH + 16 + 60);
    dir = Dir::None;
    onClick = [] { TheApp().ShowPlayer(true); };

    overlay_ = Add<Widget>();
    overlay_->dir = Dir::None;
    auto* close = overlay_->Add<MiniOverlayButton>("close", L"Stop", 28, 18);
    close->id = "close";
    close->onClick = [] { Run(Action::Stop); };
    auto* expand = overlay_->Add<MiniOverlayButton>("open", L"Open player", 28, 18);
    expand->id = "expand";
    expand->onClick = [] { TheApp().ShowPlayer(true); };
    auto* prev = overlay_->Add<MiniOverlayButton>("previous", L"Previous", 36, 24);
    prev->id = "prev";
    prev->onClick = [] { Run(Action::Previous); };
    auto* play = overlay_->Add<MiniOverlayButton>("play", L"Play", 40, 28);
    play->id = "play";
    play->onClick = [] { Run(Action::PlayPause); };
    auto* next = overlay_->Add<MiniOverlayButton>("next", L"Next", 36, 24);
    next->id = "next";
    next->onClick = [] { Run(Action::Next); };
    class QueueButton : public IconButton {
    public:
        QueueButton() : IconButton("list", Tr(L"Queue"), 28, 20) {}
        void Paint(Painter& p) override {
            active = PlayerPanelOpen(PanelKind::Queue);
            IconButton::Paint(p);
        }
    };
    auto* queue = Add<QueueButton>();
    queue->id = "queue";
    queue->onClick = [this] { TogglePlayerPanel(PanelKind::Queue, rect); };
    auto* seek = Add<SeekBar>();
    seek->compact = true;
    seek->id = "seek";

    playerConn_ = Connect(TheApp().playerChanged, [this](unsigned m) {
        if (m & (kChangeState | kChangeFile | kChangeMeta)) {
            for (auto& c : overlay_->Children())
                if (c->id == "play") static_cast<IconButton*>(c.get())->icon = PS().paused ? "play" : "pause";
            Invalidate();
        }
    });
}

MiniPlayer::~MiniPlayer() = default;

Rect MiniPlayer::VideoRect() const {
    if (!visible || !TheApp().player().State().hasVideo) return {};
    return {rect.x + 8, rect.y + 8, rect.w - 16, kMiniVideoH};
}

void MiniPlayer::OnLayout() {
    const Rect v{rect.x + 8, rect.y + 8, rect.w - 16, kMiniVideoH};
    overlay_->Arrange(v);
    overlay_->visible = hover_.Get() > 0.01f;
    for (auto& c : overlay_->Children()) {
        const Size s = c->Measure(100, 100);
        if (c->id == "close") c->Arrange({v.x + 6, v.y + 6, s.w, s.h});
        else if (c->id == "expand") c->Arrange({v.Right() - 6 - s.w, v.y + 6, s.w, s.h});
        else if (c->id == "prev") c->Arrange({v.Cx() - 20 - 36 - 8, v.Cy() - 18, s.w, s.h});
        else if (c->id == "play") c->Arrange({v.Cx() - 20, v.Cy() - 20, s.w, s.h});
        else if (c->id == "next") c->Arrange({v.Cx() + 20 + 8, v.Cy() - 18, s.w, s.h});
    }
    for (auto& c : Children()) {
        if (c->id == "seek") c->Arrange({rect.x + 8, v.Bottom() + 4, rect.w - 16, 12});
        if (c->id == "queue") c->Arrange({rect.Right() - 8 - 28, rect.Bottom() - 8 - 28, 28, 28});
    }
}

void MiniPlayer::OnMouseEnter() {
    hover_.To(1, 120);
    InvalidateLayout();
}

void MiniPlayer::OnMouseLeave() {
    Host* h = GetHost();
    // Leaving onto one of our own buttons keeps the overlay.
    if (h && h->Hot() && IsAncestorOf(h->Hot())) return;
    hover_.To(0, 160);
    InvalidateLayout();
}

void MiniPlayer::Paint(Painter& p) {
    p.Shadow(rect, 8, 18, 8);
    p.Fill(rect, P().bgPanel, 8);
    p.Stroke(rect, P().line, 8);
    const Rect v{rect.x + 8, rect.y + 8, rect.w - 16, kMiniVideoH};
    if (!PS().hasVideo) {
        p.Fill(v, P().bgCard, 4);
        p.Icon("music", v.Center(40, 40), P().textTertiary);
    }
    const QueueItem* item = TheApp().queue().CurrentItem();
    const std::wstring name = item ? (PS().title.empty() ? FileNameOf(item->path) : PS().title) : L"";
    p.Text(name, {rect.x + 12, v.Bottom() + 18, rect.w - 24, 20}, TextStyle::Body, P().textPrimary);
    std::wstring folder;
    if (item) {
        const size_t slash = item->path.find_last_of(L"\\/");
        if (slash != std::wstring::npos && item->path.find(L"://") == std::wstring::npos)
            folder = FileNameOf(item->path.substr(0, slash));
    }
    if (!PS().artist.empty()) folder = PS().artist;
    p.Icon(PS().artist.empty() ? "folder" : "artist", {rect.x + 10, rect.Bottom() - 32, 20, 20}, P().textTertiary);
    p.Text(folder, {rect.x + 36, rect.Bottom() - 34, rect.w - 84, 24}, TextStyle::Small, P().textTertiary);
    // Hover scrim over the video (DComp draws it above mpv's window); the
    // overlay buttons are children, so they paint on top of it.
    const float k = hover_.Get();
    if (k > 0.01f) p.Fill(v, Color::Hex(0x000000, 0.45f * k), 4);
}

void MiniPlayer::PaintOver(Painter&) {}

}  // namespace app
