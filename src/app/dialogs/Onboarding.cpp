// First-run window (design/ref/Onboarding_*.png, 1090x760 DIP):
//   0 welcome + media folder   1 theme + accent (live preview)   2 language
//   3 privacy, updates, metadata   4 basic keyboard shortcuts   5 summary
// Every choice is written to Config right away (the app re-themes / re-translates
// live), the media folder is added to the library when leaving the first step.
#include "app/Dialogs.h"

#include "app/Actions.h"
#include "app/App.h"
#include "app/I18n.h"
#include "app/MainWindow.h"
#include "app/dialogs/DialogKit.h"
#include "gfx/Text.h"

#include <shlobj.h>

#include <algorithm>
#include <cmath>

namespace app {

using namespace ui;
using namespace dlg;

namespace {

constexpr float kW = 1090, kH = 760;
constexpr int kSteps = 6;
constexpr const char* kAccentKeys[kAccentCount] = {"white", "pearl", "silver", "steel", "graphite"};

Config& Cfg() {
    return TheApp().config();
}

std::wstring DefaultVideosFolder() {
    PWSTR p = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Videos, 0, nullptr, &p)) && p) out = p;
    CoTaskMemFree(p);
    return out;
}

bool IsDir(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return !p.empty() && a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

int AccentIndex() {
    const std::string a = Cfg().GetString("general.accent");
    for (int i = 0; i < kAccentCount; ++i)
        if (a == kAccentKeys[i]) return i;
    return 0;
}

// Page whose children sit at fixed design coordinates (relative to the page).
class AbsPage : public Widget {
public:
    AbsPage() {
        width = Length::Grow();
        height = Length::Grow();
    }
    template <class T, class... A>
    T* At(Rect r, A&&... args) {
        T* w = Add<T>(std::forward<A>(args)...);
        spots_.push_back({w, r});
        return w;
    }
    // Centered horizontally in the window.
    template <class T, class... A>
    T* Centered(float y, float w, float h, A&&... args) {
        return At<T>(Rect{std::round((kW - w) / 2), y, w, h}, std::forward<A>(args)...);
    }
    void OnLayout() override {
        for (auto& [w, r] : spots_) w->Arrange(r.Offset(rect.x, rect.y));
    }

private:
    std::vector<std::pair<Widget*, Rect>> spots_;
};

// --- step 1 widgets ---------------------------------------------------------------------------

// Miniature Lumen window: title strip and a control bar, all in live palette colors.
class PreviewWindow : public Widget {
public:
    void Paint(Painter& p) override {
        const Palette& c = P();
        p.Fill(rect, c.bgDeep);
        const Rect title{rect.x, rect.y, rect.w, 22};
        p.Fill(title, c.bgPanel);
        p.Icon("lumen", {title.x + 6, title.y + 4, 14, 14}, c.accent);
        const char* buttons[] = {"minimize", "maximize", "fullscreen", "close"};
        for (int i = 0; i < 4; ++i)
            p.Icon(buttons[i], {title.Right() - 88 + i * 20.0f, title.y + 4, 14, 14}, c.textSecondary);

        const Rect bar{rect.x, rect.Bottom() - 30, rect.w, 30};
        p.Fill(bar, c.bgPanel);
        p.Fill({bar.x, bar.y, bar.w, 1}, c.line);
        float x = bar.x + 12;
        const float cy = bar.Cy();
        p.Icon("previous", {x, cy - 7, 14, 14}, c.textSecondary);
        x += 20;
        const Rect play{x, cy - 8, 30, 16};
        p.Fill(play, c.accent, 3);
        p.Icon("play", play.Center(11, 11), c.onAccent);
        x += 36;
        p.Icon("next", {x, cy - 7, 14, 14}, c.textSecondary);
        x += 28;
        p.Text(L"00:02:10", {x, bar.y, 60, bar.h}, TextStyle::MonoSmall, c.textSecondary, HAlign::Leading,
               VAlign::Center, 0);
        x += 62;
        const float right = bar.Right() - 236;
        const Rect track{x, cy - 7, right - x, 14};
        p.Fill(track, c.bgDeep, 3);
        p.Fill({track.x, track.y, track.w * 0.08f, track.h}, c.accentDim, 3);
        p.Fill({track.x + track.w * 0.08f - 1.5f, track.y - 1, 3, track.h + 2}, c.accent, 1.5f);
        p.Text(L"00:25:40", {right + 8, bar.y, 60, bar.h}, TextStyle::MonoSmall, c.textSecondary, HAlign::Leading,
               VAlign::Center, 0);
        float rx = right + 64;
        p.Icon("volume_value-high", {rx, cy - 7, 14, 14}, c.textSecondary);
        const Rect vol{rx + 18, cy - 4, 46, 8};
        p.Fill(vol, c.bgDeep, 2);
        p.Fill({vol.x, vol.y, vol.w * 0.4f, vol.h}, c.accentMid, 2);
        rx += 82;
        for (const char* icon : {"audio", "subtitles", "playback-settings", "list"}) {
            p.Icon(icon, {rx, cy - 7, 14, 14}, c.textSecondary);
            rx += 23;
        }
    }
};

// Option card with an accent ring when selected (theme and language steps).
class ChoiceCard : public Widget {
public:
    enum class Kind { Light, Dark, System, Text };
    ChoiceCard(Kind k, std::wstring big = {}, std::wstring sub = {})
        : kind_(k), big_(std::move(big)), small_(std::move(sub)) {
        interactive = true;
        focusable = true;
    }
    bool selected = false;
    std::wstring caption;
    void Paint(Painter& p) override {
        const Palette& c = P();
        const Rect frame{rect.x, rect.y, rect.w, rect.h - 41};
        if (selected) p.Stroke(frame, c.accent, 19, 4);
        else if (IsHot()) p.Stroke(frame, c.line, 19, 2);
        PaintFocusRing(p, frame, 19);
        const Rect inner = frame.Inset(8);
        // Theme samples show the theme itself, independent of the current palette.
        const Color light = Color::Hex(0xD6D6D6), dark = Color::Hex(0x232323);
        switch (kind_) {
            case Kind::Light:
                p.Fill(inner, light, 12);
                p.Stroke(inner, c.line, 12);
                break;
            case Kind::Dark: p.Fill(inner, dark, 12); break;
            case Kind::System: {
                p.Fill(inner, dark, 12);
                p.PushRoundClip(inner, 12);
                D2D1_MATRIX_3X2_F old;
                p.Ctx()->GetTransform(&old);
                p.Ctx()->SetTransform(D2D1::Matrix3x2F::Rotation(-24.0f, {inner.Cx(), inner.Cy()}) * old);
                p.Fill({inner.Cx() - 2, inner.y - inner.h, inner.w * 2, inner.h * 3}, light);
                p.Ctx()->SetTransform(old);
                p.PopClip();
                break;
            }
            case Kind::Text:
                p.Fill(inner, c.bgCard, 12);
                p.Text(big_, {inner.x, inner.y + inner.h / 2 - 26, inner.w, 30}, TextStyle::Section, SoftText(),
                       HAlign::Center);
                p.Text(small_, {inner.x, inner.y + inner.h / 2 + 6, inner.w, 20}, TextStyle::Body, c.textTertiary,
                       HAlign::Center);
                break;
        }
        p.Text(caption, {rect.x - 20, rect.Bottom() - 19, rect.w + 40, 19}, TextStyle::Large,
               selected || IsHot() ? c.textPrimary : c.textSecondary, HAlign::Center);
    }

private:
    Kind kind_;
    std::wstring big_, small_;
};

class AccentSwatch : public Widget {
public:
    explicit AccentSwatch(int index) : index_(index) {
        interactive = true;
        focusable = true;
        tooltip = Tr(Theme::AccentName(index));
    }
    bool selected = false;
    void Paint(Painter& p) override {
        const Color sw = Theme::Get().AccentSwatch(index_);
        const float k = selected ? 1.0f : IsHot() ? 0.8f : 0.5f;
        p.PushOpacity(k);
        if (selected) p.Stroke(rect, Color::Mix(sw, P().bgBase, 0.55f), rect.w / 2, 4);
        p.FillCircle({rect.Cx(), rect.Cy()}, 20, sw);
        p.PopOpacity();
        PaintFocusRing(p, rect, rect.w / 2);
    }

private:
    int index_;
};

// --- step 4 / 5 widgets ---------------------------------------------------------------------------

// "Space" or "← / →" as key caps.
class KeyCombo : public Widget {
public:
    explicit KeyCombo(std::vector<Action> actions) {
        dir = Dir::Row;
        align = Align::Start;
        gap = 4;
        bool first = true;
        for (Action a : actions) {
            const auto& keys = Bindings(a);
            if (keys.empty()) continue;
            if (!first) {
                auto* slash = Add<Label>(L"/", TextStyle::Large, Tone::Secondary);
                slash->height = Length::Px(42);
                slash->pad = {8, 0, 8, 0};
            }
            first = false;
            for (const auto& part : SplitKeys(KeyText(keys[0].first, keys[0].second))) Add<KeyCap>(part);
        }
    }
};

// Root of the window; the chrome is positioned by the window itself.
class OnbRoot : public Panel {
public:
    std::function<void()> onLayout;
    void OnLayout() override {
        if (onLayout) onLayout();
    }
};

}  // namespace

