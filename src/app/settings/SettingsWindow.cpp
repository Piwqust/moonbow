// Settings window (design/ref/Settings-*.png): search + grouped category list on the left, the
// selected category on the right. Rows are generated from core/SettingsSchema; writes go through
// Config, which the app applies live.
#include "app/App.h"
#include "app/Dialogs.h"
#include "app/I18n.h"
#include "library/MediaTypes.h"
#include "app/MainWindow.h"
#include "app/SettingsActions.h"
#include "app/settings/SettingsUi.h"
#include "app/settings/ShortcutStore.h"
#include "core/Config.h"
#include "gfx/Gpu.h"

#include <cmath>
#include <set>

namespace app {

using namespace ui;
using namespace sui;
using settings::Category;
using settings::Item;

namespace {

constexpr float kWindowW = 1048, kWindowH = 865;  // the frame without its drop shadow
constexpr float kHeaderH = 50;
// The design's nav column is 216; it grows when labels are longer (Russian "Конфиденциальность").
float NavColumnWidth() {
    // Measured over both languages, so the page doesn't shift when the language changes.
    float w = 0;
    for (const Category& c : settings::Categories())
        for (settings::Lang l : {settings::Lang::Ru, settings::Lang::En})
            w = std::max(w, gfx::Text::Get().Measure(c.Label(l), TextStyle::Body).width);
    // panel margin 8 + list padding 6 + icon column 44 + text + 8, mirrored on the right.
    return std::max(216.0f, std::ceil(8 + 6 + 44 + w + 8 + 6 + 8));
}

// Basic mode lists only the categories that have basic items.
bool HasBasicItems(const Category& c) {
    for (const settings::Section& s : c.sections)
        for (const Item& it : s.items)
            if (it.basic && !it.hidden) return true;
    return false;
}

bool CategoryShown(const Category& c) {
    return !BasicMode() || HasBasicItems(c);
}

Widget* FindById(Widget* w, const std::string& id) {
    if (!w) return nullptr;
    if (w->id == id) return w;
    for (auto& c : w->Children())
        if (Widget* hit = FindById(c.get(), id)) return hit;
    return nullptr;
}

class SearchField : public TextBox {
public:
    SearchField() : TextBox(Tr(L"Search"), "search") {
        width = Length::Px(200);
        height = Length::Px(34);
    }
    void Paint(Painter& p) override {
        background = P().bgRaised;
        TextBox::Paint(p);
    }
};

class CloseButton : public IconButton {
public:
    CloseButton() : IconButton("close", Tr(L"Close"), 32, 24) {}
    void Paint(Painter& p) override {
        if (IsHot()) p.Fill(rect, IsPressed() ? P().danger.Fade(0.8f) : P().danger, 4);
        PaintFocusRing(p, rect, 4);
        p.Icon(icon, rect.Center(24, 24), IsHot() ? P().textPrimary : P().textSecondary);
    }
};

class NavGroupLabel : public PadLabel {
public:
    explicit NavGroupLabel(std::wstring text) : PadLabel(std::move(text), TextStyle::Body, Tone::Tertiary) {
        width = Length::Grow();
        height = Length::Px(33);
        pad = {10, 6, 10, 7};  // a 20-DIP line box: 17 cut the descenders
    }
};

class SettingsWindow;

class NavItem : public Widget {
public:
    NavItem(SettingsWindow* window, const Category& cat) : cat_(cat), window_(window) {
        interactive = true;
        focusable = true;
        width = Length::Grow();
        height = Length::Px(40);
        id = "nav." + cat.id;
    }
    void SetSelected(bool s) {
        selected_ = s;
        Invalidate();
    }
    const Category& Cat() const { return cat_; }
    void Describe(A11y& a) override {
        a.role = Role::ListItem;
        a.name = cat_.Label(Lang());
        a.selected = selected_ ? 1 : 0;
    }
    void Paint(Painter& p) override {
        const Palette& c = P();
        const float h = HoverFade(hover_, IsHot() && !selected_);
        if (selected_)
            p.Fill(rect, c.bgSelected, 2);
        else if (IsPressed())
            p.Fill(rect, c.press, 2);
        else if (h > 0)
            p.Fill(rect, c.hover.Fade(h), 2);
        PaintFocusRing(p, rect, 2);
        const Rect icon{rect.x + 8, rect.Cy() - 12, 24, 24};
        const Color fg = Color::Mix(c.textSecondary, c.textPrimary, h);
        if (selected_)
            p.Icon(cat_.icon + "_active", icon, c.accent, c.accent);
        else
            p.Icon(cat_.icon, icon, fg);
        p.Text(cat_.Label(Lang()), {rect.x + 44, rect.y, rect.w - 52, rect.h}, TextStyle::Body,
               selected_ ? c.textPrimary : fg);
    }
    void OnClick() override;
    bool OnKeyDown(const KeyEvent& e) override;

private:
    const Category& cat_;
    SettingsWindow* window_;
    bool selected_ = false;
    AnimFloat hover_;
};

class AdvancedExpander : public Widget {
public:
    AdvancedExpander() {
        interactive = true;
        focusable = true;
        width = Length::Grow();
        height = Length::Px(38);
        id = "advanced";
    }
    bool open = false;
    std::function<void()> onToggle;
    void Paint(Painter& p) override {
        const Palette& c = P();
        p.Fill(rect, IsHot() ? Color::Mix(c.bgPanel, c.bgSelected, 0.5f) : c.bgPanel.Fade(0.95f), 2);
        PaintFocusRing(p, rect, 2);
        const std::wstring& text = settings::AdvancedOptionsLabel(Lang());
        const Color fg = IsHot() ? c.textPrimary : c.textSecondary;
        const float tw = p.Measure(text, TextStyle::Body).width;
        const float x = rect.Cx() - (tw + 34) / 2;
        p.Text(text, {x, rect.y, tw + 1, rect.h}, TextStyle::Body, fg, HAlign::Leading, VAlign::Center, 0);
        p.Icon(open ? "chevron_down" : "chevron_right", {x + tw + 10, rect.Cy() - 12, 24, 24}, fg);
    }
    void OnClick() override {
        if (onToggle) onToggle();
    }
};

class SettingsWindow : public Host {
public:
    bool Open(const std::string& category) {
        HostOptions o;
        o.title = std::wstring(Tr(L"Settings")) + L" — Moonbow";
        MainWindow* main = TheApp().main();
        o.owner = main ? main->Hwnd() : nullptr;
        o.showInTaskbar = o.owner == nullptr;
        o.minWidth = 760;
        o.minHeight = 520;
        o.width = kWindowW;  // Host::Create fits it to the owner's work area on small screens
        o.height = kWindowH;
        if (!Create(o)) return false;

        category_ = settings::FindCategory(category) ? category : std::string("general");
        page_.host = this;
        page_.state = &state_;
        page_.relayout = [this] { RequestLayout(); };
        page_.runAction = [this](const std::string& action, const Item& item) { RunAction(action, item); };
        BuildUi();

        configToken_ = Cfg().Subscribe("", [this](const std::string&) { ScheduleSync(); });
        langConn_ = Connect(TheApp().languageChanged, [this] {
            Post([this] {
                SetTitle(std::wstring(Tr(L"Settings")) + L" — Moonbow");
                // Same page, same place: the scroll offset survives the rebuild.
                const float offset = content_ ? content_->Offset() : 0.0f;
                BuildUi();
                pendingScroll_ = offset;
            });
        });
        onKey = [this](const KeyEvent& e) { return OnWindowKey(e); };
        onCloseRequest = [this] {
            SetFocus(nullptr);  // commits a field being edited
            Cfg().Flush();
            return true;
        };
        Show();
        if (gfx::Gpu::Get().Ready()) OnDeviceReady();
        return true;
    }

