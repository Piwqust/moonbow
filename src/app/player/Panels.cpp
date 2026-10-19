// Player popovers anchored above the control bar's right side
// (design/ref/components/Controls_491-1780.png): one open at a time, toggled by the bar
// buttons, closed by an outside click or Esc. The mini player shows its queue above itself
// (components/Miniplayer_970-3091.png).
#include "app/App.h"
#include "app/I18n.h"
#include "app/MainWindow.h"
#include "app/player/ControlBar.h"
#include "app/player/LiveState.h"
#include "app/player/PanelKit.h"
#include "app/player/PanelTabs.h"
#include "app/player/PlayerView.h"

namespace app {

using namespace ui;
using namespace app::panel;

namespace {

// Settings holds the tracks too: one panel (Playback / Video / Audio / Subtitles) whose last three
// tabs start with the track lists, so the audio and subtitles buttons open it there.
enum class Group : uint8_t { None, Lists, Settings, Mini };

struct PanelState {
    Popover* pop = nullptr;
    Group group = Group::None;
    int tab = 0;           // Settings: 0 playback, 1 video, 2 audio, 3 subtitles, whichever tabs show
    int settingsTab = 0;   // the gear reopens the last used tab
    bool videoTab = true;  // the open Settings popover has a Video tab (audio files don't)
};

PanelState& S() {
    static PanelState s;
    return s;
}

Group GroupFor(PanelKind k) {
    switch (k) {
        case PanelKind::Queue:
        case PanelKind::Chapters:
        case PanelKind::Bookmarks: return TheApp().PlayerShown() ? Group::Lists : Group::Mini;
        case PanelKind::Tracks:
        case PanelKind::Subtitles:
        case PanelKind::Settings:
        case PanelKind::Speed: return Group::Settings;
    }
    return Group::None;
}

int TabFor(PanelKind k) {
    switch (k) {
        case PanelKind::Queue: return 0;
        case PanelKind::Bookmarks: return 1;
        case PanelKind::Chapters: return 2;
        case PanelKind::Tracks: return 2;
        case PanelKind::Subtitles: return 3;
        case PanelKind::Settings: return S().settingsTab;
        case PanelKind::Speed: return 0;
    }
    return 0;
}

// Whether the button for `k` shows as active, given the open group and tab.
bool Matches(PanelKind k, Group g, int tab) {
    if (g == Group::None || g != GroupFor(k)) return false;
    switch (k) {
        case PanelKind::Queue: return true;
        case PanelKind::Bookmarks: return tab == 1;
        case PanelKind::Chapters: return tab == 2;
        case PanelKind::Tracks: return tab == 2;
        case PanelKind::Subtitles: return tab == 3;
        case PanelKind::Settings: return true;
        case PanelKind::Speed: return tab == 0;
    }
    return false;
}

MiniPlayer* FindMini(Widget* w) {
    if (!w) return nullptr;
    if (auto* m = dynamic_cast<MiniPlayer*>(w)) return m;
    for (const auto& c : w->Children())
        if (MiniPlayer* m = FindMini(c.get())) return m;
    return nullptr;
}

// Above the control bar, 20 DIP from the right edge and from the bar.
Rect PlaceAboveBar(float prefH) {
    MainWindow* w = TheApp().main();
    if (!w) return {};
    const Size client = w->ClientSize();
    Rect bar{0, client.h - 56, client.w, 56};
    if (PlayerView* pv = w->playerView(); pv && pv->Bar() && !pv->Bar()->rect.Empty() && pv->visible)
        bar = pv->Bar()->rect;
    const bool chrome = !w->IsFullscreen() && !w->UiHidden();
    const float top = chrome ? 40.0f + 12.0f : 12.0f;
    const float width = std::min(kPanelW, client.w - 2 * 12);
    const float right = bar.Right() - kGap;
    const float bottom = bar.y - kGap;
    const float h = std::max(120.0f, std::min(prefH, bottom - top));
    return {std::round(right - width), std::round(bottom - h), width, h};
}

// Audio without chapters gets no Chapters tab (it would always be empty).
bool HasChaptersTab() {
    const PlayerState& ps = TheApp().player().State();
    return ps.hasVideo || !ps.chapters.empty();
}

// Audio files get no Video tab: nothing in it applies to them.
bool HasVideoTab() {
    const PlayerState& ps = TheApp().player().State();
    return !ps.fileLoaded || ps.hasVideo;
}

// A Settings tab (0-3) as an index among the tabs shown, and back; without the Video tab, Video
// falls back to Playback.
int ShownTab(Group g, int tab, bool videoTab) {
    if (g != Group::Settings || videoTab) return tab;
    return tab >= 2 ? tab - 1 : 0;
}
int TabOf(Group g, int shown, bool videoTab) {
    if (g != Group::Settings || videoTab) return shown;
    return shown >= 1 ? shown + 1 : 0;
}

// Directly above the mini player, same width.
Rect PlaceAboveMini(float prefH) {
    MainWindow* w = TheApp().main();
    if (!w) return {};
    MiniPlayer* mini = FindMini(w->Root());
    if (!mini || !mini->visible) return PlaceAboveBar(prefH);
    const Rect m = mini->rect;
    const float bottom = m.y - 8;
    const float h = std::max(100.0f, std::min(prefH, bottom - 52));
    return {m.x, std::round(bottom - h), m.w, h};
}

Widget* FindId(Widget* w, const char* id) {
    if (w->id == id) return w;
    for (auto& c : w->Children())
        if (Widget* hit = FindId(c.get(), id)) return hit;
    return nullptr;
}

// The queue list takes the keyboard (arrows move the selection, Del, Enter; Space still plays
// and pauses); the other tabs leave it on the player (Space, arrows), and Tab walks into them.
// While the queue shows, files dropped on the window are added to it (MainWindow::onDropFiles).
void FocusQueue(MainWindow* w, Popover* pop) {
    Widget* list = pop ? FindId(pop, "queue-list") : nullptr;
    w->SetFocus(list);
    w->dropHint = list ? Tr(L"Drop to add to the queue") : Tr(L"Drop to play");
}

std::unique_ptr<Popover> MakePopover(Group g, int tab, bool videoTab) {
    auto pop = std::make_unique<Popover>();
    Popover* raw = pop.get();
    switch (g) {
        case Group::Lists:
        {
            std::vector<Tab> tabs = {{L"Queue", "queue", "queue_twotone"},
                                     {L"Bookmarks", "bookmarks", "bookmarks_twotone"}};
            if (HasChaptersTab()) tabs.push_back({L"Chapters", "chapters", "chapters_twotone"});
            pop->SetTabs(std::move(tabs), tab);
        }
            pop->build = [](int t) {
                return t == 1 ? BuildBookmarksTab() : t == 2 ? BuildChaptersTab() : BuildQueueTab(false);
            };
            pop->place = [] { return PlaceAboveBar(946); };
            break;
        case Group::Mini:
            pop->SetTitle(Tr(L"Queue"));
            pop->radius = 8;
            pop->fill = [] { return P().bgPanel; };
            pop->build = [](int) { return BuildQueueTab(true); };
            pop->place = [] { return PlaceAboveMini(CompactQueueHeight()); };
            break;
        case Group::Settings:
        {
            std::vector<Tab> tabs = {{L"Playback", "playback", "playback_twotone"}};
            if (videoTab) tabs.push_back({L"Video", "video", "video_twotone"});
            tabs.push_back({L"Audio", "audio-tracks", "audio-tracks_twotone"});
            tabs.push_back({L"Subtitles", "subtitles", "subtitles_twotone"});
            pop->SetTabs(std::move(tabs), ShownTab(g, tab, videoTab));
        }
            pop->build = [videoTab](int t) { return BuildSettingsTab(TabOf(Group::Settings, t, videoTab)); };
            pop->place = [] { return PlaceAboveBar(946); };
            pop->SetFooter(BuildSettingsFooter(raw));
            break;
        case Group::None: break;
    }
    pop->onTab = [g, raw, videoTab](int shown) {
        PanelState& s = S();
        const int t = TabOf(g, shown, videoTab);
        s.tab = t;
        if (g == Group::Settings) s.settingsTab = t;
        MainWindow* w = TheApp().main();
        if (!w) return;
        // Not while the popover is being built (Open focuses it once it is shown).
        if (s.pop == raw) FocusQueue(w, raw);
        w->RequestRender();
    };
    return pop;
}
void Open(Group g, int tab) {
    MainWindow* w = TheApp().main();
    if (!w) return;
    Live().Start();
    const bool videoTab = g != Group::Settings || HasVideoTab();
    if (!videoTab && tab == 1) tab = 0;
    auto pop = MakePopover(g, tab, videoTab);
    Popover* raw = pop.get();
    raw->ShowTab(ShownTab(g, tab, videoTab));
    PopupOptions o;
    o.place = Place::AboveRight;
    o.anchor = raw->place ? raw->place() : Rect{};
    o.anchor = {o.anchor.Right() - 1, o.anchor.Bottom(), 1, 1};
    o.offset = 0;
    o.focusContent = true;
    o.clickThrough = true;  // Pause or the sidebar act on the click that closes the panel
    o.onDismiss = [raw] {
        PanelState& s = S();
        if (s.pop != raw) return;
        s.pop = nullptr;
        s.group = Group::None;
        if (MainWindow* mw = TheApp().main()) {
            mw->dropHint = Tr(L"Drop to play");
            mw->RequestRender();
        }
    };
    PanelState& s = S();
    s.group = g;
    s.tab = tab;
    s.videoTab = videoTab;
    s.pop = static_cast<Popover*>(w->ShowPopup(std::move(pop), o));
    FocusQueue(w, raw);
}

// The Video tab comes and goes with the file (a song after a film in the queue): the open
// settings popover is reopened on the same tab. Connected once, for the life of the app.
void WatchVideoTab() {
    static const int id = TheApp().playerChanged.Connect([](unsigned m) {
        const PanelState& s = S();
        if (!(m & (kChangeFile | kChangeTracks)) || s.group != Group::Settings ||
            !TheApp().player().State().fileLoaded || s.videoTab == HasVideoTab())
            return;
        if (MainWindow* w = TheApp().main())
            w->Post([] {
                const PanelState& st = S();
                if (st.group != Group::Settings || st.videoTab == HasVideoTab()) return;
                const int tab = st.tab;
                ClosePlayerPanels();
                Open(Group::Settings, tab);
            });
    });
    (void)id;
}

}  // namespace

namespace panel {
int SettingsTab() {
    return S().tab;
}
}  // namespace panel

bool PlayerPanelOpen(PanelKind kind) {
    MainWindow* w = TheApp().main();
    const PanelState& s = S();
    if (!w || !s.pop || !w->IsPopupOpen(s.pop)) return false;
    return Matches(kind, s.group, s.tab);
}

void ClosePlayerPanels() {
    MainWindow* w = TheApp().main();
    PanelState& s = S();
    if (w && s.pop && w->IsPopupOpen(s.pop)) w->ClosePopup(s.pop);
    s.pop = nullptr;
    s.group = Group::None;
}

void ReopenPlayerPanels() {
    MainWindow* w = TheApp().main();
    const PanelState& s = S();
    if (!w || !s.pop || !w->IsPopupOpen(s.pop)) return;
    // Posted: whatever else the change closes or rebuilds goes first.
    w->Post([g = s.group, tab = s.tab] {
        ClosePlayerPanels();
        Open(g, tab);
    });
}

void TogglePlayerPanel(PanelKind kind, const Rect&) {
    MainWindow* w = TheApp().main();
    if (!w) return;
    PanelState& s = S();
    if (PlayerPanelOpen(kind)) {
        ClosePlayerPanels();
        return;
    }
    const Group g = GroupFor(kind);
    int tab = TabFor(kind);
    if (g == Group::Lists && tab == 2 && !HasChaptersTab()) tab = 0;
    if (s.pop && w->IsPopupOpen(s.pop) && s.group == g) {
        s.pop->ShowTab(ShownTab(g, tab, s.videoTab));
        return;
    }
    ClosePlayerPanels();
    Open(g, tab);
    WatchVideoTab();
}

}  // namespace app
