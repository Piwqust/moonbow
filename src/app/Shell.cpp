#include "app/Shell.h"

#include "Util.h"
#include "app/Actions.h"
#include "app/I18n.h"
#include "app/MainWindow.h"
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
        onClick = [this] {
            TheApp().ShowPlayer(false);
            TheApp().Navigate(Route{section_, {}});
        };
    }
    bool Selected() const { return !TheApp().PlayerShown() && TheApp().route().section == section_; }
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
            items.push_back({Tr(L"Add folder to library"), L"", "new-folder", false, true, false, false,
                             [] { TheApp().OpenFolderDialog(); }});
            items.push_back({Tr(L"Open file"), ShortcutText(Action::OpenFile), "file", false, true, false, false,
                             [] { Run(Action::OpenFile); }});
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
            p.Text(Tr(L"Add item"), {rect.x + 48, rect.y, rect.w - 56, rect.h}, TextStyle::Body, fg);
    }

private:
    NavSidebar* owner_;
};

// Window button in the title bar (36×40, close turns red on hover).
class CaptionButton : public IconButton {
public:
    CaptionButton(std::string icon, std::wstring tip, bool danger) : IconButton(std::move(icon), std::move(tip), 36, 20),
                                                                    danger_(danger) {
        height = Length::Px(kTitleH);
        focusable = false;
    }
    void Paint(Painter& p) override {
        const bool hot = IsHot(), down = IsPressed();
        if (hot) p.Fill(rect, danger_ ? P().danger : (down ? P().press : P().hover));
        const Color c = hot && danger_ ? Color::Hex(0xFFFFFF) : hot ? P().textPrimary : P().textSecondary;
        p.Icon(icon, rect.Center(20, 20), c);
    }

private:
    bool danger_;
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

    auto* logo = Add<IconView>("lumen", 20, Tone::Primary);
    logo->width = Length::Px(32);
    logo->color = [] { return P().textPrimary; };

    moreBtn_ = Add<IconButton>("more", Tr(L"Menu"), 32, 20);
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
            if (slash != std::wstring::npos) TheApp().Navigate(Route{Section::Videos, item->path.substr(0, slash)});
        }
    };
    crumb_->Add<IconView>("folder", 20, Tone::Secondary);
    crumbLabel_ = crumb_->Add<Label>(L"", TextStyle::Body, Tone::Secondary);
    crumbLabel_->maxW = 220;

    title_ = Add<Label>(L"", TextStyle::Body, Tone::Secondary);
    title_->width = Length::Grow();
    title_->halign = HAlign::Center;

    auto* min = Add<CaptionButton>("minimize", Tr(L"Minimize"), false);
    min->onClick = [window] { window->Minimize(); };
    maxBtn_ = Add<CaptionButton>("maximize", L"", false);
    maxBtn_->ncRole = NcRole::Maximize;
    maxBtn_->onClick = [window] { window->ToggleMaximize(); };
    fullBtn_ = Add<CaptionButton>("fullscreen", Tr(L"Fullscreen"), false);
    fullBtn_->onClick = [] { Run(Action::Fullscreen); };
    auto* close = Add<CaptionButton>("close", Tr(L"Close"), true);
    close->onClick = [window] { window->Close(); };

    playerConn_ = Connect(TheApp().playerChanged, [this](unsigned m) {
        if (m & (kChangeFile | kChangeMeta | kChangeState)) Refresh();
    });
    routeConn_ = Connect(TheApp().routeChanged, [this] { Refresh(); });
    screenConn_ = Connect(TheApp().screenChanged, [this] { Refresh(); });
    Refresh();
}

TitleBar::~TitleBar() = default;

void TitleBar::Refresh() {
    App& app = TheApp();
    const PlayerState& s = app.player().State();
    const QueueItem* item = app.queue().CurrentItem();
    const bool media = s.fileLoaded && item;
    std::wstring folder = media ? FolderNameOf(item->path) : std::wstring();
    crumb_->visible = app.PlayerShown() && !folder.empty();
    crumbLabel_->SetText(folder);
    std::wstring title = media ? (s.title.empty() ? FileNameOf(item->path) : s.title) : std::wstring();
    if (media && !s.artist.empty() && !app.player().State().hasVideo) title = s.artist + L" — " + title;
    title_->SetText(title.empty() ? L"Lumen" : title);
    if (window_) window_->SetTitle(title.empty() ? L"Lumen" : title + L" — Lumen");
    maxBtn_->icon = window_ && window_->IsWindowMaximized() ? "minimize" : "maximize";
    maxBtn_->tooltip = window_ && window_->IsWindowMaximized() ? Tr(L"Restore") : Tr(L"Maximize");
    if (window_ && window_->IsWindowMaximized() && gfx::Icons::Exists("window")) maxBtn_->icon = "window";
    sidebarBtn_->icon = app.PlayerShown() ? "sidebar_left" : "collapse-sidebar";
    InvalidateLayout();
}

void TitleBar::Paint(Painter& p) {
    p.Fill(rect, P().bgChrome);
}

