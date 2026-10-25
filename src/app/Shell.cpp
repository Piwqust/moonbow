#include "app/Shell.h"
#include "library/MediaTypes.h"

#include "Util.h"
#include "app/Actions.h"
#include "app/Dialogs.h"
#include "app/I18n.h"
#include "app/MainWindow.h"
#include "app/library/LibraryCommon.h"
#include "app/player/Episodes.h"
#include "app/player/NowPlaying.h"
#include "app/player/PlayerView.h"
#include "gfx/Icons.h"

namespace app {

using namespace ui;

namespace {

constexpr float kTitleH = 40;

// One entry of the navigation sidebar.
class NavItem : public Widget {
public:
    NavItem(Section section, NavSidebar* owner) : section_(section), owner_(owner) {
        interactive = true;
        focusable = true;
        height = Length::Px(40);
        width = Length::Grow();
        // The rail of the player's side panel browses that panel's library pane next to the
        // video; the library's own sidebar also switches the window to the library.
        onClick = [this] {
            if (!owner_->InPlayer()) TheApp().ShowPlayer(false);
            TheApp().Navigate(Route{section_, section_ == Section::Music ? lib::LastMusicTab() : std::wstring()});
        };
    }
    bool Selected() const {
        return (owner_->InPlayer() || !TheApp().PlayerShown()) && TheApp().route().section == section_;
    }
    const wchar_t* Name() const { return SectionName(section_); }
    void Describe(A11y& a) override {  // the collapsed rail paints no text
        a.role = Role::ListItem;
        a.name = Tr(SectionName(section_));
        a.selected = Selected() ? 1 : 0;
    }
    void Paint(Painter& p) override {
        const bool sel = Selected();
        if (sel)
            p.Fill(rect, P().bgSelected, 4);
        else if (IsHot())
            p.Fill(rect, P().hover, 4);
        PaintFocusRing(p, rect, 4);
        const Color fg = sel ? P().textPrimary : IsHot() ? P().textPrimary : P().textSecondary;
        const std::string icon = std::string(SectionIcon(section_)) + (sel ? "_active" : "");
        const std::string draw = gfx::Icons::Exists(icon) ? icon : SectionIcon(section_);
        const float ix = owner_->Collapsed() ? rect.Cx() - 12 : rect.x + 12;
        p.Icon(draw, {ix, rect.Cy() - 12, 24, 24}, sel ? P().accent : fg, sel ? P().accent : P().accentMid);
        if (!owner_->Collapsed())
            p.Text(Tr(SectionName(section_)), {rect.x + 48, rect.y, rect.w - 56, rect.h}, TextStyle::Body, fg);
    }

private:
    Section section_;
    NavSidebar* owner_;
};

class AddItem : public Widget {
public:
    explicit AddItem(NavSidebar* owner) : owner_(owner) {
        interactive = true;
        focusable = true;
        height = Length::Px(40);
        width = Length::Grow();
        onClick = [this] {
            std::vector<MenuItem> items;
            // It opens a folder picker: the ellipsis, as in the library's "⋯" menu.
            items.push_back({Tr(L"Add Folder to Library…"), L"", "new-folder", false, true, false, false,
                             [] { lib::AddFolderToLibrary(); }});
            items.push_back({Tr(L"Open file"), ShortcutText(Action::OpenFile), "file", false, true, false, false,
                             [] { Run(Action::OpenFile); }});
            items.push_back({Tr(L"Open folder"), ShortcutText(Action::OpenFolder), "folder", false, true, false, false,
                             [] { Run(Action::OpenFolder); }});
            items.push_back({Tr(L"Open network stream"), ShortcutText(Action::OpenNetwork), "streaming", false, true,
                             false, false, [] { Run(Action::OpenNetwork); }});
            ShowMenu(GetHost(), rect, std::move(items), Place::Below, 220);
        };
    }
    void Paint(Painter& p) override {
        if (IsHot()) p.Fill(rect, P().hover, 4);
        PaintFocusRing(p, rect, 4);
        const Color fg = IsHot() ? P().textPrimary : P().textSecondary;
        const float ix = owner_->Collapsed() ? rect.Cx() - 12 : rect.x + 12;
        p.Icon("add", {ix, rect.Cy() - 12, 24, 24}, fg);
        if (!owner_->Collapsed())
            p.Text(Tr(L"Add Item"), {rect.x + 48, rect.y, rect.w - 56, rect.h}, TextStyle::Body, fg);
    }

private:
    NavSidebar* owner_;
};

// Window button in the title bar (46×40 as in Windows 11, close turns red on hover). Dimmed while
// the window is inactive, like the title, so Settings and the main window tell which one has the keys.
class CaptionButton : public IconButton {
public:
    CaptionButton(std::string icon, std::wstring tip, bool danger) : IconButton(std::move(icon), std::move(tip), 46, 20),
                                                                    danger_(danger) {
        height = Length::Px(kTitleH);
        focusable = false;
    }
    void Paint(Painter& p) override {
        const bool hot = IsHot(), down = IsPressed();
        if (hot) p.Fill(rect, danger_ ? P().danger : (down ? P().press : P().hover));
        const Color rest = GetHost()->IsActive() ? P().textSecondary : P().textTertiary;
        const Color c = hot && danger_ ? Color::Hex(0xFFFFFF) : hot ? P().textPrimary : rest;
        p.Icon(icon, rect.Center(20, 20), c);
    }

private:
    bool danger_;
};

// The app menu: the logo with a chevron, so it reads as "Moonbow's menu" and not as the page's "⋯".
class AppMenuButton : public IconButton {
public:
    AppMenuButton() : IconButton("moonbow", Tr(L"Moonbow menu"), 40, 20) {
        width = Length::Px(44);
    }
    void Paint(Painter& p) override {
        const bool hot = IsHot();
        if (IsPressed()) p.Fill(rect, P().press, 4);
        else if (hot) p.Fill(rect, P().hover, 4);
        PaintFocusRing(p, rect, 4);
        p.Icon("moonbow", {rect.x + 6, rect.Cy() - 10, 20, 20}, P().textPrimary);
        p.Icon("chevron_down", {rect.x + 27, rect.Cy() - 6, 12, 12}, hot ? P().textPrimary : P().textSecondary);
    }
};

std::wstring FolderNameOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos || path.find(L"://") != std::wstring::npos) return {};
    return FileNameOf(path.substr(0, slash));
}

}  // namespace

