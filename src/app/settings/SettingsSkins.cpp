// Settings > Interface > Skin: the built-in skins with a small preview each, then "Custom file…",
// and below them what is wrong with the active skin (file warnings, low contrast) with Reset.
// Writes interface.skin / customSkin / skinFile; app/Skins.cpp applies them.
#include "app/I18n.h"
#include "app/Skins.h"
#include "app/settings/SettingsUi.h"
#include "core/Config.h"
#include "core/Json.h"

namespace app::sui {

using namespace ui;
using settings::Item;

namespace {

constexpr int kCustom = -1;

Color ToColor(const skin::Rgba& c) {
    return {c.r, c.g, c.b, c.a};
}

bool Russian() {
    return Lang() == settings::Lang::Ru;
}

std::wstring FileName(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

const wchar_t* Caption(const std::string& id) {
    if (id == "lumen") return Tr(L"Monochrome, the default");
    if (id == "oled") return Tr(L"Black page, dark theme only");
    if (id == "contrast") return Tr(L"Brighter text and lines");
    if (id == "sepia") return Tr(L"Warm tones, softer corners");
    return L"";
}

// Miniature of a skin: the page, a panel on it, a line of text and the accent.
class SkinPreview : public Widget {
public:
    SkinPreview() {
        width = Length::Px(44);
        height = Length::Px(28);
    }
    void Set(const skin::Skin* s, bool known) {
        known_ = known;
        if (!known) return;
        bool dark = Theme::Get().RequestedMode() == ThemeMode::Dark;
        if (s && s->mode != skin::Mode::Any) dark = s->mode == skin::Mode::Dark;
        colors_ = skin::Resolve(dark, Theme::Get().Accent(), s);
        Invalidate();
    }
    void Paint(Painter& p) override {
        if (!known_) {
            p.Fill(rect, P().bgDeep, 4);
            p.Stroke(rect, P().line, 4);
            p.Icon("file", rect.Center(20, 20), P().textTertiary);
            return;
        }
        const skin::Colors& c = colors_;
        p.Fill(rect, ToColor(c[skin::kBgBase]), 4);
        const Rect panel{rect.x + rect.w * 0.45f, rect.y + 4, rect.w * 0.55f - 4, rect.h - 8};
        p.Fill(panel, ToColor(c[skin::kBgPanel]), 2);
        p.Fill({rect.x + 5, rect.y + 7, rect.w * 0.3f, 2}, ToColor(c[skin::kTextPrimary]));
        p.Fill({rect.x + 5, rect.y + 12, rect.w * 0.22f, 2}, ToColor(c[skin::kTextSecondary]));
        p.Fill({rect.x + 5, rect.Bottom() - 9, 12, 4}, ToColor(c[skin::kAccent]), 2);
        p.Fill({panel.x + 4, panel.y + 4, panel.w - 8, 2}, ToColor(c[skin::kTextPrimary]));
        p.Stroke(rect, P().lineStrong, 4);
    }

private:
    skin::Colors colors_{};
    bool known_ = true;
};

class SkinPicker;

class SkinOption : public Widget {
public:
    SkinOption(SkinPicker* owner, int index);
    void Refresh();
    void OnLayout() override;
    void Paint(Painter& p) override;
    void OnClick() override;
    bool OnKeyDown(const KeyEvent& e) override;
    void DebugInfo(std::string& out) const override;
    const int index;

private:
    SkinPicker* owner_;
    SkinPreview* preview_ = nullptr;
    Label* name_ = nullptr;
    Label* caption_ = nullptr;
    IconView* check_ = nullptr;
    std::wstring path_;  // custom row: the whole path, its tooltip
};

class SkinPicker : public Widget, public Syncable {
public:
    SkinPicker(const std::vector<const Item*>& items, PageHost& page) : page_(page) {
        for (const Item* it : items) {
            if (it->key == "interface.skin") skinItem_ = it;
            if (it->key == "interface.skinFile") fileItem_ = it;
        }
        dir = Dir::Column;
        gap = 8;
        width = Length::Px(kControlW);
        auto* list = Add<Panel>();
        list->fill = [] { return P().bgDeep; };
        list->radius = 6;
        list->dir = Dir::Column;
        list->width = Length::Grow();
        list->pad = Insets::All(2);
        list->gap = 2;
        const auto& builtins = Skins::Get().Builtins();
        for (int i = 0; i < static_cast<int>(builtins.size()); ++i) options_.push_back(list->Add<SkinOption>(this, i));
        options_.push_back(list->Add<SkinOption>(this, kCustom));

        status_ = Add<Widget>();
        status_->dir = Dir::Column;
        status_->gap = 8;
        status_->width = Length::Grow();
        changed_ = Connect(Skins::Get().changed, [this] { Sync(); });
        Sync();
    }

    int Selected() const { return selected_; }

    void Choose(int index) {
        Config& c = Cfg();
        if (index == kCustom) {
            std::wstring file = c.GetWString("interface.skinFile");
            if (file.empty()) file = PickFile();
            if (file.empty()) return;
            c.SetWString("interface.skinFile", file);
            c.SetBool("interface.customSkin", true);
            return;
        }
        const auto& builtins = Skins::Get().Builtins();
        if (index < 0 || index >= static_cast<int>(builtins.size())) return;
        c.SetString("interface.skin", builtins[static_cast<size_t>(index)].id);
        c.SetBool("interface.customSkin", false);
    }

    void ChooseFile() {
        const std::wstring file = PickFile();
        if (file.empty()) return;
        Cfg().SetWString("interface.skinFile", file);
        Cfg().SetBool("interface.customSkin", true);
    }

    // Arrow keys move between the options, like a radio group.
    void Step(int from, int delta) {
        int pos = 0;
        for (int i = 0; i < static_cast<int>(options_.size()); ++i)
            if (options_[static_cast<size_t>(i)]->index == from) pos = i;
        pos = std::clamp(pos + delta, 0, static_cast<int>(options_.size()) - 1);
        if (Host* h = GetHost()) h->SetFocus(options_[static_cast<size_t>(pos)], true);
    }

    void Sync() override {
        const Config& c = Cfg();
        const bool custom = c.GetBool("interface.customSkin") && !c.GetWString("interface.skinFile").empty();
        selected_ = 0;
        const std::string want = c.GetString("interface.skin");
        const auto& builtins = Skins::Get().Builtins();
        for (int i = 0; i < static_cast<int>(builtins.size()); ++i)
            if (builtins[static_cast<size_t>(i)].id == want) selected_ = i;
        if (custom) selected_ = kCustom;
        for (SkinOption* o : options_) o->Refresh();
        BuildStatus();
        InvalidateLayout();
        if (page_.relayout) page_.relayout();
    }

private:
    std::wstring PickFile() {
        Host* host = page_.host;
        const settings::PathKind kind = fileItem_ ? fileItem_->pathKind : settings::PathKind::File;
        return PickPath(host ? host->Hwnd() : nullptr, kind, Cfg().GetWString("interface.skinFile"));
    }

    Label* AddNote(Widget* parent, std::wstring text, std::wstring tip) {
        auto* line = parent->Add<Widget>();
        line->dir = Dir::Row;
        line->gap = 8;
        line->width = Length::Grow();
        line->align = Align::Start;
        auto* icon = line->Add<IconView>("warning", 20, Tone::Secondary);
        icon->pad = {0, 1, 0, 0};
        auto* l = line->Add<Label>(std::move(text), TextStyle::Small, Tone::Secondary);
        l->wrap = true;
        l->width = Length::Grow();
        line->tooltip = std::move(tip);
        line->interactive = !line->tooltip.empty();  // hover shows the details
        return l;
    }

    void BuildStatus() {
        status_->ClearChildren();
        bool problem = false;
        const Skins& s = Skins::Get();
        if (s.CustomActive() && (s.Failed() || !s.Warnings().empty())) {
            std::wstring tip;
            for (const std::string& w : s.Warnings()) tip += (tip.empty() ? L"" : L"\n") + json::ToWide(w);
            std::wstring text = s.Failed() ? std::wstring(Tr(L"The skin file could not be used, so Lumen is shown."))
                                           : Fmt(Tr(L"Notes on the skin file: {0}."), {std::to_wstring(s.Warnings().size())});
            if (!s.LogPath().empty()) text += L" " + Fmt(Tr(L"Details are in {0}."), {FileName(s.LogPath())});
            AddNote(status_, std::move(text), std::move(tip));
            problem = s.Failed();
        }
        const auto issues = skin::CheckContrast(Theme::Get().Skin(), Theme::Get().Accent());
        if (!issues.empty()) {
            std::wstring tip;
            for (const skin::ContrastIssue& i : issues) tip += (tip.empty() ? L"" : L"\n") + json::ToWide(skin::Describe(i));
            AddNote(status_,
                    Fmt(Tr(L"Low contrast in this skin ({0} color pairs): text needs 4.5:1, icons 3:1."),
                        {std::to_wstring(issues.size())}),
                    std::move(tip));
            problem = true;
        }
        if (problem) {
            auto* bar = status_->Add<Widget>();
            bar->dir = Dir::Row;
            bar->width = Length::Grow();
            auto* reset = bar->Add<Button>(Tr(L"Reset"), ButtonKind::Secondary);
            reset->id = "skin-reset";
            reset->minW = kSmallButtonW;
            reset->tooltip = Tr(L"Go back to the Lumen skin");
            reset->onClick = [] {
                Cfg().SetString("interface.skin", "lumen");
                Cfg().SetBool("interface.customSkin", false);
            };
        }
        status_->visible = !status_->Children().empty();
    }

    PageHost& page_;
    const Item* skinItem_ = nullptr;
    const Item* fileItem_ = nullptr;
    std::vector<SkinOption*> options_;
    Widget* status_ = nullptr;
    int selected_ = 0;
    ScopedConnection changed_;
};

SkinOption::SkinOption(SkinPicker* owner, int i) : index(i), owner_(owner) {
    interactive = true;
    focusable = true;
    dir = Dir::Row;
    align = Align::Center;
    gap = 12;
    width = Length::Grow();
    height = Length::Px(48);
    pad = {8, 0, 10, 0};
    id = index == kCustom ? "skin.custom" : "skin." + Skins::Get().Builtins()[static_cast<size_t>(index)].id;
    preview_ = Add<SkinPreview>();
    auto* text = Add<Widget>();
    text->dir = Dir::Column;
    text->width = Length::Grow();
    name_ = text->Add<Label>(L"", TextStyle::Body, Tone::Primary);
    name_->width = Length::Grow();
    caption_ = text->Add<Label>(L"", TextStyle::Small, Tone::Tertiary);
    caption_->width = Length::Grow();
    if (index == kCustom) {
        auto* pick = Add<IconButton>("folder", Tr(L"Choose a skin file"), 32, 20);
        pick->id = "skin-choose-file";
        pick->focusable = true;
        pick->onClick = [this] { owner_->ChooseFile(); };
    }
    // Always laid out, so selecting a row does not narrow its text.
    check_ = Add<IconView>("check_selected", 20, Tone::Primary);
    check_->color = [this] { return owner_->Selected() == index ? P().textPrimary : Color::Clear(); };
    Refresh();
}

void SkinOption::Refresh() {
    const Config& c = Cfg();
    if (index == kCustom) {
        const std::wstring file = c.GetWString("interface.skinFile");
        name_->SetText(Tr(L"Custom file…"));
        caption_->SetText(file.empty() ? std::wstring(Tr(L"No file chosen")) : FileName(file));
        path_ = file;
        const bool active = owner_->Selected() == kCustom && !Skins::Get().Failed();
        preview_->Set(active ? Theme::Get().Skin() : nullptr, active);
    } else {
        const BuiltinSkin& b = Skins::Get().Builtins()[static_cast<size_t>(index)];
        name_->SetText(Russian() && !b.nameRu.empty() ? b.nameRu : b.name);
        caption_->SetText(Caption(b.id));
        preview_->Set(b.skin.get(), true);
    }
    InvalidateLayout();
}

void SkinOption::OnLayout() {
    Widget::OnLayout();
    // A cut name or caption (narrow window, large scale) shows in full in the tooltip.
    auto cut = [](const Label* l) {
        return gfx::Text::Get().Measure(l->Text(), l->style).width > l->rect.w + 0.5f;
    };
    if (!path_.empty())
        tooltip = path_;
    else if (cut(name_) || cut(caption_))
        tooltip = name_->Text() + L"\n" + caption_->Text();
    else
        tooltip.clear();
}

void SkinOption::Paint(Painter& p) {
    if (owner_->Selected() == index)
        p.Fill(rect, P().accentDim, 4);
    else if (IsHot())
        p.Fill(rect, P().hover, 4);
    PaintFocusRing(p, rect, 4);
}

void SkinOption::OnClick() {
    owner_->Choose(index);
}

bool SkinOption::OnKeyDown(const KeyEvent& e) {
    if (e.key == VK_UP || e.key == VK_DOWN) {
        owner_->Step(index, e.key == VK_UP ? -1 : 1);
        return true;
    }
    return false;
}

void SkinOption::DebugInfo(std::string& out) const {
    Widget::DebugInfo(out);
    if (owner_->Selected() == index) out += " selected";
}

}  // namespace

std::unique_ptr<Widget> MakeSkinPicker(const std::vector<const Item*>& items, PageHost& page) {
    const Item* skinItem = nullptr;
    for (const Item* it : items)
        if (it->key == "interface.skin") skinItem = it;
    if (!skinItem) return nullptr;
    return std::make_unique<SettingRow>(*skinItem, std::make_unique<SkinPicker>(items, page), 0.0f);
}

}  // namespace app::sui