    ~SettingsWindow() override { Detach(); }

    // In a wide window the rows keep a readable width (label and control ~900 DIP apart at most):
    // the side padding grows instead.
    void FitPage() {
        Widget* page = content_ ? content_->Content() : nullptr;
        if (!page) return;
        const float w = content_->rect.w > 0 ? content_->rect.w : ClientSize().w - 280;
        const float side = 24 + std::max(0.0f, (w - 48 - kMaxPageW) / 2);
        page->pad = {side, 20, side, 40};
        page->InvalidateLayout();
        // The header title starts where the page column starts (8 = the header's own padding).
        if (titleGap_ && titleGap_->width.value != side + 8) {
            titleGap_->width = Length::Px(side + 8);
            titleGap_->InvalidateLayout();
        }
    }
    void OnResize() override { FitPage(); }
    void OnLayoutDone() override {
        Widget* page = content_ ? content_->Content() : nullptr;
        const float w = content_ ? content_->rect.w : 0;
        const float side = 24 + std::max(0.0f, (w - 48 - kMaxPageW) / 2);
        if (page && w > 0 && std::abs(page->pad.l - side) > 0.5f) FitPage();  // first layout after a build
        if (pendingScroll_ >= 0 && content_ && content_->MaxOffset() > 0) {
            content_->ScrollTo(std::min(pendingScroll_, content_->MaxOffset()), false);
            pendingScroll_ = -1;
        }
    }
    static constexpr float kMaxPageW = 880;

