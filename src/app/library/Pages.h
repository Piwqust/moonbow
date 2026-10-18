#pragma once
// Internal to the library module: the base class of list/grid pages and the page factories.
#include "app/LibraryView.h"
#include "app/library/ItemGrid.h"
#include "app/library/LibraryCommon.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace app::lib {

// Centered message with an icon and buttons ("Your library is empty" + "Add folder").
class EmptyState : public ui::Widget {
public:
    struct Btn {
        std::wstring label;
        ui::ButtonKind kind = ui::ButtonKind::Secondary;
        std::string icon;
        std::function<void()> action;
    };
    EmptyState();
    void Set(const char* icon, const std::wstring& title, const std::wstring& text, std::vector<Btn> buttons = {});
    // Transparent outside its controls, so the page underneath stays usable.
    ui::Widget* HitTest(float x, float y) override;
};

// "Scanning library…" pill in the bottom-right corner of a page while a scan runs. Overlays the
// content (a Stack child) without taking layout space or input.
class ScanBadge : public ui::Widget {
public:
    explicit ScanBadge(bool compact = false);
    void Update();  // reads library().Scanning() and the file count
    void Paint(ui::Painter& p) override;
    ui::Widget* HitTest(float, float) override { return nullptr; }

private:
    std::wstring text_;
    bool compact_;
};

// Section title with a chevron ("Continue Watching ›"), clickable.
class SectionTitle : public ui::Widget {
public:
    SectionTitle(std::wstring text, bool compact);
    std::wstring text;
    ui::Size MeasureContent(float availW, float availH) override;
    void Paint(ui::Painter& p) override;

private:
    bool compact_;
};

// While a freshly opened page ignores input (ItemGrid::JustOpened), every hit on it goes to
// its grid, so toolbar and empty-state buttons can't catch the second click of a double-click
// either. Returns `hit` otherwise.
ui::Widget* GuardedHit(ItemGrid* grid, ui::Widget* hit);
// The first page shown after start gets the keyboard when nothing has it (arrow keys move in
// the grid instead of changing the volume). Not in the player's side panel.
void TakeFreeFocus(LibraryView& view, bool compact, bool& done);

struct HistoryInfo {
    double progress = -1;  // shown progress, < 0 none
    bool watched = false;
};

// A page whose content is one ItemGrid inside a ScrollView, with an optional toolbar,
// a scanning indicator and an empty state. Subclasses fill entries in Build().
class Page : public LibraryView {
public:
    Page(Route route, bool compact, std::string key);
    ~Page() override;

    std::vector<Crumb> Breadcrumbs() const override;
    long long ItemCount() const override { return count_; }
    bool HasViewModes() const override { return modes_ && !compact_; }
    ViewMode GetViewMode() const override { return list_ ? ViewMode::List : ViewMode::Grid; }
    void SetViewMode(ViewMode m) override;
    void SetFilter(const std::wstring& text) override;
    ui::Widget* InitialFocus() override { return grid_; }
    ui::Widget* HitTest(float x, float y) override;
    void OnLayout() override;

protected:
    // --- building (called from Build) ---
    int Push(Entry e);
    int AddRoute(Entry e, Route target);
    int AddAction(Entry e, std::function<void()> action);
    void AddHeader(const std::wstring& title, const std::wstring& right = {});
    // Media entry + queue item (title per MediaTitle, artist/album labels for songs).
    int AddMedia(const library::MediaItem& m);
    Entry FolderEntry(const library::FolderNode& f, library::MediaKind kind) const;
    HistoryInfo Progress(const std::wstring& path) const;
    bool Favorite(const std::wstring& path) const;
    bool Matches(const std::wstring& text) const;  // filter test (folded)

    virtual void Build() = 0;
    virtual GridStyle CardStyle() const { return GridStyle::VideoCards; }
    virtual unsigned RowColumns() const { return kColDuration | kColSize | kColDate; }
    virtual void FillEmpty(EmptyState& e);
    virtual void Activate(int index);
    virtual void ShowMenu(int index, ui::Point at);
    virtual void ExtraMenu(int, std::vector<ui::MenuItem>&) {}
    virtual void Delete(int) {}
    virtual void Move(int, int) {}
    virtual bool Reorderable() const { return false; }

    void Refresh();
    GridStyle CurrentStyle() const;
    const Entry* EntryAt(int index) const;
    const Route* RouteAt(int index) const;
    std::vector<QueueItem> FolderQueue(const std::wstring& folder, library::MediaKind kind) const;
    std::vector<ui::MenuItem> FolderMenu(const std::wstring& folder, library::MediaKind kind, Section section) const;
    void SortMenuItems(std::vector<ui::MenuItem>& out, const std::vector<std::pair<const wchar_t*, std::string>>& options);
    std::string SortValue(const std::string& fallback) const;
    void SetSort(const std::string& value);
    // Sorts media by the page's sort setting ("name", "name:desc", "date:desc", "size", "duration", "unwatched"...).
    void SortMedia(std::vector<const library::MediaItem*>& items, const std::string& sort) const;

    Route route_;
    bool compact_;
    std::string key_;
    bool modes_ = true;
    bool list_ = false;
    std::wstring filter_, folded_;
    std::shared_ptr<const library::Snapshot> snap_;
    std::vector<QueueItem> queue_;
    std::wstring queueName_;

    ui::ScrollView* scroll_ = nullptr;
    ui::Widget* content_ = nullptr;
    ui::Widget* toolbar_ = nullptr;   // Row above the grid, hidden unless a page fills it
    ScanBadge* scan_ = nullptr;
    ItemGrid* grid_ = nullptr;
    EmptyState* empty_ = nullptr;
    long long count_ = -1;

private:
    std::vector<Entry> entries_;
    std::vector<Route> routes_;
    std::vector<std::function<void()>> actions_;
    std::vector<Route> nextRoutes_;
    std::vector<std::function<void()>> nextActions_;
    std::unordered_map<std::wstring, HistoryInfo> history_;
    std::unordered_set<std::wstring> favorites_;  // PathKey
    ScopedConnection libConn_, playerConn_, langConn_;
    bool focusTaken_ = false;
};

std::unique_ptr<LibraryView> MakeHomePage(bool compact);
std::unique_ptr<LibraryView> MakeFolderPage(const Route& route, bool compact);
std::unique_ptr<LibraryView> MakeMusicPage(const Route& route, bool compact);
std::unique_ptr<LibraryView> MakeNetworkPage(const Route& route, bool compact);
std::unique_ptr<LibraryView> MakePlaylistsPage(const Route& route, bool compact);
std::unique_ptr<LibraryView> MakeRecentPage(const Route& route, bool compact);

// "C:\Video\Shows\S01" inside library root "C:\Video" -> crumbs Videos › Video › Shows › S01.
// With `sectionRoot` = "C:\Video" (the only library folder, shown as the section page itself)
// -> Videos › Shows › S01.
std::vector<Crumb> FolderCrumbs(const library::Snapshot& snap, Section section, const std::wstring& folder,
                                const std::wstring& sectionRoot = {});

}  // namespace app::lib
