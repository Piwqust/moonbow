#pragma once
#include <functional>
#include <utility>
#include <vector>

namespace app {

// Minimal UI-thread signal. Connect returns an id for Disconnect; widgets that
// connect must disconnect in their destructor (or use ScopedConnection).
template <class... Args>
class Signal {
public:
    int Connect(std::function<void(Args...)> fn) {
        slots_.emplace_back(++next_, std::move(fn));
        return next_;
    }
    void Disconnect(int id) {
        for (auto& s : slots_)
            if (s.first == id) s.second = nullptr;
    }
    void Emit(Args... args) {
        // Slots may connect/disconnect while running; iterate by index.
        for (size_t i = 0; i < slots_.size(); ++i)
            if (slots_[i].second) slots_[i].second(args...);
        if (slots_.size() > 32) Compact();
    }

private:
    void Compact() {
        std::vector<std::pair<int, std::function<void(Args...)>>> kept;
        for (auto& s : slots_)
            if (s.second) kept.push_back(std::move(s));
        slots_.swap(kept);
    }
    std::vector<std::pair<int, std::function<void(Args...)>>> slots_;
    int next_ = 0;
};

// Disconnects automatically when destroyed.
class ScopedConnection {
public:
    ScopedConnection() = default;
    template <class S>
    ScopedConnection(S& signal, int id) : disconnect_([&signal, id] { signal.Disconnect(id); }) {}
    ScopedConnection(ScopedConnection&& o) noexcept : disconnect_(std::move(o.disconnect_)) { o.disconnect_ = nullptr; }
    ScopedConnection& operator=(ScopedConnection&& o) noexcept {
        Reset();
        disconnect_ = std::move(o.disconnect_);
        o.disconnect_ = nullptr;
        return *this;
    }
    ~ScopedConnection() { Reset(); }
    void Reset() {
        if (disconnect_) disconnect_();
        disconnect_ = nullptr;
    }

private:
    std::function<void()> disconnect_;
};

template <class S, class F>
ScopedConnection Connect(S& signal, F&& fn) {
    return ScopedConnection(signal, signal.Connect(std::forward<F>(fn)));
}

}  // namespace app
