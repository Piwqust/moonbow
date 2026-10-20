// Settings > Library > Library Folders: every library root with its file count and whether it is
// reachable, "Remove" (with Undo) and "Show in File Explorer", and "Add folder…" below (UX-1).
#include "app/I18n.h"
#include "app/library/LibraryCommon.h"
#include "app/settings/SettingsUi.h"
#include "ui/Host.h"

namespace app::sui {

using namespace ui;
using settings::Item;

namespace {

class LibraryFolders : public Widget {
public:
    LibraryFolders(const Item& item, PageHost& page) : page_(page) {
        dir = Dir::Column;
        gap = 8;
        width = Length::Grow();
        pad = {0, kRowPad, 0, 10};
        id = item.key;
        auto* panel = Add<Panel>();
        panel->fill = [] { return P().bgDeep; };
        panel->radius = 6;
        panel->dir = Dir::Column;
        panel->width = Length::Grow();
        panel->pad = {12, 4, 6, 4};
        list_ = panel;
        auto* add = Add<Button>(Tr(L"Add folder…"), ButtonKind::Secondary, "new-folder");
        add->id = "library-add-folder";
        add->onClick = [] { lib::AddFolderToLibrary(); };
        // A removed or added root and every finished scan change the counts.
        libConn_ = Connect(TheApp().libraryChanged, [this] { Fill(); });
        Fill();
    }

private:
    void Fill() {
        list_->ClearChildren();
        library::MediaLibrary& lib = TheApp().library();
        const auto snap = lib.Current();
        if (lib.Roots().empty()) {
            auto* none = list_->Add<Label>(Tr(L"No folders yet"), TextStyle::Body, Tone::Tertiary);
            none->width = Length::Grow();
            none->height = Length::Px(40);
        }
        for (const std::wstring& root : lib.Roots()) {
            const std::wstring key = library::PathKey(root);
            bool missing = false;
            for (const std::wstring& m : snap->missingRoots) missing = missing || library::PathKey(m) == key;
            const library::FolderNode* node = snap->Folder(root);
            std::wstring status;
            if (missing)
                status = Tr(L"Unavailable: connect the drive or remove the folder");
            else if (!node && lib.Scanning())
                status = Tr(L"Scanning…");
            else
                status = Fmt(Tr(L"Available · {0}"), {lib::CountFiles(node ? node->totalVideos + node->totalAudios : 0)});

            auto* line = list_->Add<Widget>();
            line->dir = Dir::Row;
            line->align = Align::Center;
            line->gap = 12;
            line->width = Length::Grow();
            line->pad = {0, 8, 0, 8};
            auto* icon = line->Add<IconView>(missing ? "warning" : "folder", 20, Tone::Secondary);
            if (missing) icon->color = [] { return P().danger; };
            auto* text = line->Add<Widget>();
            text->dir = Dir::Column;
            text->gap = 2;
            text->width = Length::Grow();
            auto* path = text->Add<Label>(root, TextStyle::Body, Tone::Primary);
            path->width = Length::Grow();
            path->tooltip = root;
            auto* state = text->Add<Label>(status, TextStyle::Small, Tone::Tertiary);
            state->width = Length::Grow();
            if (missing) state->color = [] { return P().danger; };
            auto* show = line->Add<IconButton>("open", Tr(L"Show in File Explorer"), 32, 20);
            show->enabled = !missing;
            show->onClick = [root] { lib::ShowInExplorer(root); };
            auto* remove = line->Add<Button>(Tr(L"Remove"), ButtonKind::Secondary);
            remove->id = "library-remove-folder";
            Host* host = page_.host;
            // Posted: the removal refills this list, and with it the button whose click runs.
            remove->onClick = [root, host] {
                if (host) host->Post([root, host] { lib::RemoveLibraryRoot(root, host); });
            };
        }
        InvalidateLayout();
        if (page_.relayout) page_.relayout();
    }

    PageHost& page_;
    Widget* list_ = nullptr;
    ScopedConnection libConn_;
};

}  // namespace

std::unique_ptr<Widget> MakeLibraryFolders(const Item& item, PageHost& page) {
    return std::make_unique<LibraryFolders>(item, page);
}

}  // namespace app::sui
