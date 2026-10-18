#pragma once
#include <cstdint>

namespace ui {

enum class Ease : uint8_t { Out, InOut };

// A float that animates toward a target. While any AnimFloat is moving the
// host keeps rendering frames (vsync-paced); otherwise nothing ticks.
class AnimFloat {
public:
    explicit AnimFloat(float v = 0) : value_(v), from_(v), to_(v) {}
    ~AnimFloat();
    AnimFloat(const AnimFloat&) = delete;
    AnimFloat& operator=(const AnimFloat&) = delete;

    float Get() const { return value_; }
    float Target() const { return to_; }
    operator float() const { return value_; }

    void To(float target, float durationMs = 150, Ease ease = Ease::Out);
    void Set(float v);  // jump, no animation
    bool Active() const { return active_; }

    // Advances every active animation to `nowMs`. Returns true if any is still running.
    static bool TickAll(double nowMs);
    static bool AnyActive();
    static double Now();

private:
    void Step(double nowMs);
    float value_, from_, to_;
    double start_ = 0;
    float duration_ = 0;
    Ease ease_ = Ease::Out;
    bool active_ = false;
};

}  // namespace ui
