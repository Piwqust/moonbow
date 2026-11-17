#pragma once
// The whole song's loudness for the music seek bar (SoundCloud-style): a helper mpv decodes the
// file faster than real time into a temp PCM file (ao=pcm, mono, a low rate), and ReduceWave
// turns it into columns. One scan at a time on a worker; the newest request wins.

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct MpvApi;

namespace audio {

inline constexpr int kTrackWaveColumns = 1024;

// RMS of each of `columns` equal slices of `n` mono samples, scaled so the loudest is 1.
// Empty for no samples.
inline std::vector<float> ReduceWave(const int16_t* s, size_t n, int columns) {
    std::vector<float> out;
    if (!n || columns <= 0) return out;
    out.resize(static_cast<size_t>(columns));
    float top = 0;
    for (int c = 0; c < columns; ++c) {
        const size_t a = n * c / columns, b = std::max(a + 1, n * (c + 1) / columns);
        double sum = 0;
        for (size_t i = a; i < b && i < n; ++i) sum += static_cast<double>(s[i]) * s[i];
        out[c] = static_cast<float>(std::sqrt(sum / static_cast<double>(b - a)));
        top = std::max(top, out[c]);
    }
    for (float& v : out) v = top > 0 ? v / top : 0.0f;
    return out;
}

class TrackWave {
public:
    explicit TrackWave(const MpvApi& api) : api_(api) {}
    ~TrackWave();
    TrackWave(const TrackWave&) = delete;
    TrackWave& operator=(const TrackWave&) = delete;

    // UI thread. The columns of a local file, or null while it is read or when it can't be; the
    // first call starts the scan, which posts `message` to `notify` when done.
    const std::vector<float>* Find(const std::wstring& path, double duration, HWND notify, UINT message);

private:
    void Run();
    std::vector<float> Scan(const std::wstring& path, double duration);

    const MpvApi& api_;
    std::mutex mu_;
    std::condition_variable wake_;
    std::map<std::wstring, std::vector<float>> done_;  // an empty entry: the file can't be read
    std::wstring want_;  // the next file to scan
    double wantDuration_ = 0;
    HWND notify_ = nullptr;
    UINT message_ = 0;
    bool quit_ = false;
    std::thread worker_;
};

}  // namespace audio
