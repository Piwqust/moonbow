// Settings > General > Updates: this version, what the updater knows ("Moonbow is up to date",
// "Moonbow 0.2.0 is ready…") and the one button that fits ("Check now", "Restart now", "Download").
#include "app/I18n.h"
#include "app/Updater.h"
#include "app/settings/SettingsUi.h"
#include "net/Http.h"
#include "ui/Controls.h"

namespace app::sui {

using namespace ui;
using settings::Item;

namespace {

class UpdateStatusRow : public Widget {
public:
    explicit UpdateStatusRow(const Item& item) {
        dir = Dir::Row;
        align = Align::Center;
        width = Length::Grow();
        pad = {0, kRowPad, 0, kRowPad};
        gap = 16;
        id = item.key;
        const bool basic = item.basic && BasicMode();  // with an icon, like the other "Basic" rows
        if (basic) {
            pad = {0, 8, 0, 8};
            gap = 12;
            auto* icon = Add<IconView>(item.icon, 24, Tone::Secondary);
            icon->width = Length::Px(24);
            icon->height = Length::Px(24);
        }
        auto* text = Add<Widget>();
        text->dir = Dir::Column;
        text->width = Length::Grow();
        text->gap = 2;
        text->Add<Label>(Fmt(Tr(L"Moonbow {0}"), {net::AppVersion()}), TextStyle::Body,
                         basic ? Tone::Primary : Tone::Secondary);
        status_ = text->Add<Label>(L"", TextStyle::Small, Tone::Tertiary);
        status_->wrap = true;
        status_->width = Length::Grow();
        button_ = Add<Button>(L"", ButtonKind::Secondary);
        button_->minW = kSmallButtonW;
        button_->id = "update-action";
        // Updates the labels in place: the click that changes the status runs inside this button.
        button_->onClick = [] { Updater::Get().RunAction(); };
        conn_ = Connect(Updater::Get().changed, [this] { Refresh(); });
        Refresh();
    }

private:
    void Refresh() {
        Updater& u = Updater::Get();
        status_->SetText(u.StatusText());
        const std::wstring action = u.ActionText();
        button_->visible = !action.empty();
        if (!action.empty()) button_->SetText(action);
        button_->helpText = status_->Text();
        InvalidateLayout();
    }

    Label* status_ = nullptr;
    Button* button_ = nullptr;
    ScopedConnection conn_;
};

}  // namespace

std::unique_ptr<Widget> MakeUpdateStatus(const Item& item) {
    return std::make_unique<UpdateStatusRow>(item);
}

}  // namespace app::sui
