// The setup's window. Every page is laid out at fixed design coordinates in a 720x500 DIP window,
// like the onboarding: one centred column, one primary action, Enter does it, Escape closes.
// The work runs on a thread; a 30 ms timer moves the progress ring and picks up the result.
#include "setup/SetupWindow.h"

#include "app/I18n.h"
#include "app/dialogs/DialogKit.h"
#include "core/Config.h"
#include "core/Paths.h"
#include "gfx/Gpu.h"
#include "gfx/Text.h"
#include "setup/ReleaseNotes.h"
#include "ui/Controls.h"
#include "ui/Host.h"
#include "ui/Theme.h"

#include <windows.h>
#include <ole2.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <thread>

namespace setup {
namespace {

using namespace ui;
using app::Fmt;
using app::Tr;
using app::dlg::BtnStyle;
using app::dlg::DButton;

constexpr float kW = 720, kH = 500;
constexpr float kPi = 3.14159265f;

enum class Page { Welcome, Options, Update, Rollback, Same, Remove, Work, Done, Error };

bool Russian() {
    return app::CurrentLanguage() == app::Language::Russian;
}

std::wstring SizeText(uint64_t bytes) {
    return app::dlg::FormatBytes(static_cast<double>(bytes));
}

// --- painting helpers ------------------------------------------------------------------------------

ID2D1StrokeStyle* RoundStroke() {
    static ID2D1StrokeStyle* style = [] {
        ID2D1StrokeStyle* s = nullptr;
        gfx::Gpu::Get().D2DFactory()->CreateStrokeStyle(
            D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND), nullptr, 0, &s);
        return s;
    }();
    return style;
}

// A circle's arc from t0 to t1 (0..1, clockwise from the top), as short round-capped segments.
void Arc(Painter& p, Point c, float r, float t0, float t1, Color color, float width) {
    if (t1 <= t0) return;
    gfx::ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(p.Ctx()->CreateSolidColorBrush(color.Fade(p.Opacity()).D2D(), &brush))) return;
    auto at = [&](float t) {
        const float a = -kPi / 2 + t * 2 * kPi;
        return D2D1_POINT_2F{c.x + r * std::cos(a), c.y + r * std::sin(a)};
    };
    const int n = std::max(2, static_cast<int>(120 * (t1 - t0)));
    for (int i = 0; i < n; ++i)
        p.Ctx()->DrawLine(at(t0 + (t1 - t0) * i / n), at(t0 + (t1 - t0) * (i + 1) / n), brush.Get(), width, RoundStroke());
}

// A soft round glow fading out to its edge.
void Glow(Painter& p, Point c, float r, Color color) {
    const D2D1_GRADIENT_STOP stops[] = {{0.0f, color.Fade(p.Opacity()).D2D()},
                                        {0.5f, color.Fade(0.4f * p.Opacity()).D2D()},
                                        {1.0f, color.WithAlpha(0).D2D()}};
    gfx::ComPtr<ID2D1GradientStopCollection> collection;
    gfx::ComPtr<ID2D1RadialGradientBrush> brush;
    if (FAILED(p.Ctx()->CreateGradientStopCollection(stops, 3, &collection)) ||
        FAILED(p.Ctx()->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties({c.x, c.y}, {0, 0}, r, r),
                                                  collection.Get(), &brush)))
        return;
    p.Ctx()->FillEllipse(D2D1::Ellipse({c.x, c.y}, r, r), brush.Get());
}

void Line(Painter& p, D2D1_POINT_2F a, D2D1_POINT_2F b, Color color, float width) {
    gfx::ComPtr<ID2D1SolidColorBrush> brush;
    if (SUCCEEDED(p.Ctx()->CreateSolidColorBrush(color.Fade(p.Opacity()).D2D(), &brush)))
        p.Ctx()->DrawLine(a, b, brush.Get(), width, RoundStroke());
}

// Moonbow's mark over a soft halo; grows in once.
class LogoMark : public Widget {
public:
    explicit LogoMark(float size, bool dim = false) : size_(size), dim_(dim) { appear_.To(1, 520); }
    void Paint(Painter& p) override {
        const Palette& c = P();
        const float k = appear_.Get();
        const Point ctr{rect.Cx(), rect.Cy()};
        Glow(p, ctr, size_ * (0.75f + 0.15f * k), c.accent.Fade(0.12f * k));
        const float s = std::round(size_ * (0.92f + 0.08f * k));
        p.PushOpacity(k);
        p.Icon("moonbow", {ctr.x - s / 2, ctr.y - s / 2, s, s}, dim_ ? c.textTertiary : c.accent);
        p.PopOpacity();
    }

private:
    float size_;
    bool dim_;
    AnimFloat appear_{0};
};

