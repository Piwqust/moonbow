// Dialogs module. Minimal versions until the designed dialogs land
// (design/ref: Keyboard_Shortcuts, Open_network_stream, new_playlist,
// Add_Subtitles_*, Player-customize_*, Onboarding_*).
#include "app/Dialogs.h"

#include "app/App.h"
#include "app/I18n.h"
#include "app/MainWindow.h"
#include "ui/Controls.h"

namespace app {

using namespace ui;

void ShowCustomizePlayerDialog() {
    ShowToast(TheApp().main(), Tr(L"Customize player"), "edit");
}

void ShowKeyboardShortcutsDialog() {
    ShowToast(TheApp().main(), Tr(L"Keyboard shortcuts"), "hotkeys");
}

void ShowNetworkStreamDialog() {
    MainWindow* w = TheApp().main();
    auto d = std::make_unique<Dialog>(Tr(L"Open network stream"), 560);
    d->Body()->Add<Label>(Tr(L"Network URL"), TextStyle::Body, Tone::Tertiary);
    auto* box = d->Body()->Add<TextBox>(L"https://");
    box->width = Length::Grow();
    d->Footer()->Add<Spacer>();
    Dialog* raw = d.get();
    d->Footer()->Add<Button>(Tr(L"Cancel"), ButtonKind::Ghost)->onClick = [raw] { raw->Close(); };
    auto open = [raw, box] {
        const std::wstring u = box->Text();
        raw->Close();
        if (!u.empty()) TheApp().Open({u});
    };
    d->Footer()->Add<Button>(Tr(L"Open"), ButtonKind::Primary)->onClick = open;
    box->onSubmit = [open](const std::wstring&) { open(); };
    ShowDialog(w, std::move(d));
    w->SetFocus(box, true);
}

void ShowNewPlaylistDialog(std::function<void(const std::string&)> done) {
    if (done) done({});
}

void ShowAddSubtitlesDialog(const std::wstring&) {
    TheApp().OpenSubtitleDialog();
}

void ShowMediaInfoDialog() {}

void ShowOnboarding(std::function<void()> done) {
    if (done) done();
}

}  // namespace app