// --- the window ----------------------------------------------------------------------------------

class OnboardingWindow {
public:
    explicit OnboardingWindow(std::function<void()> done) : done_(std::move(done)) {}

    bool Open() {
        MainWindow* main = TheApp().main();
        HostOptions o;
        o.title = Tr(L"Lumen Setup");
        o.width = kW;
        o.height = kH;
        o.minWidth = kW;
        o.minHeight = kH;
        o.resizable = false;
        o.owner = main ? main->Hwnd() : nullptr;
        o.showInTaskbar = false;
        if (!host_.Create(o)) return false;
        folder_ = DefaultVideosFolder();

        auto root = std::make_unique<OnbRoot>();
        root->onLayout = [this] { LayoutChrome(); };
        root->fill = [] { return P().bgBase; };
        root->dir = Dir::None;
        root->ncRole = NcRole::Caption;  // empty space drags the window
        root_ = root.get();
        host_.SetRoot(std::move(root));
        BuildChrome();
        Go(0, false);

        host_.onKey = [this](const KeyEvent& e) { return OnKey(e); };
        host_.onCloseRequest = [this] {
            Finish();
            return false;
        };
        langConn_ = Connect(TheApp().languageChanged, [this] { Relabel(); });
        if (main) EnableWindow(main->Hwnd(), FALSE);
        host_.Show();
        return true;
    }

