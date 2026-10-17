#include "app/Actions.h"

#include "Util.h"
#include "app/App.h"
#include "app/Dialogs.h"
#include "app/I18n.h"
#include "app/MainWindow.h"
#include "app/Shell.h"
#include "app/player/ControlBar.h"
#include "app/player/PlayerView.h"

#include <cmath>
#include <map>

namespace app {

using namespace ui;

namespace {

using Keys = std::vector<std::pair<UINT, Mods>>;
constexpr Mods kNone{};
constexpr Mods kCtrl{true, false, false};
constexpr Mods kShift{false, true, false};
constexpr Mods kAlt{false, false, true};
constexpr Mods kCtrlShift{true, true, false};

const std::vector<ActionInfo>& Table() {
    static const std::vector<ActionInfo> table = {
        {Action::None, "none", L"", L"", {}},
        {Action::PlayPause, "playback.play_pause", L"Play / Pause", L"Playback", {{VK_SPACE, kNone}, {VK_MEDIA_PLAY_PAUSE, kNone}}},
        {Action::Stop, "playback.stop", L"Stop", L"Playback", {{'S', kNone}, {VK_MEDIA_STOP, kNone}}},
        {Action::Previous, "playback.previous", L"Previous item", L"Playback", {{'P', kNone}, {VK_PRIOR, kNone}, {VK_MEDIA_PREV_TRACK, kNone}}},
        {Action::Next, "playback.next", L"Next item", L"Playback", {{'N', kNone}, {VK_NEXT, kNone}, {VK_MEDIA_NEXT_TRACK, kNone}}},
        {Action::SeekForward, "playback.seek_forward", L"Seek forward 10 s", L"Playback", {{VK_RIGHT, kNone}}},
        {Action::SeekBackward, "playback.seek_backward", L"Seek backward 10 s", L"Playback", {{VK_LEFT, kNone}}},
        {Action::SeekForwardLong, "playback.seek_forward_long", L"Seek forward 1 min", L"Playback", {{VK_RIGHT, kCtrl}}},
        {Action::SeekBackwardLong, "playback.seek_backward_long", L"Seek backward 1 min", L"Playback", {{VK_LEFT, kCtrl}}},
        {Action::SeekForwardShort, "playback.seek_forward_short", L"Seek forward 3 s", L"Playback", {{VK_RIGHT, kShift}}},
        {Action::SeekBackwardShort, "playback.seek_backward_short", L"Seek backward 3 s", L"Playback", {{VK_LEFT, kShift}}},
        {Action::FrameStep, "playback.frame_step", L"Next frame", L"Playback", {{VK_OEM_PERIOD, kNone}}},
        {Action::FrameBackStep, "playback.frame_back", L"Previous frame", L"Playback", {{VK_OEM_COMMA, kNone}}},
        {Action::SpeedUp, "playback.speed_up", L"Faster", L"Playback", {{VK_OEM_6, kNone}, {VK_OEM_PLUS, kCtrl}}},
        {Action::SpeedDown, "playback.speed_down", L"Slower", L"Playback", {{VK_OEM_4, kNone}, {VK_OEM_MINUS, kCtrl}}},
        {Action::SpeedReset, "playback.speed_reset", L"Normal speed", L"Playback", {{VK_BACK, kNone}, {'0', kCtrl}}},
        {Action::NextChapter, "playback.next_chapter", L"Next chapter", L"Playback", {{VK_NEXT, kShift}}},
        {Action::PreviousChapter, "playback.prev_chapter", L"Previous chapter", L"Playback", {{VK_PRIOR, kShift}}},
        {Action::Replay, "playback.replay", L"Replay from start", L"Playback", {{VK_HOME, kNone}}},
        {Action::ToggleRepeat, "playback.repeat", L"Repeat", L"Playback", {{'L', kNone}}},
        {Action::ToggleShuffle, "playback.shuffle", L"Shuffle", L"Playback", {{'H', kNone}}},
        {Action::ABLoop, "playback.ab_loop", L"A-B loop", L"Playback", {{'L', kShift}}},
        {Action::VolumeUp, "audio.volume_up", L"Volume up", L"Audio", {{VK_UP, kNone}, {VK_VOLUME_UP, kNone}}},
        {Action::VolumeDown, "audio.volume_down", L"Volume down", L"Audio", {{VK_DOWN, kNone}, {VK_VOLUME_DOWN, kNone}}},
        {Action::Mute, "audio.mute", L"Mute", L"Audio", {{'M', kNone}, {VK_VOLUME_MUTE, kNone}}},
        {Action::CycleAudio, "audio.cycle", L"Next audio track", L"Audio", {{'A', kNone}, {'B', kNone}}},
        {Action::AudioDelayUp, "audio.delay_up", L"Audio delay +50 ms", L"Audio", {{'K', kNone}}},
        {Action::AudioDelayDown, "audio.delay_down", L"Audio delay −50 ms", L"Audio", {{'J', kNone}}},
        {Action::CycleSubtitles, "subtitles.cycle", L"Next subtitle track", L"Subtitles", {{'V', kNone}}},
        {Action::ToggleSubtitles, "subtitles.toggle", L"Show / hide subtitles", L"Subtitles", {{'V', kShift}}},
        {Action::SubtitleDelayUp, "subtitles.delay_up", L"Subtitle delay +50 ms", L"Subtitles", {{'H', kShift}}},
        {Action::SubtitleDelayDown, "subtitles.delay_down", L"Subtitle delay −50 ms", L"Subtitles", {{'G', kShift}}},
        {Action::SubtitleBigger, "subtitles.bigger", L"Bigger subtitles", L"Subtitles", {{VK_OEM_PLUS, kAlt}}},
        {Action::SubtitleSmaller, "subtitles.smaller", L"Smaller subtitles", L"Subtitles", {{VK_OEM_MINUS, kAlt}}},
        {Action::Fullscreen, "video.fullscreen", L"Fullscreen", L"Video", {{'F', kNone}, {VK_RETURN, kAlt}, {VK_F11, kNone}}},
        {Action::ExitFullscreen, "video.exit_fullscreen", L"Exit fullscreen", L"Video", {{VK_ESCAPE, kNone}}},
        {Action::HideUI, "video.hide_ui", L"Hide UI", L"Video", {{VK_OEM_5, kCtrl}}},
        {Action::PictureInPicture, "video.pip", L"Picture in Picture", L"Video", {{'P', kCtrlShift}}},
        {Action::Snapshot, "video.snapshot", L"Snapshot", L"Video", {{'S', kCtrlShift}}},
        {Action::Record, "video.record", L"Record", L"Video", {{'R', kCtrlShift}}},
        {Action::ZoomIn, "video.zoom_in", L"Zoom in", L"Video", {{'Z', kNone}}},
        {Action::ZoomOut, "video.zoom_out", L"Zoom out", L"Video", {{'Z', kShift}}},
        {Action::ZoomReset, "video.zoom_reset", L"Reset zoom", L"Video", {{'Z', kAlt}}},
        {Action::CycleAspect, "video.aspect", L"Next aspect ratio", L"Video", {{'X', kNone}}},
        {Action::Rotate, "video.rotate", L"Rotate 90°", L"Video", {{'R', kCtrl}}},
        {Action::Deinterlace, "video.deinterlace", L"Deinterlace", L"Video", {{'D', kNone}}},
        {Action::OpenFile, "media.open_file", L"Open file", L"Media", {{'O', kCtrl}}},
        {Action::OpenFolder, "media.open_folder", L"Open folder", L"Media", {{'F', kCtrl}}},
        {Action::OpenNetwork, "media.open_network", L"Open network stream", L"Media", {{'N', kCtrl}}},
        {Action::OpenSubtitles, "media.open_subtitles", L"Add subtitle file", L"Media", {{'O', kCtrlShift}}},
        {Action::CloseMedia, "media.close", L"Close media", L"Media", {{'W', kCtrl}}},
        {Action::GoHome, "media.home", L"Library", L"Media", {{VK_HOME, kAlt}}},
        {Action::ToggleSidebar, "media.sidebar", L"Toggle sidebar", L"Media", {{'B', kCtrl}}},
        {Action::TogglePlaylist, "media.playlist", L"Queue", L"Media", {{'L', kCtrl}}},
        {Action::Search, "media.search", L"Search", L"Media", {{'K', kCtrl}}},
        {Action::Back, "media.back", L"Back", L"Media", {{VK_LEFT, kAlt}, {VK_BROWSER_BACK, kNone}}},
        {Action::Forward, "media.forward", L"Forward", L"Media", {{VK_RIGHT, kAlt}, {VK_BROWSER_FORWARD, kNone}}},
        {Action::AddBookmark, "media.bookmark", L"Add bookmark", L"Media", {{'D', kCtrl}}},
        {Action::ToggleFavorite, "media.favorite", L"Favorite", L"Media", {{'F', kCtrlShift}}},
        {Action::ShowInfo, "media.info", L"Media information", L"Media", {{'I', kCtrl}}},
        {Action::Settings, "app.settings", L"Settings", L"App", {{'P', kCtrl}, {VK_OEM_COMMA, kCtrl}}},
        {Action::KeyboardShortcuts, "app.shortcuts", L"Keyboard shortcuts", L"App", {{VK_F1, kNone}, {VK_OEM_2, kCtrl}}},
        {Action::CustomizePlayer, "app.customize", L"Customize player", L"App", {}},
        {Action::Quit, "app.quit", L"Quit", L"App", {{'Q', kCtrl}}},
    };
    return table;
}

std::map<Action, Keys>& Overrides() {
    static std::map<Action, Keys> overrides;
    return overrides;
}

bool SameMods(const Mods& a, const Mods& b) {
    return a.ctrl == b.ctrl && a.shift == b.shift && a.alt == b.alt;
}

MainWindow* Win() {
    return TheApp().main();
}

void Toast(const std::wstring& text, const char* icon = "") {
    if (Win() && TheApp().config().GetBool("interface.osdMessages")) ShowToast(Win(), text, icon, 1200);
}

const PlayerState& PS() {
    return TheApp().player().State();
}

// Messages about things mpv reports back a moment later.
enum class PendingOsd { None, Audio, Subtitles, Chapter };
PendingOsd g_pendingOsd = PendingOsd::None;
double g_pendingUntil = 0;

void ExpectOsd(PendingOsd kind) {
    g_pendingOsd = kind;
    g_pendingUntil = ui::AnimFloat::Now() + 1500;
}

std::wstring SelectedTrackText(const wchar_t* type) {
    for (const Track& t : PS().tracks)
        if (t.selected && t.type == type) return t.Label();
    return Tr(L"off");
}

std::wstring Clock(double t) {
    return FormatTime(t, PS().duration >= 3600);
}

void SeekBy(double seconds) {
    const PlayerState& s = PS();
    if (!s.fileLoaded) return;
    TheApp().player().SeekRelative(seconds);
    const double target = std::clamp(s.position + seconds, 0.0, s.duration > 0 ? s.duration : s.position + seconds);
    if (s.duration > 0)
        Toast(Clock(target) + L" / " + Clock(s.duration), seconds >= 0 ? "forward-10-sec" : "backward-10-sec");
}

void ChangeSpeed(double delta, bool reset = false) {
    const double steps[] = {0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0, 2.5, 3.0, 4.0};
    double cur = PS().speed, next = 1.0;
    if (!reset) {
        next = cur;
        if (delta > 0) {
            for (double s : steps)
                if (s > cur + 0.01) { next = s; break; }
        } else {
            for (auto it = std::rbegin(steps); it != std::rend(steps); ++it)
                if (*it < cur - 0.01) { next = *it; break; }
        }
    }
    TheApp().player().SetSpeed(next);
    wchar_t buf[48];
    swprintf_s(buf, L"%s %g×", Tr(L"Speed"), next);
    Toast(buf, "speed");
}

void Zoom(double delta, bool reset = false) {
    Player& p = TheApp().player();
    double z = reset ? 0 : p.GetPropertyDouble("video-zoom", 0) + delta;
    z = std::clamp(z, -2.0, 3.0);
    p.SetProperty("video-zoom", z);
    Toast(Fmt(Tr(L"Zoom {0}%"), static_cast<long long>(std::lround(std::pow(2.0, z) * 100))), "zoom");
}

}  // namespace

const std::vector<ActionInfo>& AllActions() {
    return Table();
}

const ActionInfo& Info(Action a) {
    static const auto index = [] {
        std::map<Action, size_t> m;
        for (size_t i = 0; i < Table().size(); ++i) m[Table()[i].action] = i;
        return m;
    }();
    auto it = index.find(a);
    return Table()[it != index.end() ? it->second : 0];
}

const Keys& Bindings(Action a) {
    auto it = Overrides().find(a);
    return it != Overrides().end() ? it->second : Info(a).keys;
}

void SetBindings(Action a, Keys keys) {
    if (keys.empty())
        Overrides().erase(a);
    else
        Overrides()[a] = std::move(keys);
}

std::wstring KeyText(UINT vk, const Mods& mods) {
    if (vk == 0) return {};  // "unassigned" marker set by Settings > Controls
    std::wstring s;
    if (mods.ctrl) s += L"Ctrl + ";
    if (mods.alt) s += L"Alt + ";
    if (mods.shift) s += L"Shift + ";
    switch (vk) {
        case VK_SPACE: return s + L"Space";
        case VK_LEFT: return s + L"←";
        case VK_RIGHT: return s + L"→";
        case VK_UP: return s + L"↑";
        case VK_DOWN: return s + L"↓";
        case VK_PRIOR: return s + L"PgUp";
        case VK_NEXT: return s + L"PgDn";
        case VK_HOME: return s + L"Home";
        case VK_END: return s + L"End";
        case VK_RETURN: return s + L"Enter";
        case VK_ESCAPE: return s + L"Esc";
        case VK_BACK: return s + L"Backspace";
        case VK_TAB: return s + L"Tab";
        case VK_DELETE: return s + L"Del";
        case VK_OEM_PERIOD: return s + L".";
        case VK_OEM_COMMA: return s + L",";
        case VK_OEM_4: return s + L"[";
        case VK_OEM_6: return s + L"]";
        case VK_OEM_5: return s + L"\\";
        case VK_OEM_2: return s + L"/";
        case VK_OEM_PLUS: return s + L"+";
        case VK_OEM_MINUS: return s + L"−";
        case VK_MEDIA_PLAY_PAUSE: return s + L"Media Play";
        case VK_MEDIA_STOP: return s + L"Media Stop";
        case VK_MEDIA_NEXT_TRACK: return s + L"Media Next";
        case VK_MEDIA_PREV_TRACK: return s + L"Media Prev";
        case VK_VOLUME_UP: return s + L"Volume Up";
        case VK_VOLUME_DOWN: return s + L"Volume Down";
        case VK_VOLUME_MUTE: return s + L"Volume Mute";
        case VK_BROWSER_BACK: return s + L"Browser Back";
        case VK_BROWSER_FORWARD: return s + L"Browser Forward";
        default: break;
    }
    if (vk >= VK_F1 && vk <= VK_F24) return s + L"F" + std::to_wstring(vk - VK_F1 + 1);
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) return s + static_cast<wchar_t>(vk);
    wchar_t name[64] = {};
    const UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    if (GetKeyNameTextW(static_cast<LONG>(scan << 16), name, 64) > 0) return s + name;
    return s + L"?";
}

