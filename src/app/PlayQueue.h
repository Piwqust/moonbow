#pragma once
#include "Signal.h"  // relative: tests/core_tests.cpp builds the queue too

#include <cstdint>
#include <string>
#include <vector>

namespace app {

// The play queue: plain data, no player or UI (tests/core_tests.cpp covers it).

struct QueueItem {
    std::wstring path;   // file path or URL
    std::wstring title;  // optional display name
    double duration = 0; // seconds, 0 if unknown
};

enum class Repeat : uint8_t { Off, All, One };

class PlayQueue {
public:
    const std::vector<QueueItem>& Items() const { return items_; }
    int Current() const { return current_; }
    const QueueItem* CurrentItem() const;
    bool Shuffle() const { return shuffle_; }
    Repeat RepeatMode() const { return repeat_; }
    std::wstring Name() const { return name_; }  // e.g. folder or playlist name

    void Set(std::vector<QueueItem> items, int current, std::wstring name = {});
    void Add(std::vector<QueueItem> items);           // append
    void InsertNext(std::vector<QueueItem> items);    // after the current item (also when shuffled)
    // Removing the current item makes its successor in play order current (App plays that).
    void Remove(int index);
    void Move(int from, int to);
    void Clear();
    void SetCurrent(int index);
    void SetShuffle(bool on);
    void SetRepeat(Repeat r);
    // What the item told about itself while it played (empty title / 0 = keep); no `changed`.
    void SetInfo(int index, const std::wstring& title, double duration);

    // Index of the item that plays after/before the current one, or -1.
    int NextIndex(bool userAction) const;
    int PrevIndex() const;
    // The item to hand to mpv for a gapless move on: the next one, but not the current one again
    // (repeat-one seeks back instead); -1 when the queue stops after this item.
    int GaplessNextIndex() const;
    // Queue index of `path` after mpv moved on to it: `hint` (the index handed to mpv) when it
    // still holds that file, else the copy that plays soonest after the current item, -1 if gone.
    int IndexOfStarted(const std::wstring& path, int hint) const;

    Signal<> changed;

private:
    void Reshuffle();
    std::vector<QueueItem> items_;
    std::vector<int> order_;  // play order (identity unless shuffled)
    int current_ = -1;
    bool shuffle_ = false;
    Repeat repeat_ = Repeat::Off;
    std::wstring name_;
};

}  // namespace app