// The mark inside a ring that fills with the work; a turning arc while the amount is unknown.
class ProgressRing : public Widget {
public:
    AnimFloat value{0};
    bool spinning = false;
    void Paint(Painter& p) override {
        const Palette& c = P();
        const Point ctr{rect.Cx(), rect.Cy()};
        const float r = std::min(rect.w, rect.h) / 2 - 2;
        Glow(p, ctr, r, c.accent.Fade(0.08f));
        Arc(p, ctr, r, 0, 1, c.lineStrong, 3);
        if (spinning) {
            const float t = static_cast<float>(GetTickCount64() % 1400) / 1400.0f;
            Arc(p, ctr, r, t, t + 0.22f, c.accent, 3);
            Arc(p, ctr, r, t - 1, t + 0.22f - 1, c.accent, 3);  // the part past the top
        } else {
            Arc(p, ctr, r, 0, value.Get(), c.accent, 3);
        }
        const float s = std::round(r * 0.9f);
        p.Icon("moonbow", {ctr.x - s / 2, ctr.y - s / 2, s, s}, c.accent);
    }
};

// A ring that closes, then a tick drawn into it.
class CheckMark : public Widget {
public:
    CheckMark() { draw_.To(1, 700, Ease::InOut); }
    void Paint(Painter& p) override {
        const Palette& c = P();
        const Point ctr{rect.Cx(), rect.Cy()};
        const float r = std::min(rect.w, rect.h) / 2 - 2, k = draw_.Get();
        Glow(p, ctr, r, c.accent.Fade(0.08f * k));
        Arc(p, ctr, r, 0, std::min(1.0f, k * 1.6f), c.accent, 3);
        const D2D1_POINT_2F a{ctr.x - r * 0.32f, ctr.y + r * 0.02f}, b{ctr.x - r * 0.08f, ctr.y + r * 0.26f},
            e{ctr.x + r * 0.34f, ctr.y - r * 0.22f};
        const float t = std::clamp((k - 0.45f) / 0.55f, 0.0f, 1.0f) * 2;  // first leg 0..1, second 1..2
        auto mix = [](D2D1_POINT_2F x, D2D1_POINT_2F y, float f) { return D2D1_POINT_2F{x.x + (y.x - x.x) * f, x.y + (y.y - x.y) * f}; };
        if (t > 0) Line(p, a, mix(a, b, std::min(t, 1.0f)), c.accent, 4);
        if (t > 1) Line(p, b, mix(b, e, t - 1), c.accent, 4);
    }

private:
    AnimFloat draw_{0};
};

// "0.1.0 → 0.2.0": where Moonbow is, where it goes.
class VersionFlow : public Widget {
public:
    VersionFlow(std::wstring from, std::wstring to) : from_(std::move(from)), to_(std::move(to)) {}
    void Describe(A11y& a) override {
        a.role = Role::Text;
        a.name = from_ + L" → " + to_;
    }
    void Paint(Painter& p) override {
        const Palette& c = P();
        const float w1 = std::ceil(p.Measure(from_, TextStyle::Mono).width) + 28;
        const float w2 = std::ceil(p.Measure(to_, TextStyle::Mono).width) + 28;
        const float h = rect.h, x = std::round(rect.Cx() - (w1 + 44 + w2) / 2);
        const Rect a{x, rect.y, w1, h}, b{x + w1 + 44, rect.y, w2, h};
        p.Fill(a, c.bgRaised, h / 2);
        p.Text(from_, a, TextStyle::Mono, c.textTertiary, HAlign::Center);
        p.Icon("arrow_right", {a.Right() + 12, rect.Cy() - 10, 20, 20}, c.textTertiary);
        p.Fill(b, c.accentFaint, h / 2);
        p.Stroke(b, c.accent, h / 2, 1.5f);
        p.Text(to_, b, TextStyle::Mono, c.textPrimary, HAlign::Center);
    }

private:
    std::wstring from_, to_;
};

// A choice on the "already installed" page: icon, name, one line of what it does.
class ActionCard : public Widget {
public:
    ActionCard(std::string icon, std::wstring title, std::wstring desc)
        : icon_(std::move(icon)), title_(std::move(title)), desc_(std::move(desc)) {
        interactive = true;
        focusable = true;
        cursor = IDC_HAND;
    }
    void Describe(A11y& a) override {
        a.role = Role::Button;
        a.name = title_;
        a.help = desc_;
    }
    void Paint(Painter& p) override {
        const Palette& c = P();
        const bool hot = IsHot() || IsPressed();
        p.Fill(rect, c.bgPanel, kRadiusCard);
        if (hot) p.Fill(rect, IsPressed() ? c.press : c.hover, kRadiusCard);
        p.Stroke(rect, hot ? c.lineStrong : c.lineSubtle, kRadiusCard);
        PaintFocusRing(p, rect, kRadiusCard);
        p.Icon(icon_, {rect.x + 20, rect.y + 22, 28, 28}, hot ? c.textPrimary : c.textSecondary, c.accentMid);
        p.Text(title_, {rect.x + 20, rect.y + 66, rect.w - 40, 22}, TextStyle::Heading, c.textPrimary);
        p.Text(desc_, {rect.x + 20, rect.y + 92, rect.w - 40, rect.h - 104}, TextStyle::Small, c.textTertiary,
               HAlign::Leading, VAlign::Top, gfx::kTextWrap);
    }

private:
    std::string icon_;
    std::wstring title_, desc_;
};

