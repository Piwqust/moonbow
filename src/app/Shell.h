#pragma once
#include "app/App.h"
#include "app/LibraryView.h"
#include "ui/Controls.h"

namespace app {

class MainWindow;

// Window title bar (40 DIP): logo, app menu, sidebar toggle, breadcrumb,
// centered media title, window buttons.
class TitleBar : public ui::Widget {
public:
    explicit TitleBar(MainWindow* window);
    ~TitleBar() override;
    void Paint(ui::Painter& p) override;
    void OnLayout() override;
    void Refresh();

private:
    void ShowAppMenu();
    MainWindow* window_;
    ui::IconButton* sidebarBtn_ = nullptr;
    ui::Widget* crumb_ = nullptr;
    ui::Label* crumbLabel_ = nullptr;
    ui::Label* title_ = nullptr;
    ui::IconButton* maxBtn_ = nullptr;
    ui::IconButton* moreBtn_ = nullptr;  // the app menu (logo + chevron)
    std::wstring windowTitle_;  // last text given to the window caption
    std::vector<std::pair<ui::Widget*, const wchar_t*>> tips_;  // tooltip widget, English text
    ScopedConnection playerConn_, routeConn_, screenConn_, langConn_;
};

// Library navigation (Home, Videos, Music, Network, Playlists, Recent, Add).
class NavSidebar : public ui::Widget {
public:
    NavSidebar();
    ~NavSidebar() override;
    void SetCollapsed(bool collapsed);
    bool Collapsed() const { return collapsed_; }
    // The rail of the player's side panel: it navigates the panel and keeps the player on screen.
    void SetInPlayer(bool inPlayer) { inPlayer_ = inPlayer; }
    bool InPlayer() const { return inPlayer_; }
    void Paint(ui::Painter& p) override;
    void Refresh();

private:
    bool collapsed_ = false;
    bool inPlayer_ = false;
    ScopedConnection routeConn_, langConn_;
};

// Library header + the current LibraryView.
class LibraryPane : public ui::Widget {
public:
    explicit LibraryPane(bool compact);
    ~LibraryPane() override;
    void ShowRoute(const Route& route);
    LibraryView* View() const { return view_; }
    void FocusSearch();
    void Paint(ui::Painter& p) override;
    bool Backdrop(ui::Color& out) const override;
    void OnLayout() override;

private:
    bool countWanted_ = false;  // the view has a count (hidden anyway in a tight header)
    std::wstring searchHint_;   // "Search in “Mixed”": where the search box looks
    void Build();
    void BuildHeader();
    void RefreshHeader();
    void ClosePagePopups();
    bool compact_;
    Route route_;
    ui::Widget* header_ = nullptr;
    ui::Widget* crumbs_ = nullptr;
    ui::Label* count_ = nullptr;
    ui::IconButton* back_ = nullptr;
    ui::IconButton* forward_ = nullptr;
    ui::IconButton* sort_ = nullptr;
    ui::IconButton* mode_ = nullptr;
    ui::IconButton* searchBtn_ = nullptr;
    ui::TextBox* search_ = nullptr;
    ui::IconButton* more_ = nullptr;
    ui::Widget* body_ = nullptr;
    LibraryView* view_ = nullptr;
    ScopedConnection headerConn_, routeConn_, leaveConn_, langConn_;
};

}  // namespace app
