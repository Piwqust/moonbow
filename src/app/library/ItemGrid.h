#pragma once
// Virtualized collection of library entries: card grids (video 16:9 or square covers),
// list rows with columns, or compact side-panel rows. Items are painted directly (no
// child widgets), only those intersecting the painter's clip, so folders with thousands
// of files stay smooth. Single selection, keyboard navigation, drag to reorder (rows).
#include "app/Signal.h"
#include "ui/Controls.h"

#include <functional>
#include <string>
#include <vector>

namespace app::lib {

// Header = group title; Columns = the list's column titles (Rows style only, hidden otherwise).
enum class EntryKind : uint8_t { Media, Folder, Playlist, Album, Artist, Genre, Category, Action, Stream, Header, Columns };

struct Entry {
    EntryKind kind = EntryKind::Media;
    std::wstring title, subtitle, extra;  // extra = album column in song lists
    std::wstring path;                     // file / folder / URL (identity, "now playing")
    std::wstring image;                    // thumbnail source, empty = placeholder icon
    std::string icon;                      // placeholder / action icon
    std::string key;                       // playlist id, route key...
    double progress = -1;                  // 0..1 watched part, < 0 none
    double duration = 0;
    uint64_t size = 0;
    int64_t date = 0;   // Unix ms
    int count = -1;     // badge on folder/category cards
    bool watched = false;
    bool audio = false;  // media entry is audio (square cover placeholder)
    bool favorite = false;  // small heart mark
    bool damaged = false;   // the scanner found the file unplayable: dimmed, with a warning mark
    int track = 0;          // track number column (kColTrack), 0 = none
    int ref = -1;        // owner's index (queue position etc.)
};

enum class GridStyle : uint8_t { VideoCards, SquareCards, Rows, CompactRows };

enum Columns : unsigned {
    kColSubtitle = 1u << 0,
    kColExtra = 1u << 1,
    kColDuration = 1u << 2,
    kColSize = 1u << 3,
    kColDate = 1u << 4,
    kColTime = 1u << 5,  // date column shows "today 14:05"-style clock for recent items
    kColTrack = 1u << 6,  // track number before the thumbnail
};

// Titles of the list columns, painted by an EntryKind::Columns entry. A column with a sort
// value is clickable: the first click sorts by it, the next one reverses (":desc").
enum ColumnHead : uint8_t { kHeadTrack, kHeadTitle, kHeadSubtitle, kHeadExtra, kHeadDuration, kHeadSize, kHeadDate, kHeadCount };
struct ColumnTitle {
    std::wstring label;  // translated
    std::string sort;    // empty = not sortable
};

class ItemGrid : public ui::Widget {
public:
    ItemGrid();
    ~ItemGrid() override;

    void SetEntries(std::vector<Entry> entries);
    const std::vector<Entry>& Entries() const { return entries_; }
    void SetStyle(GridStyle s);
    GridStyle Style() const { return style_; }

    int maxRows = 0;          // 0 = all rows; home sections show one row of cards
    bool stretch = true;      // cards grow to fill the width (pages); fixed size on home
    unsigned columns = kColDuration;
    bool reorderable = false;  // Rows only
    bool roundImages = false;  // artist cards
    bool thumbs = true;        // Rows: artwork before the title (album pages show only the track number)
    ColumnTitle heads[kHeadCount];
    std::string sort;  // current sort value, marks the sorted column
    std::function<void(const std::string&)> onSort;  // a column title was clicked: the new sort value

    int Selected() const { return selected_; }
    void Select(int index, bool scrollIntoView = true);
    int SelectedOr(int fallback) const { return selected_ >= 0 ? selected_ : fallback; }
    // Absolute rect of an entry's slot (empty when hidden by maxRows).
    ui::Rect ItemRect(int index) const;
    int IndexAt(float x, float y) const;
    int VisibleCount() const;  // entries that fit (maxRows)
    // True for a double-click time after the grid was created. A page opened by a single click
    // ignores input meanwhile, so the second click of a double-click can't start what lands
    // under the cursor on the new page.
    bool JustOpened() const;

    // Media plays on double click / Enter; categories and actions open on a single click.
    std::function<void(int)> onActivate;
    std::function<void(int, ui::Point)> onMenu;      // right click / menu key / "…" button
    std::function<void(int)> onDelete;               // Delete key
    std::function<void(int from, int to)> onMove;    // drag reorder: item ends up at `to`
    std::function<void(int)> onSelectionChanged;

    ui::Size MeasureContent(float availW, float availH) override;
    void OnLayout() override;
    void Paint(ui::Painter& p) override;
    bool OnMouseDown(const ui::MouseEvent& e) override;
    bool OnMouseMove(const ui::MouseEvent& e) override;
    bool OnMouseUp(const ui::MouseEvent& e) override;
    bool OnDoubleClick(const ui::MouseEvent& e) override;
    bool OnKeyDown(const ui::KeyEvent& e) override;
    void OnMouseLeave() override;
    void OnFocusChanged(bool focused) override;
    void OnClick() override;

private:
    struct RowCells {
        ui::Rect track, thumb, title, sub, extra, duration, size, date, heart;
    };
    RowCells MediaCells(const ui::Rect& r) const;
    int HeadAt(int index, float x) const;
    void PaintColumns(ui::Painter& p, const ui::Rect& r);
    struct Geometry {
        float cardW = 0, thumbH = 0, labelH = 0;
        int cols = 1;
    };
    void Compute(float width);
    bool Selectable(int i) const;
    void PaintCard(ui::Painter& p, int i, const ui::Rect& r, bool hot, bool now);
    void PaintRow(ui::Painter& p, int i, const ui::Rect& r, bool hot, bool now);
    void PaintHeader(ui::Painter& p, int i, const ui::Rect& r);
    void PaintThumb(ui::Painter& p, const Entry& e, const ui::Rect& t, float radius, int iconSize, float opacity);
    ui::Rect MoreButtonRect(const ui::Rect& slot) const;
    ui::ScrollView* Scroller() const;
    int Navigate(int from, int dx, int dy) const;
    int DropIndexAt(float y) const;
    void UpdateHover(float x, float y);

    std::vector<Entry> entries_;
    std::vector<ui::Rect> slots_;  // relative to the widget origin; empty rect = hidden
    GridStyle style_ = GridStyle::VideoCards;
    Geometry geo_;
    float computedW_ = -1;
    float height_ = 0;
    uint64_t version_ = 1, computedVersion_ = 0;

    int selected_ = -1;
    int hover_ = -1;
    bool hoverMore_ = false;
    int hoverHead_ = -1;
    bool anyFavorite_ = false;  // rows keep the heart slot on every row, so the columns line up
    int press_ = -1;
    ui::Point pressAt_;
    bool dragging_ = false;
    int dropAt_ = -1;
    uint64_t created_ = 0;
    bool keyFocusPending_ = false;  // opened from the keyboard: select the first item after layout
    ScopedConnection imagesConn_, playerConn_;
};

}  // namespace app::lib