// Children at fixed design coordinates; a height of 0 takes the child's measured height. Fades
// and slides in when it replaces another page.
class Page_ : public Widget {
public:
    Page_() {
        width = Length::Grow();
        height = Length::Grow();
    }
    template <class T, class... A>
    T* At(Rect r, A&&... args) {
        T* w = Add<T>(std::forward<A>(args)...);
        spots_.push_back({w, r});
        return w;
    }
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
    void PaintChildren(Painter& p) override {
        const float k = appear.Get();
        if (k >= 0.999f) return Widget::PaintChildren(p);
        D2D1_MATRIX_3X2_F old;
        p.Ctx()->GetTransform(&old);
        p.Ctx()->SetTransform(D2D1::Matrix3x2F::Translation((1 - k) * 18 * direction, 0) * old);
        p.PushOpacity(k);
        Widget::PaintChildren(p);
        p.PopOpacity();
        p.Ctx()->SetTransform(old);
    }
    AnimFloat appear{1.0f};
    float direction = 1;

private:
    std::vector<std::pair<Widget*, Rect>> spots_;
};

class Root : public Panel {
public:
    std::function<void()> onLayout;
    void OnLayout() override {
        if (onLayout) onLayout();
    }
};

Label* CenteredText(Page_* pg, float y, float w, float h, const std::wstring& text, TextStyle style, Tone tone) {
    auto* l = pg->Centered<Label>(y, w, h, text, style, tone);
    l->halign = HAlign::Center;
    l->valign = VAlign::Top;
    l->wrap = true;
    return l;
}

// --- the window --------------------------------------------------------------------------------

class SetupUi {
public:
    explicit SetupUi(const Request& r) : req_(r) {
        dir_ = req_.target.dir;
        installed_ = InstalledVersion(dir_);
        incoming_ = req_.op == Op::Install ? req_.package->version : req_.version;
        action_ = Classify(installed_, incoming_);
        for (const PackedFile& f : req_.package ? req_.package->files : std::vector<PackedFile>{}) size_ += f.size;
        snapshot_ = action_ == Action::Rollback && HasDataSnapshot(incoming_);
    }

    ~SetupUi() {
        if (worker_.joinable()) worker_.join();
    }

    int Run() {
        HostOptions o;
        o.title = Tr(L"Moonbow Installer");
        o.width = o.minWidth = kW;
        o.height = o.minHeight = kH;
        o.resizable = false;
        if (!host_.Create(o)) return 1;
        auto root = std::make_unique<Root>();
        root->fill = [] { return P().bgBase; };
        root->dir = Dir::None;
        root->ncRole = NcRole::Caption;  // empty space drags the window
        root->onLayout = [this] { LayoutChrome(); };
        root_ = root.get();
        host_.SetRoot(std::move(root));
        minimize_ = root_->Add<IconButton>("minimize", Tr(L"Minimize"), 40, 16);
        minimize_->onClick = [this] { host_.Minimize(); };
        close_ = root_->Add<IconButton>("close", Tr(L"Close"), 40, 16);
        close_->onClick = [this] { host_.Close(); };
        host_.onKey = [this](const KeyEvent& e) { return OnKey(e); };
        host_.onCloseRequest = [this] { return !working_; };
        host_.onDestroyed = [] { PostQuitMessage(0); };

        if (req_.autoUpdate && req_.previewPage.empty()) {
            Go(Page::Work, false);
            Start();
        } else {
            Go(FirstPage(), false);
        }
        host_.Show();
        Host::Activate(host_.Hwnd());
        gfx::Gpu::Get().CreateDeviceAsync(host_.Hwnd(), Host::kDeviceReadyMessage);
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return result_;
    }

private:
    Page FirstPage() {
        if (req_.previewPage == L"work") return Page::Work;
        if (req_.previewPage == L"done") return Page::Done;
        if (req_.previewPage == L"error") return error_ = L"cannot write C:\\example\\libmpv-2.dll", Page::Error;
        if (req_.previewPage == L"options") return Page::Options;
        if (req_.previewPage == L"remove") return Page::Remove;
        if (req_.op == Op::Uninstall) return Page::Remove;
        switch (action_) {
            case Action::Install: return Page::Welcome;
            case Action::Update: return Page::Update;
            case Action::Rollback: return Page::Rollback;
            default: return req_.op == Op::Switch ? Page::Update : Page::Same;
        }
    }