    // Stops listening to the app; called when the window is destroyed.
    void Detach() {
        if (configToken_) Cfg().Unsubscribe(configToken_);
        configToken_ = 0;
        langConn_.Reset();
    }

    // Before Open: the first page is the search for these words.
    void SetQuery(std::wstring q) { query_ = std::move(q); }
    void Search(const std::wstring& q) {
        if (search_) search_->SetText(q, true);
    }

    void ShowCategory(const std::string& id) {
        const Category* cat = settings::FindCategory(id);
        if (!cat) return;
        category_ = CategoryShown(*cat) ? id : std::string("general");
        if (search_ && !search_->Text().empty()) search_->SetText(L"");
        query_.clear();
        RefreshNav();
        BuildPage();
    }

    void StepCategory(int delta) {
        const auto& cats = settings::Categories();
        int i = 0;
        for (size_t k = 0; k < cats.size(); ++k)
            if (cats[k].id == category_) i = static_cast<int>(k);
        const int last = static_cast<int>(cats.size()) - 1;
        const int step = delta < 0 ? -1 : 1;
        for (int left = std::abs(delta), k = i + step; left > 0 && k >= 0 && k <= last; k += step)
            if (CategoryShown(cats[static_cast<size_t>(k)])) {
                i = k;
                --left;
            }
        ShowCategory(cats[static_cast<size_t>(i)].id);
        if (Widget* item = FindById(Root(), "nav." + category_)) SetFocus(item, true);
    }