// --- TitleBar ---------------------------------------------------------------------------

TitleBar::TitleBar(MainWindow* window) : window_(window) {
    height = Length::Px(kTitleH);
    width = Length::Grow();
    dir = Dir::Row;
    align = Align::Center;
    pad = {8, 0, 0, 0};
    gap = 2;
    ncRole = NcRole::Caption;

    moreBtn_ = Add<AppMenuButton>();
    moreBtn_->onClick = [this] { ShowAppMenu(); };

    sidebarBtn_ = Add<IconButton>("sidebar_left", Tr(L"Toggle sidebar"), 32, 20);
    sidebarBtn_->onClick = [] { Run(Action::ToggleSidebar); };

    auto* sep = Add<Divider>(true);
    sep->height = Length::Px(18);
    sep->pad = {6, 0, 6, 0};

    crumb_ = Add<Widget>();
    crumb_->dir = Dir::Row;
    crumb_->align = Align::Center;
    crumb_->gap = 6;
    crumb_->pad = {6, 0, 8, 0};
    crumb_->height = Length::Px(28);
    crumb_->interactive = true;
    crumb_->tooltip = Tr(L"Show in library");
    crumb_->onClick = [] {
        const QueueItem* item = TheApp().queue().CurrentItem();
        TheApp().ShowPlayer(false);
        if (item && item->path.find(L"://") == std::wstring::npos) {
            const size_t slash = item->path.find_last_of(L"\\/");
            const Section section =
                library::KindOf(item->path) == library::MediaKind::Audio ? Section::Music : Section::Videos;
            if (slash != std::wstring::npos) TheApp().Navigate(Route{section, item->path.substr(0, slash)});
        }
    };
    crumb_->Add<IconView>("folder", 20, Tone::Secondary);
    crumbLabel_ = crumb_->Add<Label>(L"", TextStyle::Body, Tone::Secondary);
    crumbLabel_->maxW = 220;

    title_ = Add<Label>(L"", TextStyle::Body, Tone::Secondary);
    title_->width = Length::Grow();
    title_->halign = HAlign::Center;
    title_->color = [this] { return GetHost() && !GetHost()->IsActive() ? P().textTertiary : P().textSecondary; };

    auto* min = Add<CaptionButton>("minimize", Tr(L"Minimize"), false);
    min->onClick = [window] { window->Minimize(); };
    maxBtn_ = Add<CaptionButton>("maximize", L"", false);
    maxBtn_->ncRole = NcRole::Maximize;
    maxBtn_->onClick = [window] { window->ToggleMaximize(); };
    // No fullscreen button here: next to Maximize its corners read as the same thing. It lives at
    // the right end of the control bar, where YouTube and VLC users look for it.
    auto* close = Add<CaptionButton>("close", Tr(L"Close"), true);
    close->onClick = [window] { window->Close(); };

    playerConn_ = Connect(TheApp().playerChanged, [this](unsigned m) {
        if (m & (kChangeFile | kChangeMeta | kChangeState)) Refresh();
    });
    routeConn_ = Connect(TheApp().routeChanged, [this] { Refresh(); });
    screenConn_ = Connect(TheApp().screenChanged, [this] { Refresh(); });
    // Tooltips are translated when set: again on a language switch (maximize's in Refresh).
    tips_ = {{moreBtn_, L"Moonbow menu"}, {sidebarBtn_, L"Toggle sidebar"}, {crumb_, L"Show in library"},
             {min, L"Minimize"}, {close, L"Close"}};
    langConn_ = Connect(TheApp().languageChanged, [this] {
        for (const auto& [w, english] : tips_) w->tooltip = Tr(english);
        Refresh();
    });
    Refresh();
}