    void LayoutChrome() {
        const Rect r = root_->rect;
        if (page_) page_->Arrange(r);
        close_->Arrange({r.Right() - 44, r.y + 4, 40, 32});
        minimize_->Arrange({r.Right() - 84, r.y + 4, 40, 32});
        close_->visible = !working_;
    }

    bool OnKey(const KeyEvent& e) {
        if (e.key == VK_ESCAPE && !working_) {
            if (page_id_ == Page::Options) GoLater(Page::Welcome);
            else host_.Close();
            return true;
        }
        if (e.key == VK_RETURN && primary_ && !working_) {
            primary_->OnClick();
            return true;
        }
        return false;
    }

    // Rebuilding a page destroys the button whose click is being handled: defer it.
    void GoLater(Page page) {
        host_.Post([this, page] { Go(page, true); });
    }

    void Go(Page page, bool animate) {
        if (page_) root_->RemoveChild(page_);
        const bool forward = static_cast<int>(page) >= static_cast<int>(page_id_);
        page_id_ = page;
        primary_ = nullptr;
        auto pg = std::make_unique<Page_>();
        pg->dir = Dir::None;
        page_ = pg.get();
        root_->AddChild(std::move(pg), 0);  // under the window buttons
        switch (page) {
            case Page::Welcome: BuildWelcome(); break;
            case Page::Options: BuildOptions(); break;
            case Page::Update: BuildUpdate(); break;
            case Page::Rollback: BuildRollback(); break;
            case Page::Same: BuildSame(); break;
            case Page::Remove: BuildRemove(); break;
            case Page::Work: BuildWork(); break;
            case Page::Done: BuildDone(); break;
            case Page::Error: BuildError(); break;
        }
        page_->appear.Set(animate ? 0.0f : 1.0f);
        if (animate) page_->appear.To(1.0f, 220, Ease::Out);
        page_->direction = forward ? 1.0f : -1.0f;
        root_->InvalidateLayout();
        host_.RequestLayout();
        if (primary_) host_.SetFocus(primary_, false);
    }

    // The main button of a page, centred, with an optional secondary one to its left.
    DButton* Buttons(float y, const std::wstring& main, std::function<void()> onMain, const std::wstring& other = {},
                     std::function<void()> onOther = {}) {
        const float w = 200, gap = 12;
        const float total = other.empty() ? w : w * 2 + gap;
        float x = std::round((kW - total) / 2);
        if (!other.empty()) {
            auto* b = page_->At<DButton>(Rect{x, y, w, 40}, other, BtnStyle::Neutral, TextStyle::Heading);
            b->minWidth = w;
            b->onClick = std::move(onOther);
            x += w + gap;
        }
        auto* b = page_->At<DButton>(Rect{x, y, w, 40}, main, BtnStyle::Solid, TextStyle::Heading);
        b->minWidth = w;
        b->onClick = std::move(onMain);
        primary_ = b;
        return b;
    }

    void RunningNotice(float y) {
        if (!MoonbowRunning(dir_)) return;
        auto* l = CenteredText(page_, y, 560, 20, Tr(L"Moonbow is open: it will close, and where you stopped is saved."),
                               TextStyle::Small, Tone::Secondary);
        l->a11yName = l->Text();
    }

    // --- pages -------------------------------------------------------------------------------------

    void BuildWelcome() {
        page_->Centered<LogoMark>(56, 120, 120, 96.0f);
        CenteredText(page_, 196, 640, 40, Tr(L"Welcome to Moonbow"), TextStyle::Display, Tone::Primary);
        CenteredText(page_, 246, 520, 48,
                     Tr(L"A fast, quiet player for your videos and music. It installs in seconds, just for you, "
                        L"without administrator rights."),
                     TextStyle::Large, Tone::Secondary);
        CenteredText(page_, 304, 560, 18, Fmt(Tr(L"Version {0} · {1} on disk"), {incoming_, SizeText(size_)}),
                     TextStyle::Small, Tone::Tertiary);
        Buttons(352, Tr(L"Install"), [this] { Start(); });
        auto* row = page_->Centered<Widget>(408, 400, 24);
        row->dir = Dir::Row;
        row->justify = Justify::Center;
        auto* options = row->Add<LinkButton>(Tr(L"Install options"), TextStyle::Body);
        options->trailingIcon = "chevron_right";
        options->onClick = [this] { GoLater(Page::Options); };
    }