void TitleBar::ShowAppMenu() {
    std::vector<MenuItem> items;
    auto item = [](const wchar_t* label, const char* icon, Action a) {
        MenuItem m{Tr(label), ShortcutText(a), icon};
        m.enabled = Enabled(a);
        m.action = [a] { Run(a); };
        return m;
    };
    items.push_back(item(L"Open file", "file", Action::OpenFile));
    items.push_back(item(L"Open folder", "folder", Action::OpenFolder));
    items.push_back(item(L"Open network stream", "streaming", Action::OpenNetwork));
    items.push_back(item(L"Add subtitles", "subtitles", Action::OpenSubtitles));
    items.push_back(MenuItem::Separator());
    items.push_back(item(L"Customize player", "edit", Action::CustomizePlayer));
    items.push_back(item(L"Keyboard shortcuts", "hotkeys", Action::KeyboardShortcuts));
    items.push_back(item(L"Settings", "settings", Action::Settings));
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
    routeConn_ = Connect(TheApp().routeChanged, [this] { Invalidate(); });
}

NavSidebar::~NavSidebar() = default;

void NavSidebar::SetCollapsed(bool collapsed) {
    collapsed_ = collapsed;
    width = Length::Px(collapsed ? 56.0f : 200.0f);
    for (auto& c : Children()) c->tooltip = collapsed && dynamic_cast<NavItem*>(c.get()) ? L"" : L"";
    InvalidateLayout();
}

void NavSidebar::Refresh() {
    Invalidate();
}

void NavSidebar::Paint(Painter& p) {
    // The panel sits 8 DIP inside the window edge, like the design.
    p.Fill({rect.x + 8, rect.y + 8, rect.w - 8, rect.h - 16}, P().bgPanel, 6);
}

// --- LibraryPane -----------------------------------------------------------------------

namespace {

class CrumbButton : public LinkButton {
public:
    CrumbButton(std::wstring text, Route route, bool last) : LinkButton(std::move(text), TextStyle::Large), route_(std::move(route)),
                                                              last_(last) {
        onClick = [this] {
            if (!last_) TheApp().Navigate(route_);
        };
        maxW = 280;
    }
    void Paint(Painter& p) override {
        const Color c = last_ ? P().textPrimary : (IsHot() ? P().textPrimary : P().textSecondary);
        p.Text(text, rect, style, c);
    }

private:
    Route route_;
    bool last_;
};

}  // namespace

LibraryPane::LibraryPane(bool compact) : compact_(compact) {
    width = Length::Grow();
    height = Length::Grow();
    dir = Dir::Column;
    BuildHeader();
    body_ = Add<Widget>();
    body_->width = Length::Grow();
    body_->height = Length::Grow();
    body_->dir = Dir::Stack;
    body_->align = Align::Stretch;
    routeConn_ = Connect(TheApp().routeChanged, [this] { ShowRoute(TheApp().route()); });
    ShowRoute(TheApp().route());
}

LibraryPane::~LibraryPane() = default;

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

    crumbs_ = header_->Add<Widget>();
    crumbs_->dir = Dir::Row;
    crumbs_->align = Align::Center;
    crumbs_->gap = 2;
    crumbs_->pad = {8, 0, 8, 0};
    crumbs_->width = Length::Grow();
    crumbs_->clipChildren = true;

    count_ = header_->Add<Label>(L"", TextStyle::Body, Tone::Tertiary);
    count_->pad = {0, 0, 12, 0};
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
    search_->background = P().bgRaised;
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
    route_ = route;
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
    RefreshHeader();
}

void LibraryPane::RefreshHeader() {
    App& app = TheApp();
    back_->enabled = app.CanGoBack();
    forward_->enabled = app.CanGoForward();
    crumbs_->ClearChildren();
    if (view_) {
        auto crumbs = view_->Breadcrumbs();
        // Long paths collapse to "… › parent › current".
        const size_t keep = compact_ ? 2 : 4;
        size_t start = crumbs.size() > keep ? crumbs.size() - keep : 0;
        if (start > 0) crumbs_->Add<Label>(L"…", TextStyle::Large, Tone::Secondary);
        for (size_t i = start; i < crumbs.size(); ++i) {
            if (i > start || start > 0) crumbs_->Add<IconView>("chevron_right", 16, Tone::Tertiary);
            crumbs_->Add<CrumbButton>(crumbs[i].label, crumbs[i].route, i + 1 == crumbs.size());
        }
        const long long n = view_->ItemCount();
        count_->visible = n >= 0;
        count_->SetText(n >= 0 ? CountItems(n) : L"");
        mode_->visible = view_->HasViewModes();
        mode_->icon = view_->GetViewMode() == ViewMode::Grid ? "list" : "grid";
        mode_->tooltip = view_->GetViewMode() == ViewMode::Grid ? Tr(L"List view") : Tr(L"Grid view");
        sort_->visible = !view_->SortMenu().empty();
        search_->visible = view_->HasSearch() && (!compact_ || search_->visible);
        searchBtn_->visible = compact_ && view_->HasSearch();
        more_->visible = !view_->MoreMenu().empty();
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

void LibraryPane::Paint(Painter& p) {
    p.Fill(rect, compact_ ? P().bgPanel : P().bgBase);
    const float y = header_->rect.Bottom();
    p.Fill({rect.x + (compact_ ? 0.0f : 8.0f), y, rect.w - (compact_ ? 0.0f : 16.0f), 1}, P().lineSubtle);
}

}  // namespace app
