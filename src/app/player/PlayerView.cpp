#include "app/player/PlayerView.h"

#include "Util.h"
#include "app/Actions.h"
#include "app/App.h"
#include "app/I18n.h"
#include "app/MainWindow.h"
#include "app/Shell.h"
#include "app/library/LibraryCommon.h"
#include "app/player/ControlBar.h"
#include "app/player/LiveState.h"
#include "app/player/PanelKit.h"
#include "app/player/PanelTabs.h"
#include "app/player/PlayerOverlays.h"
#include "app/player/SubStyle.h"
#include "app/player/SubWords.h"
#include "gfx/Text.h"

#include <algorithm>
#include <cmath>

namespace app {

using namespace ui;

namespace {

constexpr float kSideRail = 56;
constexpr float kSidePanel = 360;
constexpr float kFloatMargin = 16;  // the floating control bar's distance from the window edges
constexpr float kVideoPad = 10;

const PlayerState& PS() {
    return TheApp().player().State();
}

// The open video or PiP context menu: its labels ("Play", the checked track, the queue) are a
// snapshot, so it closes when the file, play/pause or the window mode changes under it.
Host* g_menuHost = nullptr;
Widget* g_menu = nullptr;

// The pointer outlives the menu: the id tells it from another popup made later at the address.
constexpr const char* kStaleMenuId = "player-menu";

void KeepMenu(Host* host, Widget* menu) {
    if (menu) menu->id = kStaleMenuId;
    g_menuHost = host;
    g_menu = menu;
}

// Posted: it can be asked for in the middle of a layout (a fullscreen switch lays out from WM_SIZE).
void CloseStaleMenu() {
    if (!g_menuHost || !g_menu) return;
    Host* host = g_menuHost;
    Widget* menu = g_menu;
    g_menuHost = nullptr;
    g_menu = nullptr;
    host->Post([host, menu] {
        if (host->IsPopupOpen(menu) && menu->id == kStaleMenuId) host->ClosePopup(menu);
    });
}

MenuItem Item(const wchar_t* label, Action a, const char* icon = "") {
    MenuItem m{Tr(label), ShortcutText(a), icon};
    m.enabled = Enabled(a);
    m.action = [a] { Run(a); };
    return m;
}

// One track of a type is active: radio items (a dot marks the current one).
std::vector<MenuItem> TrackItems(const wchar_t* type, const char* prop, bool allowOff) {
    std::vector<MenuItem> items;
    bool any = false;
    int ordinal = 0;
    for (const Track& t : PS().tracks) {
        if (t.type != type) continue;
        const std::wstring detail = panel::TrackDetail(t);
        const std::wstring name = panel::TrackName(t, ++ordinal);
        MenuItem m{detail.empty() ? name : name + L" · " + detail};
        m.radio = true;
        m.checked = t.selected;
        any |= t.selected;
        const int64_t id = t.id;
        m.action = [prop, id] { TheApp().player().SetTrack(prop, id); };
        items.push_back(std::move(m));
    }
    if (allowOff) {
        MenuItem off{Tr(L"Disable")};
        off.radio = true;
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

MenuItem SpeedMenu(std::initializer_list<double> speeds) {
    MenuItem speed{Tr(L"Speed")};
    for (double sp : speeds) {
        wchar_t buf[32];
        swprintf_s(buf, L"%g×", sp);
        MenuItem m{panel::LocalDecimal(buf)};
        m.radio = true;
        m.checked = std::abs(PS().speed - sp) < 0.01;
        m.action = [sp] { TheApp().player().SetSpeed(sp); };
        speed.submenu.push_back(std::move(m));
    }
    return speed;
}

MenuItem PlayItem() {
    const PlayerState& s = PS();
    return Item(s.paused || !s.fileLoaded ? L"Play" : L"Pause", Action::PlayPause, s.paused || !s.fileLoaded ? "play" : "pause");
}

void ShowVideoContextMenu(Host* host, Point at) {
    App& app = TheApp();
    const PlayerState& s = PS();
    std::vector<MenuItem> items;
    items.push_back(PlayItem());
    items.push_back(Item(L"Stop", Action::Stop, "stop_twotone"));
    items.push_back(Item(L"Previous Item", Action::Previous, "previous_twotone"));
    items.push_back(Item(L"Next Item", Action::Next, "next_twotone"));
    items.push_back(Item(L"Record", Action::Record, "record"));
    items.push_back(Item(L"Hide UI", Action::HideUI, "ui"));

    MenuItem playback{Tr(L"Playback"), L"", "playback"};
    // In their own submenu, so typing "speed" in the menu finds them.
    playback.submenu.push_back(SpeedMenu({0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0, 3.0}));
    playback.submenu.push_back(MenuItem::Separator());
    // Text-only groups: a menu group has icons on all of its items or on none.
    playback.submenu.push_back(Item(L"Next chapter", Action::NextChapter));
    playback.submenu.push_back(Item(L"Previous chapter", Action::PreviousChapter));
    MenuItem repeat = Item(L"Repeat", Action::ToggleRepeat);
    repeat.checked = app.queue().RepeatMode() != Repeat::Off;
    playback.submenu.push_back(repeat);
    MenuItem shuffle = Item(L"Shuffle", Action::ToggleShuffle);
    shuffle.checked = app.queue().Shuffle();
    playback.submenu.push_back(shuffle);
    playback.submenu.push_back(Item(L"A-B loop", Action::ABLoop));
    items.push_back(playback);

    MenuItem video{Tr(L"Video"), L"", "video"};
    video.submenu = TrackItems(L"video", "vid", true);
    video.submenu.push_back(MenuItem::Separator());
    MenuItem aspect{Tr(L"Aspect ratio")};
    for (const auto& [label, value] : std::vector<std::pair<const wchar_t*, const char*>>{
             {L"Default", "auto"}, {L"16:9", "16:9"}, {L"4:3", "4:3"}, {L"21:9", "21:9"}, {L"1:1", "1:1"}}) {
        MenuItem m{Tr(label)};
        const std::string v = value;
        m.radio = true;
        m.checked = Live().aspect == v;
        m.action = [v] { Live().SetAspect(v); };  // through LiveState, so the Video tab agrees
        aspect.submenu.push_back(std::move(m));
    }
    video.submenu.push_back(aspect);
    video.submenu.push_back(Item(L"Zoom in", Action::ZoomIn));
    video.submenu.push_back(Item(L"Zoom out", Action::ZoomOut));
    video.submenu.push_back(Item(L"Reset zoom", Action::ZoomReset));
    video.submenu.push_back(Item(L"Rotate", Action::Rotate));
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
    items.push_back(MenuItem::Separator());
    items.push_back(Item(L"Media information", Action::ShowInfo, "info"));
    MenuItem stats = Item(L"Statistics", Action::ToggleStats, "wrench");
    stats.checked = app.config().GetBool("general.developerMode");
    items.push_back(stats);
    items.push_back(Item(L"Show in File Explorer", Action::ShowInExplorer, "folder"));

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
    KeepMenu(host, ShowContextMenu(host, at, std::move(items)));
}

// Whether text is a, or a + "\n" + b when b is not empty, without building that string per frame.
bool TipIs(const std::wstring& text, const std::wstring& a, const std::wstring& b) {
    if (b.empty()) return text == a;
    return text.size() == a.size() + 1 + b.size() && text.compare(0, a.size(), a) == 0 && text[a.size()] == L'\n' &&
           text.compare(a.size() + 1, b.size(), b) == 0;
}

}  // namespace

void ShowPipContextMenu(Host* host, Point at) {
    // Short: the PiP window is as small as 256x144 DIP and an in-window menu can't leave it.
    std::vector<MenuItem> items;
    items.push_back(PlayItem());
    MenuItem audio{Tr(L"Audio"), L"", "audio"};
    audio.submenu = TrackItems(L"audio", "aid", true);
    items.push_back(std::move(audio));
    MenuItem subs{Tr(L"Subtitle"), L"", "subtitles"};
    subs.submenu = TrackItems(L"sub", "sid", true);
    items.push_back(std::move(subs));
    MenuItem speed = SpeedMenu({0.5, 1.0, 1.25, 1.5, 2.0});
    speed.icon = "speed";
    items.push_back(std::move(speed));
    items.push_back({Tr(L"Back to full window"), ShortcutText(Action::PictureInPicture), "open", false, true, false, false,
                     [] { TheApp().main()->Post([] { TheApp().main()->SetPictureInPicture(false); }); }});
    KeepMenu(host, ShowContextMenu(host, at, std::move(items)));
}

// The video rectangle: a transparent hole the mpv child window shows through,
// framed by the chrome color. Handles clicks, double clicks, wheel, menu, and
// press-and-hold for a temporary 2× speed.
class PlayerView::VideoArea : public Widget {
public:
    VideoArea(MainWindow* window, PlayerView* owner) : window_(window), owner_(owner) {
        interactive = true;
        onContextMenu = [this](const MouseEvent& e) { ShowVideoContextMenu(GetHost(), {e.x, e.y}); };
    }
    ~VideoArea() override {
        window_->CancelTimer(holdTimer_);
        window_->CancelTimer(revealTimer_);
        if (holding_) TheApp().player().SetSpeed(holdSpeed_);
    }
    bool Holding() const { return holding_; }
    bool OnMouseDown(const MouseEvent& e) override {
        if (e.button == MouseButton::Left) {
            // The click acts on release, so that a long press can mean 2× instead.
            down_ = true;
            clickPending_ = false;
            window_->CancelTimer(holdTimer_);
            holdTimer_ = 0;
            const PlayerState& s = PS();
            if (s.fileLoaded && !s.paused && !s.eof)
                holdTimer_ = window_->StartTimer(kHoldMs, [this] {
                    holdTimer_ = 0;
                    BeginHold();
                });
        } else if (e.button == MouseButton::Middle) {
            Run(Action::Fullscreen);
        } else if (e.button == MouseButton::X1) {
            Run(Action::Previous);
        } else if (e.button == MouseButton::X2) {
            Run(Action::Next);
        }
        return true;
    }
    bool OnMouseUp(const MouseEvent& e) override {
        if (e.button != MouseButton::Left || !down_) return true;  // e.g. the release after a double click
        down_ = false;
        window_->CancelTimer(holdTimer_);
        holdTimer_ = 0;
        if (holding_) EndHold();
        else clickPending_ = true;
        return true;
    }
    // Host calls this right after OnMouseUp, and only for a release over the video: a release
    // elsewhere or a lost capture (Alt+Tab mid-press) is no click.
    void OnClick() override {
        if (!clickPending_) return;
        clickPending_ = false;
        // On the subtitle line, when a click doesn't pause by itself: pause for the words.
        if (Host* h = GetHost(); h && owner_->words_->ClaimVideoClick(h->MousePos().x, h->MousePos().y, owner_->SubtitleRect()))
            return;
        // Settings > Controls > Mouse; play/pause by default, like MPC-HC (a double click undoes it).
        pausedByClick_ = PS().fileLoaded && TheApp().config().GetString("controls.singleClick") == "playPause";
        VideoSingleClick();
    }
    bool OnDoubleClick(const MouseEvent&) override {
        down_ = clickPending_ = false;
        if (window_->PictureInPicture()) return true;
        const bool undo = pausedByClick_ && TheApp().config().GetString("controls.doubleClick") != "none";
        pausedByClick_ = false;
        if (undo) Run(Action::PlayPause);
        VideoDoubleClick();
        return true;
    }
    bool OnMouseMove(const MouseEvent& e) override {
        window_->PointerMoved(e.x, e.y);
        if (owner_->topTip_.empty() || !owner_->topInfo_.Contains(e.x, e.y)) tooltip.clear();
        else if (tooltip != owner_->topTip_) tooltip = owner_->topTip_;
        // Near the bottom edge the floating bar stays up while the pointer rests there.
        const bool nearBottom = window_->BarOverlays() && e.y >= rect.Bottom() - kRevealZone;
        if (nearBottom && !revealTimer_) {
            revealTimer_ = window_->StartTimer(500, [this] {
                if (window_->Hot() == this && window_->BarOverlays()) window_->PokeChrome();
            }, true);
        } else if (!nearBottom && revealTimer_) {
            window_->CancelTimer(revealTimer_);
            revealTimer_ = 0;
        }
        return false;
    }
    void OnMouseLeave() override {
        window_->CancelTimer(revealTimer_);
        revealTimer_ = 0;
    }
    bool OnWheel(const WheelEvent& e) override {
        VideoWheel(e.delta, e.horizontal);
        return true;
    }
    Rect Inner(bool immersive) const { return immersive ? rect : Rect{rect.x + kVideoPad, rect.y, rect.w - 2 * kVideoPad, rect.h}; }

private:
    static constexpr UINT kHoldMs = 400;
    static constexpr float kRevealZone = 120;
    void BeginHold() {
        const PlayerState& s = PS();
        if (!down_ || !s.fileLoaded || s.paused) return;
        holding_ = true;
        holdSpeed_ = s.speed;
        TheApp().player().SetSpeed(2.0);
        owner_->Invalidate();
    }
    void EndHold() {
        holding_ = false;
        TheApp().player().SetSpeed(holdSpeed_);
        owner_->Invalidate();
    }
    MainWindow* window_;
    PlayerView* owner_;
    bool pausedByClick_ = false;
    bool down_ = false, holding_ = false, clickPending_ = false;
    double holdSpeed_ = 1;
    UINT_PTR holdTimer_ = 0, revealTimer_ = 0;
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
        const bool missing = app.FailedPath().find(L"://") == std::wstring::npos && !lib::MaybeExists(app.FailedPath());
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

    // Children paint first-to-last and are hit-tested last-to-first: whatever may lie over the
    // full-size video (the fullscreen side panel, the chips, the bar) comes after it.
    video_ = Add<VideoArea>(window, this);
    // Right above the video: everything else (panels, chips, the bar) still takes clicks first.
    words_ = Add<SubtitleWords>(window);
    words_->forwardDoubleClick = [this](const MouseEvent& e) { return video_->OnDoubleClick(e); };
    words_->fallbackMenu = [this](const MouseEvent& e) { ShowVideoContextMenu(GetHost(), {e.x, e.y}); };
    music_ = AddChild(CreateMusicView());
    music_->visible = false;
    error_ = Add<ErrorCard>();
    error_->visible = false;

    // Opaque: the rail draws its panel 8 DIP inside, and over the picture that gutter showed video.
    auto* side = Add<Panel>();
    side->fill = [] { return P().bgBase; };
    side_ = side;
    side_->dir = Dir::Row;
    sideWanted_ = TheApp().UiValue("player.sidePanel") == "1";  // as it was left (ui.json)
    side_->visible = sideWanted_;
    auto* rail = side_->Add<NavSidebar>();
    rail->SetCollapsed(true);
    rail->SetInPlayer(true);
    sidePane_ = side_->Add<LibraryPane>(true);
    sidePane_->width = Length::Px(kSidePanel);

    pip_ = AddChild(CreatePipOverlay(window));
    pip_->visible = false;
    nextEpisode_ = Add<NextEpisodeCard>(window);
    skipChapter_ = Add<SkipChapterButton>(nextEpisode_);
    bar_ = Add<ControlBar>();
    stats_ = std::make_unique<StatsOverlay>(window, [this] { Invalidate(); });

    playerConn_ = Connect(TheApp().playerChanged, [this](unsigned m) {
        if (m & (kChangeError | kChangeFile)) error_->Refresh();
        if ((m & kChangeFile) || ((m & kChangeState) && PS().paused != menuPaused_)) CloseStaleMenu();
        menuPaused_ = PS().paused;
        // The fullscreen top panel names the chapter and the title.
        if ((m & (kChangeChapters | kChangeMeta)) && window_->IsFullscreen()) Invalidate();
        // In a queue mpv goes on to the next file by itself: say which one it skipped.
        if ((m & kChangeAdvance) && !PS().skippedPath.empty())
            ShowToast(window_, Fmt(Tr(L"Skipped “{0}”: it can't be played"), {FileNameOf(PS().skippedPath)}), "info",
                      4000);
        if (m & (kChangeTracks | kChangeFile | kChangeState | kChangeError)) InvalidateLayout();
        // The thin progress line repaints only when it grows by a pixel.
        if ((m & kChangeTime) && thinShown_) {
            const PlayerState& s = PS();
            const int px = s.duration > 0 ? static_cast<int>(thinW_ * std::clamp(s.position / s.duration, 0.0, 1.0)) : 0;
            if (px != thinPx_) Invalidate();
        }
    });
    screenConn_ = Connect(TheApp().screenChanged, [this] { UpdateStats(); });
    // Subtitles shown, moved or resized: the skip / next-episode chips keep clear of them.
    liveConn_ = Connect(Live().changed, [this] {
        if (skipChapter_->visible || nextEpisode_->visible) InvalidateLayout();
    });
    configToken_ = TheApp().config().Subscribe("general.developerMode", [this](const std::string&) { UpdateStats(); });
}

PlayerView::~PlayerView() {
    TheApp().config().Unsubscribe(configToken_);
}

void PlayerView::UpdateStats() {
    stats_->SetShown(TheApp().config().GetBool("general.developerMode") && TheApp().PlayerShown() &&
                     !window_->PictureInPicture());
}

void PlayerView::SetSidePanel(bool shown) {
    sideWanted_ = shown;
    TheApp().SetUiValue("player.sidePanel", shown ? "1" : "0");
    side_->visible = shown && !window_->PictureInPicture();
    InvalidateLayout();
}

bool PlayerView::PipControlsShown() const {
    return pip_ && PipOverlayShown(pip_);
}

Rect PlayerView::VideoRect() const {
    const bool immersive = window_->IsFullscreen() || window_->UiHidden();
    if (!visible || music_->visible) return {};
    return video_->Inner(immersive);
}

// Height of the strip mpv draws two lines of subtitles in, from the bottom of a picture `h` DIP
// tall; 0 while none are shown. mpv sizes them in pixels of a 720-line picture.
float PlayerView::SubtitleBand(float h) const {
    const PlayerState& s = PS();
    const LiveValues& lv = Live().V();
    // Hidden from mpv only while the word overlay draws the same line.
    const bool shown = (lv.subVisible || words_->HidesMpvSubs()) && std::any_of(s.tracks.begin(), s.tracks.end(), [](const Track& t) {
        return t.selected && t.type == L"sub";
    });
    if (!shown || h <= 0) return 0;
    const Config& c = TheApp().config();
    const double lines = 2.5 * SubtitleFontSize(c) * (lv.subScale > 0 ? lv.subScale : 1);
    const double margin = c.GetDouble("subtitles.marginBottom");
    const double raised = std::clamp(100 - lv.subPos, 0.0, 100.0) / 100 * h;
    if (c.GetString("subtitles.sizeMode") == "pixels") {
        const float scale = GetHost() ? GetHost()->Scale() : 1.0f;
        return static_cast<float>((lines + margin * h / 720) / scale + raised);
    }
    return static_cast<float>((lines + margin) / 720 * h + raised);
}

// Where a click means the subtitle line: the band above, lifted over a floating bar.
Rect PlayerView::SubtitleRect() const {
    const bool immersive = window_->IsFullscreen() || window_->UiHidden();
    const Rect v = video_->Inner(immersive);
    const bool over = (immersive || window_->BarOverlays()) && bar_->visible;
    const float lift = over ? std::max(0.0f, v.Bottom() - bar_->rect.y) : 0.0f;
    const float band = std::max(SubtitleBand(v.h), v.h * 0.12f);
    const float top = std::max(v.y, v.Bottom() - lift - band - 12);
    return {v.x, top, v.w, v.Bottom() - top};
}

void PlayerView::OnLayout() {
    const bool immersive = window_->IsFullscreen() || window_->UiHidden();
    const PlayerState& s = PS();
    // Fullscreen, hide-UI or PiP switched: an open video menu sits where the old layout had it.
    const int mode = (window_->IsFullscreen() ? 1 : 0) | (window_->UiHidden() ? 2 : 0) | (window_->PictureInPicture() ? 4 : 0);
    if (mode != layoutMode_) {
        if (layoutMode_ >= 0) CloseStaleMenu();
        layoutMode_ = mode;
    }
    const bool audioOnly = s.fileLoaded && !s.hasVideo;
    const float barH = bar_->BarHeight();
    const bool pip = window_->PictureInPicture();

    // PiP has no room for it; it comes back with the full window.
    side_->visible = sideWanted_ && !pip;
    float x = rect.x;
    // At most 45% of a small window (800 DIP: 360 instead of 416), so the video stays usable.
    const float sideW = std::min({kSideRail + kSidePanel, std::max(280.0f, std::floor(rect.w * 0.45f)), rect.w});
    if (side_->visible) sidePane_->width = Length::Px(std::max(0.0f, sideW - kSideRail));
    if (side_->visible && !immersive) {
        side_->Arrange({rect.x, rect.y, sideW, rect.h - barH});
        x += sideW;
    } else if (side_->visible) {
        // Fullscreen: an opaque sheet along the whole left edge over the picture (it kept its
        // windowed rect before, with the video showing around it). The bar draws on top of it.
        side_->Arrange({rect.x, rect.y, sideW, rect.h});
    }
    // A floating bar (fullscreen, or windowed auto-hide) leaves the whole height to the video.
    const bool overlay = window_->BarOverlays() && !audioOnly;
    const Rect area = immersive ? rect : Rect{x, rect.y, rect.Right() - x, rect.h - (overlay ? 0.0f : barH)};
    video_->Arrange(area);
    music_->visible = audioOnly;
    if (audioOnly) music_->Arrange(immersive ? area : video_->Inner(false));

    const float chrome = window_->ChromeOpacity();
    bar_->opacity = immersive || overlay ? chrome : 1.0f;
    bar_->visible = !pip && (!(immersive || overlay) || chrome > 0.01f);
    // Over the video the bar floats (unless "full width" is chosen) and slides a few DIP while it
    // fades, so showing and hiding read as one motion rather than a blink.
    // The fullscreen side panel is a full-height sheet: the bar keeps to the picture beside it
    // (centred there), or it covered the panel's bottom rows (VF-02).
    const float barX = side_->visible && immersive ? rect.x + sideW : rect.x;
    const float barW = rect.Right() - barX;
    const bool floats = (immersive || overlay) && !bar_->Classic() && barW >= 480;
    bar_->floating = floats;
    const float slide = immersive || overlay ? (1 - chrome) * 10 : 0.0f;
    if (floats) {
        // Fullscreen and maximized: a longer timeline (70 % of the width, 1180-1600 DIP) instead
        // of 13 s per DIP of a film on a 2560-wide screen; the minimal bar keeps its narrow width.
        const bool wide = window_->IsFullscreen() || window_->IsWindowMaximized();
        const float full = wide ? std::clamp(std::floor(rect.w * 0.7f), 1180.0f, 1600.0f) : 1180.0f;
        const float w = std::min(barW - 32, bar_->Minimal() ? 760.0f : full);
        bar_->Arrange({std::round(barX + (barW - w) / 2), rect.Bottom() - kFloatMargin - barH + slide, w, barH});
    } else {
        bar_->Arrange({barX, rect.Bottom() - barH + slide, barW, barH});
    }
    const float barSpace = barH + (floats ? kFloatMargin : 0.0f);
    pip_->visible = pip;
    if (pip) pip_->Arrange(rect);
    words_->Place(audioOnly ? Rect{} : video_->Inner(immersive));
    if (words_->visible) words_->Arrange(words_->Bounds());

    // Only once mpv has nothing left to try: a failed file in a queue is skipped (a toast says so),
    // and the card would flash with the next file's name.
    error_->visible = !s.fileLoaded && !TheApp().FailedPath().empty() && s.idle && !pip;
    if (error_->visible) {
        const Rect v = video_->Inner(immersive);
        const Size sz = error_->Measure(std::min(440.0f, v.w - 32), v.h);
        error_->Arrange(v.Center(sz.w, sz.h));
    }

    // Bottom-right of the picture, above a floating bar and above the subtitle lines; top-right
    // when that would put them in the upper half of a small picture.
    // A floating bar lifts the subtitles above it too (MainWindow::UpdateSubtitleLift).
    const Rect v = video_->Inner(immersive);
    const float lift = (immersive || overlay) && bar_->visible ? barSpace : 0.0f;
    const float bottom = v.Bottom() - lift - std::max(20.0f, SubtitleBand(v.h) + 12);
    auto place = [&](Widget* w, Size sz) {
        const float top = bottom - sz.h < v.y + v.h * 0.5f ? v.y + (immersive ? 64.0f : 20.0f) : bottom - sz.h;
        w->Arrange({v.Right() - 24 - sz.w, top, sz.w, sz.h});
    };
    nextEpisode_->visible = nextEpisode_->Wanted() && s.fileLoaded && !pip && v.w > 400;
    if (nextEpisode_->visible) place(nextEpisode_, nextEpisode_->Measure(std::min(340.0f, v.w - 48), v.h));
    skipChapter_->visible = skipChapter_->Wanted() && !nextEpisode_->visible && !pip;
    if (skipChapter_->visible) place(skipChapter_, skipChapter_->Measure(v.w, 40));
    UpdateStats();
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

// Fullscreen: the title (and the chapter) in a panel at the top that comes and goes with the
// control bar, under the same opacity, sliding the other way (uosc, IINA). Painted, not a widget,
// so clicks on it still reach the video. Returns the height it takes from the top.
float PlayerView::PaintTopInfo(Painter& p) {
    const PlayerState& s = PS();
    const float k = window_->ChromeOpacity();
    auto none = [this] {
        topInfo_ = {};
        video_->tooltip.clear();  // the panel went with the chrome: so does its tooltip
        return 0.0f;
    };
    if (!window_->IsFullscreen() || window_->PictureInPicture() || !s.fileLoaded || !bar_->visible || k <= 0.01f)
        return none();
    const QueueItem* item = TheApp().queue().CurrentItem();
    std::wstring title = !s.title.empty() ? s.title : item ? FileNameOf(item->path) : std::wstring();
    // Untagged, mpv's media-title is the file name: shown without its extension, as the title
    // bar and the queue show it (VF-06).
    if (item && item->path.find(L"://") == std::wstring::npos && _wcsicmp(title.c_str(), FileNameOf(item->path).c_str()) == 0) {
        const size_t dot = title.find_last_of(L'.');
        if (dot != std::wstring::npos && dot > 0) title.resize(dot);
    }
    if (title.empty()) return none();
    std::wstring chapter;
    if (s.chapter >= 0 && s.chapter < static_cast<int64_t>(s.chapters.size()))
        chapter = s.chapters[static_cast<size_t>(s.chapter)].title;
    const float side = 20, space = 10, h = 40;
    // Over the picture beside the fullscreen side panel, like the bar (VF-02).
    const float left = side_->visible ? side_->rect.Right() : rect.x;
    const float widest = std::min(rect.Right() - left - 32, std::max(bar_->rect.w, 480.0f));
    const float cw = chapter.empty() ? 0.0f : gfx::Text::Get().Measure(chapter, TextStyle::Body).width + 2;
    const float sep = chapter.empty() ? 0.0f : space * 2 + 1;
    const float chapterW = std::min(cw, std::floor((widest - 2 * side - sep) * 0.4f));
    const float titleNatural = gfx::Text::Get().Measure(title, TextStyle::BodyStrong).width + 2;
    const float titleW = std::min(titleNatural, widest - 2 * side - sep - chapterW);
    const float w = std::round(titleW + sep + chapterW + 2 * side);
    const Rect r{std::round((left + rect.Right()) / 2 - w / 2), rect.y + kFloatMargin - (1 - k) * 10, w, h};
    topInfo_ = r;
    if (titleNatural <= titleW + 0.5f && cw <= chapterW + 0.5f) topTip_.clear();
    else if (!TipIs(topTip_, title, chapter)) topTip_ = chapter.empty() ? title : title + L"\n" + chapter;
    p.PushOpacity(k);
    p.Shadow(r, 14, 18, 6, 0.40f);
    p.Fill(r, P().bgChrome, 14);
    p.Stroke(r, P().line, 14);
    p.Text(title, {r.x + side, r.y, titleW, h}, TextStyle::BodyStrong, P().textPrimary);
    if (!chapter.empty()) {
        const float x = r.x + side + titleW + space;
        p.Fill({x, r.y + 12, 1, h - 24}, P().lineStrong);
        p.Text(chapter, {x + 1 + space, r.y, chapterW, h}, TextStyle::Body, P().textSecondary);
    }
    p.PopOpacity();
    return kFloatMargin + h;
}

void PlayerView::PaintOver(Painter& p) {
    const PlayerState& s = PS();
    const bool immersive = window_->IsFullscreen() || window_->UiHidden();
    const Rect video = video_->Inner(immersive);
    const bool pip = window_->PictureInPicture();
    if (!pip) stats_->Paint(p, video);
    words_->Frame();

    const float info = PaintTopInfo(p);
    if (video_->Holding()) {
        const Rect badge{std::round(video.Cx() - 36), video.y + 20 + info, 72, 32};
        p.Fill(badge, P().bgMenu.Fade(0.9f), 16);
        p.Icon("speed", {badge.x + 10, badge.y + 6, 20, 20}, P().textPrimary);
        p.Text(L"2×", {badge.x + 34, badge.y, badge.w - 40, badge.h}, TextStyle::BodyStrong, P().textPrimary);
    }

    // Settings > Interface: a thin progress line along the bottom while the floating bar is hidden.
    std::string thin = TheApp().config().GetString("interface.thinProgress");
    if (thin.empty()) thin = "fullscreen";
    const bool overlay = window_->BarOverlays() && s.hasVideo;
    const float hidden = 1.0f - window_->ChromeOpacity();
    thinShown_ = s.fileLoaded && s.duration > 0 && s.hasVideo && !pip && hidden > 0.01f &&
                 (thin == "always" ? immersive || overlay : thin == "fullscreen" && window_->IsFullscreen());
    if (thinShown_) {
        const float scale = GetHost() ? GetHost()->Scale() : 1.0f;
        thinW_ = video.w * scale;
        const double k = std::clamp(s.position / s.duration, 0.0, 1.0);
        thinPx_ = static_cast<int>(thinW_ * k);
        const Rect track{video.x, video.Bottom() - 3, video.w, 3};
        p.Fill(track, P().bgDeep.Fade(0.55f * hidden));
        p.Fill({track.x, track.y, static_cast<float>(thinPx_) / scale, track.h}, P().accent.Fade(0.9f * hidden));
    }

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
// Below this window size the full card would cover a third of the library.
constexpr float kMiniCompactW = 1000, kMiniCompactH = 560;
constexpr float kMiniCompactCardW = 320, kMiniCompactCardH = 72;

class MiniOverlayButton : public IconButton {
public:
    MiniOverlayButton(const char* icon, const wchar_t* tip, int box, int isz)
        : IconButton(icon, Tr(tip), box, isz) {
        round = true;
    }
};

}  // namespace

MiniPlayer::MiniPlayer(MainWindow*) {
    interactive = true;
    width = Length::Auto();
    height = Length::Auto();
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
    imagesConn_ = Connect(TheApp().images().ready, [this] { Invalidate(); });
}

MiniPlayer::~MiniPlayer() = default;

Size MiniPlayer::MeasureContent(float availW, float availH) {
    compact_ = availW < kMiniCompactW || availH < kMiniCompactH;
    return compact_ ? Size{kMiniCompactCardW, kMiniCompactCardH} : Size{kMiniW, kMiniVideoH + 16 + 60};
}

Rect MiniPlayer::VideoBox() const {
    if (compact_) return {rect.x + 8, rect.y + 8, 100, rect.h - 16};  // 16:9 at 56 DIP
    return {rect.x + 8, rect.y + 8, rect.w - 16, kMiniVideoH};
}

Rect MiniPlayer::VideoRect() const {
    if (!visible || !TheApp().player().State().hasVideo) return {};
    return VideoBox();
}

void MiniPlayer::OnLayout() {
    const Rect v = VideoBox();
    if (compact_) {
        // Play/pause stays visible on the card (not over the picture); the rest is one click away
        // in the player.
        overlay_->Arrange(rect);
        overlay_->visible = true;
        for (auto& c : overlay_->Children()) {
            c->visible = c->id == "play";
            if (c->visible) {
                const Size s = c->Measure(100, 100);
                c->Arrange({rect.Right() - 12 - s.w, rect.Cy() - s.h / 2, s.w, s.h});
            }
        }
        for (auto& c : Children()) {
            if (c->id == "queue") c->visible = false;
            if (c->id == "seek") c->Arrange({v.Right() + 10, rect.Bottom() - 24, rect.Right() - 60 - v.Right() - 10, 24});
        }
        return;
    }
    overlay_->Arrange(v);
    overlay_->visible = hover_.Get() > 0.01f;
    for (auto& c : overlay_->Children()) {
        c->visible = true;
        const Size s = c->Measure(100, 100);
        if (c->id == "close") c->Arrange({v.x + 6, v.y + 6, s.w, s.h});
        else if (c->id == "expand") c->Arrange({v.Right() - 6 - s.w, v.y + 6, s.w, s.h});
        else if (c->id == "prev") c->Arrange({v.Cx() - 20 - 36 - 8, v.Cy() - 18, s.w, s.h});
        else if (c->id == "play") c->Arrange({v.Cx() - 20, v.Cy() - 20, s.w, s.h});
        else if (c->id == "next") c->Arrange({v.Cx() + 20 + 8, v.Cy() - 18, s.w, s.h});
    }
    for (auto& c : Children()) {
        // 24 DIP tall for the pointer (WCAG 2.5.8); the thin track stays where it was.
        if (c->id == "seek") c->Arrange({rect.x + 8, v.Bottom() - 2, rect.w - 16, 24});
        if (c->id == "queue") {
            c->visible = true;
            c->Arrange({rect.Right() - 8 - 28, rect.Bottom() - 8 - 28, 28, 28});
        }
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
    const PlayerState& s = PS();
    const Rect v = VideoBox();
    const float k = compact_ ? 0.0f : hover_.Get();
    p.Shadow(rect, 8, 18, 8);
    p.Fill(rect, P().bgPanel, 8);
    p.Stroke(rect, P().line, 8);
    if (s.hasVideo) {
        p.Fill(v, P().videoBg, 4);
        // mpv's window sits under the whole UI layer at VideoRect(), and the library page
        // below us paints there too: clear the area to transparent so the video shows through.
        if (s.fileLoaded) {
            p.Ctx()->PushAxisAlignedClip(v.D2D(), D2D1_ANTIALIAS_MODE_ALIASED);
            p.Ctx()->Clear(D2D1::ColorF(0, 0, 0, 0));
            p.Ctx()->PopAxisAlignedClip();
        }
    } else {
        const QueueItem* cur = TheApp().queue().CurrentItem();
        const std::wstring path = cur ? cur->path : s.path;
        ID2D1Bitmap* bmp = nullptr;
        if (!path.empty() && GetHost())
            bmp = TheApp().images().Get(path, static_cast<int>(std::ceil(v.w * GetHost()->Scale())));
        p.Fill(v, P().bgCard, 4);
        if (bmp) {
            // Square cover centered over a dimmed, cropped copy of itself.
            p.Image(bmp, v, 4, true, 0.35f);
            p.Image(bmp, {std::round(v.Cx() - v.h / 2), v.y, v.h, v.h}, 0, true);
        } else {
            // The hover controls take the center: fade the note out under them.
            const float icon = compact_ ? 24.0f : 40.0f;
            p.Icon("music", v.Center(icon, icon), P().textTertiary.Fade(1 - k));
        }
    }
    const QueueItem* item = TheApp().queue().CurrentItem();
    const std::wstring name = item ? (PS().title.empty() ? FileNameOf(item->path) : PS().title) : L"";
    // A cut title stays readable in the card's tooltip.
    const float titleW = compact_ ? std::max(0.0f, rect.Right() - 60 - v.Right() - 12) : rect.w - 24;
    tooltip = gfx::Text::Get().Measure(name, TextStyle::Body).width > titleW ? name : std::wstring();
    if (compact_) {
        // Title and folder/artist between the thumbnail and the play button; seek bar below.
        const float x = v.Right() + 12, w = std::max(0.0f, rect.Right() - 60 - x);
        p.Text(name, {x, rect.y + 10, w, 20}, TextStyle::Body, P().textPrimary);
        std::wstring sub = PS().artist;
        if (sub.empty() && item) {
            const size_t slash = item->path.find_last_of(L"\\/");
            if (slash != std::wstring::npos && item->path.find(L"://") == std::wstring::npos)
                sub = FileNameOf(item->path.substr(0, slash));
        }
        p.Text(sub, {x, rect.y + 30, w, 18}, TextStyle::Small, P().textTertiary);
        return;
    }
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
    // overlay buttons are children, so they paint on top of it. They get solid backings of
    // their own: through the scrim alone, light cover art (its title text) reads through the icons.
    if (k > 0.01f) {
        p.Fill(v, Color::Hex(0x000000, 0.72f * k), 4);
        const Color back = P().bgPanel.Fade(0.92f * k), edge = P().line.Fade(k);
        const Rect pill{v.Cx() - 20 - 36 - 8 - 6, v.Cy() - 26, 6 + 36 + 8 + 40 + 8 + 36 + 6, 52};
        p.Fill(pill, back, 26);
        p.Stroke(pill, edge, 26);
        for (const Rect& r : {Rect{v.x + 6, v.y + 6, 28, 28}, Rect{v.Right() - 34, v.y + 6, 28, 28}}) {
            p.Fill(r, back, 14);
            p.Stroke(r, edge, 14);
        }
    }
}

}  // namespace app