    void BuildOptions() {
        page_->At<Label>(Rect{60, 52, 600, 32}, Tr(L"Install options"), TextStyle::Title, Tone::Primary);
        page_->At<Label>(Rect{60, 112, 600, 18}, Tr(L"Folder"), TextStyle::Body, Tone::Secondary);
        auto* folder = page_->At<TextBox>(Rect{60, 138, 466, 36}, std::wstring(), "folder");
        folder->SetText(dir_);
        folder->onChange = [this](const std::wstring& t) { dir_ = t; };
        auto* browse = page_->At<DButton>(Rect{536, 138, 124, 36}, Tr(L"Browse"), BtnStyle::Neutral, TextStyle::Large);
        browse->minWidth = 124;
        browse->onClick = [this, folder] {
            const std::wstring f = app::dlg::PickFolder(host_.Hwnd(), folder->Text());
            if (f.empty()) return;
            dir_ = f + (f.size() > 3 && f.find(L"Moonbow") == std::wstring::npos ? L"\\Moonbow" : L"");
            folder->SetText(dir_);
        };
        auto* hint = page_->At<Label>(Rect{60, 184, 600, 36},
                                      Tr(L"Only for you, no administrator rights needed. Earlier versions are kept here, "
                                         L"so you can go back to them."),
                                      TextStyle::Small, Tone::Tertiary);
        hint->wrap = true;
        hint->valign = VAlign::Top;

        page_->At<Divider>(Rect{60, 240, 600, 1});
        page_->At<Label>(Rect{60, 262, 500, 20}, Tr(L"Open videos in Moonbow by default"), TextStyle::Body, Tone::Primary);
        page_->At<Label>(Rect{60, 286, 500, 18}, Tr(L"Music and playlists stay with the app you use now."),
                         TextStyle::Small, Tone::Tertiary);
        auto* toggle = page_->At<Toggle>(Rect{620, 268, 40, 20}, defaultPlayer_);
        toggle->a11yName = Tr(L"Open videos in Moonbow by default");
        toggle->onChange = [this](bool on) { defaultPlayer_ = on; };

        auto* back = page_->At<DButton>(Rect{60, 420, 120, 40}, Tr(L"Back"), BtnStyle::Neutral, TextStyle::Heading);
        back->minWidth = 120;
        back->onClick = [this] { GoLater(Page::Welcome); };
        auto* install = page_->At<DButton>(Rect{460, 420, 200, 40}, Tr(L"Install"), BtnStyle::Solid, TextStyle::Heading);
        install->minWidth = 200;
        install->onClick = [this] { Start(); };
        primary_ = install;
    }

    void BuildUpdate() {
        page_->Centered<LogoMark>(20, 76, 76, 56.0f);
        CenteredText(page_, 100, 640, 32,
                     req_.op == Op::Switch ? Fmt(Tr(L"Switch to Moonbow {0}"), {incoming_}) : std::wstring(Tr(L"Moonbow update")),
                     TextStyle::Title, Tone::Primary);
        page_->Centered<VersionFlow>(144, 400, 32, installed_, incoming_);
        const std::vector<std::wstring> notes = NotesBetween(installed_, incoming_, Russian());
        if (!notes.empty()) {
            auto* card = page_->Centered<Panel>(194, 520, 166);
            card->fill = [] { return P().bgPanel; };
            card->radius = kRadiusCard;
            card->dir = Dir::Column;
            auto* scroll = card->Add<ScrollView>();
            scroll->width = Length::Grow();
            scroll->height = Length::Grow();
            auto* list = scroll->SetContent<Widget>();
            list->dir = Dir::Column;
            list->width = Length::Grow();
            list->pad = {20, 14, 20, 14};
            list->gap = 8;
            list->Add<Label>(Tr(L"What's new"), TextStyle::BodyStrong, Tone::Primary);
            for (const std::wstring& line : notes) {
                auto* l = list->Add<Label>(L"•  " + line, TextStyle::Body, Tone::Secondary);
                l->wrap = true;
                l->width = Length::Grow();
            }
        }
        const float y = notes.empty() ? 210.0f : 378.0f;
        if (MoonbowRunning(dir_)) RunningNotice(y);
        else
            CenteredText(page_, y, 560, 20, Fmt(Tr(L"Version {0} stays on this computer, so you can go back to it."), {installed_}),
                         TextStyle::Small, Tone::Tertiary);
        Buttons(420, req_.op == Op::Switch ? Fmt(Tr(L"Switch to {0}"), {incoming_}) : std::wstring(Tr(L"Update")),
                [this] { Start(); }, Tr(L"Not now"), [this] { host_.Close(); });
    }

