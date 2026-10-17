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

enum class Group : uint8_t { None, Lists, Tracks, Settings, Mini };

struct PanelState {
    Popover* pop = nullptr;
    Group group = Group::None;
    int tab = 0;
    int settingsTab = 0;  // reopened on the last used settings tab
    double closedAt = -1e9;
    Group closedGroup = Group::None;
    int closedTab = 0;
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
        case PanelKind::Subtitles: return Group::Tracks;
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
        case PanelKind::Tracks: return 1;
        case PanelKind::Subtitles: return 2;
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
        case PanelKind::Tracks: return tab != 2;
        case PanelKind::Subtitles: return tab == 2;
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

std::unique_ptr<Popover> MakePopover(Group g, int tab) {
    auto pop = std::make_unique<Popover>();
    Popover* raw = pop.get();
    switch (g) {
        case Group::Lists:
            pop->SetTabs({{L"Queue", "queue", "queue_twotone"},
                          {L"Bookmarks", "bookmarks", "bookmarks_twotone"},
                          {L"Chapters", "chapters", "chapters_twotone"}},
                         tab);
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
        case Group::Tracks:
            pop->SetTabs({{L"Video", "video", "video_twotone"},
                          {L"Audio", "audio", "audio_twotone"},
                          {L"Subtitles", "subtitles", "subtitles_twotone"}},
                         tab);
            pop->build = [](int t) { return BuildTracksTab(t); };
            pop->place = [] { return PlaceAboveBar(524); };
            pop->SetFooter(BuildTracksFooter(raw));
            break;
        case Group::Settings:
            pop->SetTabs({{L"Playback", "playback", "playback_twotone"},
                          {L"Video", "video", "video_twotone"},
                          {L"Audio", "audio", "audio_twotone"},
                          {L"Subtitles", "subtitles", "subtitles_twotone"}},
                         tab);
            pop->build = [](int t) { return BuildSettingsTab(t); };
            pop->place = [] { return PlaceAboveBar(946); };
            pop->SetFooter(BuildSettingsFooter(raw));
            break;
        case Group::None: break;
    }
    pop->onTab = [g](int t) {
        S().tab = t;
        if (g == Group::Settings) S().settingsTab = t;
        if (MainWindow* w = TheApp().main()) w->RequestRender();
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
    o.onDismiss = [raw] {
        PanelState& s = S();
        if (s.pop != raw) return;
        s.closedAt = AnimFloat::Now();
        s.closedGroup = s.group;
        s.closedTab = s.tab;
        s.pop = nullptr;
        s.group = Group::None;
        if (MainWindow* mw = TheApp().main()) mw->RequestRender();
    };
    PanelState& s = S();
    s.group = g;
    s.tab = tab;
    s.pop = static_cast<Popover*>(w->ShowPopup(std::move(pop), o));
    // Keep the keyboard on the player (Space, arrows); Tab walks into the panel.
    w->SetFocus(nullptr);
}

}  // namespace

namespace panel {

bool PanelJustClosed() {
    return AnimFloat::Now() - S().closedAt < 350;
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

void TogglePlayerPanel(PanelKind kind, const Rect&) {
    MainWindow* w = TheApp().main();
    if (!w) return;
    PanelState& s = S();
    if (PlayerPanelOpen(kind)) {
        ClosePlayerPanels();
        return;
    }
    // The mouse-down of this very click already closed the panel the button belongs to.
    if (AnimFloat::Now() - s.closedAt < 400 && Matches(kind, s.closedGroup, s.closedTab)) {
        s.closedAt = -1e9;
        return;
    }
    const Group g = GroupFor(kind);
    const int tab = TabFor(kind);
    if (s.pop && w->IsPopupOpen(s.pop) && s.group == g) {
        s.pop->ShowTab(tab);
        return;
    }
    ClosePlayerPanels();
    Open(g, tab);
}

}  // namespace app