TitleBar::~TitleBar() = default;

void TitleBar::Refresh() {
    App& app = TheApp();
    const PlayerState& s = app.player().State();
    const QueueItem* item = app.queue().CurrentItem();
    // Not while the player waits for the file's first frame: the title changes with the screen.
    // A file opened at launch is named while it opens: the title doesn't change when it loads.
    const bool media = (s.fileLoaded || (app.PlayerShown() && app.OpeningSince() >= 0)) && item && !app.PlayerPending();
    std::wstring folder = media ? FolderNameOf(item->path) : std::wstring();
    // Only what changed is laid out again: this also runs for every size change of the window.
    bool changed = false;
    const bool crumbShown = app.PlayerShown() && !folder.empty();
    changed |= crumb_->visible != crumbShown;
    crumb_->visible = crumbShown;
    crumbLabel_->SetText(folder);
    // The library's tags when it knows the file (CP1251 and artist lists read right), as on the
    // player screens.
    const PlayingTags& tags = CurrentTags();
    std::wstring title = media ? (tags.title.empty() ? FileNameOf(item->path) : tags.title) : std::wstring();
    if (media && _wcsicmp(title.c_str(), FileNameOf(item->path).c_str()) == 0) {
        const size_t dot = title.find_last_of(L'.');
        if (dot != std::wstring::npos && dot > 0) title.resize(dot);
    }
    if (media && !tags.artist.empty() && !s.hasVideo) title += L" · " + tags.artist;
    if (std::wstring episode = media ? episodes::Title(item->path) : std::wstring(); !episode.empty()) title = episode;
    if (!app.config().GetBool("interface.titleFileName")) title.clear();
    title_->SetText(title.empty() ? L"Moonbow" : title);
    std::wstring caption = title.empty() ? L"Moonbow" : title + L" — Moonbow";
    if (window_ && caption != windowTitle_) {
        windowTitle_ = std::move(caption);
        window_->SetTitle(windowTitle_);
    }
    const bool maximized = window_ && window_->IsWindowMaximized();
    std::string maxIcon = maximized ? "minimize" : "maximize";
    if (maximized && gfx::Icons::Exists("window")) maxIcon = "window";
    const std::string sideIcon = app.PlayerShown() ? "sidebar_left" : "collapse-sidebar";
    changed |= maxBtn_->icon != maxIcon || sidebarBtn_->icon != sideIcon;
    maxBtn_->icon = std::move(maxIcon);
    maxBtn_->tooltip = maximized ? Tr(L"Restore") : Tr(L"Maximize");
    sidebarBtn_->icon = sideIcon;
    if (changed) InvalidateLayout();  // the labels' SetText lays out by itself when their text changes
}

void TitleBar::Paint(Painter& p) {
    p.Fill(rect, P().bgChrome);
}

