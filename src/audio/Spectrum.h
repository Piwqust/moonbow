#pragma once
// Signal analysis for the music view's visualizer: a real FFT, log-spaced bands, smoothing and
// the oscilloscope trigger. Header-only and allocation-free after Configure(), so the capture
// thread (audio/LoopbackCapture.cpp) runs it per frame and tests/core_tests.cpp checks it.

#include <algorithm>
#include <cmath>
#include <vector>

namespace audio {

inline constexpr int kFftSize = 2048;
inline constexpr int kBands = 48;
inline constexpr int kScopePoints = 256;
inline constexpr int kWaveColumns = 96;
inline constexpr float kFloorDb = -72.0f;  // a bar starts above this
inline constexpr float kTopDb = -6.0f;     // and is full here

// In-place iterative radix-2 FFT of n complex values (n a power of two).
inline void Fft(float* re, float* im, int n) {
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const double ang = -2 * 3.14159265358979323846 / len;
        const float wr = static_cast<float>(std::cos(ang)), wi = static_cast<float>(std::sin(ang));
        for (int i = 0; i < n; i += len) {
            float cr = 1, ci = 0;
            for (int k = 0; k < len / 2; ++k) {
                const int a = i + k, b = a + len / 2;
                const float tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
                const float nr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = nr;
            }
        }
    }
}

// Height 0..1 of a bar at `db` (dBFS, a full-scale sine = 0 dB).
inline float BarLevel(float db) {
    return std::clamp((db - kFloorDb) / (kTopDb - kFloorDb), 0.0f, 1.0f);
}

// Exponential approach: fast up, slower down (time constants in seconds).
inline float Smooth(float current, float target, float dt, float attack, float release) {
    const float tau = target > current ? attack : release;
    const float k = tau > 0 ? 1.0f - std::exp(-dt / tau) : 1.0f;
    return current + (target - current) * k;
}

// Index of the rising zero crossing in s[0, limit) that starts the steepest rise, so a steady
// tone draws as a standing wave; 0 when there is none.
inline int ScopeTrigger(const float* s, int limit) {
    int best = 0;
    float bestSlope = 0;
    for (int i = 1; i < limit; ++i)
        if (s[i - 1] < 0 && s[i] >= 0 && s[i] - s[i - 1] > bestSlope) {
            bestSlope = s[i] - s[i - 1];
            best = i;
        }
    return best;
}

class SpectrumAnalyzer {
public:
    // Bands from `low` to `high` Hz, equally spaced on a log scale.
    void Configure(double sampleRate, double low = 40, double high = 16000) {
        window_.resize(kFftSize);
        re_.resize(kFftSize);
        im_.resize(kFftSize);
        for (int i = 0; i < kFftSize; ++i)
            window_[i] = static_cast<float>(0.5 - 0.5 * std::cos(2 * 3.14159265358979323846 * i / (kFftSize - 1)));
        const double binHz = sampleRate / kFftSize;
        high = std::min(high, sampleRate / 2 * 0.95);
        for (int b = 0; b < kBands; ++b) {
            const double f0 = low * std::pow(high / low, static_cast<double>(b) / kBands);
            const double f1 = low * std::pow(high / low, static_cast<double>(b + 1) / kBands);
            lo_[b] = std::max(1, static_cast<int>(std::floor(f0 / binHz)));
            hi_[b] = std::max(lo_[b], static_cast<int>(std::ceil(f1 / binHz)) - 1);
            hi_[b] = std::min(hi_[b], kFftSize / 2 - 1);
        }
    }

    // `samples`: the latest kFftSize mono samples, oldest first, already divided by the output
    // gain. Writes each band's loudest bin in dBFS.
    void Analyze(const float* samples, float* bandsDb) {
        if (window_.empty()) return;
        for (int i = 0; i < kFftSize; ++i) {
            re_[i] = samples[i] * window_[i];
            im_[i] = 0;
        }
        Fft(re_.data(), im_.data(), kFftSize);
        // A sine of amplitude A peaks at A * N / 4 under a Hann window.
        const float norm = 4.0f / kFftSize;
        for (int b = 0; b < kBands; ++b) {
            float peak = 0;
            for (int k = lo_[b]; k <= hi_[b]; ++k) peak = std::max(peak, re_[k] * re_[k] + im_[k] * im_[k]);
            const float mag = std::sqrt(peak) * norm;
            bandsDb[b] = mag > 1e-9f ? 20.0f * std::log10(mag) : -180.0f;
        }
    }

    int BandLow(int b) const { return lo_[b]; }
    int BandHigh(int b) const { return hi_[b]; }

private:
    std::vector<float> window_, re_, im_;
    int lo_[kBands] = {}, hi_[kBands] = {};
};

}  // namespace audio
