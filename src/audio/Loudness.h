#pragma once
// Loudness of a stereo signal after EBU R128 / ITU-R BS.1770-4: K-weighting, momentary (400 ms),
// short-term (3 s) and gated integrated loudness, and the loudness range (EBU Tech 3342).
// Header-only and allocation-free, so the capture thread (audio/LoopbackCapture.cpp) feeds it per
// sample and tests/core_tests.cpp checks it against EBU Tech 3341/3342's test signals.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace audio {

inline constexpr float kLoudnessNone = -200.0f;  // not enough signal measured yet

class LoudnessMeter {
public:
    void Configure(double rate) {
        // BS.1770's two K-weighting stages for any sample rate, in the bilinear form libebur128 uses.
        constexpr double pi = 3.14159265358979323846;
        double k = std::tan(pi * 1681.974450955533 / rate), q = 0.7071752369554196;
        const double vh = std::pow(10.0, 3.999843853973347 / 20), vb = std::pow(vh, 0.4996667741545416);
        double a0 = 1 + k / q + k * k;
        shelf_ = {(vh + vb * k / q + k * k) / a0, 2 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0,
                  2 * (k * k - 1) / a0, (1 - k / q + k * k) / a0};
        k = std::tan(pi * 38.13547087602444 / rate);
        q = 0.5003270373238773;
        a0 = 1 + k / q + k * k;
        high_ = {1, -2, 1, 2 * (k * k - 1) / a0, (1 - k / q + k * k) / a0};
        blockLen_ = std::max(1, static_cast<int>(std::lround(rate / 10)));  // 100 ms
        Reset();
    }

    // Starts a new measurement (a new song, the reset button).
    void Reset() {
        for (auto& s : state_) s = {};
        sum_ = 0;
        count_ = 0;
        blocks_ = 0;
        std::fill(std::begin(ring_), std::end(ring_), 0.0);
        std::fill(std::begin(intCount_), std::end(intCount_), 0u);
        std::fill(std::begin(intEnergy_), std::end(intEnergy_), 0.0);
        std::fill(std::begin(stCount_), std::end(stCount_), 0u);
        std::fill(std::begin(stEnergy_), std::end(stEnergy_), 0.0);
    }

    void Add(float l, float r) {
        const double kl = Weigh(0, l), kr = Weigh(1, r);
        sum_ += kl * kl + kr * kr;  // L and R weigh 1 each
        if (++count_ == blockLen_) CloseBlock();
    }

    // LUFS, kLoudnessNone until the window is full.
    float Momentary() const { return blocks_ >= 4 ? Lufs(Mean(4)) : kLoudnessNone; }
    float ShortTerm() const { return blocks_ >= kRing ? Lufs(Mean(kRing)) : kLoudnessNone; }
    // Gated at -70 LUFS and 10 LU under the ungated mean (BS.1770-4).
    float Integrated() const { return Gated(intCount_, intEnergy_, 10, nullptr); }
    // LU between the 10th and 95th percentile of the short-term loudness, gated at -70 LUFS and
    // 20 LU under its mean (EBU Tech 3342).
    float Range() const {
        float lra = kLoudnessNone;
        Gated(stCount_, stEnergy_, 20, &lra);
        return lra;
    }

private:
    struct Biquad {
        double b0, b1, b2, a1, a2;
    };
    struct Stage {
        double s1 = 0, s2 = 0;
    };
    struct Channel {
        Stage shelf, high;
    };
    static constexpr int kRing = 30;  // 100 ms blocks: 3 s
    // 0.1 LU bins from -70 to +10 LUFS. shortcut: the relative gate falls on a bin edge (±0.1 LU),
    // exact enough for a meter; keep every block's value if a measurement must match a reference tool.
    static constexpr int kBins = 800;

    static double Run(const Biquad& f, Stage& s, double x) {  // transposed direct form II
        const double y = f.b0 * x + s.s1;
        s.s1 = f.b1 * x - f.a1 * y + s.s2;
        s.s2 = f.b2 * x - f.a2 * y;
        return y;
    }
    double Weigh(int ch, float x) {
        const double v = std::isfinite(x) ? x : 0.0;
        return Run(high_, state_[ch].high, Run(shelf_, state_[ch].shelf, v));
    }
    static float Lufs(double energy) {
        return energy > 1e-12 ? static_cast<float>(-0.691 + 10 * std::log10(energy)) : kLoudnessNone;
    }
    static int Bin(float lufs) { return std::clamp(static_cast<int>(std::floor((lufs + 70) * 10)), 0, kBins - 1); }
    double Mean(int n) const {
        double e = 0;
        for (int i = 0; i < n; ++i) e += ring_[(blocks_ - 1 - i) % kRing];
        return e / n;
    }

    void CloseBlock() {
        ring_[blocks_ % kRing] = sum_ / blockLen_;
        ++blocks_;
        sum_ = 0;
        count_ = 0;
        // Gating blocks of 400 ms overlap by 75 %: one ends every 100 ms; short-term values the same.
        if (blocks_ >= 4) Count(Mean(4), intCount_, intEnergy_);
        if (blocks_ >= kRing) Count(Mean(kRing), stCount_, stEnergy_);
    }
    static void Count(double energy, uint32_t* counts, double* energies) {
        const float l = Lufs(energy);
        if (l <= -70) return;
        ++counts[Bin(l)];
        energies[Bin(l)] += energy;
    }
    // The gated mean in LUFS; with `range`, also the 10th-95th percentile spread of what passed.
    static float Gated(const uint32_t* counts, const double* energies, float relative, float* range) {
        double e = 0;
        uint64_t n = 0;
        for (int i = 0; i < kBins; ++i) e += energies[i], n += counts[i];
        if (!n) return kLoudnessNone;
        const int from = std::clamp(static_cast<int>(std::ceil((Lufs(e / n) - relative + 70) * 10)), 0, kBins);
        e = 0;
        n = 0;
        for (int i = from; i < kBins; ++i) e += energies[i], n += counts[i];
        if (!n) return kLoudnessNone;
        if (range) {
            const auto at = [&](double share) {  // the bin centre holding that share of the blocks
                uint64_t seen = 0;
                for (int i = from; i < kBins; ++i)
                    if ((seen += counts[i]) > share * (n - 1)) return -70 + (i + 0.5f) / 10;
                return 10.0f;
            };
            *range = at(0.95) - at(0.10);
        }
        return Lufs(e / n);
    }

    Biquad shelf_{1, 0, 0, 0, 0}, high_{1, 0, 0, 0, 0};
    Channel state_[2];
    int blockLen_ = 4800;
    double sum_ = 0;
    int count_ = 0;
    uint64_t blocks_ = 0;
    double ring_[kRing] = {};
    uint32_t intCount_[kBins] = {}, stCount_[kBins] = {};
    double intEnergy_[kBins] = {}, stEnergy_[kBins] = {};
};

}  // namespace audio