void TitleBar::OnLayout() {
    Widget::OnLayout();
    // A long title (worst-case tags) is cut: the full text shows on hover. The label stays part of
    // the caption, so it still drags the window (the host tracks tooltips over the caption).
    const std::wstring& t = title_->Text();
    const bool cut = !t.empty() && gfx::Text::Get().Measure(t, title_->style).width > title_->rect.w + 0.5f;
    if (!cut) title_->tooltip.clear();
    else if (title_->tooltip != t) title_->tooltip = t;
}

void TitleBar::ShowAppMenu() {
    std::vector<MenuItem> items;
    auto item = [](const wchar_t* label, const char* icon, Action a) {
        MenuItem m{Tr(label), ShortcutText(a), icon};
        m.enabled = Enabled(a);
        m.action = [a] { Run(a); };
        return m;
    };
    if (TheApp().PlayerShown()) {
        items.push_back(item(L"Library", "home", Action::GoHome));
        items.push_back(MenuItem::Separator());
    }
    items.push_back(item(L"Open file", "file", Action::OpenFile));
    items.push_back(item(L"Open folder", "folder", Action::OpenFolder));
    items.push_back(item(L"Open network stream", "streaming", Action::OpenNetwork));
    MenuItem addFolder{Tr(L"Add Folder to Library…"), L"", "new-folder"};
    addFolder.action = [] { lib::AddFolderToLibrary(); };
    items.push_back(std::move(addFolder));
    items.push_back(item(L"Add subtitles", "subtitles", Action::OpenSubtitles));
    items.push_back(item(L"Media information", "info", Action::ShowInfo));
    items.push_back(MenuItem::Separator());
    items.push_back(item(L"Customize player", "edit", Action::CustomizePlayer));
    items.push_back(item(L"Keyboard shortcuts", "hotkeys", Action::KeyboardShortcuts));
    items.push_back(item(L"Settings", "settings", Action::Settings));
    MenuItem about{Tr(L"About Moonbow"), L"", "info"};
    about.action = [] { ShowAboutDialog(); };
    items.push_back(std::move(about));
    items.push_back(MenuItem::Separator());
    items.push_back(item(L"Quit", "quit", Action::Quit));
    ShowMenu(GetHost(), moreBtn_->rect, std::move(items), Place::Below, 240);
}

// --- NavSidebar -------------------------------------------------------------------------

NavSidebar::NavSidebar() {
    width = Length::Px(200);
    height = Length::Grow();
    dir = Dir::Column;
    pad = {8, 8, 8, 8};
    gap = 6;
    for (Section s : {Section::Home, Section::Videos, Section::Music, Section::Network, Section::Playlists,
                      Section::Recent})
        Add<NavItem>(s, this);
    Add<AddItem>(this);
    width = Length::Px(200);
    routeConn_ = Connect(TheApp().routeChanged, [this] { Invalidate(); });
    langConn_ = Connect(TheApp().languageChanged, [this] { SetCollapsed(collapsed_); });  // the rail's tooltips
}

NavSidebar::~NavSidebar() = default;

void NavSidebar::SetCollapsed(bool collapsed) {
    collapsed_ = collapsed;
    width = Length::Px(collapsed ? 56.0f : 200.0f);
    // Icons-only rail: the labels move into tooltips.
    for (auto& c : Children()) {
        if (auto* item = dynamic_cast<NavItem*>(c.get())) item->tooltip = collapsed ? Tr(item->Name()) : L"";
        else c->tooltip = collapsed ? Tr(L"Add Item") : L"";
        c->tooltipBeside = collapsed;
    }
    InvalidateLayout();
}

void NavSidebar::Toggle() {
    pin_ = collapsed_ ? Pin::Open : Pin::Closed;
    SetCollapsed(pin_ == Pin::Closed);
}

void NavSidebar::FitWindow(float windowW) {
    // Fluent's NavigationView goes compact below 1008 epx: the content keeps its room.
    const bool rail = pin_ == Pin::Closed || (pin_ == Pin::Auto && windowW < 1000);
    if (rail != collapsed_) SetCollapsed(rail);
}

void NavSidebar::Refresh() {
    Invalidate();
}