std::wstring ShortcutText(Action a) {
    for (const auto& [vk, mods] : Bindings(a))
        if (vk != 0) return KeyText(vk, mods);
    return L"";
}

Action ActionForKey(const KeyEvent& e) {
    for (const ActionInfo& info : Table())
        for (const auto& [vk, mods] : Bindings(info.action))
            if (vk == e.key && SameMods(mods, e.mods)) return info.action;
    return Action::None;
}

bool Enabled(Action a) {
    const PlayerState& s = PS();
    switch (a) {
        case Action::PlayPause:
        case Action::Stop:
        case Action::Snapshot:
        case Action::Record:
        case Action::SeekForward:
        case Action::SeekBackward:
        case Action::CloseMedia:
        case Action::AddBookmark:
        case Action::ShowInfo:
        case Action::OpenSubtitles: return s.fileLoaded;
        case Action::Previous: return TheApp().queue().PrevIndex() >= 0 || s.fileLoaded;
        case Action::Next: return TheApp().queue().NextIndex(true) >= 0;
        case Action::NextChapter:
        case Action::PreviousChapter: return !s.chapters.empty();
        default: return true;
    }
}

void Run(Action a) {
    App& app = TheApp();
    Player& p = app.player();
    MainWindow* w = Win();
    const PlayerState& s = PS();
    switch (a) {
        case Action::None:
        case Action::Count: break;
        case Action::PlayPause:
            if (s.fileLoaded)
                p.TogglePause();
            else if (app.queue().CurrentItem())
                app.PlayIndex(app.queue().Current());
            else
                app.OpenFileDialog();
            break;
        case Action::Stop:
            if (!s.fileLoaded) {
                app.StopPlayback();
                break;
            }
            p.SetPause(true);
            p.SeekAbsolute(0, true);
            Toast(Tr(L"Stopped"), "stop");
            break;
        case Action::Previous: app.Previous(); break;
        case Action::Next:
            if (!app.Next(true)) Toast(Tr(L"End of the list"), "next");
            break;
        case Action::SeekForward: SeekBy(10); break;
        case Action::SeekBackward: SeekBy(-10); break;
        case Action::SeekForwardLong: SeekBy(60); break;
        case Action::SeekBackwardLong: SeekBy(-60); break;
        case Action::SeekForwardShort: SeekBy(3); break;
        case Action::SeekBackwardShort: SeekBy(-3); break;
        case Action::FrameStep: p.FrameStep(false); break;
        case Action::FrameBackStep: p.FrameStep(true); break;
        case Action::SpeedUp: ChangeSpeed(+1); break;
        case Action::SpeedDown: ChangeSpeed(-1); break;
        case Action::SpeedReset: ChangeSpeed(0, true); break;
        case Action::NextChapter:
        case Action::PreviousChapter:
            if (s.chapters.empty()) {
                Toast(Tr(L"No chapters"), "chapters");
                break;
            }
            p.SeekChapter(a == Action::NextChapter ? 1 : -1);
            ExpectOsd(PendingOsd::Chapter);
            break;
        case Action::Replay: p.SeekAbsolute(0, true); p.SetPause(false); break;
        case Action::ToggleRepeat: {
            PlayQueue& q = app.queue();
            const Repeat next = q.RepeatMode() == Repeat::Off ? Repeat::All : q.RepeatMode() == Repeat::All ? Repeat::One : Repeat::Off;
            // Through the setting: it persists and drives mpv's loop-file.
            app.config().SetString("playback.repeat", next == Repeat::One ? "one" : next == Repeat::All ? "all" : "off");
            Toast(next == Repeat::Off ? Tr(L"Repeat off") : next == Repeat::All ? Tr(L"Repeat all") : Tr(L"Repeat one"),
                  "repeat_right");
            break;
        }
        case Action::ToggleShuffle:
            app.config().SetBool("playback.shuffle", !app.queue().Shuffle());
            Toast(app.queue().Shuffle() ? Tr(L"Shuffle on") : Tr(L"Shuffle off"), "shuffle_right");
            break;
        case Action::ABLoop: {
            // mpv's ab-loop cycles: set A, set B, clear.
            const std::string pa = p.GetPropertyString("ab-loop-a"), pb = p.GetPropertyString("ab-loop-b");
            p.Command({"ab-loop"});
            if (pa.empty() || pa == "no")
                Toast(Fmt(Tr(L"Loop start {0}"), {Clock(s.position)}), "repeat_right");
            else if (pb.empty() || pb == "no")
                Toast(Fmt(Tr(L"Loop {0} – {1}"), {Clock(std::atof(pa.c_str())), Clock(s.position)}), "repeat_right");
            else
                Toast(Tr(L"Loop off"), "repeat_right");
            break;
        }
        case Action::VolumeUp:
        case Action::VolumeDown:
        {
            if (s.mute) p.SetMute(false);
            const double step = std::clamp(app.config().GetDouble("audio.volumeStep"), 1.0, 25.0);
            p.SetVolume(std::round(s.volume + (a == Action::VolumeUp ? step : -step)));
            Toast(Fmt(Tr(L"Volume {0}%"), static_cast<long long>(std::lround(PS().volume))),
                  VolumeIcon(PS().volume, false));
            break;
        }
        case Action::Mute:
            p.ToggleMute();
            Toast(PS().mute ? Tr(L"Muted") : Tr(L"Sound on"), VolumeIcon(PS().volume, PS().mute));
            break;
        case Action::CycleAudio:
            p.CycleTrack("aid");
            ExpectOsd(PendingOsd::Audio);
            break;
        case Action::AudioDelayUp:
        case Action::AudioDelayDown: {
            const double d = p.GetPropertyDouble("audio-delay", 0) + (a == Action::AudioDelayUp ? 0.05 : -0.05);
            p.SetProperty("audio-delay", d);
            Toast(Fmt(Tr(L"Audio delay {0} ms"), static_cast<long long>(std::lround(d * 1000))), "audio");
            break;
        }
        case Action::CycleSubtitles:
            p.CycleTrack("sid");
            ExpectOsd(PendingOsd::Subtitles);
            break;
        case Action::ToggleSubtitles: {
            const bool shown = p.GetPropertyString("sub-visibility") != "no";
            p.Command({"cycle", "sub-visibility"});
            Toast(shown ? Tr(L"Subtitles hidden") : Tr(L"Subtitles shown"), "subtitles");
            break;
        }
        case Action::SubtitleDelayUp:
        case Action::SubtitleDelayDown: {
            const double d = p.GetPropertyDouble("sub-delay", 0) + (a == Action::SubtitleDelayUp ? 0.05 : -0.05);
            p.SetProperty("sub-delay", d);
            Toast(Fmt(Tr(L"Subtitle delay {0} ms"), static_cast<long long>(std::lround(d * 1000))), "subtitles");
            break;
        }
        case Action::SubtitleBigger: p.Command({"add", "sub-scale", "0.1"}); break;
        case Action::SubtitleSmaller: p.Command({"add", "sub-scale", "-0.1"}); break;
        case Action::Fullscreen:
            if (!w) break;
            if (!app.PlayerShown() && s.fileLoaded) app.ShowPlayer(true);
            w->SetFullscreen(!w->IsFullscreen(), app.config().GetString("interface.fullscreenScreen") == "primary");
            w->PokeChrome();
            break;
        case Action::ExitFullscreen:
            if (w && w->IsFullscreen()) w->SetFullscreen(false);
            else if (w && w->UiHidden()) w->SetUiHidden(false);
            break;
        case Action::HideUI:
            if (w) w->SetUiHidden(!w->UiHidden());
            break;
        case Action::PictureInPicture:
            if (!w) break;
            if (!w->PictureInPicture() && !s.fileLoaded) {
                Toast(Tr(L"Nothing is playing"), "pip");
                break;
            }
            w->SetPictureInPicture(!w->PictureInPicture());
            break;
        case Action::Snapshot: {
            if (!s.fileLoaded) break;
            p.Screenshot();
            std::wstring dir = Utf8ToWide(app.config().GetString("video.screenshotDir"));
            if (dir.empty()) dir = KnownFolderPath(FOLDERID_Pictures, L"Lumen");
            Toast(Fmt(Tr(L"Snapshot saved to {0}"), {dir}), "snapshot");
            break;
        }
        case Action::Record: {
            static bool recording = false;
            recording = !recording;
            if (recording) {
                const std::wstring dir = KnownFolderPath(FOLDERID_Videos, L"Lumen");
                SYSTEMTIME t;
                GetLocalTime(&t);
                wchar_t name[64];
                swprintf_s(name, L"\\Lumen %04d-%02d-%02d %02d-%02d-%02d.mkv", t.wYear, t.wMonth, t.wDay, t.wHour,
                           t.wMinute, t.wSecond);
                p.SetRecording(dir + name);
                Toast(Tr(L"Recording started"), "record");
            } else {
                p.SetRecording(L"");
                Toast(Tr(L"Recording saved to Videos\\Lumen"), "record");
            }
            break;
        }
        case Action::ZoomIn: Zoom(0.1); break;
        case Action::ZoomOut: Zoom(-0.1); break;
        case Action::ZoomReset: Zoom(0, true); break;
        case Action::CycleAspect: {
            static int i = 0;
            const char* values[] = {"-1", "16:9", "4:3", "2.35:1", "1:1"};
            const wchar_t* labels[] = {L"Default", L"16:9", L"4:3", L"2.35:1", L"1:1"};
            i = (i + 1) % 5;
            p.SetProperty("video-aspect-override", std::string(values[i]));
            Toast(Fmt(Tr(L"Aspect ratio: {0}"), {Tr(labels[i])}), "video");
            break;
        }
        case Action::Rotate: {
            const int r = (static_cast<int>(p.GetPropertyDouble("video-rotate", 0)) + 90) % 360;
            p.SetProperty("video-rotate", static_cast<double>(r));
            Toast(Fmt(Tr(L"Rotation {0}°"), r), "refresh");
            break;
        }
        case Action::Deinterlace: p.Command({"cycle", "deinterlace"}); Toast(Tr(L"Deinterlace toggled"), "video"); break;
        case Action::OpenFile: app.OpenFileDialog(); break;
        case Action::OpenFolder: app.OpenFolderDialog(); break;
        case Action::OpenNetwork: app.OpenUrl(L""); break;
        case Action::OpenSubtitles: ShowAddSubtitlesDialog(); break;
        case Action::CloseMedia: app.StopPlayback(); break;
        case Action::GoHome: app.ShowPlayer(false); app.Navigate(Route{Section::Home, {}}); break;
        case Action::ToggleSidebar:
            if (!w) break;
            if (app.PlayerShown() && w->playerView())
                w->playerView()->SetSidePanel(!w->playerView()->SidePanel());
            else if (w->sidebar())
                w->sidebar()->SetCollapsed(!w->sidebar()->Collapsed());
            break;
        case Action::TogglePlaylist: TogglePlayerPanel(PanelKind::Queue, {}); break;
        case Action::Search:
            if (!w) break;
            app.ShowPlayer(false);
            if (w->library()) w->library()->FocusSearch();
            break;
        case Action::Back: app.GoBack(); break;
        case Action::Forward: app.GoForward(); break;
        case Action::AddBookmark: {
            if (!s.fileLoaded) break;
            const QueueItem* item = app.queue().CurrentItem();
            const std::wstring path = item ? item->path : s.path;
            if (path.empty()) break;
            app.bookmarks().Add(path, s.position);
            Toast(Fmt(Tr(L"Bookmark added at {0}"), {FormatTime(s.position, true)}), "bookmark");
            break;
        }
        case Action::ToggleFavorite: {
            const QueueItem* item = app.queue().CurrentItem();
            const std::wstring path = item ? item->path : s.path;
            if (path.empty()) break;
            const bool on = app.favorites().Toggle(path);
            Toast(on ? Tr(L"Added to favorites") : Tr(L"Removed from favorites"), on ? "favorite_active" : "favorite");
            break;
        }
        case Action::ShowInfo: ShowMediaInfoDialog(); break;
        case Action::Settings: app.OpenSettings(); break;
        case Action::KeyboardShortcuts: ShowKeyboardShortcutsDialog(); break;
        case Action::CustomizePlayer: ShowCustomizePlayerDialog(); break;
        case Action::Quit: app.Quit(); break;
    }
}

