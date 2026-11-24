#pragma once
// Finding a series' intro by its sound: the opening song is the same in every episode, so the
// longest stretch of audio two episodes share near their start is the intro. Header-only and
// free of I/O, so tests/core_tests.cpp checks it on synthetic signals.
//
// - Fingerprint(): every 0.1 s a 32-bit word from the energy of 33 bands between 300 and 2000 Hz:
//   bit m says whether the difference between bands m and m+1 grew since the last frame (the
//   Philips/Haitsma-Kalker scheme behind Chromaprint). Robust to volume, codecs and noise; quiet
//   frames are marked and never match, so silence in two files is not "shared".
// - FindShared(): candidate alignments from words that are equal in both prints (an index of one,
//   a histogram of offsets), then along each the longest run of near-equal words (at most 12 of
//   32 bits differ), allowing short dropouts. 15 s to 200 s counts as an intro.

#include "Spectrum.h"  // Fft

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace audio::intro {

inline constexpr int kRate = 5512;               // decode rate: bands up to 2 kHz need little more
inline constexpr int kFrame = 2048;              // 0.37 s analysis window
inline constexpr int kHop = 551;                 // 0.1 s between words
inline constexpr double kHopSeconds = static_cast<double>(kHop) / kRate;
inline constexpr uint32_t kQuiet = 0xFFFFFFFFu;  // marks a frame too quiet to compare (never matches)

struct Print {
    std::vector<uint32_t> words;
};

inline Print Fingerprint(const int16_t* samples, size_t n) {
    Print p;
    if (n < static_cast<size_t>(kFrame)) return p;
    constexpr int kBandsN = 33;
    int edges[kBandsN + 1];
    for (int i = 0; i <= kBandsN; ++i) {
        const double f = 300.0 * std::pow(2000.0 / 300.0, static_cast<double>(i) / kBandsN);
        edges[i] = std::clamp(static_cast<int>(f * kFrame / kRate), 1, kFrame / 2 - 1);
    }
    std::vector<float> window(kFrame), re(kFrame), im(kFrame);
    for (int i = 0; i < kFrame; ++i)
        window[i] = static_cast<float>(0.5 - 0.5 * std::cos(2 * 3.14159265358979323846 * i / (kFrame - 1)));
    double prev[kBandsN] = {};
    bool havePrev = false;
    for (size_t at = 0; at + kFrame <= n; at += kHop) {
        double power = 0;
        for (int i = 0; i < kFrame; ++i) {
            const float s = samples[at + i] / 32768.0f;
            power += static_cast<double>(s) * s;
            re[i] = s * window[i];
            im[i] = 0;
        }
        Fft(re.data(), im.data(), kFrame);
        double band[kBandsN], total = 0;
        for (int b = 0; b < kBandsN; ++b) {
            double e = 0;
            for (int k = edges[b]; k < std::max(edges[b] + 1, edges[b + 1]); ++k)
                e += static_cast<double>(re[k]) * re[k] + static_cast<double>(im[k]) * im[k];
            band[b] = e;
            total += e;
        }
        // Bands 30 dB under the frame's average sit on one floor: their noise would flip bits at
        // random between two copies of the same sound.
        const double floor = std::max(1e-12, total / kBandsN * 1e-3);
        for (double& e : band) e = std::log(std::max(e, floor));
        // About -50 dBFS RMS and below: silence or near it.
        const bool quiet = power / kFrame < 1e-5;
        uint32_t word = 0;
        if (havePrev)
            for (int m = 0; m < 32; ++m)
                if ((band[m] - band[m + 1]) - (prev[m] - prev[m + 1]) > 0) word |= 1u << m;
        std::copy(band, band + kBandsN, prev);
        if (!havePrev) {
            havePrev = true;
            continue;  // the first frame has nothing to compare with
        }
        p.words.push_back(quiet || word == kQuiet ? kQuiet : word);
    }
    return p;
}

struct Shared {
    double startA = 0, endA = 0;  // seconds in the first print
    double startB = 0, endB = 0;  // and in the second
    double Length() const { return endA - startA; }
};

// The longest stretch both prints share (15..200 s); a zero-length result when there is none.
inline Shared FindShared(const Print& a, const Print& b, double minSeconds = 15, double maxSeconds = 200) {
    Shared best;
    const int na = static_cast<int>(a.words.size()), nb = static_cast<int>(b.words.size());
    if (na == 0 || nb == 0) return best;
    // Each half of a word is a key: two copies of a sound share whole 32-bit words too rarely
    // once a codec flips a bit in ten, but a 16-bit half survives often enough to vote.
    auto key = [](uint32_t w, int half) { return (static_cast<uint64_t>(half ? w >> 16 : w & 0xFFFF) << 1) | half; };
    std::unordered_map<uint64_t, std::vector<int>> index;
    index.reserve(static_cast<size_t>(nb) * 2);
    for (int j = 0; j < nb; ++j)
        if (b.words[j] != kQuiet)
            for (int half = 0; half < 2; ++half) index[key(b.words[j], half)].push_back(j);
    std::vector<int> votes(static_cast<size_t>(na + nb), 0);  // offset j - i + na
    for (int i = 0; i < na; ++i) {
        if (a.words[i] == kQuiet) continue;
        for (int half = 0; half < 2; ++half) {
            const auto it = index.find(key(a.words[i], half));
            if (it == index.end() || it->second.size() > 50) continue;  // a common key says nothing
            for (int j : it->second) ++votes[static_cast<size_t>(j - i + na)];
        }
    }
    // The strongest alignments (an offset and its neighbours vote together).
    std::vector<std::pair<int, int>> candidates;  // (votes over 3 offsets, offset)
    for (int o = 1; o + 1 < static_cast<int>(votes.size()); ++o) {
        const int v = votes[o - 1] + votes[o] + votes[o + 1];
        if (v >= 8 && votes[o] >= votes[o - 1] && votes[o] >= votes[o + 1]) candidates.push_back({v, o - na});
    }
    std::sort(candidates.begin(), candidates.end(), [](auto& x, auto& y) { return x.first > y.first; });
    if (candidates.size() > 6) candidates.resize(6);
    const int minRun = static_cast<int>(minSeconds / kHopSeconds), maxRun = static_cast<int>(maxSeconds / kHopSeconds);
    constexpr int kMaxGap = 8;  // 0.8 s of differing frames inside a run (a sound effect, a cut)
    for (const auto& [v, offset] : candidates) {
        int runStart = -1, lastGood = -1;
        auto close = [&](int endI) {
            if (runStart < 0) return;
            const int len = endI - runStart + 1;
            if (len >= minRun && len <= maxRun && len * kHopSeconds > best.Length()) {
                best.startA = runStart * kHopSeconds;
                best.endA = (endI + 1) * kHopSeconds + static_cast<double>(kFrame) / kRate;
                best.startB = (runStart + offset) * kHopSeconds;
                best.endB = (endI + 1 + offset) * kHopSeconds + static_cast<double>(kFrame) / kRate;
            }
            runStart = -1;
        };
        for (int i = std::max(0, -offset); i < na && i + offset < nb; ++i) {
            const uint32_t x = a.words[i], y = b.words[i + offset];
            const bool good = x != kQuiet && y != kQuiet && std::popcount(x ^ y) <= 12;
            if (good) {
                if (runStart < 0) runStart = i;
                lastGood = i;
            } else if (runStart >= 0 && i - lastGood > kMaxGap) {
                close(lastGood);
            }
        }
        close(lastGood);
    }
    return best;
}

}  // namespace audio::intro