    const std::string& CurrentCategory() const { return category_; }

private:
    void BuildUi() {
        // The close button is the last child, so Tab reaches it after the nav and the page
        // (reading order), although it sits in the header: a stack lays it over the header's end.
        auto root = std::make_unique<Panel>();
        root->fill = [] { return P().bgBase; };
        root->dir = Dir::Stack;
        root->align = Align::End;
        auto* frame = root->Add<Widget>();
        frame->dir = Dir::Column;
        frame->width = Length::Grow();
        frame->height = Length::Grow();

        // Header: search (over the nav column), category title, close. Drags the window.
        auto* header = frame->Add<Panel>();
        header->fill = [] { return P().bgDeep; };
        header->dir = Dir::Row;
        header->align = Align::Center;
        header->width = Length::Grow();
        header->height = Length::Px(kHeaderH);
        header->pad = {8, 0, 14, 0};
        header->ncRole = NcRole::Caption;
        search_ = header->Add<SearchField>();
        const float navW = NavColumnWidth();
        search_->width = Length::Px(navW - 16);
        search_->SetText(query_);
        search_->onChange = [this](const std::wstring& t) {
            query_ = t;
            RefreshNav();
            BuildPage();
        };
        search_->onEscape = [this] {
            if (!search_->Text().empty())
                search_->SetText(L"", true);
            else
                Close();
        };
        titleGap_ = header->Add<Widget>();
        titleGap_->width = Length::Px(32);
        title_ = header->Add<Label>(L"", TextStyle::Large, Tone::Secondary);
        title_->width = Length::Grow();
        title_->maxW = kMaxPageW;
        auto* mode = header->Add<Segmented>(
            std::vector<ui::Choice>{{Tr(L"Basic"), "basic", {}}, {Tr(L"All"), "all", {}}}, BasicMode() ? 0 : 1);
        mode->id = "settings-mode";
        mode->segmentWidth = 88;
        mode->onChange = [this](int i) { SetBasicMode(i == 0); };
        header->Add<Widget>()->width = Length::Px(16 + 32 + 8);  // under the close button

        auto* body = frame->Add<Widget>();
        body->dir = Dir::Row;
        body->width = Length::Grow();
        body->height = Length::Grow();

        auto* left = body->Add<Widget>();
        left->dir = Dir::Column;
        left->width = Length::Px(navW);
        left->height = Length::Grow();
        left->pad = Insets::All(8);
        auto* navPanel = left->Add<Panel>();
        navPanel->fill = [] { return P().bgPanel.Fade(0.95f); };
        navPanel->radius = 6;
        navPanel->dir = Dir::Column;
        navPanel->width = Length::Grow();
        navPanel->height = Length::Grow();
        auto* navScroll = navPanel->Add<ScrollView>();
        navScroll->width = Length::Grow();
        navScroll->height = Length::Grow();
        auto* nav = navScroll->SetContent<Widget>();
        nav->dir = Dir::Column;
        nav->pad = Insets::All(6);
        nav->gap = 8;
        nav->width = Length::Grow();
        navItems_.clear();
        navGroups_.clear();
        for (const settings::Group& g : settings::Groups()) {
            navGroups_.push_back({nav->Add<NavGroupLabel>(g.Label(Lang())), g.id});
            for (const Category& c : settings::Categories())
                if (c.group == g.id) navItems_.push_back(nav->Add<NavItem>(this, c));
        }
        // Version, libmpv and licences.
        auto* aboutRow = navPanel->Add<Widget>();
        aboutRow->dir = Dir::Column;  // under a line: not one of the categories above
        aboutRow->width = Length::Grow();
        aboutRow->pad = {16, 0, 16, 10};
        aboutRow->gap = 8;
        aboutRow->Add<Divider>();
        auto* about = aboutRow->Add<LinkButton>(Tr(L"About Moonbow"), TextStyle::Small);
        about->id = "about";
        about->onClick = [this] { ShowAboutDialog(this); };

        content_ = body->Add<ScrollView>();

        auto* closeBox = root->Add<Widget>();
        closeBox->dir = Dir::Row;
        closeBox->align = Align::Center;
        closeBox->height = Length::Px(kHeaderH);
        closeBox->pad = {0, 0, 14, 0};
        closeBox->ncRole = NcRole::Caption;
        auto* close = closeBox->Add<CloseButton>();
        close->onClick = [this] { Close(); };
        SetRoot(std::move(root));
        RefreshNav();
        BuildPage();
    }

    void SetBasicMode(bool basic) {
        Cfg().SetString("interface.settingsMode", basic ? "basic" : "all");
        if (auto* mode = dynamic_cast<Segmented*>(FindById(Root(), "settings-mode"))) mode->SetSelected(basic ? 0 : 1);
        if (const Category* c = settings::FindCategory(category_); c && !CategoryShown(*c)) category_ = "general";
        RefreshNav();
        BuildPage();
    }