const char* VolumeIcon(double volume, bool mute) {
    if (mute || volume <= 0) return "close_value-none";
    if (volume < 34) return "close_value-low";
    if (volume < 67) return "close_value-medium";
    return "volume_value-high";
}

void OnPlayerChangeForOsd(unsigned changes) {
    if (g_pendingOsd == PendingOsd::None) return;
    if (ui::AnimFloat::Now() > g_pendingUntil) {
        g_pendingOsd = PendingOsd::None;
        return;
    }
    const PlayerState& s = PS();
    if (g_pendingOsd == PendingOsd::Audio && (changes & kChangeTracks)) {
        Toast(Fmt(Tr(L"Audio: {0}"), {SelectedTrackText(L"audio")}), "audio");
    } else if (g_pendingOsd == PendingOsd::Subtitles && (changes & kChangeTracks)) {
        Toast(Fmt(Tr(L"Subtitles: {0}"), {SelectedTrackText(L"sub")}), "subtitles");
    } else if (g_pendingOsd == PendingOsd::Chapter && (changes & kChangeChapters) && s.chapter >= 0 &&
               s.chapter < static_cast<long long>(s.chapters.size())) {
        const std::wstring& title = s.chapters[static_cast<size_t>(s.chapter)].title;
        std::wstring text = Fmt(Tr(L"Chapter {0}/{1}"), {std::to_wstring(s.chapter + 1), std::to_wstring(s.chapters.size())});
        if (!title.empty()) text += L": " + title;
        Toast(text, "chapters");
    } else {
        return;
    }
    g_pendingOsd = PendingOsd::None;
}

}  // namespace app
