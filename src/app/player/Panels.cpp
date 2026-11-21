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

#include <algorithm>

namespace app {

using namespace ui;
using namespace app::panel;

namespace {

// Each settings button has its own short popover (no tabs); the lists share one with tabs.
enum class Group : uint8_t { None, Lists, Mini, Playback, Video, Audio, Subtitles };

struct PanelState {
    Popover* pop = nullptr;
    Group group = Group::None;
    int tab = 0;  // Lists: 0 queue, 1 bookmarks, 2 chapters
    Rect anchor;  // the bar button that opened it (empty: the keyboard, a menu)
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
        case PanelKind::Tracks: return Group::Audio;
        case PanelKind::Subtitles: return Group::Subtitles;
        case PanelKind::Video: return Group::Video;
        case PanelKind::Settings:
        case PanelKind::Speed: return Group::Playback;
    }
    return Group::None;
}

// The panel a settings group shows (its builders take the bar button's kind).
PanelKind KindOf(Group g) {
    switch (g) {
        case Group::Audio: return PanelKind::Tracks;
        case Group::Subtitles: return PanelKind::Subtitles;
        case Group::Video: return PanelKind::Video;
        default: return PanelKind::Settings;
    }
}

int TabFor(PanelKind k) {
    switch (k) {
        case PanelKind::Bookmarks: return 1;
        case PanelKind::Chapters: return 2;
        default: return 0;
    }
}

// Whether the button for `k` shows as active, given the open group and tab.
bool Matches(PanelKind k, Group g, int tab) {
    if (g == Group::None || g != GroupFor(k)) return false;
    switch (k) {
        case PanelKind::Bookmarks: return tab == 1;
        case PanelKind::Chapters: return tab == 2;
        default: return true;
    }
}

MiniPlayer* FindMini(Widget* w) {
    if (!w) return nullptr;
    if (auto* m = dynamic_cast<MiniPlayer*>(w)) return m;
    for (const auto& c : w->Children())
        if (MiniPlayer* m = FindMini(c.get())) return m;
    return nullptr;
}

// Above the control bar, over the button that opened it (Apple HIG: a popover points at its
// source), kept inside the window; without a button, 20 DIP from the right edge.
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
    const Rect& a = S().anchor;
    const float right = a.w > 0 ? std::clamp(std::round(a.Cx() + width / 2), 12 + width, bar.Right() - kGap)
                                : bar.Right() - kGap;
    const float bottom = bar.y - kGap;
    const float h = std::max(120.0f, std::min(prefH, bottom - top));
    return {std::round(right - width), std::round(bottom - h), width, h};
}

