#pragma once
// Decisions and gain curves of the crossfade between songs (Settings > Audio > Crossfade). Pure
// functions, so tests/core_tests.cpp checks them without mpv; audio/Crossfader.cpp runs them.
//
// Timeline of one transition, in the outgoing file's own time (D = its duration, F = the fade):
//
//   S = T - kHandoff - kPreroll   the helper instance, paused at S since a few seconds, starts
//   T - kHandoff .. T             the same song plays in both instances; main fades it out and the
//                                 helper fades it in (linear, the two gains always add up to 1)
//   T = D - F                     main jumps to the next song, which fades in (equal power) while
//   T .. D                        the helper plays the old song's last F seconds, fading out
//
// So the queue, the title and everything per file move on at T, the start of the fade.

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <string>
#include <string_view>

namespace audio {

inline constexpr double kHandoff = 0.25;  // seconds both instances play the same audio
inline constexpr double kPreroll = 1.6;   // the helper plays this long (silent) before the handoff
inline constexpr double kArmLead = 3.0;   // the helper is started this long before S
inline constexpr double kMinFade = 1.0;
inline constexpr double kMaxFade = 12.0;
inline constexpr double kMinDb = -150.0;  // mpv volume-gain-min that Moonbow sets

// The fade a song of `duration` seconds gets: at most half of it minus a second, so a short song
// is never all fade; 0 = no crossfade.
inline double EffectiveFade(double requested, double duration) {
    if (!(requested >= kMinFade) || !(duration > 0)) return 0;
    const double f = std::min({requested, kMaxFade, duration / 2 - 1});
    return f >= kMinFade ? f : 0;
}

// Where the outgoing file must be for the helper to start (S), or -1 without a fade.
inline double HelperStart(double fade, double duration) {
    if (fade <= 0) return -1;
    return duration - fade - kHandoff - kPreroll;
}

enum class ArmDecision { Wait, Arm, TooLate };

// Called on time updates while a next file is queued: Wait until S - kArmLead, then Arm; a
// position past S - margin (a seek into the last seconds) plays on gaplessly instead. A length
// not known yet (mpv reports it after the file loads) waits for it.
inline ArmDecision DecideArm(double position, double duration, double fade) {
    if (!(duration > 0)) return ArmDecision::Wait;
    const double s = HelperStart(fade, duration);
    if (s < 0) return ArmDecision::TooLate;
    if (position < s - kArmLead) return ArmDecision::Wait;
    if (position > s - 0.4) return ArmDecision::TooLate;
    return ArmDecision::Arm;
}

inline std::wstring FoldAlbum(std::wstring_view s) {
    std::wstring out;
    for (wchar_t c : s)
        if (!std::iswspace(c)) out.push_back(static_cast<wchar_t>(std::towlower(c)));
    return out;
}

inline std::wstring_view ParentOf(std::wstring_view path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring_view::npos ? std::wstring_view() : path.substr(0, slash);
}

// "Only between songs of different albums": both album tags known -> compare them (case and
// spaces ignored); otherwise songs in the same folder count as one album.
inline bool SameAlbum(std::wstring_view albumA, std::wstring_view albumB, std::wstring_view pathA,
                      std::wstring_view pathB) {
    const std::wstring a = FoldAlbum(albumA), b = FoldAlbum(albumB);
    if (!a.empty() && !b.empty()) return a == b;
    const std::wstring_view pa = ParentOf(pathA), pb = ParentOf(pathB);
    if (pa.size() != pb.size() || pa.empty()) return false;
    for (size_t i = 0; i < pa.size(); ++i)
        if (std::towlower(pa[i]) != std::towlower(pb[i])) return false;
    return true;
}

// Equal-power curves for two different songs (u = 0..1 through the fade).
inline double FadeInGain(double u) {
    u = std::clamp(u, 0.0, 1.0);
    return std::sin(u * 1.5707963267948966);
}
inline double FadeOutGain(double u) {
    u = std::clamp(u, 0.0, 1.0);
    return std::cos(u * 1.5707963267948966);
}

// Linear gain -> mpv volume-gain in dB, floored at kMinDb.
inline double GainToDb(double g) {
    if (!(g > 0)) return kMinDb;
    return std::max(kMinDb, std::min(0.0, 20.0 * std::log10(g)));
}

}  // namespace audio
