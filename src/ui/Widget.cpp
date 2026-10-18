#include "ui/Widget.h"

#include "ui/Host.h"

namespace ui {

Widget::Widget() = default;

Widget::~Widget() {
    // Children go first, while this widget (and its host link) is still intact.
    ClearChildren();
    Host::ForgetWidget(this);
}

Widget* Widget::AddChild(std::unique_ptr<Widget> child, int index) {
    child->parent_ = this;
    Widget* raw = child.get();
    if (index < 0 || index >= static_cast<int>(children_.size()))
        children_.push_back(std::move(child));
    else
        children_.insert(children_.begin() + index, std::move(child));
    InvalidateLayout();
    return raw;
}

std::unique_ptr<Widget> Widget::RemoveChild(Widget* child) {
    for (auto it = children_.begin(); it != children_.end(); ++it) {
        if (it->get() != child) continue;
        std::unique_ptr<Widget> out = std::move(*it);
        children_.erase(it);
        out->parent_ = nullptr;
        Host::ForgetSubtree(out.get());
        InvalidateLayout();
        return out;
    }
    return nullptr;
}

void Widget::ClearChildren() {
    if (children_.empty()) return;
    InvalidateLayout();
    // Destroy back to front so siblings stay valid while each one unwinds.
    while (!children_.empty()) children_.pop_back();
}

Host* Widget::GetHost() const {
    const Widget* w = this;
    while (w->parent_) w = w->parent_;
    return w->host_;
}

bool Widget::IsAncestorOf(const Widget* w) const {
    for (; w; w = w->parent_)
        if (w == this) return true;
    return false;
}

bool Widget::IsHot() const {
    Host* h = GetHost();
    return h && h->Hot() == this;
}

bool Widget::IsPressed() const {
    Host* h = GetHost();
    return h && h->Pressed() == this && h->Hot() == this;
}

bool Widget::IsFocused() const {
    Host* h = GetHost();
    return h && h->Focus() == this;
}

bool Widget::IsEnabled() const {
    for (const Widget* w = this; w; w = w->parent_)
        if (!w->enabled) return false;
    return true;
}

bool Widget::ShowFocusRing() const {
    Host* h = GetHost();
    return h && h->Focus() == this && h->FocusVisible();
}

void Widget::Invalidate() const {
    if (Host* h = GetHost()) h->RequestRender();
}

void Widget::InvalidateLayout() const {
    if (Host* h = GetHost()) h->RequestLayout();
}

void Widget::OnClick() {
    if (onClick) onClick();
}

void Widget::DebugInfo(std::string&) const {}

// --- layout ---------------------------------------------------------------------

Size Widget::Measure(float availW, float availH) {
    const bool fixedW = width.kind == Length::Fixed, fixedH = height.kind == Length::Fixed;
    Size content{};
    if (!fixedW || !fixedH) {
        const float cw = fixedW ? width.value : std::min(availW, maxW);
        const float ch = fixedH ? height.value : std::min(availH, maxH);
        content = MeasureContent(cw, ch);
    }
    Size s{fixedW ? width.value : content.w, fixedH ? height.value : content.h};
    s.w = std::clamp(s.w, minW, std::max(minW, maxW));
    s.h = std::clamp(s.h, minH, std::max(minH, maxH));
    return s;
}

void Widget::Arrange(const Rect& r) {
    rect = r;
    OnLayout();
}

Size Widget::MeasureContent(float availW, float availH) {
    return FlexMeasure(availW, availH);
}

void Widget::OnLayout() {
    FlexLayout();
}

Size Widget::FlexMeasure(float availW, float availH) {
    const float iw = std::max(0.0f, availW - pad.H()), ih = std::max(0.0f, availH - pad.V());
    float main = 0, cross = 0;
    int n = 0;
    for (auto& c : children_) {
        if (!c->visible) continue;
        Size cs = c->Measure(iw, ih);
        ++n;
        if (dir == Dir::Row) {
            main += cs.w;
            cross = std::max(cross, cs.h);
        } else if (dir == Dir::Column) {
            main += cs.h;
            cross = std::max(cross, cs.w);
        } else {
            main = std::max(main, cs.w);
            cross = std::max(cross, cs.h);
        }
    }
    const float gaps = (dir == Dir::Row || dir == Dir::Column) && n > 1 ? gap * (n - 1) : 0;
    if (dir == Dir::Row) return {main + gaps + pad.H(), cross + pad.V()};
    if (dir == Dir::Column) return {cross + pad.H(), main + gaps + pad.V()};
    return {main + pad.H(), cross + pad.V()};
}

void Widget::FlexLayout() {
    const Rect in = rect.Inset(pad);

    if (dir == Dir::None) {
        for (auto& c : children_)
            if (c->visible) c->Arrange(c->rect);
        return;
    }

    if (dir == Dir::Stack) {
        for (auto& c : children_) {
            if (!c->visible) continue;
            Size cs = c->Measure(in.w, in.h);
            const float w = c->width.kind == Length::Fill || align == Align::Stretch ? in.w : std::min(cs.w, in.w);
            const float h = c->height.kind == Length::Fill || align == Align::Stretch ? in.h : std::min(cs.h, in.h);
            float x = in.x, y = in.y;
            if (align == Align::Center) x += (in.w - w) / 2;
            if (align == Align::End) x += in.w - w;
            if (justify == Justify::Center) y += (in.h - h) / 2;
            if (justify == Justify::End) y += in.h - h;
            c->Arrange({x, y, w, h});
        }
        return;
    }

    const bool row = dir == Dir::Row;
    const float mainSize = row ? in.w : in.h, crossSize = row ? in.h : in.w;

    struct Item {
        Widget* w;
        Size s;
        bool fill;
        float weight;
    };
    std::vector<Item> items;
    items.reserve(children_.size());
    float fixed = 0, weights = 0;
    for (auto& c : children_) {
        if (!c->visible) continue;
        Size cs = c->Measure(row ? in.w : in.w, row ? in.h : in.h);
        const Length& ml = row ? c->width : c->height;
        const bool fill = ml.kind == Length::Fill;
        items.push_back({c.get(), cs, fill, fill ? std::max(0.0001f, ml.value) : 0});
        if (fill)
            weights += ml.value;
        else
            fixed += row ? cs.w : cs.h;
    }
    if (items.empty()) return;

    const float gaps = gap * static_cast<float>(items.size() - 1);
    float free = mainSize - fixed - gaps;
    float pos = row ? in.x : in.y;
    float extra = 0;
    if (weights <= 0 && free > 0) {
        if (justify == Justify::Center) pos += free / 2;
        if (justify == Justify::End) pos += free;
        if (justify == Justify::SpaceBetween && items.size() > 1) extra = free / static_cast<float>(items.size() - 1);
    }

    for (const Item& it : items) {
        Widget* c = it.w;
        float m = row ? it.s.w : it.s.h;
        if (it.fill) {
            m = std::max(0.0f, free) * it.weight / weights;
            m = std::clamp(m, row ? c->minW : c->minH, std::max(row ? c->minW : c->minH, row ? c->maxW : c->maxH));
        }
        const Length& cl = row ? c->height : c->width;
        float x = row ? it.s.h : it.s.w;  // cross size
        if (align == Align::Stretch || cl.kind == Length::Fill)
            x = std::clamp(crossSize, row ? c->minH : c->minW, std::max(row ? c->minH : c->minW, row ? c->maxH : c->maxW));
        float cpos = row ? in.y : in.x;
        if (align == Align::Center) cpos += (crossSize - x) / 2;
        if (align == Align::End) cpos += crossSize - x;
        if (row)
            c->Arrange({pos, cpos, m, x});
        else
            c->Arrange({cpos, pos, x, m});
        pos += m + gap + extra;
    }
}

// --- painting --------------------------------------------------------------------

void Widget::Paint(Painter&) {}
void Widget::PaintOver(Painter&) {}

void Widget::PaintChildren(Painter& p) {
    const Rect clip = p.Clip();  // by value: children may push clips (vector reallocation)
    for (auto& c : children_)
        if (c->visible && c->rect.Intersects(clip)) c->PaintTree(p);
}

void Widget::PaintTree(Painter& p) {
    if (!visible) return;
    const bool recording = p.RecordingText();  // tree dumps only
    if (recording) p.SetTextOwner(this);
    Paint(p);
    if (clipChildren) p.PushClip(rect);
    PaintChildren(p);
    if (clipChildren) p.PopClip();
    if (recording) p.SetTextOwner(this);
    PaintOver(p);
}

void Widget::PaintFocusRing(Painter& p, const Rect& r, float radius) const {
    if (ShowFocusRing()) p.Stroke(r.Inset(-2), P().accentMid, radius + 2, 1.5f);
}

Widget* Widget::HitTest(float x, float y) {
    if (!visible || !rect.Contains(x, y)) return nullptr;
    for (auto it = children_.rbegin(); it != children_.rend(); ++it)
        if (Widget* hit = (*it)->HitTest(x, y)) return hit;
    return this;
}

}  // namespace ui
