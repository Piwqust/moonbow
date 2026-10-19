#include "ui/Accessibility.h"

#include "ui/Controls.h"
#include "ui/Host.h"

#include <ole2.h>
#include <uiautomation.h>

#include <typeinfo>
#include <unordered_map>

namespace ui::a11y {

namespace {

bool g_connected = false;  // a client asked for a window: uiautomationcore.dll is loaded
bool g_recordOnce = false;

// Leaked on purpose: widgets of static objects die after a static map would.
// Per live widget that has had an element: a serial no other widget gets, so an element of a
// destroyed widget (or of a new one at the same address) is recognised as gone.
std::unordered_map<const Widget*, int>& Serials() {
    static auto* m = new std::unordered_map<const Widget*, int>;
    return *m;
}
int g_nextSerial = 0;
// What each widget painted in the last recorded frame of its window: the names of custom widgets.
std::unordered_map<const Widget*, std::wstring>& Painted() {
    static auto* m = new std::unordered_map<const Widget*, std::wstring>;
    return *m;
}

bool IsLeaf(Role r) {
    switch (r) {
        case Role::Button: case Role::CheckBox: case Role::ComboBox: case Role::Edit: case Role::Slider:
        case Role::ListItem: case Role::MenuItem: case Role::TabItem:
            return true;
        default:
            return false;
    }
}

bool HasInteractiveDescendant(const Widget* w) {
    for (auto& c : w->Children())
        if (c->visible && (c->interactive || c->focusable || HasInteractiveDescendant(c.get()))) return true;
    return false;
}

std::wstring PaintedBy(const Widget* w) {
    auto it = Painted().find(w);
    if (it != Painted().end()) return it->second;
    // Not painted yet (scrolled out of view): a label still knows its text.
    if (auto* l = dynamic_cast<const Label*>(w)) return l->Text();
    return {};
}

// The texts a widget and its visible descendants paint, in tree order; `labels` leaves out the
// controls among the descendants.
void PaintedDeep(const Widget* w, std::wstring& out, bool labels = false) {
    if (out.size() > 300) return;
    std::wstring own = PaintedBy(w);
    if (!own.empty()) out += (out.empty() ? L"" : L" ") + own;
    for (auto& c : w->Children())
        if (c->visible && !(labels && (c->interactive || c->focusable))) PaintedDeep(c.get(), out, labels);
}

// The label right before a control (settings rows: "Label [control]", or a "Label [value]" row
// above a slider), looking up through wrappers the control is the first child of. A control
// right before it means a row of controls: no label. A list takes its section title, which may
// be a button.
std::wstring LabelBefore(const Widget* w, bool buttons = false) {
    for (int depth = 0; w && w->Parent() && depth < 3; ++depth, w = w->Parent()) {
        const auto& sib = w->Parent()->Children();
        size_t at = 0;
        while (at < sib.size() && sib[at].get() != w) ++at;
        while (at > 0 && !sib[at - 1]->visible) --at;
        if (at == 0) continue;
        const Widget* s = sib[at - 1].get();
        if (!buttons && (s->interactive || s->focusable)) return {};
        std::wstring t;
        PaintedDeep(s, t, !buttons);
        return t;
    }
    return {};
}

std::wstring Widen(const std::string& s) {
    std::wstring out;
    for (char c : s) out += static_cast<wchar_t>(static_cast<unsigned char>(c));
    return out;
}

Host* HostOf(HWND hwnd) {
    if (!hwnd) return nullptr;
    for (Host* h : Host::All())
        if (h->Hwnd() == hwnd) return h;
    return nullptr;
}

bool IsPopupRoot(const Widget* w) {
    Host* h = w->GetHost();
    return !w->Parent() && h && h->Root() != w;
}

A11y Describe(Widget* w) {
    A11y a;
    if (auto* l = dynamic_cast<Label*>(w)) {
        a.role = l->Text().empty() ? Role::Skip : Role::Text;
        a.name = l->Text();
    } else if (auto* t = dynamic_cast<Toggle*>(w)) {
        a.role = Role::CheckBox;
        a.toggle = t->Value() ? 1 : 0;
    } else if (auto* c = dynamic_cast<Checkbox*>(w)) {
        a.role = Role::CheckBox;
        a.toggle = c->Value() ? 1 : 0;
    } else if (auto* d = dynamic_cast<Dropdown*>(w)) {
        a.role = Role::ComboBox;
        const int i = d->Selected();
        a.value = i >= 0 && i < static_cast<int>(d->Choices().size()) ? d->Choices()[static_cast<size_t>(i)].label
                                                                       : d->placeholder;
    } else if (auto* s = dynamic_cast<Slider*>(w)) {
        a.role = Role::Slider;
        a.min = s->min;
        a.max = s->max;
        a.now = s->Value();
        a.step = s->step;
        a.setRange = [s](double v) {  // like the arrow keys
            s->SetValue(static_cast<float>(v));
            if (s->onChange) s->onChange(s->Value());
            if (s->onCommit) s->onCommit(s->Value());
        };
    } else if (auto* tb = dynamic_cast<TextBox*>(w)) {
        a.role = Role::Edit;
        a.name = tb->placeholder;
        a.password = tb->password;
        if (!tb->password) a.value = tb->Text();
        if (!tb->readOnly) a.setValue = [tb](const std::wstring& v) { tb->SetText(v, true); };
    } else if (dynamic_cast<ScrollView*>(w) || dynamic_cast<IconView*>(w) || dynamic_cast<Divider*>(w)) {
        a.role = Role::Skip;
    } else if (dynamic_cast<Dialog*>(w)) {
        a.role = Role::Window;
    }
    w->Describe(a);
    if (a.role == Role::Auto) {
        const bool text = Painted().count(w) > 0;
        if (!w->interactive)
            a.role = text ? Role::Text : Role::Skip;
        else if (HasInteractiveDescendant(w))
            a.role = Role::Group;
        else  // a nameless surface the keyboard can't reach (the video, a panel's background) is no button
            a.role = w->focusable || text || !w->tooltip.empty() ? Role::Button : Role::Skip;
    }
    // Popups (dialogs, panels) are windows of their own; menus, tooltips and toasts keep their role.
    if ((a.role == Role::Skip || a.role == Role::Group) && IsPopupRoot(w)) a.role = Role::Window;
    return a;
}

Role RoleOf(Widget* w) {
    return Describe(w).role;
}

std::wstring NameOf(Widget* w, const A11y& a) {
    if (!a.name.empty()) return a.name;
    std::wstring n;
    switch (a.role) {
        case Role::Text:
            n = PaintedBy(w);
            break;
        case Role::Button: case Role::ListItem: case Role::MenuItem: case Role::TabItem:
            PaintedDeep(w, n);
            break;
        case Role::CheckBox:
            // A switch paints only "ON"/"OFF".
            if (!dynamic_cast<Toggle*>(w)) PaintedDeep(w, n);
            if (n.empty()) n = LabelBefore(w);
            break;
        case Role::ComboBox: case Role::Edit: case Role::Slider:
            n = LabelBefore(w);
            break;
        case Role::List:
            n = LabelBefore(w, true);
            break;
        case Role::Window: {
            // A dialog or panel: its first text (the title).
            std::vector<const Widget*> stack{w};
            while (!stack.empty() && n.empty()) {
                const Widget* x = stack.front();
                stack.erase(stack.begin());
                if (x != w && !x->interactive) n = PaintedBy(x);
                for (auto& c : x->Children())
                    if (c->visible) stack.push_back(c.get());
            }
            break;
        }
        default:
            break;
    }
    if (n.empty()) n = w->tooltip;
    if (n.empty() && a.role != Role::Text && a.role != Role::Window) n = LabelBefore(w);
    if (n.empty())
        if (auto* ib = dynamic_cast<IconButton*>(w)) n = Widen(ib->icon);  // shortcut: icon id, until it has a tooltip
    if (n.size() > 400) n.resize(400);
    return n;
}

CONTROLTYPEID ControlType(Role r) {
    switch (r) {
        case Role::Text: return UIA_TextControlTypeId;
        case Role::Button: return UIA_ButtonControlTypeId;
        case Role::CheckBox: return UIA_CheckBoxControlTypeId;
        case Role::ComboBox: return UIA_ComboBoxControlTypeId;
        case Role::Edit: return UIA_EditControlTypeId;
        case Role::Slider: return UIA_SliderControlTypeId;
        case Role::List: return UIA_ListControlTypeId;
        case Role::ListItem: return UIA_ListItemControlTypeId;
        case Role::Menu: return UIA_MenuControlTypeId;
        case Role::MenuItem: return UIA_MenuItemControlTypeId;
        case Role::Tab: return UIA_TabControlTypeId;
        case Role::TabItem: return UIA_TabItemControlTypeId;
        case Role::Window: return UIA_WindowControlTypeId;
        default: return UIA_GroupControlTypeId;
    }
}

// An element: the window (w == nullptr), a widget (item < 0) or one of its painted entries.
struct Ref {
    Widget* w = nullptr;
    int item = -1;
    bool operator==(const Ref& o) const { return w == o.w && item == o.item; }
};

// Elements directly under `w` that are widgets: Skip widgets hand their children up.
void Collect(Widget* w, std::vector<Ref>& out) {
    for (auto& c : w->Children()) {
        if (!c->visible) continue;
        if (RoleOf(c.get()) == Role::Skip)
            Collect(c.get(), out);
        else
            out.push_back({c.get(), -1});
    }
}

std::vector<Ref> ChildrenOf(Host* h, const Ref& r) {
    std::vector<Ref> out;
    if (!r.w) {
        if (h->Root()) Collect(h->Root(), out);
        for (size_t i = 0; i < h->PopupCount(); ++i)
            if (Widget* p = h->PopupAt(i); p && p->visible) out.push_back({p, -1});
        return out;
    }
    if (r.item >= 0) return out;
    const A11y a = Describe(r.w);
    for (int i = 0; i < a.items && a.item; ++i) {
        A11y::Item it;
        a.item(i, it);
        if (it.role != Role::Skip) out.push_back({r.w, i});
    }
    if (!IsLeaf(a.role)) Collect(r.w, out);
    return out;
}

Ref ParentOf(Host* h, const Ref& r) {
    if (!r.w) return {};
    if (r.item >= 0) return {r.w, -1};
    for (Widget* p = r.w->Parent(); p && p != h->Root(); p = p->Parent())
        if (RoleOf(p) != Role::Skip) return {p, -1};
    return {};
}

// The element that stands for widget `w`: a control swallows the widgets it is drawn with.
Ref Exposed(Host* h, Widget* w) {
    Widget* top = nullptr;
    for (Widget* a = w; a && a != h->Root(); a = a->Parent())
        if (IsLeaf(RoleOf(a))) top = a;
    if (top) return {top, -1};
    for (Widget* a = w; a && a != h->Root(); a = a->Parent())
        if (RoleOf(a) != Role::Skip) return {a, -1};
    return {};
}

Ref FocusRef(Host* h) {
    Widget* f = h->Focus();
    if (!f) return {};
    Ref r = Exposed(h, f);
    if (r.w == f) {
        const A11y a = Describe(f);
        if (a.focusedItem >= 0 && a.focusedItem < a.items) r.item = a.focusedItem;
    }
    return r;
}

bool Offscreen(Host* h, const Widget* w, Rect r) {
    if (r.Empty()) return true;
    const Size c = h->ClientSize();
    if (!r.Intersects({0, 0, c.w, c.h})) return true;
    for (const Widget* a = w; a; a = a->Parent()) {
        if (!a->visible) return true;
        if (a != w && a->clipChildren && !r.Intersects(a->rect)) return true;
    }
    return false;
}

// Everything one call needs, checked: the element still exists.
struct Ctx {
    Host* host = nullptr;
    Widget* w = nullptr;
    int item = -1;
    A11y a;
    A11y::Item it;
    Role Kind() const { return item >= 0 ? it.role : a.role; }
};

bool Load(HWND hwnd, Widget* w, int serial, int item, Ctx& c) {
    c.host = HostOf(hwnd);
    if (!c.host) return false;
    c.w = w;
    c.item = item;
    if (!w) return true;
    auto s = Serials().find(w);
    if (s == Serials().end() || s->second != serial || w->GetHost() != c.host) return false;
    c.a = Describe(w);
    if (item >= 0) {
        if (item >= c.a.items || !c.a.item) return false;
        c.a.item(item, c.it);
    }
    return true;
}

class Node final : public IRawElementProviderSimple,
                   public IRawElementProviderFragment,
                   public IRawElementProviderFragmentRoot,
                   public IInvokeProvider,
                   public IToggleProvider,
                   public IValueProvider,
                   public IRangeValueProvider,
                   public ISelectionItemProvider {
public:
    Node(HWND hwnd, const Ref& r) : hwnd_(hwnd), w_(r.w), item_(r.item) {
        if (w_) serial_ = Serials().try_emplace(w_, ++g_nextSerial).first->second;
    }
    // A new reference (count 1) to the element `r` of the window, or nullptr.
    static Node* Make(HWND hwnd, const Ref& r) { return new Node(hwnd, r); }

    // IUnknown
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG n = InterlockedDecrement(&refs_);
        if (n == 0) delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IRawElementProviderSimple))
            *out = static_cast<IRawElementProviderSimple*>(this);
        else if (iid == __uuidof(IRawElementProviderFragment))
            *out = static_cast<IRawElementProviderFragment*>(this);
        else if (iid == __uuidof(IRawElementProviderFragmentRoot) && !w_)
            *out = static_cast<IRawElementProviderFragmentRoot*>(this);
        else if (iid == __uuidof(IInvokeProvider))
            *out = static_cast<IInvokeProvider*>(this);
        else if (iid == __uuidof(IToggleProvider))
            *out = static_cast<IToggleProvider*>(this);
        else if (iid == __uuidof(IValueProvider))
            *out = static_cast<IValueProvider*>(this);
        else if (iid == __uuidof(IRangeValueProvider))
            *out = static_cast<IRangeValueProvider*>(this);
        else if (iid == __uuidof(ISelectionItemProvider))
            *out = static_cast<ISelectionItemProvider*>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }

    // IRawElementProviderSimple
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* out) override {
        if (!out) return E_POINTER;
        *out = ProviderOptions_ServerSideProvider;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID id, IUnknown** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        Ctx c;
        if (!Load(c)) return UIA_E_ELEMENTNOTAVAILABLE;
        if (!c.w) return S_OK;
        const Role r = c.Kind();
        const int toggle = c.item >= 0 ? c.it.toggle : c.a.toggle;
        bool yes = false;
        switch (id) {
            case UIA_InvokePatternId:
                yes = c.item >= 0 ? c.a.invokeItem && (r == Role::MenuItem || r == Role::ListItem || r == Role::Button)
                                  : r == Role::Button || r == Role::MenuItem || r == Role::ComboBox;
                break;
            case UIA_TogglePatternId: yes = toggle >= 0; break;
            case UIA_ValuePatternId: yes = c.item < 0 && (r == Role::Edit || r == Role::ComboBox); break;
            case UIA_RangeValuePatternId: yes = c.item < 0 && r == Role::Slider; break;
            case UIA_SelectionItemPatternId:
                yes = c.item >= 0 && c.a.selectItem && (r == Role::ListItem || r == Role::TabItem);
                break;
            default: break;
        }
        if (yes) {
            *out = static_cast<IRawElementProviderSimple*>(this);
            AddRef();
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id, VARIANT* out) override {
        if (!out) return E_POINTER;
        out->vt = VT_EMPTY;
        Ctx c;
        if (!Load(c)) return UIA_E_ELEMENTNOTAVAILABLE;
        if (id == UIA_FrameworkIdPropertyId) return Str(out, L"Lumen");
        if (!c.w) return S_OK;  // the window's own properties come from its HWND
        const std::wstring name = c.item >= 0 ? c.it.name : NameOf(c.w, c.a);
        switch (id) {
            case UIA_ControlTypePropertyId:
                out->vt = VT_I4;
                out->lVal = ControlType(c.Kind());
                return S_OK;
            case UIA_NamePropertyId:
                return Str(out, name);
            case UIA_HelpTextPropertyId:
                return c.item < 0 && !c.w->tooltip.empty() && c.w->tooltip != name ? Str(out, c.w->tooltip) : S_OK;
            case UIA_AutomationIdPropertyId: {
                std::wstring id16 = Widen(c.w->id);
                if (c.item >= 0) id16 += L"#" + std::to_wstring(c.item);
                return id16.empty() ? S_OK : Str(out, id16);
            }
            case UIA_ClassNamePropertyId: {
                const char* n = typeid(*c.w).name();
                if (strncmp(n, "class ", 6) == 0) n += 6;
                return Str(out, Widen(n));
            }
            case UIA_IsEnabledPropertyId: return Bool(out, c.w->IsEnabled() && (c.item < 0 || c.it.enabled));
            case UIA_IsKeyboardFocusablePropertyId: return Bool(out, c.w->focusable);
            case UIA_HasKeyboardFocusPropertyId: return Bool(out, FocusRef(c.host) == Ref{c.w, c.item});
            case UIA_IsOffscreenPropertyId:
                return Bool(out, Offscreen(c.host, c.w, c.item >= 0 ? c.it.rect : c.w->rect));
            case UIA_IsPasswordPropertyId: return Bool(out, c.item < 0 && c.a.password);
            default: return S_OK;
        }
    }

    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        return w_ ? S_OK : UiaHostProviderFromHwnd(hwnd_, out);
    }

    // IRawElementProviderFragment
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection dir, IRawElementProviderFragment** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        Ctx c;
        if (!Load(c)) return UIA_E_ELEMENTNOTAVAILABLE;
        const Ref self{c.w, c.item};
        Ref to;
        bool found = false;
        if (dir == NavigateDirection_Parent) {
            if (!c.w) return S_OK;
            to = ParentOf(c.host, self);
            found = true;
        } else if (dir == NavigateDirection_FirstChild || dir == NavigateDirection_LastChild) {
            const std::vector<Ref> kids = ChildrenOf(c.host, self);
            if (kids.empty()) return S_OK;
            to = dir == NavigateDirection_FirstChild ? kids.front() : kids.back();
            found = true;
        } else if (c.w) {
            const std::vector<Ref> sibs = ChildrenOf(c.host, ParentOf(c.host, self));
            for (size_t i = 0; i < sibs.size(); ++i) {
                if (!(sibs[i] == self)) continue;
                if (dir == NavigateDirection_NextSibling && i + 1 < sibs.size()) to = sibs[i + 1], found = true;
                if (dir == NavigateDirection_PreviousSibling && i > 0) to = sibs[i - 1], found = true;
                break;
            }
        }
        if (found) *out = Make(hwnd_, to);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        Ctx c;
        if (!Load(c)) return UIA_E_ELEMENTNOTAVAILABLE;
        if (!c.w) return S_OK;  // the window's id comes from its HWND
        const int ids[] = {UiaAppendRuntimeId, serial_, item_ + 1};
        *out = SafeArrayCreateVector(VT_I4, 0, 3);
        if (!*out) return E_OUTOFMEMORY;
        for (LONG i = 0; i < 3; ++i) SafeArrayPutElement(*out, &i, const_cast<int*>(&ids[i]));
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* out) override {
        if (!out) return E_POINTER;
        *out = {};
        Ctx c;
        if (!Load(c)) return UIA_E_ELEMENTNOTAVAILABLE;
        if (!c.w) return S_OK;  // the window's rect comes from its HWND
        const Rect r = c.item >= 0 ? c.it.rect : c.w->rect;
        if (Offscreen(c.host, c.w, r)) return S_OK;
        POINT o{0, 0};
        ClientToScreen(hwnd_, &o);
        const double s = c.host->Scale();
        *out = {o.x + r.x * s, o.y + r.y * s, r.w * s, r.h * s};
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetFocus() override {
        Ctx c;
        if (!Load(c)) return UIA_E_ELEMENTNOTAVAILABLE;
        if (!c.w) return S_OK;
        if (c.item >= 0 && c.a.selectItem) c.a.selectItem(c.item);
        if (c.w->focusable) c.host->SetFocus(c.w, true);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (!HostOf(hwnd_)) return UIA_E_ELEMENTNOTAVAILABLE;
        *out = Make(hwnd_, {});
        return S_OK;
    }

    // IRawElementProviderFragmentRoot
    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        Ctx c;
        if (!Load(c)) return UIA_E_ELEMENTNOTAVAILABLE;
        POINT pt{static_cast<LONG>(x), static_cast<LONG>(y)};
        ScreenToClient(hwnd_, &pt);
        const float s = c.host->Scale(), dx = static_cast<float>(pt.x) / s, dy = static_cast<float>(pt.y) / s;
        // The host's hit test: popups on top first, then the window's tree.
        Widget* hit = nullptr;
        for (size_t i = c.host->PopupCount(); i-- > 0 && !hit;)
            if (Widget* p = c.host->PopupAt(i)) hit = p->HitTest(dx, dy);
        if (!hit && c.host->Root()) hit = c.host->Root()->HitTest(dx, dy);
        Ref r = hit ? Exposed(c.host, hit) : Ref{};
        if (r.w) {
            const A11y a = Describe(r.w);
            for (int i = 0; i < a.items && a.item; ++i) {
                A11y::Item it;
                a.item(i, it);
                if (it.role != Role::Skip && it.rect.Contains(dx, dy)) {
                    r.item = i;
                    break;
                }
            }
        }
        *out = Make(hwnd_, r);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        Ctx c;
        if (!Load(c)) return UIA_E_ELEMENTNOTAVAILABLE;
        const Ref r = FocusRef(c.host);
        if (r.w) *out = Make(hwnd_, r);
        return S_OK;
    }

    // IInvokeProvider: after this call returns, as UI Automation asks (the click may open a
    // dialog or destroy this very widget).
    HRESULT STDMETHODCALLTYPE Invoke() override { return Later(); }

    // IToggleProvider
    HRESULT STDMETHODCALLTYPE Toggle() override { return Later(); }
    HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState* out) override {
        if (!out) return E_POINTER;
        Ctx c;
        if (!Load(c) || !c.w) return UIA_E_ELEMENTNOTAVAILABLE;
        *out = (c.item >= 0 ? c.it.toggle : c.a.toggle) > 0 ? ToggleState_On : ToggleState_Off;
        return S_OK;
    }

    // IValueProvider
    HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR v) override {
        Ctx c;
        if (!Load(c) || !c.w) return UIA_E_ELEMENTNOTAVAILABLE;
        if (!c.a.setValue || !c.w->IsEnabled()) return UIA_E_ELEMENTNOTENABLED;
        c.a.setValue(v ? v : L"");
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Value(BSTR* out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        Ctx c;
        if (!Load(c) || !c.w) return UIA_E_ELEMENTNOTAVAILABLE;
        *out = SysAllocStringLen(c.a.value.data(), static_cast<UINT>(c.a.value.size()));
        return *out ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* out) override {
        if (!out) return E_POINTER;
        Ctx c;
        if (!Load(c) || !c.w) return UIA_E_ELEMENTNOTAVAILABLE;
        *out = c.a.role == Role::Slider ? !c.a.setRange : !c.a.setValue;
        return S_OK;
    }

    // IRangeValueProvider
    HRESULT STDMETHODCALLTYPE SetValue(double v) override {
        Ctx c;
        if (!Load(c) || !c.w) return UIA_E_ELEMENTNOTAVAILABLE;
        if (!c.a.setRange || !c.w->IsEnabled()) return UIA_E_ELEMENTNOTENABLED;
        if (v < c.a.min || v > c.a.max) return E_INVALIDARG;
        c.a.setRange(v);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Value(double* out) override { return Num(out, [](const A11y& a) { return a.now; }); }
    HRESULT STDMETHODCALLTYPE get_Maximum(double* out) override { return Num(out, [](const A11y& a) { return a.max; }); }
    HRESULT STDMETHODCALLTYPE get_Minimum(double* out) override { return Num(out, [](const A11y& a) { return a.min; }); }
    HRESULT STDMETHODCALLTYPE get_SmallChange(double* out) override {
        return Num(out, [](const A11y& a) { return a.step > 0 ? a.step : (a.max - a.min) / 20; });
    }
    HRESULT STDMETHODCALLTYPE get_LargeChange(double* out) override {
        return Num(out, [](const A11y& a) { return a.step > 0 ? a.step * 10 : (a.max - a.min) / 10; });
    }

    // ISelectionItemProvider (single selection)
    HRESULT STDMETHODCALLTYPE Select() override {
        Ctx c;
        if (!Load(c) || c.item < 0 || !c.a.selectItem) return UIA_E_ELEMENTNOTAVAILABLE;
        c.a.selectItem(c.item);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE AddToSelection() override { return Select(); }
    HRESULT STDMETHODCALLTYPE RemoveFromSelection() override { return UIA_E_INVALIDOPERATION; }
    HRESULT STDMETHODCALLTYPE get_IsSelected(BOOL* out) override {
        if (!out) return E_POINTER;
        Ctx c;
        if (!Load(c) || c.item < 0) return UIA_E_ELEMENTNOTAVAILABLE;
        *out = c.it.selected;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_SelectionContainer(IRawElementProviderSimple** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        Ctx c;
        if (!Load(c) || c.item < 0) return UIA_E_ELEMENTNOTAVAILABLE;
        *out = static_cast<IRawElementProviderSimple*>(Make(hwnd_, {c.w, -1}));
        return S_OK;
    }

private:
    ~Node() = default;

    bool Load(Ctx& c) const { return a11y::Load(hwnd_, w_, serial_, item_, c); }

    // Runs the element's click on the window's thread after this call, if it still exists then.
    HRESULT Later() {
        Ctx c;
        if (!Load(c) || !c.w) return UIA_E_ELEMENTNOTAVAILABLE;
        if (!c.w->IsEnabled() || (c.item >= 0 && !c.it.enabled)) return UIA_E_ELEMENTNOTENABLED;
        c.host->Post([hwnd = hwnd_, w = w_, serial = serial_, item = item_] {
            Ctx now;
            if (!a11y::Load(hwnd, w, serial, item, now)) return;
            if (item >= 0) {
                if (now.a.invokeItem) now.a.invokeItem(item);
            } else if (now.a.invoke) {
                now.a.invoke();
            } else {
                w->OnClick();
            }
        });
        return S_OK;
    }

    template <class F>
    HRESULT Num(double* out, F get) const {
        if (!out) return E_POINTER;
        Ctx c;
        if (!Load(c) || !c.w) return UIA_E_ELEMENTNOTAVAILABLE;
        *out = get(c.a);
        return S_OK;
    }

    static HRESULT Str(VARIANT* out, const std::wstring& s) {
        out->vt = VT_BSTR;
        out->bstrVal = SysAllocStringLen(s.data(), static_cast<UINT>(s.size()));
        return out->bstrVal ? S_OK : E_OUTOFMEMORY;
    }
    static HRESULT Bool(VARIANT* out, bool b) {
        out->vt = VT_BOOL;
        out->boolVal = b ? VARIANT_TRUE : VARIANT_FALSE;
        return S_OK;
    }

    LONG refs_ = 1;
    HWND hwnd_;
    Widget* w_;  // checked against Serials() before every use, never dereferenced unchecked
    int serial_ = 0;
    int item_;
};

}  // namespace

bool WantsRoot(LPARAM objectId) {
    if (static_cast<LONG>(objectId) != UiaRootObjectId) return false;
    g_connected = true;
    g_recordOnce = true;
    return true;
}

LRESULT ReturnRoot(Host* host, WPARAM wp, LPARAM lp) {
    Node* root = Node::Make(host->Hwnd(), {});
    const LRESULT r = UiaReturnRawElementProvider(host->Hwnd(), wp, lp, root);
    root->Release();
    return r;
}

bool Recording() {
    return g_recordOnce || (g_connected && UiaClientsAreListening());
}

void TextsPainted(const std::vector<TextRecord>& texts) {
    g_recordOnce = false;
    std::unordered_map<const Widget*, std::wstring> frame;
    const TextRecord* last = nullptr;
    for (const TextRecord& t : texts) {
        if (t.faded || !t.owner) continue;
        // Text drawn twice in one place (outlines, shadows) counts once.
        if (last && last->owner == t.owner && last->text == t.text && last->box.x == t.box.x && last->box.y == t.box.y)
            continue;
        last = &t;
        std::wstring& s = frame[t.owner];
        if (s.size() > 400) continue;
        if (!s.empty()) s += L' ';
        s += t.text;
    }
    // Widgets not painted this time (scrolled away) keep what they showed last.
    for (auto& [w, s] : frame) Painted()[w] = std::move(s);
}

void Forget(const Widget* w) {
    if (!Serials().empty()) Serials().erase(w);
    if (!Painted().empty()) Painted().erase(w);
}

void WindowDestroyed(HWND hwnd) {
    // Clients drop their references to this window's elements.
    if (g_connected) UiaReturnRawElementProvider(hwnd, 0, 0, nullptr);
}

void FocusChanged(Host* host) {
    if (!g_connected || !host || !host->Hwnd() || !UiaClientsAreListening()) return;
    const Ref r = FocusRef(host);
    if (!r.w) return;
    Node* n = Node::Make(host->Hwnd(), r);
    UiaRaiseAutomationEvent(n, UIA_AutomationFocusChangedEventId);
    n->Release();
}

void StructureChanged(Host* host) {
    if (!g_connected || !host || !host->Hwnd() || !UiaClientsAreListening()) return;
    Node* n = Node::Make(host->Hwnd(), {});
    UiaRaiseStructureChangedEvent(n, StructureChangeType_ChildrenInvalidated, nullptr, 0);
    n->Release();
}

}  // namespace ui::a11y