void NavSidebar::Paint(Painter& p) {
    // The panel sits 8 DIP inside the window edge, like the design; in the player it runs flush
    // into the side pane beside it, where that gutter read as gaps.
    if (inPlayer_) p.Fill(rect, P().bgPanel);
    else p.Fill({rect.x + 8, rect.y + 8, rect.w - 8, rect.h - 16}, P().bgPanel, 6);
}

// --- LibraryPane -----------------------------------------------------------------------

namespace {

// The search box's hint as a phrase that reads naturally (UX-18): "Search history", "Search in
// folder “Videos”", never a section name put into "Search in “…”".
std::wstring SearchHint(const Route& r, const std::vector<Crumb>& crumbs) {
    const std::wstring& p = r.path;
    auto starts = [&p](const wchar_t* prefix) { return p.rfind(prefix, 0) == 0; };
    if (p == L"favorites") return Tr(L"Search favorites");
    switch (r.section) {
        case Section::Home: break;
        case Section::Recent: return Tr(L"Search history");
        case Section::Network: return Tr(L"Search Network");
        case Section::Playlists: return Tr(p.empty() ? L"Search playlists" : L"Search this playlist");
        case Section::Videos:
        case Section::Music:
            if (lib::LooksLikeFolder(p) && !crumbs.empty()) return Fmt(Tr(L"Search in folder “{0}”"), {crumbs.back().label});
            if (starts(L"show:")) return Tr(L"Search episodes");
            if (starts(L"album:")) return Tr(L"Search this album");
            if (starts(L"artist:")) return Tr(L"Search this artist");
            if (starts(L"genre:")) return Tr(L"Search this genre");
            return Tr(r.section == Section::Music ? L"Search music" : L"Search videos");
    }
    return Tr(L"Search library");
}

class CrumbButton : public LinkButton {
public:
    CrumbButton(std::wstring text, Route route, bool last) : LinkButton(std::move(text), TextStyle::Large), route_(std::move(route)),
                                                              last_(last) {
        onClick = [this] {
            if (!last_) TheApp().Navigate(route_);
        };
    }
    bool Last() const { return last_; }
    void Paint(Painter& p) override {
        const Color c = last_ ? P().textPrimary : (IsHot() ? P().textPrimary : P().textSecondary);
        p.Text(text, rect, style, c);
        PaintFocusRing(p, rect, 4);
    }

private:
    Route route_;
    bool last_;
};

// "Videos › Shows › S01" in the library header. Parents that don't fit go into a "…" menu, the
// nearest ones stay; the current page keeps all the room that is left before it is cut, and
// every cut crumb shows its full name as a tooltip.
class CrumbBar : public Widget {
public:
    CrumbBar() {
        clipChildren = true;
        pad = {8, 0, 8, 0};
    }

    void Set(std::vector<Crumb> crumbs, bool compact) {
        ClearChildren();
        crumbs_ = std::move(crumbs);
        compact_ = compact;
        buttons_.clear();
        chevrons_.clear();
        natural_.clear();
        more_ = Add<LinkButton>(L"…", TextStyle::Large);
        more_->tooltip = Tr(L"Show the full path");
        more_->onClick = [this] { ShowHidden(); };
        moreChevron_ = Add<IconView>("chevron_right", 16, Tone::Tertiary);
        for (size_t i = 0; i < crumbs_.size(); ++i) {
            chevrons_.push_back(i > 0 ? Add<IconView>("chevron_right", 16, Tone::Tertiary) : nullptr);
            buttons_.push_back(Add<CrumbButton>(crumbs_[i].label, crumbs_[i].route, i + 1 == crumbs_.size()));
            natural_.push_back(std::ceil(gfx::Text::Get().Measure(crumbs_[i].label, TextStyle::Large).width) + 1);
        }
        InvalidateLayout();
    }

    Size MeasureContent(float availW, float) override {
        float w = pad.H();
        for (size_t i = 0; i < natural_.size(); ++i) w += natural_[i] + (i > 0 ? kChevron + 2 * kGap : 0);
        return {std::min(w, availW), std::max(28.0f, gfx::Text::Get().LineHeight(TextStyle::Large) + 4)};
    }