    void Finish() {
        if (finished_) return;
        finished_ = true;
        // Skip, Escape or closing on the first page add nothing: the default folder (the user's
        // real Videos) is scanned only after Continue confirmed it.
        if (folderConfirmed_) CommitFolder();
        MainWindow* main = TheApp().main();
        // Re-enable the owner before this window goes away so activation returns to it.
        if (main) EnableWindow(main->Hwnd(), TRUE);
        ShowWindow(host_.Hwnd(), SW_HIDE);
        auto done = std::move(done_);
        if (done) done();
        if (main) main->Post([] { Instance().reset(); });
    }

    static std::unique_ptr<OnboardingWindow>& Instance() {
        static std::unique_ptr<OnboardingWindow> instance;
        return instance;
    }

private:
    // --- chrome: footer buttons and step dots ----------------------------------------------------

    class Dots : public Widget {
    public:
        explicit Dots(OnboardingWindow* w) : w_(w) {}
        void Paint(Painter& p) override {
            const float total = (kSteps - 1) * 14.0f + 22;
            float x = std::round(rect.Cx() - total / 2);
            for (int i = 0; i < kSteps; ++i) {
                const bool cur = i == w_->step_;
                const float wdt = cur ? 22.0f : 6.0f;
                p.Fill({x, rect.Cy() - 3, wdt, 6}, cur ? P().accent : i < w_->step_ ? P().textTertiary : P().lineStrong,
                       3);
                x += wdt + 8;
            }
        }

    private:
        OnboardingWindow* w_;
    };

    void BuildChrome() {
        page_ = nullptr;
        skip_ = root_->Add<LinkButton>(Tr(L"Skip"), TextStyle::Large);
        back_ = root_->Add<DButton>(Tr(L"Back"), BtnStyle::Neutral, TextStyle::Heading);
        next_ = root_->Add<DButton>(Tr(L"Continue"), BtnStyle::Solid, TextStyle::Heading);
        dots_ = root_->Add<Dots>(this);
        skip_->tooltip = Tr(L"Keep the defaults and start using Lumen");
        skip_->onClick = [this] { Finish(); };
        back_->onClick = [this] { Go(step_ - 1, true); };
        next_->onClick = [this] { Next(); };
    }

