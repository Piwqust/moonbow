// Player popovers (tracks, settings, queue...). Placeholder menus until the
// full panels from the design (design/ref/components/Controls_491-1780.png) land.
#include "Util.h"
#include "app/App.h"
#include "app/I18n.h"
#include "app/MainWindow.h"
#include "app/player/ControlBar.h"

namespace app {

using namespace ui;

namespace {

Widget* g_panel = nullptr;
PanelKind g_kind = PanelKind::Tracks;

std::vector<MenuItem> Tracks(const wchar_t* type, const char* prop) {
    std::vector<MenuItem> items;
    for (const Track& t : TheApp().player().State().tracks) {
        if (t.type != type) continue;
        MenuItem m{t.Label()};
        m.checked = t.selected;
        const int64_t id = t.id;
        m.action = [prop, id] { TheApp().player().SetTrack(prop, id); };
        items.push_back(std::move(m));
    }
    return items;
}

}  // namespace

bool PlayerPanelOpen(PanelKind kind) {
    MainWindow* w = TheApp().main();
    return w && g_panel && g_kind == kind && w->IsPopupOpen(g_panel);
}

void ClosePlayerPanels() {
    MainWindow* w = TheApp().main();
    if (w && g_panel && w->IsPopupOpen(g_panel)) w->ClosePopup(g_panel);
    g_panel = nullptr;
}

void TogglePlayerPanel(PanelKind kind, const Rect& anchor) {
    MainWindow* w = TheApp().main();
    if (!w) return;
    if (PlayerPanelOpen(kind)) {
        ClosePlayerPanels();
        return;
    }
    ClosePlayerPanels();
    std::vector<MenuItem> items;
    switch (kind) {
        case PanelKind::Tracks: items = Tracks(L"audio", "aid"); break;
        case PanelKind::Subtitles:
            items = Tracks(L"sub", "sid");
            items.insert(items.begin(), {Tr(L"Disable"), L"", "", false, true, false, false,
                                         [] { TheApp().player().SetTrack("sid", 0); }});
            break;
        case PanelKind::Speed:
        case PanelKind::Settings:
            for (double sp : {0.5, 0.75, 1.0, 1.25, 1.5, 2.0}) {
                wchar_t buf[32];
                swprintf_s(buf, L"%g×", sp);
                MenuItem m{buf};
                m.checked = std::abs(TheApp().player().State().speed - sp) < 0.01;
                m.action = [sp] { TheApp().player().SetSpeed(sp); };
                items.push_back(std::move(m));
            }
            break;
        case PanelKind::Queue: {
            const auto& q = TheApp().queue().Items();
            for (size_t i = 0; i < q.size(); ++i) {
                MenuItem m{q[i].title.empty() ? FileNameOf(q[i].path) : q[i].title};
                m.checked = static_cast<int>(i) == TheApp().queue().Current();
                m.action = [i] { TheApp().PlayIndex(static_cast<int>(i)); };
                items.push_back(std::move(m));
            }
            break;
        }
        case PanelKind::Chapters:
            for (const Chapter& c : TheApp().player().State().chapters) {
                MenuItem m{(c.title.empty() ? Tr(L"Chapter") : c.title) + std::wstring(L"  ") + FormatTime(c.time, true)};
                const double t = c.time;
                m.action = [t] { TheApp().player().SeekAbsolute(t, true); };
                items.push_back(std::move(m));
            }
            break;
        case PanelKind::Bookmarks: break;
    }
    if (items.empty()) items.push_back({Tr(L"Nothing here yet"), L"", "", false, false});
    g_kind = kind;
    g_panel = ShowMenu(w, anchor, std::move(items), Place::AboveRight, 260);
}

}  // namespace app