    void BuildRollback() {
        const float y = snapshot_ ? 0.0f : 40.0f;  // without the checkbox the page is shorter: keep it centred
        page_->Centered<LogoMark>(36 + y, 88, 88, 64.0f);
        CenteredText(page_, 132 + y, 640, 32, Fmt(Tr(L"Go back to Moonbow {0}"), {incoming_}), TextStyle::Title, Tone::Primary);
        page_->Centered<VersionFlow>(180 + y, 400, 32, installed_, incoming_);
        CenteredText(page_, 232 + y, 520, 44,
                     Fmt(Tr(L"Version {0} stays on this computer, so you can return to it later."), {installed_}),
                     TextStyle::Body, Tone::Secondary);
        if (snapshot_) {
            auto* row = page_->Centered<Widget>(292, 520, 24);
            row->dir = Dir::Row;
            row->justify = Justify::Center;
            auto* box = row->Add<Checkbox>(Fmt(Tr(L"Bring back the settings and library Moonbow {0} had"), {incoming_}), restoreData_);
            box->onChange = [this](bool on) { restoreData_ = on; };
            CenteredText(page_, 322, 520, 18, Tr(L"Otherwise the current ones stay."), TextStyle::Small, Tone::Tertiary);
        }
        RunningNotice(372);
        Buttons(420, Fmt(Tr(L"Go back to {0}"), {incoming_}), [this] { Start(); }, Tr(L"Cancel"), [this] { host_.Close(); });
    }

    void BuildSame() {
        page_->Centered<LogoMark>(36, 88, 88, 64.0f);
        CenteredText(page_, 132, 640, 32, Fmt(Tr(L"Moonbow {0} is installed"), {installed_}), TextStyle::Title, Tone::Primary);
        CenteredText(page_, 176, 640, 22, Tr(L"What would you like to do?"), TextStyle::Large, Tone::Secondary);
        const float w = 196, gap = 16, x0 = std::round((kW - 3 * w - 2 * gap) / 2);
        auto* open = page_->At<ActionCard>(Rect{x0, 230, w, 150}, "play", Tr(L"Open Moonbow"),
                                           Tr(L"Start the installed version."));
        open->onClick = [this] {
            Launch();
            host_.Close();
        };
        auto* repair = page_->At<ActionCard>(Rect{x0 + w + gap, 230, w, 150}, "wrench", Tr(L"Repair"),
                                             Tr(L"Put its files, file types and shortcut back in place."));
        repair->onClick = [this] { Start(); };
        auto* remove = page_->At<ActionCard>(Rect{x0 + 2 * (w + gap), 230, w, 150}, "remove", Tr(L"Uninstall"),
                                             Tr(L"Remove Moonbow from this computer."));
        remove->onClick = [this] {
            req_.op = Op::Uninstall;
            GoLater(Page::Remove);
        };
        primary_ = nullptr;
        host_.Post([this, open] { host_.SetFocus(open, false); });
    }

    void BuildRemove() {
        page_->Centered<LogoMark>(36, 88, 88, 64.0f, true);
        CenteredText(page_, 132, 640, 32, Tr(L"Uninstall Moonbow?"), TextStyle::Title, Tone::Primary);
        CenteredText(page_, 178, 540, 44,
                     Tr(L"Moonbow and all its kept versions will be removed. Your videos and music stay where they are."),
                     TextStyle::Body, Tone::Secondary);
        auto* row = page_->Centered<Widget>(256, 520, 24);
        row->dir = Dir::Row;
        row->justify = Justify::Center;
        auto* box = row->Add<Checkbox>(Tr(L"Also delete settings, library and logs"), removeData_);
        box->onChange = [this](bool on) { removeData_ = on; };
        CenteredText(page_, 286, 520, 18, Tr(L"Keep them if you plan to install Moonbow again."), TextStyle::Small,
                     Tone::Tertiary);
        RunningNotice(372);
        Buttons(420, Tr(L"Uninstall"), [this] { Start(); }, Tr(L"Cancel"), [this] { host_.Close(); });
    }

    std::wstring WorkTitle() const {
        if (req_.op == Op::Uninstall) return Tr(L"Removing Moonbow…");
        switch (action_) {
            case Action::Install: return Fmt(Tr(L"Installing Moonbow {0}…"), {incoming_});
            case Action::Update: return Fmt(Tr(L"Updating to {0}…"), {incoming_});
            case Action::Rollback: return Fmt(Tr(L"Going back to {0}…"), {incoming_});
            default: return Tr(L"Repairing Moonbow…");
        }
    }

