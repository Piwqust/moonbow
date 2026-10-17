// Library pages. Placeholder until the library module (home, folders, music,
// network, playlists, recent) lands.
#include "app/I18n.h"
#include "app/LibraryView.h"

namespace app {

using namespace ui;

namespace {

class PlaceholderView : public LibraryView {
public:
    explicit PlaceholderView(Route route) : route_(std::move(route)) {
        dir = Dir::Column;
        justify = Justify::Center;
        align = Align::Center;
        gap = 12;
        Add<IconView>(SectionIcon(route_.section), 40, Tone::Tertiary);
        Add<Label>(Tr(L"Drop media files here or open a file"), TextStyle::Body, Tone::Tertiary);
        auto* open = Add<Button>(Tr(L"Open file"), ButtonKind::Secondary, "file");
        open->onClick = [] { TheApp().OpenFileDialog(); };
    }
    std::vector<Crumb> Breadcrumbs() const override { return {{Tr(SectionName(route_.section)), Route{route_.section, {}}}}; }
    bool HasSearch() const override { return false; }

private:
    Route route_;
};

}  // namespace

std::unique_ptr<LibraryView> CreateLibraryView(const Route& route, bool) {
    return std::make_unique<PlaceholderView>(route);
}

}  // namespace app
