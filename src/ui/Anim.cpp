#include "ui/Anim.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace ui {

namespace {

std::vector<AnimFloat*>& ActiveList() {
    static std::vector<AnimFloat*> list;
    return list;
}

float Curve(Ease e, float t) {
    switch (e) {
        case Ease::Out: return 1 - (1 - t) * (1 - t) * (1 - t);
        case Ease::InOut: return t < 0.5f ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3.0f) / 2;
    }
    return t;
}

}  // namespace

AnimFloat::~AnimFloat() {
    if (active_) {
        auto& list = ActiveList();
        list.erase(std::remove(list.begin(), list.end(), this), list.end());
    }
}

double AnimFloat::Now() {
    static LARGE_INTEGER freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return static_cast<double>(t.QuadPart) * 1000.0 / static_cast<double>(freq.QuadPart);
}

void AnimFloat::To(float target, float durationMs, Ease ease) {
    if (target == to_ && (active_ || value_ == target)) return;
    if (durationMs <= 0) {
        Set(target);
        return;
    }
    from_ = value_;
    to_ = target;
    start_ = Now();
    duration_ = durationMs;
    ease_ = ease;
    if (!active_) {
        active_ = true;
        ActiveList().push_back(this);
    }
}

void AnimFloat::Set(float v) {
    value_ = from_ = to_ = v;
    if (active_) {
        active_ = false;
        auto& list = ActiveList();
        list.erase(std::remove(list.begin(), list.end(), this), list.end());
    }
}

void AnimFloat::Step(double nowMs) {
    const float t = std::clamp(static_cast<float>((nowMs - start_) / duration_), 0.0f, 1.0f);
    value_ = from_ + (to_ - from_) * Curve(ease_, t);
    if (t >= 1) {
        value_ = to_;
        active_ = false;
    }
}

bool AnimFloat::TickAll(double nowMs) {
    auto& list = ActiveList();
    for (AnimFloat* a : list) a->Step(nowMs);  // Step never adds or removes: no copy each frame
    list.erase(std::remove_if(list.begin(), list.end(), [](AnimFloat* a) { return !a->active_; }), list.end());
    return !list.empty();
}

bool AnimFloat::AnyActive() {
    return !ActiveList().empty();
}

}  // namespace ui