    void RefreshNav() {
        const bool searching = !query_.empty();
        for (NavItem* n : navItems_) n->visible = CategoryShown(n->Cat());
        for (auto& [label, group] : navGroups_) {
            label->visible = false;
            for (NavItem* n : navItems_)
                if (n->visible && n->Cat().group == group) label->visible = true;
        }
        // One Tab stop for the whole list (the selected item); Up/Down move inside it.
        for (NavItem* n : navItems_) {
            const bool sel = !searching && n->Cat().id == category_;
            n->SetSelected(sel);
            n->focusable = sel || (searching && n == navItems_.front());
        }
        if (!title_) return;
        if (searching) {
            title_->SetText(Tr(L"Search results"));
        } else if (const Category* c = settings::FindCategory(category_)) {
            title_->SetText(c->Label(Lang()));
        }
    }

    void BuildPage() {
        if (!content_) return;
        CloseAllPopups();  // menus of the old rows point at widgets about to go
        auto* page = content_->SetContent<Widget>();
        page->dir = Dir::Column;
        page->width = Length::Grow();
        page->gap = 20;
        FitPage();
        if (!query_.empty())
            BuildSearch(page);
        else if (const Category* c = settings::FindCategory(category_))
            BuildCategory(page, *c);
        content_->ScrollTo(0, false);
        RequestLayout();
    }

    // Adds rows for `items`, drawing items that share a `group` as one widget.
    void AddItems(Widget* parent, const std::vector<const Item*>& items) {
        for (size_t i = 0; i < items.size(); ++i) {
            const Item* it = items[i];
            if (!it->group.empty()) {
                std::vector<const Item*> members;
                size_t j = i;
                for (; j < items.size() && items[j]->group == it->group; ++j)
                    if (!items[j]->hidden) members.push_back(items[j]);
                i = j - 1;
                if (auto w = MakeGroupRow(it->group, members, page_)) parent->AddChild(std::move(w));
                continue;
            }
            if (auto w = MakeItemRow(*it, page_)) parent->AddChild(std::move(w));
        }
    }

    Widget* AddSection(Widget* page, const std::wstring& title) {
        auto* sec = page->Add<Widget>();
        sec->dir = Dir::Column;
        sec->width = Length::Grow();
        if (!title.empty()) sec->AddChild(MakeHeading(title));
        return sec;
    }

    void BuildCategory(Widget* page, const Category& cat) {
        const settings::Lang lang = Lang();
        const bool basic = BasicMode();
        bool anyAdvanced = false;
        for (const settings::Section& s : cat.sections) {
            std::vector<const Item*> normal;
            for (const Item& it : s.items) {
                if (it.hidden || (basic && !it.basic)) continue;
                if (it.advanced) anyAdvanced = true;
                else normal.push_back(&it);
            }
            if (normal.empty()) continue;
            Widget* sec = AddSection(page, s.Title(lang));
            AddItems(sec, normal);
            if (cat.id == "controls" && s.id == "shortcuts") {
                auto editor = MakeShortcutEditor(page_);
                editor->visible = state_.Open("shortcuts");
                sec->AddChild(std::move(editor));
                if (state_.Open("shortcuts"))
                    if (auto* b = dynamic_cast<Button*>(FindById(sec, "edit-shortcuts")))
                        b->SetText(Tr(L"Hide keyboard shortcuts"));
            }
        }
        if (anyAdvanced) {
            const std::string key = "advanced." + cat.id;
            auto* expander = page->Add<AdvancedExpander>();
            auto* adv = page->Add<Widget>();
            adv->dir = Dir::Column;
            adv->width = Length::Grow();
            adv->gap = 20;
            adv->visible = state_.Open(key);
            expander->open = adv->visible;
            expander->onToggle = [this, key, expander, adv] {
                const bool open = !adv->visible;
                adv->visible = open;
                expander->open = open;
                state_.open[key] = open;
                RequestLayout();
                RequestRender();
            };
            for (const settings::Section& s : cat.sections) {
                std::vector<const Item*> items;
                bool hasNormal = false;
                for (const Item& it : s.items) {
                    if (it.hidden) continue;
                    if (it.advanced) items.push_back(&it);
                    else hasNormal = true;
                }
                if (items.empty()) continue;
                Widget* sec = AddSection(adv, hasNormal ? std::wstring() : s.Title(lang));
                AddItems(sec, items);
            }
        }
        // Reset the whole category.
        auto* footer = page->Add<Widget>();
        footer->dir = Dir::Row;
        footer->width = Length::Grow();
        footer->align = Align::Center;
        footer->pad = {0, 8, 0, 0};
        if (basic) {
            auto* more = footer->Add<LinkButton>(Tr(L"Show all settings"));
            more->id = "show-all";
            more->trailingIcon = "chevron_right";
            more->onClick = [this] { Post([this] { SetBasicMode(false); }); };  // the rebuild deletes this link
        }
        footer->Add<Spacer>();
        // Ghost: a rare, undoable-only-by-hand action must not be the loudest thing on every page.
        auto* reset = footer->Add<Button>(Tr(L"Reset to defaults"), ButtonKind::Ghost, "refresh");
        reset->id = "reset-category";
        const std::string id = cat.id;
        const std::wstring label = cat.Label(lang);
        reset->onClick = [this, id, label] {
            Confirm(this, Tr(L"Reset to defaults?"),
                    Fmt(Tr(L"All settings in “{0}” return to their default values."), {label}), Tr(L"Reset"),
                    [id] { Cfg().ResetCategory(id); });
        };
    }

