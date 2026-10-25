// Developer gallery of every UI control: `Moonbow.exe --gallery`.
#include "app/Gallery.h"

#include "gfx/Gpu.h"
#include "ui/Controls.h"

namespace app {

using namespace ui;

namespace {

Widget* Row(Widget* parent, float gap = 12) {
    auto* r = parent->Add<Widget>();
    r->dir = Dir::Row;
    r->gap = gap;
    r->align = Align::Center;
    return r;
}

Label* Heading(Widget* parent, const wchar_t* text) {
    auto* l = parent->Add<Label>(text, TextStyle::Heading, Tone::Primary);
    l->pad.t = 8;
    return l;
}

}  // namespace

int RunGallery() {
    auto host = std::make_unique<Host>();
    HostOptions o;
    o.title = L"Moonbow — UI gallery";
    o.width = 980;
    o.height = 760;
    if (!host->Create(o)) return 1;

    auto root = std::make_unique<Panel>();
    root->fill = [] { return P().bgBase; };
    root->dir = Dir::Column;

    auto* title = root->Add<Panel>();
    title->fill = [] { return P().bgChrome; };
    title->height = Length::Px(40);
    title->width = Length::Grow();
    title->dir = Dir::Row;
    title->align = Align::Center;
    title->pad = {16, 0, 0, 0};
    title->gap = 10;
    title->ncRole = NcRole::Caption;
    title->Add<IconView>("moonbow", 20, Tone::Primary);
    title->Add<Label>(L"UI gallery", TextStyle::Body, Tone::Secondary)->width = Length::Grow();
    Host* h = host.get();
    auto* min = title->Add<IconButton>("minimize", L"Свернуть", 40, 20);
    min->height = Length::Px(40);
    min->onClick = [h] { h->Minimize(); };
    auto* max = title->Add<IconButton>("maximize", L"", 40, 20);
    max->height = Length::Px(40);
    max->ncRole = NcRole::Maximize;
    max->onClick = [h] { h->ToggleMaximize(); };
    auto* close = title->Add<IconButton>("close", L"Закрыть", 40, 20);
    close->height = Length::Px(40);
    close->onClick = [h] { h->Close(); };

    auto* scroll = root->Add<ScrollView>();
    auto* body = scroll->SetContent<Widget>();
    body->dir = Dir::Column;
    body->pad = Insets::All(28);
    body->gap = 14;

    Heading(body, L"Buttons");
    auto* buttons = Row(body);
    buttons->Add<Button>(L"Next", ButtonKind::Primary);
    buttons->Add<Button>(L"Browse", ButtonKind::Secondary);
    buttons->Add<Button>(L"Cancel", ButtonKind::Ghost);
    buttons->Add<Button>(L"Outline", ButtonKind::Outline, "add");
    auto* disabled = buttons->Add<Button>(L"Disabled", ButtonKind::Primary);
    disabled->enabled = false;
    for (const char* icon : {"play", "pause", "previous", "next", "shuffle_right", "repeat_right", "subtitles", "settings"}) {
        auto* b = buttons->Add<IconButton>(icon, L"Иконка");
        if (std::string(icon) == "shuffle_right") b->active = true;
    }

    Heading(body, L"Inputs");
    auto* inputs = Row(body, 20);
    inputs->Add<Toggle>(true);
    inputs->Add<Toggle>(false);
    inputs->Add<Checkbox>(L"Remember position", true);
    auto* seg = inputs->Add<Segmented>(std::vector<Choice>{{L"Dark", "dark"}, {L"Light", "light"}, {L"System", "system"}}, 0);
    seg->onChange = [](int i) {
        Theme::Get().Set(i == 1 ? ThemeMode::Light : ThemeMode::Dark, Theme::Get().Accent());
        for (Host* x : Host::All()) x->OnThemeChanged();
    };

    auto* inputs2 = Row(body, 20);
    auto* dd = inputs2->Add<Dropdown>(std::vector<Choice>{{L"English", "en"}, {L"Русский", "ru"}, {L"Deutsch", "de"}}, 1);
    dd->width = Length::Px(260);
    auto* slider = inputs2->Add<Slider>(0.0f, 100.0f, 40.0f, 1.0f);
    slider->width = Length::Px(220);
    auto* accent = inputs2->Add<AccentPicker>(0);
    accent->onChange = [](int i) {
        Theme::Get().Set(Theme::Get().Mode(), i);
        for (Host* x : Host::All()) x->OnThemeChanged();
    };

    auto* inputs3 = Row(body, 20);
    auto* search = inputs3->Add<TextBox>(L"Search", "search");
    search->width = Length::Px(324);
    auto* plain = inputs3->Add<TextBox>(L"~/Videos");
    plain->width = Length::Px(300);

    Heading(body, L"Popups");
    auto* popups = Row(body);
    auto* menuBtn = popups->Add<Button>(L"Menu", ButtonKind::Secondary, "more");
    menuBtn->onClick = [h, menuBtn] {
        std::vector<MenuItem> items;
        items.push_back({L"Play", L"Space", "play"});
        items.push_back({L"Stop", L"S", "stop"});
        items.push_back({L"Record", L"R", "record"});
        items.push_back(MenuItem::Separator());
        MenuItem playback{L"Playback", L"", "playback"};
        playback.submenu = {{L"Speed 0.5×"}, {L"Speed 1×", L"", "", true}, {L"Speed 2×"}};
        items.push_back(playback);
        MenuItem video{L"Video", L"", "video"};
        video.submenu = {{L"Crop"}, {L"Zoom"}, {L"Deinterlace"}};
        items.push_back(video);
        items.push_back({L"Quit", L"Ctrl + Q", "quit"});
        ShowMenu(h, menuBtn->rect, std::move(items));
    };
    auto* dialogBtn = popups->Add<Button>(L"Dialog", ButtonKind::Secondary);
    dialogBtn->onClick = [h] {
        auto d = std::make_unique<Dialog>(L"Open network stream", 560);
        d->Body()->Add<Label>(L"Network URL", TextStyle::Body, Tone::Tertiary);
        auto* url = d->Body()->Add<TextBox>(L"https://");
        url->width = Length::Grow();
        d->Footer()->Add<Spacer>();
        Dialog* raw = d.get();
        d->Footer()->Add<Button>(L"Cancel", ButtonKind::Ghost)->onClick = [raw] { raw->Close(); };
        d->Footer()->Add<Button>(L"Open", ButtonKind::Primary)->onClick = [raw] { raw->Close(); };
        ShowDialog(h, std::move(d));
    };
    auto* toastBtn = popups->Add<Button>(L"Toast", ButtonKind::Secondary);
    toastBtn->onClick = [h] { ShowToast(h, L"Громкость 70%", "volume_value-high"); };
    auto* tipBtn = popups->Add<IconButton>("info", L"Подсказка с длинным текстом, которая переносится на несколько строк");
    (void)tipBtn;

    Heading(body, L"Typography");
    const std::pair<TextStyle, const wchar_t*> styles[] = {
        {TextStyle::Display, L"Display — Добро пожаловать в Moonbow"},
        {TextStyle::Title, L"Title — Настройки"},
        {TextStyle::Section, L"Section — Продолжить просмотр"},
        {TextStyle::Heading, L"Heading — Appearance"},
        {TextStyle::Body, L"Body — The quick brown fox, съешь же ещё этих мягких французских булок"},
        {TextStyle::Mono, L"Mono — 00:02:10 / 01:25:40"},
        {TextStyle::Small, L"Small — 110 items"},
    };
    for (auto& s : styles) body->Add<Label>(s.second, s.first, Tone::Primary);

    Heading(body, L"Icons");
    auto* icons = body->Add<Widget>();
    icons->dir = Dir::Row;
    icons->gap = 6;
    for (const char* n : {"home", "videos", "music", "streaming", "playlist", "recent", "add", "folder", "search", "sort",
                          "grid", "list", "more", "fullscreen", "pip", "snapshot", "record", "hdr", "zoom", "chapters",
                          "bookmark", "favorite", "eq", "audio", "subtitles", "settings", "volume_value-high"})
        icons->Add<IconView>(n, 24, Tone::Secondary)->tooltip = std::wstring(n, n + strlen(n));

    for (int i = 0; i < 30; ++i) body->Add<Label>(L"Строка прокрутки " + std::to_wstring(i + 1), TextStyle::Body, Tone::Tertiary);

    host->SetRoot(std::move(root));
    host->onDestroyed = [] { PostQuitMessage(0); };
    host->Show();
    gfx::Gpu::Get().CreateDeviceAsync(host->Hwnd(), Host::kDeviceReadyMessage);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

}  // namespace app
