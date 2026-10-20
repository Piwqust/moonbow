// First-run window (design/ref/Onboarding_*.png, 1090x760 DIP), three steps:
//   0 welcome: language + media folder   1 theme (live preview)   2 summary + privacy toggles
// The accent stays in Settings (five greys say little on a first run); the basic keys come as a
// hint at the first playback (PlayerView). Every choice is written to Config right away (the app
// re-themes / re-translates live), the media folder is added to the library when leaving step 0.
#include "app/Dialogs.h"

#include "app/App.h"
#include "app/I18n.h"
#include "app/MainWindow.h"
#include "app/dialogs/DialogKit.h"
#include "core/SettingsSchema.h"
#include "gfx/Text.h"
#include "library/MediaTypes.h"

#include <shlobj.h>

#include <algorithm>
#include <cmath>

namespace app {

using namespace ui;
using namespace dlg;

namespace {

constexpr float kW = 1090, kH = 760;
constexpr int kSteps = 3;

Config& Cfg() {
    return TheApp().config();
}

std::wstring DefaultVideosFolder() {
    // A test instance must never offer the owner's real Videos: one Continue would scan it.
    if (ui::Host::Automation()) return {};
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

// A whole drive, the user profile or Windows: thousands of folders that hold no media, and a
// slow first scan. 1 = a drive, 2 = one of those folders, 0 = neither.
int TooBroad(const std::wstring& p) {
    const std::wstring key = library::PathKey(p);
    if (key.size() <= 3 && key.size() >= 2 && key[1] == L':') return 1;
    for (REFKNOWNFOLDERID id : {FOLDERID_Profile, FOLDERID_Windows}) {
        PWSTR known = nullptr;
        const bool same =
            SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &known)) && known && library::PathKey(known) == key;
        CoTaskMemFree(known);
        if (same) return 2;
    }
    return 0;
}

// Page whose children sit at fixed design coordinates (relative to the page); a height of 0
// takes the child's measured height.
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
        for (auto& [w, r] : spots_) {
            Rect a = r.Offset(rect.x, rect.y);
            if (a.h <= 0) a.h = w->Measure(a.w, kH).h;
            w->Arrange(a);
        }
    }

private:
    std::vector<std::pair<Widget*, Rect>> spots_;
};

// --- step 1 widgets ---------------------------------------------------------------------------

// A sketch of a Lumen window in live palette colors: plain shapes instead of real icons and
// text, so it reads as a picture of the theme and not as a player that could be clicked.
class PreviewWindow : public Widget {
public:
    void Paint(Painter& p) override {
        const Palette& c = P();
        constexpr float r = 10;
        p.PushRoundClip(rect, r);
        p.Fill(rect, c.bgBase);
        const Rect title{rect.x, rect.y, rect.w, 20};
        p.Fill(title, c.bgChrome);
        p.Icon("lumen", {title.x + 8, title.y + 4, 12, 12}, c.textPrimary);
        for (int i = 0; i < 3; ++i) p.Fill({title.Right() - 22 - i * 18.0f, title.Cy() - 1, 8, 2}, c.textTertiary, 1);

        const Rect bar{rect.x, rect.Bottom() - 30, rect.w, 30};
        const Rect side{rect.x + 6, title.Bottom() + 6, 104, bar.y - title.Bottom() - 12};
        p.Fill(side, c.bgPanel, 6);
        for (int i = 0; i < 5; ++i) {
            const Rect row{side.x + 4, side.y + 6 + i * 22.0f, side.w - 8, 18};
            if (i == 0) p.Fill(row, c.bgSelected, 4);
            p.FillCircle({row.x + 10, row.Cy()}, 3.5f, i == 0 ? c.accent : c.textTertiary);
            p.Fill({row.x + 20, row.Cy() - 2, 36.0f + (i * 13 % 3) * 9, 4}, i == 0 ? c.textPrimary : c.textTertiary, 2);
        }

        const float x0 = side.Right() + 14, x1 = rect.Right() - 14;
        p.Fill({x0, side.y + 6, 84, 6}, c.textSecondary, 3);
        const float space = 10, cw = std::floor((x1 - x0 - 2 * space) / 3), ch = std::round(cw * 9 / 16);
        for (int row = 0; row < 2; ++row)
            for (int col = 0; col < 3; ++col) {
                const Rect card{x0 + col * (cw + space), side.y + 24 + row * (ch + 26), cw, ch};
                p.Fill(card, c.bgCard, kRadiusCard);
                p.Fill({card.x, card.Bottom() + 7, cw * (col == 1 ? 0.45f : 0.6f), 4}, c.textTertiary, 2);
            }

        p.Fill(bar, c.bgDeep);
        p.Fill({bar.x, bar.y, bar.w, 1}, c.line);
        const float cy = bar.Cy();
        p.FillCircle({bar.x + 22, cy}, 9, c.accent);
        p.Icon("play", {bar.x + 17, cy - 5, 10, 10}, c.onAccent);
        const Rect track{bar.x + 42, cy - 1.5f, bar.w - 42 - 70, 3};
        p.Fill(track, c.accentDim, 1.5f);
        p.Fill({track.x, track.y, track.w * 0.3f, track.h}, c.accent, 1.5f);
        p.Fill({track.Right() + 16, cy - 1.5f, 36, 3}, c.accentMid, 1.5f);
        p.PopClip();
        p.Stroke(rect, c.line, r);
    }
};