    // The searched words in the row labels (VIS-17); the section titles stay plain.
    static void Underline(Widget* w, const std::wstring& folded) {
        if (auto* l = dynamic_cast<Label*>(w); l && l->style == TextStyle::Body && !l->wrap)
            l->marks = library::MatchRanges(l->Text(), folded);
        for (auto& c : w->Children()) Underline(c.get(), folded);
    }

    void BuildSearch(Widget* page) {
        const std::vector<const Item*> hits = settings::Search(query_);
        std::set<const Item*> hit(hits.begin(), hits.end());
        // A hit inside a group ("60 Hz") brings the whole group (the equalizer).
        std::set<std::string> groups;
        for (const Item* it : hits)
            if (!it->group.empty()) groups.insert(it->category + "/" + it->group);
        bool any = false;
        const std::wstring folded = library::SearchFold(query_);
        for (const Category& cat : settings::Categories()) {
            std::vector<const Item*> items;
            for (const settings::Section& s : cat.sections)
                for (const Item& it : s.items) {
                    if (it.hidden) continue;
                    if (hit.count(&it) || (!it.group.empty() && groups.count(cat.id + "/" + it.group))) items.push_back(&it);
                }
            if (items.empty()) continue;
            any = true;
            Widget* sec = AddSection(page, cat.Label(Lang()));
            AddItems(sec, items);
            Underline(sec, folded);
        }
        if (!any) {
            auto* none = page->Add<PadLabel>(Fmt(Tr(L"Nothing found for “{0}”"), {query_}), TextStyle::Body, Tone::Tertiary);
            none->pad = {0, 12, 0, 0};
        }
    }

    void ScheduleSync() {
        if (syncPosted_) return;
        syncPosted_ = true;
        Post([this] {
            syncPosted_ = false;
            if (content_) SyncTree(content_->Content());
            RequestLayout();
        });
    }