    void OnLayout() override {
        const size_t n = buttons_.size();
        if (!more_) return;
        more_->visible = moreChevron_->visible = false;
        if (n == 0) return;
        const Rect in = rect.Inset(pad);
        const float moreW = std::ceil(more_->Measure(kInf, kInf).w);
        const float cur = natural_[n - 1];
        // Width of everything before the current crumb with `hidden` parents in the menu and the
        // visible ones capped at `cap`.
        auto before = [&](size_t hidden, float cap) {
            float w = hidden > 0 ? moreW + kChevron + 2 * kGap : 0.0f;
            for (size_t i = hidden; i + 1 < n; ++i) w += std::min(natural_[i], cap) + kChevron + 2 * kGap;
            return w;
        };
        size_t hidden = compact_ ? n - 1 : 0;  // the side panel names only the current page
        float cap = kParentMax;
        bool found = compact_;
        for (size_t h = 0; h < n && !found; ++h)
            for (float c : {kParentMax, kParentMin})
                if (before(h, c) + cur <= in.w) {
                    hidden = h;
                    cap = c;
                    found = true;
                    break;
                }
        if (!found) {
            hidden = n - 1;  // only "…" and the current page: the current page gets the rest
            cap = kParentMin;
        }
        const float lead = compact_ ? 0.0f : before(hidden, cap);
        const float curW = std::max(0.0f, std::min(cur, in.w - lead));

        float x = in.x;
        auto place = [&](Widget* w, float boxW) {
            const float h = std::min(in.h, w->Measure(boxW, in.h).h);
            w->visible = true;
            w->Arrange({x, std::round(in.y + (in.h - h) / 2), boxW, h});
            x += boxW + kGap;
        };
        if (hidden > 0 && !compact_) {
            place(more_, moreW);
            place(moreChevron_, kChevron);
        }
        for (size_t i = 0; i < n; ++i) {
            const bool show = i >= hidden;
            buttons_[i]->visible = show;
            if (chevrons_[i]) chevrons_[i]->visible = false;
            if (!show) continue;
            if (chevrons_[i] && i > hidden) {
                place(chevrons_[i], kChevron);
            }
            const float w = i + 1 == n ? curW : std::min(natural_[i], cap);
            place(buttons_[i], w);
            buttons_[i]->tooltip = w + 0.5f < natural_[i] ? crumbs_[i].label : std::wstring();
        }
    }

private:
    static constexpr float kGap = 2, kChevron = 16;
    // Parents first shrink to kParentMin, then move into the "…" menu; the current page keeps its
    // whole name while anything else can give way.
    static constexpr float kParentMax = 220, kParentMin = 110;

    void ShowHidden() {
        std::vector<MenuItem> items;
        for (size_t i = 0; i < buttons_.size(); ++i) {
            if (buttons_[i]->visible) break;
            MenuItem m{crumbs_[i].label, L"", i == 0 ? SectionIcon(crumbs_[i].route.section) : "folder"};
            const Route r = crumbs_[i].route;
            m.action = [r] { TheApp().Navigate(r); };
            items.push_back(std::move(m));
        }
        if (!items.empty()) ShowMenu(GetHost(), more_->rect, std::move(items), Place::Below, 260);
    }

    std::vector<Crumb> crumbs_;
    bool compact_ = false;
    LinkButton* more_ = nullptr;
    IconView* moreChevron_ = nullptr;
    std::vector<CrumbButton*> buttons_;
    std::vector<IconView*> chevrons_;
    std::vector<float> natural_;
};

}  // namespace

LibraryPane::LibraryPane(bool compact) : compact_(compact) {
    width = Length::Grow();
    height = Length::Grow();
    dir = Dir::Column;
    Build();
    routeConn_ = Connect(TheApp().routeChanged, [this] { ShowRoute(TheApp().route()); });
    // Back/Forward come back to the same scroll offset and selection.
    leaveConn_ = Connect(TheApp().routeLeaving, [this] {
        if (view_ && route_ == TheApp().route()) TheApp().CurrentViewState(compact_) = view_->SaveState();
    });
    // Header and page strings are translated when built: rebuild on a language switch, keeping the
    // scroll offset and selection as Back/Forward do (ShowRoute restores the saved state).
    langConn_ = Connect(TheApp().languageChanged, [this] {
        if (view_ && route_ == TheApp().route()) TheApp().CurrentViewState(compact_) = view_->SaveState();
        ClosePagePopups();
        headerConn_.Reset();
        view_ = nullptr;
        ClearChildren();
        Build();
    });
}