    void BuildWork() {
        ring_ = page_->Centered<ProgressRing>(104, 136, 136);
        ring_->spinning = req_.op == Op::Uninstall || req_.op == Op::Switch;
        CenteredText(page_, 274, 640, 32, WorkTitle(), TextStyle::Title, Tone::Primary);
        status_ = CenteredText(page_, 318, 560, 20, L"", TextStyle::Body, Tone::Tertiary);
        UpdateStatus();
    }

    void UpdateStatus() {
        if (!status_) return;
        const int phase = phase_.load();
        const float f = static_cast<float>(progress_.load());
        std::wstring text = phase == 0   ? Tr(L"Closing Moonbow…")
                            : phase == 2 ? Tr(L"Almost done…")
                            : req_.op == Op::Uninstall ? Tr(L"Removing files…")
                            : f >= 1.0f  ? Tr(L"Connecting file types and the shortcut…")
                                         : Tr(L"Unpacking files…");
        if (phase == 1 && req_.op == Op::Install && f < 1.0f) text += L"  " + std::to_wstring(static_cast<int>(f * 100)) + L" %";
        if (status_->Text() != text) status_->SetText(text);
        if (ring_ && !ring_->spinning && std::abs(ring_->value.Target() - f) > 0.001f) ring_->value.To(f, 240);
        if (ring_) ring_->Invalidate();
    }

    std::wstring DoneTitle() const {
        if (req_.op == Op::Uninstall) return Tr(L"Moonbow is removed");
        switch (action_) {
            case Action::Install: return Tr(L"Moonbow is installed");
            case Action::Update: return Fmt(Tr(L"Moonbow is updated to {0}"), {incoming_});
            case Action::Rollback: return Fmt(Tr(L"You are back on Moonbow {0}"), {incoming_});
            default: return Tr(L"Moonbow is repaired");
        }
    }

    std::wstring DoneText() const {
        if (req_.op == Op::Uninstall)
            return removeData_ ? Tr(L"Its settings and library are deleted too.")
                               : Tr(L"Your settings and library are kept for the next install.");
        switch (action_) {
            case Action::Install:
                return defaultPlayer_ ? Tr(L"Videos now open in Moonbow. It is also in the Start menu.")
                                      : Tr(L"You will find it in the Start menu.");
            case Action::Update:
            case Action::Rollback:
                return Fmt(Tr(L"Version {0} is kept: Settings › About Moonbow takes you back to it."), {installed_});
            default: return Tr(L"Its files, file types and shortcut are in place again.");
        }
    }

    void BuildDone() {
        page_->Centered<CheckMark>(104, 112, 112);
        CenteredText(page_, 248, 640, 32, DoneTitle(), TextStyle::Title, Tone::Primary);
        CenteredText(page_, 292, 600, 44, DoneText(), TextStyle::Body, Tone::Secondary);
        if (req_.op == Op::Uninstall) {
            Buttons(420, Tr(L"Close"), [this] { host_.Close(); });
            return;
        }
        Buttons(420, Tr(L"Open Moonbow"),
                [this] {
                    Launch();
                    host_.Close();
                },
                Tr(L"Close"), [this] { host_.Close(); });
    }

    void BuildError() {
        page_->Centered<IconView>(112, 56, 56, "warning", 56, Tone::Secondary);
        CenteredText(page_, 192, 640, 32,
                     req_.op == Op::Uninstall ? Tr(L"Moonbow could not be removed.") : Tr(L"Moonbow could not be installed."),
                     TextStyle::Title, Tone::Primary);
        if (req_.op != Op::Uninstall)
            CenteredText(page_, 236, 540, 22, Tr(L"Nothing changed: Moonbow is as it was."), TextStyle::Body, Tone::Secondary);
        CenteredText(page_, 272, 560, 60, error_, TextStyle::Small, Tone::Tertiary);
        Buttons(420, Tr(L"Retry"), [this] { Start(); }, req_.autoUpdate ? Tr(L"Open Moonbow") : Tr(L"Close"), [this] {
            if (req_.autoUpdate) Launch();
            host_.Close();
        });
    }

    // --- the work ----------------------------------------------------------------------------------

