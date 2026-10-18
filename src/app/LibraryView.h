#pragma once
#include "app/App.h"
#include "app/Signal.h"
#include "ui/Controls.h"

#include <memory>
#include <string>
#include <vector>

namespace app {

struct Crumb {
    std::wstring label;  // already translated
    Route route;
};

enum class ViewMode : uint8_t { Grid, List };

// Content of one library page (Home, a folder, Music > Albums, a playlist...).
// LibraryPane draws the header (crumbs, count, sort, view mode, search) and
// asks the view for what to show there. The same views render in the player's
// side panel with `compact` = true (single-column list, 360 DIP wide).
class LibraryView : public ui::Widget {
public:
    virtual std::vector<Crumb> Breadcrumbs() const = 0;
    virtual long long ItemCount() const { return -1; }  // < 0 hides the counter
    // Nothing to sort or lay out: the page shows its empty state (the counter may be hidden too).
    virtual bool IsEmpty() const { return false; }
    virtual bool HasViewModes() const { return false; }
    virtual ViewMode GetViewMode() const { return ViewMode::Grid; }
    virtual void SetViewMode(ViewMode) {}
    virtual std::vector<ui::MenuItem> SortMenu() { return {}; }
    virtual bool HasSearch() const { return true; }
    virtual void SetFilter(const std::wstring&) {}
    virtual std::vector<ui::MenuItem> MoreMenu() { return {}; }
    // Keyboard focus target when the page opens (list/grid).
    virtual ui::Widget* InitialFocus() { return nullptr; }
    // Scroll offset and selection, saved when the page is left and restored by Back/Forward.
    virtual ViewState SaveState() const { return {}; }
    virtual void RestoreState(const ViewState&) {}

    Signal<> headerChanged;  // count / crumbs / modes changed
};

// Implemented by the library module (src/app/library).
std::unique_ptr<LibraryView> CreateLibraryView(const Route& route, bool compact);

}  // namespace app