    void LayoutChrome() {
        const Rect r = root_->rect;
        if (page_) page_->Arrange(r);
        next_->Arrange({r.x + 928, r.y + 671, 100, 32});
        back_->Arrange({r.x + 820, r.y + 671, 100, 32});
        const Size s = skip_->Measure(200, 32);
        skip_->Arrange({(step_ == 0 ? r.x + 928 : r.x + 820) - 24 - s.w, r.y + 671 + (32 - s.h) / 2, s.w, s.h});
        dots_->Arrange({r.x, r.y + 727, kW, 16});
    }

    void Relabel() {
        SetWindowTextW(host_.Hwnd(), Tr(L"Lumen Setup"));
        skip_->text = Tr(L"Skip");
        skip_->tooltip = Tr(L"Keep the defaults and start using Lumen");
        back_->SetText(Tr(L"Back"));
        next_->SetText(step_ == kSteps - 1 ? Tr(L"Finish") : Tr(L"Continue"));
        GoLater(step_, false);
    }

    // Rebuilding the page destroys the widget whose click is being handled: defer it.
    void GoLater(int step, bool animate) {
        host_.Post([this, step, animate] {
            if (!finished_) Go(step, animate);
        });
    }

    bool OnKey(const KeyEvent& e) {
        if (e.key == VK_ESCAPE) {
            Finish();
            return true;
        }
        if (e.key == VK_RETURN) {
            Next();
            return true;
        }
        if ((e.key == VK_LEFT && e.mods.alt) || (e.key == VK_BACK && step_ > 0)) {
            if (step_ > 0) Go(step_ - 1, true);
            return true;
        }
        return false;
    }

    void Next() {
        if (step_ == 0) {
            CommitFolder();
            folderConfirmed_ = true;
        }
        if (step_ >= kSteps - 1) {
            Finish();
            return;
        }
        Go(step_ + 1, true);
    }

    // Adds the chosen media folder to the library (and drops one added earlier).
    void CommitFolder() {
        std::wstring f = folderBox_ && step_ == 0 ? folderBox_->Text() : folder_;
        while (!f.empty() && (f.back() == L'\\' || f.back() == L'/') && f.size() > 3) f.pop_back();
        folder_ = f;
        auto& lib = TheApp().library();
        bool changed = false;
        if (!added_.empty() && library::PathKey(added_) != library::PathKey(f)) {
            changed = lib.RemoveRoot(added_);
            added_.clear();
        }
        if (IsDir(f) && added_.empty()) {
            if (lib.AddRoot(f)) added_ = f;
            changed = true;
        }
        // Also when the new text is no folder: the old root is gone from disk and the snapshot too.
        if (changed) {
            lib.Save();
            lib.Rescan();
        }
    }

    void Go(int step, bool animate) {
        step = std::clamp(step, 0, kSteps - 1);
        const int from = step_;
        if (page_) {
            if (step_ == 0 && folderBox_) folder_ = folderBox_->Text();
            root_->RemoveChild(page_);
            page_ = nullptr;
            folderBox_ = nullptr;
        }
        step_ = step;
        auto page = std::make_unique<SlidingPage>();
        page->dir = Dir::None;
        page_ = page.get();
        root_->AddChild(std::move(page), 0);  // below the footer buttons
        switch (step_) {
            case 0: BuildWelcome(page_); break;
            case 1: BuildAppearance(page_); break;
            case 2: BuildLanguage(page_); break;
            case 3: BuildPreferences(page_); break;
            case 4: BuildShortcuts(page_); break;
            default: BuildSummary(page_); break;
        }
        back_->visible = step_ > 0;
        skip_->visible = step_ < kSteps - 1;
        next_->SetText(step_ == kSteps - 1 ? Tr(L"Finish") : Tr(L"Continue"));
        page_->appear.Set(animate ? 0.0f : 1.0f);
        if (animate) page_->appear.To(1.0f, 180, Ease::Out);
        page_->direction = step_ >= from ? 1.0f : -1.0f;
        root_->InvalidateLayout();
        host_.RequestLayout();
        host_.SetFocus(next_, false);
    }

    // The current page; fades and slides in when the step changes.
    class SlidingPage : public AbsPage {
    public:
        AnimFloat appear{1.0f};
        float direction = 1;
        void PaintChildren(Painter& p) override {
            const float k = appear.Get();
            if (k >= 0.999f) {
                Widget::PaintChildren(p);
                return;
            }
            D2D1_MATRIX_3X2_F old;
            p.Ctx()->GetTransform(&old);
            p.Ctx()->SetTransform(D2D1::Matrix3x2F::Translation((1 - k) * 16 * direction, 0) * old);
            p.PushOpacity(k);
            Widget::PaintChildren(p);
            p.PopOpacity();
            p.Ctx()->SetTransform(old);
        }
    };