// Theme option: a sample of the theme with an accent ring when selected.
class ChoiceCard : public Widget {
public:
    enum class Kind { Light, Dark, System };
    explicit ChoiceCard(Kind k) : kind_(k) {
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
        }
        p.Text(caption, {rect.x - 20, rect.Bottom() - 19, rect.w + 40, 19}, TextStyle::Large,
               selected || IsHot() ? c.textPrimary : c.textSecondary, HAlign::Center);
    }

private:
    Kind kind_;
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
        // Skip, Escape or closing on the first page leave the default folder (the user's real
        // Videos) alone: it is scanned only after Continue confirmed it. A folder the user typed or
        // picked is a choice already and is kept.
        if (folderConfirmed_ || folderEdited_) CommitFolder();
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
        pg->Centered<IconView>(84, 104, 104, "lumen", 104, Tone::Accent);
        pg->Centered<Label>(214, 700, 39, Tr(L"Welcome to Lumen!"), TextStyle::Display, Tone::Primary)->halign =
            HAlign::Center;
        auto* d = pg->At<Label>(Rect{248, 276, 595, 72},
                                Tr(L"Lumen is a fast, quiet player for your videos and music. It opens almost any file, "
                                   L"folder or network stream, remembers where you stopped and keeps your library in "
                                   L"order."),
                                TextStyle::Large, Tone::Secondary);
        d->wrap = true;
        d->valign = VAlign::Top;

        // The language comes first: whoever reads the other one should not click through the setup in it.
        pg->At<Label>(Rect{248, 380, 300, 36}, Tr(L"Interface language"), TextStyle::Body, Tone::Secondary)->valign =
            VAlign::Center;
        const bool en = Cfg().GetString("general.language") == "en";
        auto* lang = pg->At<Segmented>(Rect{843 - 212, 380, 212, 36},
                                       std::vector<Choice>{{L"Русский", "ru", {}}, {L"English", "en", {}}}, en ? 1 : 0);
        lang->onChange = [](int i) { Cfg().SetString("general.language", i == 1 ? "en" : "ru"); };  // relabels

        pg->At<Label>(Rect{248, 452, 595, 17}, Tr(L"Choose a Media Folder to get started with"), TextStyle::Body,
                      Tone::Secondary);
        folderBox_ = pg->At<TextBox>(Rect{248, 480, 471, 36}, Tr(L"Folder with your videos or music"), "folder");
        folderBox_->SetText(folder_);
        folderBox_->onSubmit = [this](const std::wstring&) { host_.Post([this] { Next(); }); };
        auto* status = pg->At<Label>(Rect{248, 524, 595, 18}, L"", TextStyle::Small, Tone::Tertiary);
        auto check = [status](const std::wstring& t) {
            const bool dir = IsDir(t);
            const int broad = dir ? TooBroad(t) : 0;
            const wchar_t* text =
                t.empty()     ? L"You can add folders later from the sidebar."
                : broad == 1 ? L"This is a whole drive: Lumen would scan all of it. Better pick your media folder."
                : broad == 2 ? L"This is a system folder: Lumen would scan all of it. Better pick your media folder."
                : dir        ? L"Folder found. Lumen will look for videos and music in it."
                             : L"This folder doesn't exist. It won't be added.";
            status->SetText(Tr(text));
            // A missing folder is an error and a whole drive a warning, not the grey helper line.
            // Primary, not danger: red text is under 4.5:1 on the dark theme.
            status->tone = !t.empty() && !dir ? Tone::Primary : broad ? Tone::Secondary : Tone::Tertiary;
            status->Invalidate();
        };
        check(folder_);
        folderBox_->onChange = [this, check](const std::wstring& t) {
            folderEdited_ = true;
            check(t);
        };
        auto* browse = pg->At<DButton>(Rect{729, 480, 114, 36}, Tr(L"Browse"), BtnStyle::Neutral, TextStyle::Large);
        browse->minWidth = 114;
        browse->onClick = [this, check] {
            const std::wstring f = PickFolder(host_.Hwnd(), folderBox_->Text());
            if (f.empty()) return;
            folderBox_->SetText(f);
            folderEdited_ = true;
            check(f);
        };
    }

    void BuildAppearance(AbsPage* pg) {
        pg->Centered<Label>(56, 800, 42, Tr(L"Choose a theme."), TextStyle::Title, Tone::Secondary)->halign =
            HAlign::Center;
        pg->Centered<PreviewWindow>(124, 480, 270);
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
            auto* card = pg->At<ChoiceCard>(Rect{331 + i * 156.0f, 434, 116, 157}, opts[i].kind);
            card->caption = Tr(opts[i].label);
            card->selected = theme == opts[i].value;
            const char* v = opts[i].value;
            card->onClick = [this, v] {
                Cfg().SetString("general.theme", v);
                GoLater(step_, false);
            };
        }
    }

    void BuildSummary(AbsPage* pg) {
        pg->Centered<Label>(80, 700, 39, Tr(L"You're all set."), TextStyle::Display, Tone::Primary)->halign =
            HAlign::Center;
        pg->Centered<Label>(132, 700, 22, Tr(L"Here is how Lumen is set up. Everything can be changed later in Settings."),
                            TextStyle::Large, Tone::Tertiary)
            ->halign = HAlign::Center;
        auto card = [pg](float y, float h) {
            auto* c = pg->Centered<Panel>(y, 680, h);
            c->fill = [] { return P().bgPanel; };
            c->radius = kRadiusCard;
            c->dir = Dir::Column;
            c->pad = {20, 8, 12, 8};
            return c;
        };

        const std::string theme = Cfg().GetString("general.theme");
        const wchar_t* themeName = theme == "light" ? L"Light" : theme == "system" ? L"System" : L"Dark";
        struct Row {
            const wchar_t* key;
            std::wstring value;
            int step;
        };
        const std::vector<Row> rows = {
            {L"Media folder", IsDir(folder_) ? folder_ : std::wstring(Tr(L"Not chosen")), 0},
            {L"Theme", Tr(themeName), 1},
            {L"Language", Cfg().GetString("general.language") == "en" ? L"English" : L"Русский", 0},
        };
        auto* setup = card(178, rows.size() * 48.0f + 16);
        // The key column fits the longest key, not a fixed width.
        float keyW = 0;
        for (const Row& row : rows)
            keyW = std::max(keyW, std::ceil(gfx::Text::Get().Measure(Tr(row.key), TextStyle::Body).width) + 1);
        for (size_t i = 0; i < rows.size(); ++i) {
            if (i > 0) AddSeparator(setup);
            auto* r = setup->Add<Widget>();
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

        // What Lumen keeps about the user, right here instead of on a page of its own.
        auto* prefs = card(178 + rows.size() * 48.0f + 16 + 16, 0);  // 0: as tall as its rows
        // The Settings rows' own words, so a switch is found there under the same name.
        const char* const opts[] = {"privacy.saveHistory", "privacy.rememberPosition", "library.thumbnails"};
        const settings::Lang lang =
            CurrentLanguage() == Language::Russian ? settings::Lang::Ru : settings::Lang::En;
        for (size_t i = 0; i < std::size(opts); ++i) {
            const settings::Item* item = settings::FindItem(opts[i]);
            if (!item) continue;
            if (i > 0) AddSeparator(prefs);
            auto* row = prefs->Add<Widget>();
            row->dir = Dir::Row;
            row->align = Align::Center;
            row->width = Length::Grow();
            row->pad = {0, 12, 0, 12};
            row->gap = 24;
            auto* text = row->Add<Widget>();
            text->dir = Dir::Column;
            // A known width: measured as Grow, the description was one line and its second line
            // ran into the separator below.
            text->width = Length::Px(680 - 20 - 12 - 24 - 66);  // card - padding - gap - toggle
            text->gap = 4;
            text->Add<Label>(item->Label(lang), TextStyle::Body)->color = SoftText;
            auto* d = text->Add<Label>(item->Description(lang), TextStyle::Small, Tone::Tertiary);
            d->wrap = true;
            d->width = Length::Grow();
            const std::string key = opts[i];
            auto* t = row->Add<Toggle>(Cfg().GetBool(key));
            t->onChange = [key](bool on) { Cfg().SetBool(key, on); };
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
    bool folderEdited_ = false;  // typed or picked on the first page: kept even on Skip
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