// Audio without chapters gets no Chapters tab (it would always be empty).
bool HasChaptersTab() {
    const PlayerState& ps = TheApp().player().State();
    return ps.hasVideo || !ps.chapters.empty();
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

std::unique_ptr<Popover> MakePopover(Group g, int tab) {
    auto pop = std::make_unique<Popover>();
    Popover* raw = pop.get();
    switch (g) {
        case Group::Lists:
        {
            std::vector<Tab> tabs = {{L"Queue"}, {L"Bookmarks"}};
            if (HasChaptersTab()) tabs.push_back({L"Chapters"});
            pop->SetTabs(std::move(tabs), tab);
            pop->a11yName = Tr(L"Queue");  // as the bar's button that opens it
        }
            pop->build = [](int t) {
                return t == 1 ? BuildBookmarksTab() : t == 2 ? BuildChaptersTab() : BuildQueueTab(false);
            };
            // As tall as the list, like the settings panels (a long queue scrolls), but never shorter
            // while open: the tabs stay under the pointer when they switch or a row goes.
            // Beside the music the cover and the lyrics move over to leave it the right side (the
            // music view's Place, in a window wide enough for both): there it takes that whole
            // column, not a box at the bottom of an empty one.
            pop->place = [raw, tallest = 300.0f]() mutable {
                tallest = std::max(tallest, raw->NaturalHeight());
                const PlayerState& ps = TheApp().player().State();
                const MainWindow* w = TheApp().main();
                const bool column = ps.fileLoaded && !ps.hasVideo && w && w->ClientSize().w - (kPanelW + kGap) >= 352;
                return PlaceAboveBar(column ? 100000.0f : tallest);
            };
            break;
        case Group::Mini:
            pop->SetTitle(Tr(L"Queue"));
            pop->fill = [] { return P().bgPanel; };
            pop->build = [](int) { return BuildQueueTab(true); };
            pop->place = [] { return PlaceAboveMini(CompactQueueHeight()); };
            break;
        case Group::Playback:
        case Group::Video:
        case Group::Audio:
        case Group::Subtitles:
        {
            static const wchar_t* kTitles[] = {L"Playback", L"Video", L"Audio", L"Subtitles"};
            const PanelKind kind = KindOf(g);
            pop->SetTitle(Tr(kTitles[static_cast<int>(g) - static_cast<int>(Group::Playback)]));
            pop->a11yName = Tr(kTitles[static_cast<int>(g) - static_cast<int>(Group::Playback)]);
            pop->build = [kind](int page) { return BuildSettingsPanel(kind, page); };
            pop->pageTitle = [kind](int page) { return SettingsPageTitle(kind, page); };
            // As tall as its rows, up to the room above the bar (then it scrolls).
            pop->place = [raw] { return PlaceAboveBar(raw->NaturalHeight()); };
            pop->SetFooter(BuildSettingsFooter(kind));
            break;
        }
        case Group::None: break;
    }
    pop->onTab = [raw](int t) {
        PanelState& s = S();
        s.tab = t;
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
    auto pop = MakePopover(g, tab);
    Popover* raw = pop.get();
    raw->ShowTab(tab);
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
    s.pop = static_cast<Popover*>(w->ShowPopup(std::move(pop), o));
    FocusQueue(w, raw);
}

// The video panel goes when the next file has no picture (a song after a film in the queue).
// Connected once, for the life of the app.
void WatchVideoPanel() {
    static const int id = TheApp().playerChanged.Connect([](unsigned m) {
        if (!(m & (kChangeFile | kChangeTracks)) || S().group != Group::Video) return;
        const PlayerState& ps = TheApp().player().State();
        if (!ps.fileLoaded || ps.hasVideo) return;
        if (MainWindow* w = TheApp().main()) w->Post([] {
                if (S().group == Group::Video) ClosePlayerPanels();
            });
    });
    (void)id;
}

}  // namespace


namespace panel {
void ShowPanelPage(int page) {
    PanelState& s = S();
    MainWindow* w = TheApp().main();
    if (!w || !s.pop || !w->IsPopupOpen(s.pop) || s.group == Group::Lists || s.group == Group::Mini) return;
    // Posted: the row that asks is part of the page being replaced.
    w->Post([page] {
        PanelState& st = S();
        MainWindow* mw = TheApp().main();
        if (mw && st.pop && mw->IsPopupOpen(st.pop)) st.pop->ShowTab(page);
    });
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

void TogglePlayerPanel(PanelKind kind, const Rect& anchor) {
    MainWindow* w = TheApp().main();
    if (!w) return;
    PanelState& s = S();
    if (PlayerPanelOpen(kind)) {
        ClosePlayerPanels();
        return;
    }
    S().anchor = anchor;
    const Group g = GroupFor(kind);
    int tab = TabFor(kind);
    if (g == Group::Lists && tab == 2 && !HasChaptersTab()) tab = 0;
    if (s.pop && w->IsPopupOpen(s.pop) && s.group == g) {
        s.pop->ShowTab(tab);
        return;
    }
    ClosePlayerPanels();
    Open(g, tab);
    WatchVideoPanel();
}

}  // namespace app