    // --- steps -----------------------------------------------------------------------------------

    void BuildWelcome(AbsPage* pg) {
        pg->Centered<IconView>(116, 141, 141, "lumen", 141, Tone::Accent);
        pg->Centered<Label>(286, 700, 39, Tr(L"Welcome to Lumen!"), TextStyle::Display, Tone::Primary)->halign =
            HAlign::Center;
        auto* d = pg->At<Label>(Rect{248, 362, 595, 90},
                                Tr(L"Lumen is a fast, quiet player for your videos and music. It opens almost any file, "
                                   L"folder or network stream, remembers where you stopped and keeps your library in "
                                   L"order."),
                                TextStyle::Large, Tone::Secondary);
        d->wrap = true;
        d->valign = VAlign::Top;
        pg->At<Label>(Rect{248, 500, 595, 17}, Tr(L"Choose a Media Folder to get started with"), TextStyle::Body,
                      Tone::Secondary);
        folderBox_ = pg->At<TextBox>(Rect{248, 533, 471, 36}, Tr(L"Folder with your videos or music"), "folder");
        folderBox_->SetText(folder_);
        folderBox_->onSubmit = [this](const std::wstring&) { host_.Post([this] { Next(); }); };
        auto* status = pg->At<Label>(Rect{248, 577, 595, 18}, L"", TextStyle::Small, Tone::Tertiary);
        auto check = [status](const std::wstring& t) {
            status->SetText(t.empty() ? Tr(L"You can add folders later from the sidebar.")
                                      : IsDir(t) ? L""
                                                 : Tr(L"This folder doesn't exist. It won't be added."));
        };
        check(folder_);
        folderBox_->onChange = check;
        auto* browse = pg->At<DButton>(Rect{729, 533, 114, 36}, Tr(L"Browse"), BtnStyle::Neutral, TextStyle::Large);
        browse->minWidth = 114;
        browse->onClick = [this, check] {
            const std::wstring f = PickFolder(host_.Hwnd(), folderBox_->Text());
            if (f.empty()) return;
            folderBox_->SetText(f);
            check(f);
        };
    }

    void BuildAppearance(AbsPage* pg) {
        pg->At<PreviewWindow>(Rect{17, 19, 1056, 594});
        pg->Centered<Label>(173, 800, 42, Tr(L"Select your preferred theme and color."), TextStyle::Title,
                            Tone::Secondary)
            ->halign = HAlign::Center;
        const std::string theme = Cfg().GetString("general.theme");
        struct Opt {
            ChoiceCard::Kind kind;
            const wchar_t* label;
            const char* value;
        };
        const Opt opts[] = {{ChoiceCard::Kind::Light, L"Light", "light"},
                            {ChoiceCard::Kind::Dark, L"Dark", "dark"},
                            {ChoiceCard::Kind::System, L"System", "system"}};
        for (int i = 0; i < 3; ++i) {
            auto* card = pg->At<ChoiceCard>(Rect{331 + i * 156.0f, 286, 116, 157}, opts[i].kind);
            card->caption = Tr(opts[i].label);
            card->selected = theme == opts[i].value;
            const char* v = opts[i].value;
            card->onClick = [this, v] {
                Cfg().SetString("general.theme", v);
                GoLater(step_, false);
            };
        }
        const int accent = AccentIndex();
        for (int i = 0; i < kAccentCount; ++i) {
            auto* sw = pg->At<AccentSwatch>(Rect{41 + i * 80.0f, 655, 64, 64}, i);
            sw->selected = i == accent;
            sw->onClick = [this, i] {
                Cfg().SetString("general.accent", kAccentKeys[i]);
                GoLater(step_, false);
            };
        }
        // Up to the Skip link (x 703), not under it: the names are one short word.
        pg->At<Label>(Rect{441, 678, 240, 20}, Tr(Theme::AccentName(accent)), TextStyle::Body, Tone::Tertiary);
    }