    void Start() {
        if (working_) return;
        if (worker_.joinable()) worker_.join();
        if (dir_.empty()) dir_ = DefaultInstallDir();
        while (dir_.size() > 3 && (dir_.back() == L'\\' || dir_.back() == L'/')) dir_.pop_back();
        working_ = true;
        finished_ = false;
        phase_ = 0;
        progress_ = 0;
        error_.clear();
        started_ = GetTickCount64();
        if (page_id_ != Page::Work || !page_) host_.Post([this] { Go(Page::Work, true); });
        if (!req_.previewPage.empty()) return;
        const Request req = req_;
        const std::wstring dir = dir_;
        const bool restore = restoreData_, removeData = removeData_, defaultPlayer = defaultPlayer_;
        const Action action = action_;
        worker_ = std::thread([this, req, dir, restore, removeData, defaultPlayer, action] {
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);  // the Start menu shortcut
            std::wstring error;
            bool ok = CloseRunningMoonbow(dir, 15000);
            if (!ok) error = Tr(L"Moonbow did not close. Close it and try again.");
            phase_ = 1;
            const Target target{dir, req.target.system};
            if (ok) {
                if (req.op == Op::Uninstall) ok = Uninstall(target, removeData, &error);
                else if (req.op == Op::Switch) ok = SwitchVersion(target, req.version, &error);
                else ok = Install(*req.package, target, [this](double f) { progress_ = f; }, &error);
            }
            phase_ = 2;
            if (ok && restore) RestoreData(req.op == Op::Switch ? req.version : req.package->version);
            if (ok && action == Action::Install && req.op == Op::Install && defaultPlayer && target.system)
                RunAndWait(dir + L"\\Moonbow.exe", L"--set-default-player");
            CoUninitialize();
            error_ = error;
            ok_ = ok;
            finished_ = true;
        });
        poll_ = host_.StartTimer(30, [this] { Poll(); }, true);
    }

    void Poll() {
        UpdateStatus();
        // At least a moment on this page: a switch takes 0.2 s and would only flash it.
        if (!finished_ || GetTickCount64() - started_ < 1100) return;
        host_.CancelTimer(poll_);
        if (worker_.joinable()) worker_.join();
        working_ = false;
        result_ = ok_ ? 0 : 1;
        Go(ok_ ? Page::Done : Page::Error, true);
        // "Restart to update": a moment on "Moonbow is updated", then the new Moonbow.
        if (ok_ && req_.autoUpdate)
            host_.StartTimer(900, [this] {
                Launch();
                host_.Close();
            });
    }

    void Launch() {
        if (Host::Automation()) return;  // background test runs never open a player window
        ShellExecuteW(nullptr, L"open", (dir_ + L"\\Moonbow.exe").c_str(), nullptr, dir_.c_str(), SW_SHOWNORMAL);
    }

    Request req_;
    Host host_;
    Root* root_ = nullptr;
    Page_* page_ = nullptr;
    Page page_id_ = Page::Welcome;
    IconButton* minimize_ = nullptr;
    IconButton* close_ = nullptr;
    DButton* primary_ = nullptr;
    ProgressRing* ring_ = nullptr;
    Label* status_ = nullptr;
    std::wstring dir_, installed_, incoming_, error_;
    Action action_ = Action::Install;
    uint64_t size_ = 0;
    bool snapshot_ = false;
    bool defaultPlayer_ = true, restoreData_ = false, removeData_ = false;
    bool working_ = false;
    int result_ = 1;
    ULONGLONG started_ = 0;
    UINT_PTR poll_ = 0;
    std::thread worker_;
    std::atomic<int> phase_{0};
    std::atomic<double> progress_{0};
    std::atomic<bool> finished_{false}, ok_{false};
};

// Theme, accent and language as the user's Moonbow has them, so an update looks like their player.
void ApplyMoonbowLook() {
    Config cfg;
    const std::wstring path = DataFilePath(L"settings.json");
    const bool has = !path.empty() && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES && cfg.LoadFrom(path);
    const std::string lang = has ? cfg.GetString("general.language") : "";
    app::SetLanguage(lang == "en"   ? app::Language::English
                     : lang == "ru" ? app::Language::Russian
                     : PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN ? app::Language::Russian
                                                                                  : app::Language::English);
    const std::string mode = has ? cfg.GetString("general.theme") : "system";
    bool dark = mode != "light";
    if (mode == "system") {
        DWORD light = 0, size = sizeof(light);
        if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                         L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size) == ERROR_SUCCESS)
            dark = light == 0;
    }
    static const char* const kAccents[] = {"white", "pearl", "silver", "steel", "graphite"};
    int accent = 0;
    const std::string a = has ? cfg.GetString("general.accent") : "";
    for (int i = 0; i < 5; ++i)
        if (a == kAccents[i]) accent = i;
    Theme::Get().Set(dark ? ThemeMode::Dark : ThemeMode::Light, accent);
}

}  // namespace

int RunWindow(const Request& request) {
    ApplyMoonbowLook();
    OleInitialize(nullptr);
    int code = 1;
    if (gfx::Gpu::Get().CreateFactories()) {
        gfx::Text::Get().SetScale(Host::TextScale());
        gfx::Text::Get().InitAsync(GetModuleHandleW(nullptr));
        code = SetupUi(request).Run();
    }
    OleUninitialize();
    return code;
}

}  // namespace setup
