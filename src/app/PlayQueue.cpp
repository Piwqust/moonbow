#include "PlayQueue.h"

#include <algorithm>
#include <numeric>
#include <random>

namespace app {

namespace {
std::mt19937& Rng() {
    static std::mt19937 rng{std::random_device{}()};
    return rng;
}
}  // namespace

const QueueItem* PlayQueue::CurrentItem() const {
    return current_ >= 0 && current_ < static_cast<int>(items_.size()) ? &items_[static_cast<size_t>(current_)] : nullptr;
}

void PlayQueue::Set(std::vector<QueueItem> items, int current, std::wstring name) {
    items_ = std::move(items);
    current_ = items_.empty() ? -1 : std::clamp(current, 0, static_cast<int>(items_.size()) - 1);
    name_ = std::move(name);
    Reshuffle();
    changed.Emit();
}

// Edits keep the shuffled order of the items already queued (a full reshuffle would change
// what plays next on every drag); new items get random places among those still to play.
void PlayQueue::Add(std::vector<QueueItem> items) {
    const int first = static_cast<int>(items_.size());
    for (auto& i : items) items_.push_back(std::move(i));
    if (current_ < 0 && !items_.empty()) current_ = 0;
    if (!shuffle_) {
        Reshuffle();
    } else {
        for (int i = first; i < static_cast<int>(items_.size()); ++i) {
            const auto cur = std::find(order_.begin(), order_.end(), current_);
            const auto from = cur == order_.end() ? order_.begin() : cur + 1;
            const auto span = static_cast<int>(order_.end() - from);
            order_.insert(from + std::uniform_int_distribution<int>(0, span)(Rng()), i);
        }
    }
    changed.Emit();
}

void PlayQueue::InsertNext(std::vector<QueueItem> items) {
    const int at = current_ < 0 ? static_cast<int>(items_.size()) : current_ + 1;
    const int n = static_cast<int>(items.size());
    items_.insert(items_.begin() + at, items.begin(), items.end());
    if (current_ < 0 && !items_.empty()) current_ = 0;
    if (!shuffle_) {
        Reshuffle();
    } else {
        for (int& o : order_)
            if (o >= at) o += n;
        auto cur = std::find(order_.begin(), order_.end(), current_);
        auto pos = cur == order_.end() ? order_.begin() : cur + 1;
        for (int i = 0; i < n; ++i) pos = order_.insert(pos, at + i) + 1;
    }
    changed.Emit();
}

void PlayQueue::Remove(int index) {
    if (index < 0 || index >= static_cast<int>(items_.size())) return;
    // The current item goes: its successor in play order becomes current.
    int successor = index == current_ ? NextIndex(true) : -1;
    if (successor == index) successor = -1;
    items_.erase(items_.begin() + index);
    order_.erase(std::remove(order_.begin(), order_.end(), index), order_.end());
    for (int& o : order_)
        if (o > index) --o;
    if (successor > index) --successor;
    if (index == current_ && successor >= 0)
        current_ = successor;
    else if (index < current_)
        --current_;
    if (current_ >= static_cast<int>(items_.size())) current_ = static_cast<int>(items_.size()) - 1;
    if (!shuffle_) Reshuffle();
    changed.Emit();
}

void PlayQueue::Move(int from, int to) {
    const int n = static_cast<int>(items_.size());
    if (from < 0 || from >= n || to < 0 || to >= n || from == to) return;
    QueueItem item = std::move(items_[static_cast<size_t>(from)]);
    items_.erase(items_.begin() + from);
    items_.insert(items_.begin() + to, std::move(item));
    auto remap = [from, to](int k) {
        if (k == from) return to;
        if (from < to && k > from && k <= to) return k - 1;
        if (from > to && k >= to && k < from) return k + 1;
        return k;
    };
    current_ = remap(current_);
    for (int& o : order_) o = remap(o);
    if (!shuffle_) Reshuffle();
    changed.Emit();
}

void PlayQueue::Clear() {
    items_.clear();
    order_.clear();
    current_ = -1;
    changed.Emit();
}

void PlayQueue::SetCurrent(int index) {
    if (index < 0 || index >= static_cast<int>(items_.size())) return;
    current_ = index;
    changed.Emit();
}

void PlayQueue::SetShuffle(bool on) {
    shuffle_ = on;
    Reshuffle();
    changed.Emit();
}

void PlayQueue::SetRepeat(Repeat r) {
    repeat_ = r;
    changed.Emit();
}

void PlayQueue::SetInfo(int index, const std::wstring& title, double duration) {
    if (index < 0 || index >= static_cast<int>(items_.size())) return;
    QueueItem& item = items_[static_cast<size_t>(index)];
    if (!title.empty()) item.title = title;
    if (duration > 0) item.duration = duration;
}

void PlayQueue::Reshuffle() {
    order_.resize(items_.size());
    std::iota(order_.begin(), order_.end(), 0);
    if (!shuffle_ || order_.size() < 2) return;
    std::shuffle(order_.begin(), order_.end(), Rng());
    // The current item stays first so "next" walks the rest.
    auto it = std::find(order_.begin(), order_.end(), current_);
    if (it != order_.end()) std::iter_swap(order_.begin(), it);
}

int PlayQueue::NextIndex(bool userAction) const {
    if (items_.empty()) return -1;
    if (repeat_ == Repeat::One && !userAction) return current_;
    if (!shuffle_) {
        if (current_ + 1 < static_cast<int>(items_.size())) return current_ + 1;
        return repeat_ == Repeat::All ? 0 : -1;
    }
    auto it = std::find(order_.begin(), order_.end(), current_);
    if (it == order_.end() || it + 1 == order_.end()) return repeat_ == Repeat::All ? order_.front() : -1;
    return *(it + 1);
}

int PlayQueue::PrevIndex() const {
    if (items_.empty()) return -1;
    if (!shuffle_) return current_ > 0 ? current_ - 1 : (repeat_ == Repeat::All ? static_cast<int>(items_.size()) - 1 : -1);
    auto it = std::find(order_.begin(), order_.end(), current_);
    if (it == order_.end()) return -1;
    // At the start of the shuffled order Repeat All wraps to its end, as NextIndex wraps forward.
    if (it == order_.begin()) return repeat_ == Repeat::All ? order_.back() : -1;
    return *(it - 1);
}

int PlayQueue::GaplessNextIndex() const {
    if (!CurrentItem()) return -1;
    const int i = NextIndex(false);
    return i == current_ ? -1 : i;
}

int PlayQueue::IndexOfStarted(const std::wstring& path, int hint) const {
    const int n = static_cast<int>(items_.size());
    if (hint >= 0 && hint < n && items_[static_cast<size_t>(hint)].path == path) return hint;
    // Walk the play order from the current item on, wrapping around.
    const auto cur = std::find(order_.begin(), order_.end(), current_);
    const int start = cur == order_.end() ? 0 : static_cast<int>(cur - order_.begin()) + 1;
    for (int k = 0; k < n; ++k) {
        const int i = order_[static_cast<size_t>((start + k) % n)];
        if (items_[static_cast<size_t>(i)].path == path) return i;
    }
    return -1;
}

double PlayQueue::TotalDuration() const {
    double sum = 0;
    for (const QueueItem& i : items_) sum += i.duration;
    return sum;
}

}  // namespace app