    void BuildLanguage(AbsPage* pg) {
        pg->Centered<Label>(173, 800, 42, Tr(L"Choose the interface language."), TextStyle::Title, Tone::Secondary)
            ->halign = HAlign::Center;
        const bool en = Cfg().GetString("general.language") == "en";
        struct Opt {
            const wchar_t *big, *sub, *caption;
            const char* value;
        };
        const Opt opts[] = {{L"Русский", L"Привет!", L"Русский", "ru"},
                            {L"English", L"Hello!", L"English", "en"}};
        for (int i = 0; i < 2; ++i) {
            auto* card = pg->At<ChoiceCard>(Rect{301 + i * 260.0f, 286, 228, 157}, ChoiceCard::Kind::Text,
                                            opts[i].big, opts[i].sub);
            card->caption = opts[i].caption;
            card->selected = (i == 1) == en;
            const char* v = opts[i].value;
            card->onClick = [v] { Cfg().SetString("general.language", v); };  // relabels via languageChanged
        }
        pg->Centered<Label>(480, 700, 20, Tr(L"You can change it later in Settings > General."), TextStyle::Body,
                            Tone::Tertiary)
            ->halign = HAlign::Center;
    }

    void BuildPreferences(AbsPage* pg) {
        pg->Centered<Label>(60, 800, 42, Tr(L"A few preferences."), TextStyle::Title, Tone::Secondary)->halign =
            HAlign::Center;
        auto* list = pg->Centered<Widget>(140, 760, 480);
        list->dir = Dir::Column;
        struct Opt {
            const char* key;
            const wchar_t *title, *desc;
        };
        const Opt opts[] = {
            {"privacy.saveHistory", L"Remember what you play",
             L"Keep a list of played files for History, Continue watching and Listen again."},
            {"privacy.rememberPosition", L"Continue where you left off",
             L"Save where you stopped and continue from there next time."},
            {"library.thumbnails", L"Show thumbnails", L"Use Windows thumbnails for videos and embedded covers for music."},
        };
        for (size_t i = 0; i < std::size(opts); ++i) {
            if (i > 0) AddSeparator(list);
            auto* row = list->Add<Widget>();
            row->dir = Dir::Row;
            row->align = Align::Center;
            row->width = Length::Grow();
            row->pad = {0, 16, 0, 16};
            row->gap = 24;
            auto* text = row->Add<Widget>();
            text->dir = Dir::Column;
            // A known width: measured as Grow, the description was one line and its second line
            // ran into the separator below.
            text->width = Length::Px(760 - 24 - 66);  // list - gap - toggle
            text->gap = 4;
            text->Add<Label>(Tr(opts[i].title), TextStyle::Large)->color = SoftText;
            auto* d = text->Add<Label>(Tr(opts[i].desc), TextStyle::Body, Tone::Tertiary);
            d->wrap = true;
            d->width = Length::Grow();
            const std::string key = opts[i].key;
            auto* t = row->Add<Toggle>(Cfg().GetBool(key));
            t->onChange = [key](bool on) { Cfg().SetBool(key, on); };
        }
    }

    void BuildShortcuts(AbsPage* pg) {
        pg->Centered<Label>(60, 800, 42, Tr(L"Learn the basic keyboard shortcuts."), TextStyle::Title, Tone::Secondary)
            ->halign = HAlign::Center;
        struct Item {
            const wchar_t* label;
            std::vector<Action> actions;
        };
        const std::vector<Item> left = {
            {L"Play / Pause", {Action::PlayPause}},
            {L"Seek Backward / Forward", {Action::SeekBackward, Action::SeekForward}},
            {L"Previous / Next Item", {Action::Previous, Action::Next}},
            {L"Previous / Next Frame", {Action::FrameBackStep, Action::FrameStep}},
            {L"Cycle Subtitle Track", {Action::CycleSubtitles}},
            {L"Cycle Audio Track", {Action::CycleAudio}},
        };
        const std::vector<Item> right = {
            {L"Toggle Fullscreen", {Action::Fullscreen}},
            {L"Increase / Decrease Volume", {Action::VolumeUp, Action::VolumeDown}},
            {L"Show / Hide Queue", {Action::TogglePlaylist}},
            {L"Show / Hide Sidebar", {Action::ToggleSidebar}},
            {L"Take a Snapshot", {Action::Snapshot}},
            {L"Record", {Action::Record}},
        };
        auto column = [pg](const std::vector<Item>& items, float x) {
            for (size_t i = 0; i < items.size(); ++i) {
                const float y = 154 + i * 82.0f;
                auto* l = pg->At<Label>(Rect{x, y, 240, 42}, Tr(items[i].label), TextStyle::Large, Tone::Secondary);
                l->valign = VAlign::Center;
                pg->At<KeyCombo>(Rect{x + 248, y, 210, 46}, items[i].actions);  // Ctrl Shift S: 207
            }
        };
        // Both columns keep the 62 DIP page margin (the right one ran 3 DIP past the window).
        column(left, 62);
        column(right, kW - 62 - 458);
        pg->At<Label>(Rect{62, 671, 600, 32},
                      Fmt(Tr(L"Find the full list in the menu or press {0}."), {ShortcutText(Action::KeyboardShortcuts)}),
                      TextStyle::Large, Tone::Tertiary);
    }

