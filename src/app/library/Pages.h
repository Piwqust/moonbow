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

// "Indexing your library… 412 of 1,284" with a progress bar and a "Hide" button, a slim strip
// above a page's content while a scan runs (MARKET P2-6). It appears only after the scan has run
// for a second, so quick rescans never flash it, and goes when the scan ends. "Hide" keeps it
// away for the rest of that scan on every page. While a scan runs it polls the scan's counters
// on a 250 ms timer; otherwise it has no timer and no work.
class ScanBanner : public ui::Widget {
public:
    explicit ScanBanner(bool compact = false);
    ~ScanBanner() override;
    // After the library changed. `suppressed`: the page's empty state explains the scan instead.
    void Update(bool suppressed = false);
    // From the page's OnLayout: starts following a scan that began before the page had a window.
    void Attach();
    void Paint(ui::Painter& p) override;
    void OnLayout() override;
    ui::Widget* HitTest(float x, float y) override;  // the strip only, not the space above it
    void DebugInfo(std::string& out) const override;  // " following" / " timer" / " value=0.42"

private:
    void Poll();
    void StopTimer();
    void UpdateTip();
    ui::Rect Box() const;

    bool compact_;
    ui::Insets box_;  // the strip inside rect
    bool suppressed_ = false;
    bool following_ = false;  // a scan runs: the timer should run
    std::wstring text_;
    float fraction_ = -1;  // < 0: still counting files, no fill
    ui::Widget* hide_ = nullptr;
    UINT_PTR timer_ = 0;
    ui::Host* timerHost_ = nullptr;
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
// What a page shows as "New" is not new next session once the user has seen it: on Home or a
// Videos/Music page in the main window, not in the side panel or behind the player screen.
void MarkSeenIfShown(Section section, bool compact);
// The selection of a grid as a ViewState (by path and title: the content may change meanwhile),
// and the entry such a state names (-1 = none).
void SaveSelection(const ItemGrid& grid, ViewState& st);
int FindStateEntry(const ItemGrid& grid, const ViewState& st);
// Selects the entry `st` names and scrolls to the saved offset (or brings the entry into view),
// without animation. True when the entry was found.
bool ApplyViewState(ItemGrid& grid, ui::ScrollView& scroll, const ViewState& st);

struct HistoryInfo {
    double progress = -1;  // shown progress, < 0 none
    bool watched = false;
    bool played = false;  // in the history at all
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
    bool IsEmpty() const override { return empty_ && empty_->visible; }
    ui::Widget* InitialFocus() override { return grid_; }
    ViewState SaveState() const override;
    void RestoreState(const ViewState& st) override;
    ui::Widget* HitTest(float x, float y) override;
    void OnLayout() override;

protected:
    // --- building (called from Build) ---
    int Push(Entry e);
    int AddRoute(Entry e, Route target);
    int AddAction(Entry e, std::function<void()> action);
    void SetSubtitle(int index, std::wstring text);  // of an entry added in this Build
    void AddHeader(const std::wstring& title, const std::wstring& right = {});
    // Column titles above list rows (list mode only). `sort` is the page's current sort value.
    struct Column {
        ColumnHead head;
        const wchar_t* label;  // untranslated
        std::string sort;      // empty = not sortable
    };
    void AddColumns(const std::string& sort, std::initializer_list<Column> columns);
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
    // Radio items for the sort menu; the current value defaults to `fallback`, else the first option.
    void SortMenuItems(std::vector<ui::MenuItem>& out, const std::vector<std::pair<const wchar_t*, std::string>>& options,
                       const std::string& fallback = {});
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
    ScanBanner* scan_ = nullptr;
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
    ColumnTitle heads_[kHeadCount];
    std::string headSort_;
    ScopedConnection libConn_, playerConn_, screenConn_, langConn_;
    bool focusTaken_ = false;
    bool stale_ = false;  // a file started or stopped while the player screen hid the page
    SearchDebounce searchDebounce_;
    ViewState pendingState_;  // RestoreState before the first layout: applied in OnLayout
    bool restorePending_ = false;
};

std::unique_ptr<LibraryView> MakeHomePage(bool compact);
std::unique_ptr<LibraryView> MakeFolderPage(const Route& route, bool compact);
// Videos: "movies", "shows", "show:<key>", "folders" or a folder; "" opens the last of the tabs.
std::unique_ptr<LibraryView> MakeVideosPage(const Route& route, bool compact);
// The "Movies / Shows / Folders" tabs above a Videos page (`current` = 0..2); remembers the tab.
void AddVideoTabs(ui::Widget* toolbar, int current);
// The snapshot's shows and the videos that aren't episodes, grouped once per snapshot.
struct ShowIndex {
    std::shared_ptr<const library::Snapshot> snap;
    std::vector<library::Show> shows;
    std::vector<const library::MediaItem*> movies;
};
const ShowIndex& ShowsOf(const std::shared_ptr<const library::Snapshot>& snap);
// Card of a show (Videos › Shows, Home): "Next: S01E03" and the episode count; route to its page.
Entry ShowEntry(const library::Show& show, const std::function<bool(const std::wstring&)>& watched);
Route ShowRoute(const library::Show& show);
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
