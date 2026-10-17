// Settings window. Placeholder until the schema-driven window lands
// (design/ref/Settings-*.png).
#include "app/App.h"
#include "app/Dialogs.h"
#include "app/I18n.h"
#include "app/MainWindow.h"
#include "ui/Controls.h"

namespace app {

void ShowSettingsWindow(const std::string&) {
    ui::ShowToast(TheApp().main(), Tr(L"Settings"), "settings");
}

void CloseSettingsWindow() {}

}  // namespace app