void LibraryPane::Build() {
    BuildHeader();
    body_ = Add<Widget>();
    body_->width = Length::Grow();
    body_->height = Length::Grow();
    body_->dir = Dir::Stack;
    body_->align = Align::Stretch;
    ShowRoute(TheApp().route());
}

LibraryPane::~LibraryPane() = default;

// Menus opened from the page (sort, "⋯", a row's) call back into it, and Alt+Left, the Back
// key or a language switch can rebuild it while one is open: they go first.
void LibraryPane::ClosePagePopups() {
    if (Host* host = GetHost()) host->CloseAllPopups();
}

void LibraryPane::BuildHeader() {
    header_ = Add<Widget>();
    header_->width = Length::Grow();
    header_->height = Length::Px(compact_ ? 48.0f : 70.0f);
    header_->dir = Dir::Row;
    header_->align = Align::Center;
    header_->pad = compact_ ? Insets{12, 0, 8, 0} : Insets{28, 0, 20, 0};
    header_->gap = 4;

    back_ = header_->Add<IconButton>("arrow_left", Tr(L"Back"));
    back_->onClick = [] { TheApp().GoBack(); };
    forward_ = header_->Add<IconButton>("arrow_right", Tr(L"Forward"));
    forward_->onClick = [] { TheApp().GoForward(); };

    crumbs_ = header_->Add<CrumbBar>();
    crumbs_->width = Length::Grow();

    count_ = header_->Add<Label>(L"", TextStyle::Body, Tone::Tertiary);
    // A label ignores padding: this keeps 16 DIP (with the gaps) after the count, even when the
    // search box follows it directly (VIS-14).
    countGap_ = header_->Add<Widget>();
    countGap_->width = Length::Px(8);
    sort_ = header_->Add<IconButton>("sort", Tr(L"Sort"));
    sort_->onClick = [this] {
        if (!view_) return;
        auto items = view_->SortMenu();
        if (!items.empty()) ShowMenu(GetHost(), sort_->rect, std::move(items), Place::BelowRight, 200);
    };
    mode_ = header_->Add<IconButton>("grid", Tr(L"View"));
    mode_->onClick = [this] {
        if (!view_ || !view_->HasViewModes()) return;
        view_->SetViewMode(view_->GetViewMode() == ViewMode::Grid ? ViewMode::List : ViewMode::Grid);
        RefreshHeader();
    };
    searchBtn_ = header_->Add<IconButton>("search", Tr(L"Search"));
    searchBtn_->visible = compact_;
    searchBtn_->onClick = [this] {
        search_->visible = !search_->visible;
        if (search_->visible) GetHost()->SetFocus(search_);
        InvalidateLayout();
    };
    search_ = header_->Add<TextBox>(Tr(L"Search"), "search");
    search_->width = Length::Px(compact_ ? 160.0f : 324.0f);
    search_->height = Length::Px(32);
    search_->raised = true;
    search_->visible = !compact_;
    search_->onChange = [this](const std::wstring& text) {
        if (view_) view_->SetFilter(text);
    };
    search_->onEscape = [this] {
        search_->SetText(L"", true);
        GetHost()->SetFocus(nullptr);
    };
    more_ = header_->Add<IconButton>("more", Tr(L"More"));
    more_->onClick = [this] {
        if (!view_) return;
        auto items = view_->MoreMenu();
        if (!items.empty()) ShowMenu(GetHost(), more_->rect, std::move(items), Place::BelowRight, 220);
    };
}

void LibraryPane::ShowRoute(const Route& route) {
    if (view_ && route == route_) return;
    const Route from = route_;
    const bool hadView = view_ != nullptr;
    route_ = route;
    if (hadView) ClosePagePopups();
    headerConn_.Reset();
    body_->ClearChildren();
    auto view = CreateLibraryView(route, compact_);
    view_ = view.get();
    if (view_) {
        view_->width = Length::Grow();
        view_->height = Length::Grow();
        body_->AddChild(std::move(view));
        headerConn_ = Connect(view_->headerChanged, [this] { RefreshHeader(); });
    }
    search_->SetText(L"");
    if (view_) {
        ViewState st = TheApp().CurrentViewState(compact_);
        // Up a level (a crumb, the "…" menu) to a page not seen before: select the folder we came from.
        const bool folder = lib::LooksLikeFolder(from.path);
        if (st.Empty() && hadView && folder && from.section == route.section && from.path.size() > route.path.size()) {
            st.selPath = from.path;
            st.within = true;
        }
        if (!st.Empty()) view_->RestoreState(st);
    }
    RefreshHeader();
    // Arrow keys work on a page right after it opens (not in the player's side
    // panel, where the player keeps the keyboard, nor in the hidden library while the side
    // panel navigates).
    if (!compact_ && view_ && !TheApp().PlayerShown())
        if (ui::Host* host = GetHost())
            if (ui::Widget* first = view_->InitialFocus()) host->SetFocus(first);
}