    void BuildSummary(AbsPage* pg) {
        pg->Centered<IconView>(64, 56, 56, "lumen", 56, Tone::Accent);
        pg->Centered<Label>(140, 700, 39, Tr(L"You're all set."), TextStyle::Display, Tone::Primary)->halign =
            HAlign::Center;
        pg->Centered<Label>(192, 700, 22, Tr(L"Here is how Lumen is set up. Everything can be changed later in Settings."),
                            TextStyle::Large, Tone::Tertiary)
            ->halign = HAlign::Center;
        auto* card = pg->Centered<Panel>(250, 680, 5 * 48.0f + 16);
        card->fill = [] { return P().bgPanel; };
        card->radius = 12;
        card->dir = Dir::Column;
        card->pad = {20, 8, 12, 8};

        const std::string theme = Cfg().GetString("general.theme");
        const wchar_t* themeName = theme == "light" ? L"Light" : theme == "system" ? L"System" : L"Dark";
        auto onOff = [](const char* key) { return std::wstring(Cfg().GetBool(key) ? Tr(L"On") : Tr(L"Off")); };
        struct Row {
            const wchar_t* key;
            std::wstring value;
            int step;
        };
        const std::vector<Row> rows = {
            {L"Media folder", IsDir(folder_) ? folder_ : std::wstring(Tr(L"Not chosen")), 0},
            {L"Theme", Tr(themeName), 1},
            {L"Accent", Tr(Theme::AccentName(AccentIndex())), 1},
            {L"Language", Cfg().GetString("general.language") == "en" ? L"English" : L"Русский", 2},
            {L"Playback history", onOff("privacy.saveHistory"), 3},
        };
        // The key column fits the longest key ("История воспроизведения"), not a fixed 170.
        float keyW = 0;
        for (const Row& row : rows)
            keyW = std::max(keyW, std::ceil(gfx::Text::Get().Measure(Tr(row.key), TextStyle::Body).width) + 1);
        for (size_t i = 0; i < rows.size(); ++i) {
            if (i > 0) AddSeparator(card);
            auto* r = card->Add<Widget>();
            r->dir = Dir::Row;
            r->align = Align::Center;
            r->width = Length::Grow();
            r->height = Length::Px(47);
            r->gap = 16;
            auto* k = r->Add<Label>(Tr(rows[i].key), TextStyle::Body, Tone::Tertiary);
            k->width = Length::Px(keyW);
            auto* v = r->Add<Label>(rows[i].value, TextStyle::Body);
            v->color = SoftText;
            v->width = Length::Grow();
            auto* change = r->Add<LinkButton>(Tr(L"Change"), TextStyle::Body);
            const int s = rows[i].step;
            change->onClick = [this, s] { GoLater(s, true); };
        }
    }

    Host host_;
    std::function<void()> done_;
    OnbRoot* root_ = nullptr;
    SlidingPage* page_ = nullptr;
    LinkButton* skip_ = nullptr;
    DButton* back_ = nullptr;
    DButton* next_ = nullptr;
    Dots* dots_ = nullptr;
    TextBox* folderBox_ = nullptr;
    std::wstring folder_, added_;
    int step_ = 0;
    bool finished_ = false;
    bool folderConfirmed_ = false;
    ScopedConnection langConn_;
};

void ShowOnboarding(std::function<void()> done) {
    auto& inst = OnboardingWindow::Instance();
    if (inst) return;  // already open
    inst = std::make_unique<OnboardingWindow>(std::move(done));
    if (!inst->Open()) {
        auto failed = std::move(inst);
        failed->Finish();
    }
}

}  // namespace app