    void RunAction(const std::string& action, const Item& item) {
        if (action == "edit-shortcuts") {
            const bool open = !state_.Open("shortcuts");
            state_.open["shortcuts"] = open;
            if (Widget* editor = FindById(Root(), "shortcut-editor")) editor->visible = open;
            if (auto* b = dynamic_cast<Button*>(FindById(Root(), "edit-shortcuts")))
                b->SetText(open ? std::wstring(Tr(L"Hide keyboard shortcuts")) : item.ActionLabel(Lang()));
            RequestLayout();
            return;
        }
        if (action == "reset-shortcuts") {
            Confirm(this, Tr(L"Reset all shortcuts?"), Tr(L"Every keyboard shortcut returns to its default."),
                    Tr(L"Reset"), [this] {
                        ResetAllBindings();
                        ShowToast(this, Tr(L"Shortcuts reset"), "hotkeys");
                    });
            return;
        }
        if (action == "clear-history") {
            Confirm(this, Tr(L"Clear history?"), item.Description(Lang()), item.ActionLabel(Lang()),
                    [action] { RunSettingsAction(action); });
            return;
        }
        if (action == "reset-picture") {
            for (const char* key : {"video.hue", "video.brightness", "video.contrast", "video.saturation", "video.gamma"})
                Cfg().Reset(key);
            RunSettingsAction(action);
            return;
        }
        if (action == "rescan-plugins") {
            RescanAddons();
            ShowToast(this, Tr(L"Plugins rescanned"), "refresh");
            return;
        }
        if (!RunSettingsAction(action)) ShowToast(this, Tr(L"Not available yet"), "info");
    }

    bool OnWindowKey(const KeyEvent& e) {
        if (e.key == VK_ESCAPE && !e.mods.ctrl && !e.mods.alt && !e.mods.shift) {
            Close();
            return true;
        }
        if (e.key == 'F' && e.mods.ctrl && search_) {
            SetFocus(search_, true);
            search_->SelectAll();
            return true;
        }
        if ((e.key == VK_NEXT || e.key == VK_PRIOR) && e.mods.ctrl) {
            StepCategory(e.key == VK_NEXT ? 1 : -1);
            return true;
        }
        return false;
    }

    std::string category_;
    std::wstring query_;
    UiState state_;
    PageHost page_;
    Config::Token configToken_ = 0;
    ScopedConnection langConn_;
    bool syncPosted_ = false;

    SearchField* search_ = nullptr;
    Widget* titleGap_ = nullptr;
    float pendingScroll_ = -1;
    Label* title_ = nullptr;
    ScrollView* content_ = nullptr;
    std::vector<NavItem*> navItems_;
    std::vector<std::pair<NavGroupLabel*, std::string>> navGroups_;
};

void NavItem::OnClick() {
    window_->ShowCategory(cat_.id);
}

bool NavItem::OnKeyDown(const KeyEvent& e) {
    if (e.key == VK_UP || e.key == VK_DOWN) {
        window_->StepCategory(e.key == VK_UP ? -1 : 1);
        return true;
    }
    if (e.key == VK_HOME || e.key == VK_END) {
        window_->StepCategory(e.key == VK_HOME ? -100 : 100);
        return true;
    }
    return false;
}

// Raw on purpose: a window still open at exit is left to the process teardown instead of being
// destroyed after the UI framework's statics.
SettingsWindow* g_window = nullptr;

}  // namespace

void ShowSettingsWindow(const std::string& category, const std::wstring& query) {
    if (g_window && g_window->Hwnd()) {
        if (!category.empty()) g_window->ShowCategory(category);
        if (!query.empty()) g_window->Search(query);
        HWND h = g_window->Hwnd();
        ui::Host::Activate(h);
        return;
    }
    auto* w = new SettingsWindow();
    w->onDestroyed = [w] {
        w->Detach();
        if (g_window == w) g_window = nullptr;
        // Deleting a Host from inside its own WM_DESTROY is not safe; do it after the message.
        // Without a main window the app is quitting and the process cleans up.
        MainWindow* main = TheApp().main();
        if (main && main->Hwnd()) main->Post([w] { delete w; });
    };
    g_window = w;
    w->SetQuery(query);
    if (!w->Open(category)) {
        g_window = nullptr;
        delete w;
    }
}

void CloseSettingsWindow() {
    if (g_window && g_window->Hwnd()) g_window->Close();
}

}  // namespace app