void LibraryPane::RefreshHeader() {
    App& app = TheApp();
    back_->enabled = app.CanGoBack();
    forward_->enabled = app.CanGoForward();
    if (view_) {
        std::vector<Crumb> crumbs = view_->Breadcrumbs();
        // The box says where it searches (UX-9): Home searches the whole library, a page only itself.
        searchHint_ = SearchHint(route_, crumbs);
        if (search_) search_->a11yName = searchHint_;  // the count label before it is no label of it
        static_cast<CrumbBar*>(crumbs_)->Set(std::move(crumbs), compact_);
        const long long n = view_->ItemCount();
        // The side panel is too narrow for crumbs and a count side by side. "0 items" would only
        // repeat "Nothing found" (UX-19).
        countWanted_ = n > 0 && !compact_;
        count_->visible = countWanted_;
        count_->SetText(n > 0 ? view_->CountText(n) : L"");
        // Nothing to sort or lay out on an empty page (its count is hidden: n is -1 there, not 0).
        const bool content = n != 0 && !view_->IsEmpty();
        mode_->visible = view_->HasViewModes() && content;
        mode_->icon = view_->GetViewMode() == ViewMode::Grid ? "list" : "grid";
        mode_->tooltip = view_->GetViewMode() == ViewMode::Grid ? Tr(L"List view") : Tr(L"Grid view");
        sort_->visible = !view_->SortMenu().empty() && content;
        // An empty or "not found" page has nothing to search (UX-18); "Nothing found" keeps its query.
        const bool searchable = view_->HasSearch() && !(view_->IsEmpty() && search_->Text().empty());
        search_->visible = searchable && (!compact_ || search_->visible);
        searchBtn_->visible = compact_ && searchable;
        more_->visible = !view_->MoreMenu().empty();
    } else {
        static_cast<CrumbBar*>(crumbs_)->Set({}, compact_);
    }
    InvalidateLayout();
}

void LibraryPane::FocusSearch() {
    if (!search_) return;
    search_->visible = true;
    GetHost()->SetFocus(search_, true);
    search_->SelectAll();
    InvalidateLayout();
}

void LibraryPane::Search(const std::wstring& text) {
    if (!search_) return;
    search_->SetText(text, true);
    FocusSearch();
}

void LibraryPane::OnLayout() {
    if (!compact_ && search_) search_->width = Length::Px(std::clamp(rect.w - 520.0f, 140.0f, 324.0f));
    // In a tight header the item count steps aside for the crumbs (CrumbBar keeps the current
    // page readable by folding its parents into "…").
    const bool tight = !compact_ && rect.w < 900;
    if (count_) count_->visible = countWanted_ && !tight;
    if (countGap_) countGap_->visible = count_ && count_->visible;
    Widget::OnLayout();
    // A box too narrow for the whole hint says just "Search", not a cut "Search in “…".
    if (search_) {
        const float room = search_->rect.w - 48;  // the icon and the padding
        search_->placeholder = gfx::Text::Get().Measure(searchHint_, TextStyle::Body).width <= room ? searchHint_ : std::wstring(Tr(L"Search"));
    }
}

bool LibraryPane::Backdrop(Color& out) const {
    out = compact_ ? P().bgPanel : P().bgBase;
    return true;
}

void LibraryPane::Paint(Painter& p) {
    p.Fill(rect, compact_ ? P().bgPanel : P().bgBase);
    const float y = header_->rect.Bottom();
    p.Fill({rect.x + (compact_ ? 0.0f : 8.0f), y, rect.w - (compact_ ? 0.0f : 16.0f), 1}, P().lineSubtle);
}

}  // namespace app
