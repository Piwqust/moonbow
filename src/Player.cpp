#include "Player.h"

#include "Trace.h"
#include "Util.h"
#include "audio/CrossfadeMath.h"
#include "audio/Crossfader.h"
#include "app/player/Lyrics.h"
#include "core/Json.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/SettingsSchema.h"
#include "library/MediaTypes.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

enum PropId : uint64_t {
    kPropTimePos = 1,
    kPropDuration,
    kPropPause,
    kPropVolume,
    kPropMute,
    kPropTitle,
    kPropPath,
    kPropTrackList,
    kPropIdle,
    kPropWidth,
    kPropHeight,
    kPropSpeed,
    kPropEof,
    kPropSeekable,
    kPropPausedForCache,
    kPropBuffering,
    kPropCacheTime,
    kPropChapter,
    kPropChapterList,
    kPropMetadata,
    kPropHwdec,
    kPropGamma,
    kPropVolumeMax,
    kPropSpdif,
    kPropAbLoopA,
    kPropAbLoopB,
};

const mpv_node* MapGet(const mpv_node& map, const char* key) {
    if (map.format != MPV_FORMAT_NODE_MAP) return nullptr;
    for (int i = 0; i < map.u.list->num; ++i)
        if (_stricmp(map.u.list->keys[i], key) == 0) return &map.u.list->values[i];
    return nullptr;
}

std::wstring NodeString(const mpv_node* n) {
    return n && n->format == MPV_FORMAT_STRING ? Utf8ToWide(n->u.string) : std::wstring();
}

int64_t NodeInt(const mpv_node* n) {
    return n && n->format == MPV_FORMAT_INT64 ? n->u.int64 : 0;
}

double NodeDouble(const mpv_node* n) {
    if (!n) return 0;
    if (n->format == MPV_FORMAT_DOUBLE) return n->u.double_;
    if (n->format == MPV_FORMAT_INT64) return static_cast<double>(n->u.int64);
    return 0;
}

// reply_userdata of QueueNext's loadfile; the low bits carry Player::queueGen_.
constexpr uint64_t kAppendReply = 1ull << 40;
// reply_userdata of the track list read after a file loads; the low bits carry its loadSerial.
constexpr uint64_t kTracksReply = 1ull << 41;

bool NodeFlag(const mpv_node* n) {
    return n && n->format == MPV_FORMAT_FLAG && n->u.flag;
}

// Writes the GPU shaders embedded in the exe (res/shaders) to <data>\cache\shaders, where the
// glsl-shaders setting points (core/SettingsSchema.cpp). Runs on the init thread; rewrites a file
// only when the exe carries a different version.
void InstallBuiltinShaders(const std::wstring& dataDir) {
    if (dataDir.empty()) return;
    HRSRC res = FindResourceW(nullptr, L"MOONBOW_CAS", RT_RCDATA);
    HGLOBAL mem = res ? LoadResource(nullptr, res) : nullptr;
    const char* data = mem ? static_cast<const char*>(LockResource(mem)) : nullptr;
    if (!data) return;
    const std::string text(data, SizeofResource(nullptr, res));
    const std::wstring dir = dataDir + L"\\cache\\shaders";
    CreateDirectoryW((dataDir + L"\\cache").c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);
    const std::wstring path = dir + L"\\moonbow-cas.glsl";
    std::string current;
    if (json::ReadFileUtf8(path, &current) && current == text) return;
    json::WriteFileAtomic(path, text);
}

// Inter, the player's own font, for subtitles: libass loads every font in mpv's sub-fonts-dir
// (~~/fonts, the data folder's "fonts"). Written once; a file of the same size is left alone.
void InstallSubtitleFonts(const std::wstring& dataDir) {
    if (dataDir.empty()) return;
    const std::wstring dir = dataDir + L"\\fonts";
    CreateDirectoryW(dir.c_str(), nullptr);
    const std::pair<int, const wchar_t*> fonts[] = {
        {201, L"Inter-Light.ttf"}, {202, L"Inter-Regular.ttf"}, {203, L"Inter-Medium.ttf"}, {204, L"Inter-SemiBold.ttf"}};
    for (const auto& [id, name] : fonts) {
        HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
        HGLOBAL mem = res ? LoadResource(nullptr, res) : nullptr;
        const char* data = mem ? static_cast<const char*>(LockResource(mem)) : nullptr;
        if (!data) continue;
        const DWORD size = SizeofResource(nullptr, res);
        const std::wstring path = dir + L"\\" + name;
        WIN32_FILE_ATTRIBUTE_DATA a{};
        if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a) && a.nFileSizeHigh == 0 && a.nFileSizeLow == size)
            continue;
        json::WriteFileAtomic(path, std::string(data, size));
    }
}

}  // namespace

void SetQuietMpvOptions(const MpvApi& api, mpv_handle* mpv) {
    for (const char* name : {"terminal", "config", "osc", "ytdl", "load-scripts", "load-stats-overlay", "load-console",
                             "load-auto-profiles", "load-select", "load-commands", "load-positioning",
                             "load-context-menu", "media-controls", "input-default-bindings", "input-vo-keyboard",
                             "resume-playback", "save-position-on-quit", "stop-screensaver", "sub-auto",
                             "audio-file-auto", "cover-art-auto", "autoload-files", "cache"})
        api.mpv_set_option_string(mpv, name, "no");
}

std::wstring Track::Label() const {
    std::wstring label = Name();
    if (!codec.empty()) label += L" · " + codec;
    return label;
}

bool Track::TitleFromFileName() const {
    // mpv titles a sidecar file with the part of its name that differs from the video's: for
    // "Movie.srt" next to "Movie.mp4" that is just "srt".
    if (!external || file.empty()) return false;
    if (title.empty()) return true;
    const std::wstring name = FileNameOf(file);
    return name.size() > title.size() && name.compare(name.size() - title.size(), title.size(), title) == 0;
}

std::wstring Track::Name() const {
    std::wstring label;
    if (TitleFromFileName())
        label = FileNameOf(file);
    else if (!title.empty())
        label = title;
    else if (!lang.empty())
        label = lang;
    else
        label = L"#" + std::to_wstring(id);
    if ((!title.empty() || TitleFromFileName()) && !lang.empty()) label += L" (" + lang + L")";
    return label;
}

Player::Player() = default;

Player::~Player() {
    Shutdown();
}

void Player::StartAsync(HWND notifyWnd, HWND videoWnd) {
    notify_ = notifyWnd;
    int64_t wid = static_cast<int64_t>(reinterpret_cast<intptr_t>(videoWnd));
    init_ = std::thread([this, wid] {
        bool ok = InitOnWorker(wid);
        PostMessageW(notify_, WM_APP_MPV_READY, ok ? 1 : 0, 0);
    });
}

bool Player::InitOnWorker(int64_t wid) {
    Trace("mpv load dll");
    if (!api_.Load(ExeDir() + L"libmpv-2.dll", &error_)) return false;
    Trace("mpv dll loaded");
    requestLog_ = reinterpret_cast<decltype(requestLog_)>(GetProcAddress(api_.module, "mpv_request_log_messages"));
    getAsync_ = reinterpret_cast<decltype(getAsync_)>(GetProcAddress(api_.module, "mpv_get_property_async"));

    mpv_ = api_.mpv_create();
    if (!mpv_) {
        error_ = L"mpv_create failed";
        return false;
    }

    auto opt = [this](const char* name, const std::string& value) {
        api_.mpv_set_option_string(mpv_, name, value.c_str());
    };

    api_.mpv_set_option(mpv_, "wid", MPV_FORMAT_INT64, &wid);
    opt("vo", "gpu-next");
    // Defaults for the settings below (performance.hwdec / decodeThreads); see SettingsSchema.h.
    opt("hwdec", settings::kAutoHwdec);
    opt("vd-lavc-threads", std::to_string(settings::AutoDecodeThreads()));
    opt("keep-open", "yes");
    opt("idle", "yes");
    opt("osc", "no");
    opt("osd-level", "0");
    opt("input-default-bindings", "no");
    opt("input-vo-keyboard", "no");
    opt("input-cursor", "no");
    opt("cursor-autohide", "no");
    opt("load-scripts", "no");
    // mpv's built-in clients Moonbow has its own UI for: each is a Lua VM (or thread) to start and
    // tear down with every launch and exit. mpv's own media session would also sit next to
    // Moonbow's in the Windows media flyout (app/SystemIntegration).
    for (const char* name : {"load-stats-overlay", "load-console", "load-select", "load-positioning",
                             "load-commands", "load-context-menu", "media-controls"})
        opt(name, "no");
    opt("ytdl", "no");
    opt("terminal", "no");
    opt("volume-max", "150");
    opt("volume-gain-min", "-150");  // the crossfade's silent end (audio/CrossfadeMath.h kMinDb)
    // Moonbow writes watch-later itself (SaveWatchLater / Shutdown): mpv's own save also fired
    // when a finished file was replaced by the next one, so finished files resumed at the end.
    opt("save-position-on-quit", "no");
    // Per file only the position and the chosen tracks; delays, mute and the
    // A-B loop are for the current file, not the next one.
    opt("watch-later-options", "start,aid,sid,secondary-sid");
    // The picture transforms (zoom with its 1:1 mode, pan, rotation, aspect; LiveState drops
    // the flip filter) are per file too. A recording stops with its file: mpv would reopen the
    // same name for the next one and truncate what was recorded.
    opt("reset-on-next-file",
        "audio-delay,sub-delay,ab-loop-a,ab-loop-b,video-aspect-override,video-zoom,video-unscaled,video-pan-x,"
        "video-pan-y,video-rotate,stream-record");
    opt("audio-display", "no");  // cover art is shown by our music view
    opt("background-color", "#000000");

    std::wstring appData = DataDir();
    InstallBuiltinShaders(appData);
    InstallSubtitleFonts(appData);
    if (!appData.empty()) {
        opt("config-dir", WideToUtf8(appData));
        opt("config", "yes");
        std::wstring watchLater = appData + L"\\watch_later";
        CreateDirectoryW(watchLater.c_str(), nullptr);
        opt("watch-later-dir", WideToUtf8(watchLater));
    }
    std::wstring shots = KnownFolderPath(FOLDERID_Pictures, L"Moonbow");
    if (!shots.empty()) opt("screenshot-dir", WideToUtf8(shots));

    std::string hwdec = settings::kAutoHwdec, vf;
    for (const auto& [name, value] : startOptions_) {
        if (CrossfadeOption(name, value)) continue;
        opt(name.c_str(), value);
        if (name == "hwdec") hwdec = value;
        if (name == "vf") vf = value;
    }
    // CPU filters (a Bwdif/Yadif deinterlacer, deblocking) need the frames in RAM: on zero-copy
    // d3d11 frames mpv drops the filter. app/player/LiveState switches this live.
    if (!vf.empty() && vf.find("d3d11vpp") == std::string::npos) opt("hwdec", CopyBackHwdec(hwdec));

    Trace("mpv initialize");
    int err = api_.mpv_initialize(mpv_);
    Trace("mpv initialized");
    if (err < 0) {
        error_ = L"mpv_initialize: " + Utf8ToWide(api_.mpv_error_string(err));
        Log("mpv: " + WideToUtf8(error_));
        return false;
    }
    // Warnings and errors go to Moonbow's log. Not verbose: that makes every mpv module format its
    // verbose lines (+2% of a core at 1080p).
    if (requestLog_) requestLog_(mpv_, "warn");
    // A .lrc next to a song is its lyrics (the music view shows them), not a subtitle track.
    const char* noLrc[] = {"change-list", "sub-auto-exts", "remove", "lrc", nullptr};
    api_.mpv_command(mpv_, noLrc);
    return true;
}

bool Player::FinishStart(bool ok) {
    if (init_.joinable()) init_.join();
    if (!ok) {
        if (mpv_) {
            api_.mpv_terminate_destroy(mpv_);
            mpv_ = nullptr;
        }
        return false;
    }

    struct {
        PropId id;
        const char* name;
        mpv_format format;
    } const props[] = {
        {kPropTimePos, "time-pos", MPV_FORMAT_DOUBLE},
        {kPropDuration, "duration", MPV_FORMAT_DOUBLE},
        {kPropPause, "pause", MPV_FORMAT_FLAG},
        {kPropVolume, "volume", MPV_FORMAT_DOUBLE},
        {kPropMute, "mute", MPV_FORMAT_FLAG},
        {kPropTitle, "media-title", MPV_FORMAT_STRING},
        {kPropPath, "path", MPV_FORMAT_STRING},
        {kPropTrackList, "track-list", MPV_FORMAT_NODE},
        {kPropIdle, "idle-active", MPV_FORMAT_FLAG},
        // The decoder's display size: dwidth/dheight would also count video-crop and the
        // AI upscaler's d3d11vpp scaling (app/player/LiveState), which must not resize the window.
        {kPropWidth, "video-params/dw", MPV_FORMAT_INT64},
        {kPropHeight, "video-params/dh", MPV_FORMAT_INT64},
        {kPropSpeed, "speed", MPV_FORMAT_DOUBLE},
        {kPropEof, "eof-reached", MPV_FORMAT_FLAG},
        {kPropSeekable, "seekable", MPV_FORMAT_FLAG},
        {kPropPausedForCache, "paused-for-cache", MPV_FORMAT_FLAG},
        {kPropBuffering, "cache-buffering-state", MPV_FORMAT_DOUBLE},
        {kPropCacheTime, "demuxer-cache-time", MPV_FORMAT_DOUBLE},
        {kPropChapter, "chapter", MPV_FORMAT_INT64},
        {kPropChapterList, "chapter-list", MPV_FORMAT_NODE},
        {kPropMetadata, "metadata", MPV_FORMAT_NODE},
        {kPropHwdec, "hwdec-current", MPV_FORMAT_STRING},
        {kPropGamma, "video-params/gamma", MPV_FORMAT_STRING},
        {kPropVolumeMax, "volume-max", MPV_FORMAT_DOUBLE},
        {kPropSpdif, "audio-spdif", MPV_FORMAT_STRING},
        {kPropAbLoopA, "ab-loop-a", MPV_FORMAT_STRING},
        {kPropAbLoopB, "ab-loop-b", MPV_FORMAT_STRING},
    };
    for (const auto& p : props) api_.mpv_observe_property(mpv_, p.id, p.name, p.format);
    api_.mpv_set_wakeup_callback(mpv_, &Player::Wakeup, this);

    state_.ready = true;
    return true;
}

void Player::Wakeup(void* ctx) {
    auto* self = static_cast<Player*>(ctx);
    if (!self->wakePending_.exchange(true)) PostMessageW(self->notify_, WM_APP_MPV_WAKEUP, 0, 0);
}

unsigned Player::ProcessEvents() {
    wakePending_ = false;
    if (!mpv_) return 0;
    unsigned changes = 0;
    for (;;) {
        mpv_event* ev = api_.mpv_wait_event(mpv_, 0);
        if (ev->event_id == MPV_EVENT_NONE) break;
        switch (ev->event_id) {
            case MPV_EVENT_PROPERTY_CHANGE: {
                auto* prop = static_cast<mpv_event_property*>(ev->data);
                if (ev->reply_userdata >= 1000) {
                    auto it = observers_.find(ev->reply_userdata);
                    if (it != observers_.end()) it->second(*prop);
                } else {
                    changes |= OnProperty(prop, ev->reply_userdata);
                }
                break;
            }
            case MPV_EVENT_START_FILE: {
                settledSerial_ = 0;
                mpvLogLines_ = 0;
                // Another file before the fade began (mpv went on by itself): the plan is void.
                if (crossfader_ && !crossfader_->Switched()) CancelCrossfade();
                // Forget the previous file's size, so window fitting waits for this one's.
                state_.videoW = state_.videoH = 0;
                changes |= kChangeVideoSize;
                // mpv went on to the queued file by itself.
                const auto* start = static_cast<const mpv_event_start_file*>(ev->data);
                const auto it = std::find_if(appended_.begin(), appended_.end(), [start](const auto& e) {
                    return start && e.first == start->playlist_entry_id;
                });
                // A file that failed right before: mpv skipped it on its own (the UI says so).
                state_.skippedPath = it != appended_.end() && !state_.failedPath.empty() ? state_.failedPath
                                                                                          : std::wstring();
                if (it != appended_.end()) startedPath_ = it->second;
                if (it != appended_.end()) {
                    advancedDuration_ = endedDuration_;
                    advancedPath_ = it->second;
                    // Also when this was not the latest entry: App queues the next one anew.
                    queuedNext_.clear();
                    queuedEntry_ = 0;
                    appended_.clear();
                    ++queueGen_;
                    changes |= kChangeAdvance;
                }
                break;
            }
            case MPV_EVENT_COMMAND_REPLY:
                if (ev->reply_userdata == kAppendReply + queueGen_ && ev->error >= 0 && !queuedNext_.empty()) {
                    const auto* cmd = static_cast<const mpv_event_command*>(ev->data);
                    queuedEntry_ = cmd ? NodeInt(MapGet(cmd->result, "playlist_entry_id")) : 0;
                    if (queuedEntry_ > 0) {
                        if (appended_.size() >= 8) appended_.erase(appended_.begin());
                        appended_.emplace_back(queuedEntry_, queuedNext_);
                    }
                }
                break;
            case MPV_EVENT_GET_PROPERTY_REPLY:
                // The list as mpv had it after the file loaded: a track-list change event may still
                // be on its way, and none comes when the list equals the previous file's.
                if (ev->reply_userdata == kTracksReply + state_.loadSerial && ev->error >= 0) {
                    changes |= OnProperty(static_cast<mpv_event_property*>(ev->data), kPropTrackList);
                    tracksSerial_ = state_.loadSerial;
                }
                break;
            case MPV_EVENT_FILE_LOADED:
                Trace("file loaded");
                state_.fileLoaded = true;
                ++state_.loadSerial;
                settledSerial_ = state_.loadSerial;
                holdResume_ = false;
                loadedPath_ = startedPath_;
                if (confirmTracks && getAsync_)
                    getAsync_(mpv_, kTracksReply + state_.loadSerial, "track-list", MPV_FORMAT_NODE);
                state_.eof = false;
                state_.lastError.clear();
                state_.failedPath.clear();
                changes |= kChangeFile | kChangeState;
                break;
            case MPV_EVENT_PLAYBACK_RESTART:
                if (state_.fileLoaded && state_.shownSerial != state_.loadSerial) {
                    Trace("playback started");
                    state_.shownSerial = state_.loadSerial;
                    changes |= kChangeState;
                }
                break;
            case MPV_EVENT_LOG_MESSAGE: {
                // On a device without passthrough mpv falls back to decoding the track, but then never
                // restarts playback: the picture stays on its first frame until a seek. Seek in place.
                // The audio filters failed on the undecoded data and mpv dropped them; set them again.
                // The fallback itself is logged at verbose level, which makes every mpv module format its
                // verbose lines (+2% of a core at 1080p), so wait for the error just before it instead.
                // mpv falls back in the same call, before it reads these commands.
                const auto* m = static_cast<const mpv_event_log_message*>(ev->data);
                // A broken file can repeat a warning every frame: 50 lines per file are enough.
                if (++mpvLogLines_ <= 50) {
                    std::string text = m->text;
                    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
                    Log(std::string("mpv [") + m->prefix + "] " + m->level + ": " + text);
                    if (mpvLogLines_ == 50) Log("mpv: further messages for this file are not logged");
                }
                if (spdif_ && std::strcmp(m->prefix, "ao") == 0 &&
                    std::strncmp(m->text, "Failed to initialize audio driver", 33) == 0) {
                    Trace("passthrough unsupported, audio decoded instead");
                    if (const std::string af = GetPropertyString("af"); !af.empty()) {
                        SetString("af", "");
                        SetString("af", af);
                    }
                    Cmd({"seek", "0", "relative+exact"});
                }
                break;
            }
            case MPV_EVENT_END_FILE: {
                auto* end = static_cast<mpv_event_end_file*>(ev->data);
                // Property changes come after queued events, so the duration is still this
                // file's; by START_FILE of the next one it can already be gone.
                endedDuration_ = state_.duration;
                settledSerial_ = 0;
                if (end->reason == MPV_END_FILE_REASON_ERROR) {
                    state_.lastError = Utf8ToWide(api_.mpv_error_string(end->error));
                    state_.failedPath = startedPath_;
                    const size_t slash = startedPath_.find_last_of(L"\\/");
                    Log("playback error: " + WideToUtf8(state_.lastError) + " (" +
                        WideToUtf8(slash == std::wstring::npos ? startedPath_ : startedPath_.substr(slash + 1)) + ")");
                    // The previous file's "loaded" must not outlive it: opened after a playing file,
                    // the path change would otherwise count as a load and clear the error at once.
                    state_.fileLoaded = false;
                    changes |= kChangeError | kChangeState;
                }
                break;
            }
            default:
                break;
        }
    }
    if (changes & (kChangeTime | kChangeState)) CheckCrossfade();
    return changes;
}

unsigned Player::OnProperty(const mpv_event_property* prop, uint64_t id) {
    const bool has = prop->format != MPV_FORMAT_NONE && prop->data;
    auto dbl = [&] { return has ? *static_cast<double*>(prop->data) : 0.0; };
    auto flag = [&] { return has && *static_cast<int*>(prop->data) != 0; };
    auto str = [&] { return has ? Utf8ToWide(*static_cast<char**>(prop->data)) : std::wstring(); };
    auto i64 = [&] { return has ? *static_cast<int64_t*>(prop->data) : int64_t{0}; };
    auto node = [&]() -> const mpv_node* {
        return has && prop->format == MPV_FORMAT_NODE ? static_cast<mpv_node*>(prop->data) : nullptr;
    };

    switch (id) {
        case kPropTimePos: state_.position = dbl(); return kChangeTime;
        case kPropDuration: state_.duration = dbl(); return kChangeTime;
        case kPropPause:
            state_.paused = flag();
            if (!state_.paused) holdResume_ = false;
            SyncCrossfade();
            return kChangeState;
        case kPropVolume:
            state_.volume = has ? dbl() : 100.0;
            // Volume 0 from anywhere (the startup volume, a setting) becomes the tap's stand-in.
            if (has && state_.volume <= 0 && signalTap_) SetDouble("volume", kTapVolume);
            if (state_.volume < kTapVolume * 10) state_.volume = 0;  // SetSignalTap's stand-in for 0
            SyncCrossfade();
            return kChangeVolume;
        case kPropMute: state_.mute = flag(); SyncCrossfade(); return kChangeVolume;
        case kPropTitle: state_.title = str(); return kChangeMeta;
        case kPropPath: state_.path = str(); return kChangeFile;
        case kPropIdle:
            state_.idle = flag();
            if (state_.idle) {
                state_.fileLoaded = false;
                state_.position = state_.duration = 0;
                state_.chapters.clear();
            }
            return kChangeState;
        case kPropWidth: state_.videoW = i64(); return kChangeVideoSize;
        case kPropHeight: state_.videoH = i64(); return kChangeVideoSize;
        case kPropSpeed: state_.speed = has ? dbl() : 1.0; SyncCrossfade(); return kChangeState;
        case kPropEof: {
            const bool was = state_.eof;
            state_.eof = flag();
            return kChangeState | (state_.eof && !was ? kChangeEnd : 0u);
        }
        case kPropSeekable: state_.seekable = !has || flag(); return kChangeState;
        case kPropPausedForCache: state_.buffering = flag(); return kChangeState;
        case kPropBuffering: state_.bufferPercent = dbl(); return kChangeState;
        case kPropCacheTime: state_.cacheTime = dbl(); return kChangeTime;
        case kPropChapter: state_.chapter = has ? i64() : -1; return kChangeChapters;
        case kPropHwdec: state_.hwdec = str(); return kChangeState;
        case kPropVolumeMax: volumeMax_ = has ? dbl() : 100.0; return 0;
        case kPropSpdif: {
            spdif_ = has && !str().empty();  // see MPV_EVENT_LOG_MESSAGE
            return 0;
        }
        case kPropAbLoopA:
        case kPropAbLoopB:
            // A loop that reaches into the fade would be cut by the move to the next song.
            abLoop_[id - kPropAbLoopA] = has && str() != L"no";
            if (abLoop_[id - kPropAbLoopA]) CancelCrossfade();
            return 0;
        case kPropGamma: {
            const std::wstring g = str();
            state_.hdr = g == L"pq" || g == L"hlg";
            return kChangeState;
        }
        case kPropChapterList: {
            state_.chapters.clear();
            if (const mpv_node* n = node(); n && n->format == MPV_FORMAT_NODE_ARRAY)
                for (int i = 0; i < n->u.list->num; ++i) {
                    const mpv_node& c = n->u.list->values[i];
                    state_.chapters.push_back({NodeString(MapGet(c, "title")), NodeDouble(MapGet(c, "time"))});
                }
            return kChangeChapters;
        }
        case kPropMetadata: {
            state_.artist.clear();
            state_.album.clear();
            state_.lyricsTags.clear();
            if (const mpv_node* n = node()) {
                state_.artist = NodeString(MapGet(*n, "artist"));
                if (state_.artist.empty()) state_.artist = NodeString(MapGet(*n, "album_artist"));
                state_.album = NodeString(MapGet(*n, "album"));
                // Kept here, so the music view never reads the tags one by one from mpv.
                for (int i = 0; n->format == MPV_FORMAT_NODE_MAP && i < n->u.list->num; ++i)
                    if (app::IsLyricsTag(n->u.list->keys[i]))
                        state_.lyricsTags.push_back(NodeString(&n->u.list->values[i]));
            }
            return kChangeMeta;
        }
        case kPropTrackList: {
            state_.tracks.clear();
            state_.hasVideo = false;
            if (const mpv_node* n = node(); n && n->format == MPV_FORMAT_NODE_ARRAY) {
                for (int i = 0; i < n->u.list->num; ++i) {
                    const mpv_node& t = n->u.list->values[i];
                    Track track;
                    track.id = NodeInt(MapGet(t, "id"));
                    track.type = NodeString(MapGet(t, "type"));
                    track.title = NodeString(MapGet(t, "title"));
                    track.lang = NodeString(MapGet(t, "lang"));
                    track.codec = NodeString(MapGet(t, "codec"));
                    track.channels = static_cast<int>(NodeInt(MapGet(t, "demux-channel-count")));
                    track.selected = NodeFlag(MapGet(t, "selected"));
                    track.external = NodeFlag(MapGet(t, "external"));
                    track.isDefault = NodeFlag(MapGet(t, "default"));
                    track.file = NodeString(MapGet(t, "external-filename"));
                    if (track.type == L"video" && !NodeFlag(MapGet(t, "albumart"))) state_.hasVideo = true;
                    state_.tracks.push_back(std::move(track));
                }
            }
            return kChangeTracks;
        }
        default:
            return 0;
    }
}

// Commands from the UI thread are async: mpv's video window is a child of our
// window, so a synchronous call that waits on mpv's VO thread could deadlock.
void Player::Cmd(std::initializer_list<const char*> args) {
    if (!mpv_) return;
    const char* argv[10] = {};
    size_t n = 0;
    for (const char* a : args)
        if (n < std::size(argv) - 1) argv[n++] = a;
    api_.mpv_command_async(mpv_, 0, argv);
}

void Player::Command(const std::vector<std::string>& args) {
    if (!mpv_ || args.empty()) return;
    std::vector<const char*> argv;
    for (const std::string& a : args) argv.push_back(a.c_str());
    argv.push_back(nullptr);
    api_.mpv_command_async(mpv_, 0, argv.data());
}

void Player::SetDouble(const char* name, double value) {
    if (mpv_) api_.mpv_set_property_async(mpv_, 0, name, MPV_FORMAT_DOUBLE, &value);
}

void Player::SetString(const char* name, const std::string& value) {
    if (!mpv_) return;
    const char* v = value.c_str();
    api_.mpv_set_property_async(mpv_, 0, name, MPV_FORMAT_STRING, &v);
}

void Player::SetProperty(const char* name, double value) {
    SetDouble(name, value);
}

void Player::SetProperty(const char* name, bool value) {
    if (!mpv_) return;
    int flag = value ? 1 : 0;
    api_.mpv_set_property_async(mpv_, 0, name, MPV_FORMAT_FLAG, &flag);
}

void Player::SetProperty(const char* name, const std::string& value) {
    CountTrackChoice(name);
    ApplySetting(name, value);
}

void Player::ApplySetting(const std::string& name, const std::string& value) {
    if (CrossfadeOption(name, value)) return;
    SetString(name.c_str(), value);
}

std::string Player::GetPropertyString(const char* name) {
    if (!mpv_) return {};
    char* v = nullptr;
    if (api_.mpv_get_property(mpv_, name, MPV_FORMAT_STRING, &v) < 0 || !v) return {};
    std::string out = v;
    api_.mpv_free(v);
    return out;
}

double Player::GetPropertyDouble(const char* name, double fallback) {
    double v = fallback;
    if (!mpv_ || api_.mpv_get_property(mpv_, name, MPV_FORMAT_DOUBLE, &v) < 0) return fallback;
    return v;
}

uint64_t Player::Observe(const char* name, mpv_format format, PropertyCallback cb) {
    if (!mpv_) return 0;
    const uint64_t id = nextObserver_++;
    observers_[id] = std::move(cb);
    api_.mpv_observe_property(mpv_, id, name, format);
    return id;
}

void Player::Unobserve(uint64_t id) {
    if (!mpv_ || !id) return;
    api_.mpv_unobserve_property(mpv_, id);
    observers_.erase(id);
}

void Player::Open(const std::wstring& path) {
    CancelCrossfade();
    if (beforeLeave) beforeLeave();
    settledSerial_ = 0;
    if (state_.fileLoaded) SaveWatchLater();
    // "replace" clears mpv's playlist, the queued next file included.
    queuedNext_.clear();
    queuedEntry_ = 0;
    appended_.clear();
    ++queueGen_;
    startedPath_ = path;
    const std::string utf8 = WideToUtf8(path);
    const std::string opts = LoadfileOptions(path);
    if (opts.empty())
        Cmd({"loadfile", utf8.c_str(), "replace"});
    else
        Cmd({"loadfile", utf8.c_str(), "replace", "-1", opts.c_str()});
}

std::string Player::LoadfileOptions(const std::wstring& path) const {
    std::string opts = fileOptions && !path.empty() ? fileOptions(path) : std::string();
    // Songs always start from the top, also when an older Moonbow saved a position for them.
    if (IsMusic(path)) opts += opts.empty() ? "resume-playback=no" : ",resume-playback=no";
    return opts;
}

// Positions are remembered for videos only: a three-minute song is heard again from its start.
bool Player::IsMusic(const std::wstring& path) {
    return library::KindOf(path) == library::MediaKind::Audio;
}

void Player::Stop() {
    CancelCrossfade();
    if (beforeLeave) beforeLeave();
    settledSerial_ = 0;
    if (state_.fileLoaded) SaveWatchLater();
    queuedNext_.clear();
    queuedEntry_ = 0;
    appended_.clear();
    ++queueGen_;
    Cmd({"stop"});
}

void Player::QueueNext(const std::wstring& path) {
    if (!mpv_ || path == queuedNext_) return;
    // The fade about to start was planned for the file queued before.
    if (crossfader_ && !crossfader_->Switched()) CancelCrossfade();
    queuedNext_ = path;
    queuedEntry_ = 0;
    ++queueGen_;
    // Everything but the playing entry: the file queued before, or the one that just ended.
    Cmd({"playlist-clear"});
    if (path.empty()) return;
    const std::string utf8 = WideToUtf8(path);
    const std::string opts = LoadfileOptions(path);
    const char* argv[] = {"loadfile", utf8.c_str(), "append", opts.empty() ? nullptr : "-1", opts.c_str(), nullptr};
    api_.mpv_command_async(mpv_, kAppendReply + queueGen_, argv);
}

void Player::RequeueNext() {
    const std::wstring path = queuedNext_;
    if (path.empty()) return;
    queuedNext_.clear();
    QueueNext(path);
}

void Player::ForgetPosition(const std::wstring& path) {
    if (path.empty()) return;
    const std::string utf8 = WideToUtf8(path);
    Cmd({"delete-watch-later-config", utf8.c_str()});
}

std::string Player::CopyBackHwdec(const std::string& hwdec) {
    std::string out;
    size_t start = 0;
    while (start <= hwdec.size()) {
        size_t comma = hwdec.find(',', start);
        if (comma == std::string::npos) comma = hwdec.size();
        std::string api = hwdec.substr(start, comma - start);
        start = comma + 1;
        if (api.empty()) continue;
        if (api == "no" || api == "yes") {
            if (api == "yes") api = "auto-copy";
        } else if (api.rfind("auto", 0) == 0) {
            api = "auto-copy";
        } else if (api.size() < 5 || api.compare(api.size() - 5, 5, "-copy") != 0) {
            api += "-copy";
        }
        if (("," + out + ",").find("," + api + ",") == std::string::npos) out += (out.empty() ? "" : ",") + api;
    }
    return out.empty() ? hwdec : out;
}

void Player::TogglePause() {
    if (!state_.fileLoaded) return;
    // Play after Stop continues where Stop was pressed: Stop shows 0:00 but loses nothing.
    if (holdResume_ && state_.paused && stopPos_ >= 1) {
        SeekAbsolute(stopPos_, true);
        SetPause(false);
        return;
    }
    if (state_.eof && state_.paused) Cmd({"seek", "0", "absolute"});
    Cmd({"cycle", "pause"});
}

void Player::SetPause(bool paused) {
    SetProperty("pause", paused);
}

void Player::StopAtStart() {
    if (!state_.fileLoaded) return;
    SaveWatchLater();
    stopPos_ = state_.position;
    SetPause(true);
    SeekAbsolute(0, true);
    holdResume_ = true;
}

void Player::SeekAbsolute(double seconds, bool exact) {
    if (!state_.fileLoaded) return;
    holdResume_ = false;
    CancelCrossfade();
    std::string pos = std::to_string(seconds < 0 ? 0 : seconds);
    Cmd({"seek", pos.c_str(), exact ? "absolute+exact" : "absolute+keyframes"});
}

void Player::SeekRelative(double seconds) {
    if (!state_.fileLoaded) return;
    holdResume_ = false;
    CancelCrossfade();
    std::string delta = std::to_string(seconds);
    Cmd({"seek", delta.c_str(), "relative+exact"});
}

void Player::SeekChapter(int delta) {
    if (!state_.fileLoaded || state_.chapters.empty()) return;
    holdResume_ = false;
    CancelCrossfade();
    std::string d = std::to_string(delta);
    Cmd({"add", "chapter", d.c_str()});
}

void Player::FrameStep(bool back) {
    if (!state_.fileLoaded) return;
    CancelCrossfade();
    Cmd({back ? "frame-back-step" : "frame-step"});
}

void Player::SetVolume(double volume) {
    if (volume < 0) volume = 0;
    if (volume > volumeMax_) volume = volumeMax_;  // audio.maxVolume
    state_.volume = volume;  // optimistic, so repeated wheel steps accumulate
    SetDouble("volume", MpvVolume(volume));
}

void Player::SetMute(bool mute) {
    state_.mute = mute;
    SetProperty("mute", mute);
}

void Player::ToggleMute() {
    SetMute(!state_.mute);
}

void Player::SetSpeed(double speed) {
    if (speed < 0.1) speed = 0.1;
    if (speed > 8.0) speed = 8.0;
    state_.speed = speed;
    SetDouble("speed", speed);
}

void Player::CountTrackChoice(const char* prop) {
    const std::string_view p = prop;
    if (p == "aid") ++trackChoices_[0];
    if (p == "sid") ++trackChoices_[1];
    if (p == "secondary-sid") ++trackChoices_[2];
    if (onTrackChoice && (p == "aid" || p == "sid" || p == "secondary-sid")) onTrackChoice(prop);
}

void Player::SetTrack(const char* prop, int64_t id, bool quiet) {
    if (!quiet) CountTrackChoice(prop);
    SetString(prop, id > 0 ? std::to_string(id) : "no");
}

void Player::CycleTrack(const char* prop) {
    CountTrackChoice(prop);
    Cmd({"cycle", prop});
}

void Player::AddSubtitle(const std::wstring& path, bool quiet) {
    if (!quiet) CountTrackChoice("sid");
    const std::string p = WideToUtf8(path);
    Cmd({"sub-add", p.c_str(), "select"});
}

void Player::AddAudio(const std::wstring& path, bool quiet) {
    if (!quiet) CountTrackChoice("aid");
    const std::string p = WideToUtf8(path);
    Cmd({"audio-add", p.c_str(), "select"});
}

void Player::Screenshot(const std::wstring& file, bool withSubtitles) {
    if (!state_.fileLoaded) return;
    const char* mode = withSubtitles ? "subtitles" : "video";
    if (file.empty()) {
        Cmd({"screenshot", mode});
    } else {
        const std::string f = WideToUtf8(file);
        Cmd({"screenshot-to-file", f.c_str(), mode});
    }
}

void Player::SetRecording(const std::wstring& file) {
    SetString("stream-record", WideToUtf8(file));
}

// A file that was (nearly) finished starts from the beginning next time, the
// way mpv and MPC-HC treat it.
bool Player::NearEnd() const {
    if (state_.eof) return true;
    if (state_.duration <= 0) return false;
    return state_.position >= state_.duration - std::max(5.0, state_.duration * 0.05);
}

void Player::SaveWatchLater() {
    if (!state_.fileLoaded || holdResume_) return;
    if (NearEnd() || IsMusic(state_.path))
        Cmd({"delete-watch-later-config"});
    else if (rememberPosition)
        Cmd({"write-watch-later-config"});
}

void Player::Detach() {
    // Still starting: the init thread owns mpv_ until Shutdown joins it.
    if (init_.joinable()) return;
    detached_ = mpv_;
    mpv_ = nullptr;
    detachedFader_ = std::move(crossfader_);
    state_.ready = false;
}

void Player::Shutdown() {
    if (init_.joinable()) init_.join();
    // The crossfade's helper instance goes first; it also uses the main one.
    detachedFader_.reset();
    crossfader_.reset();
    mpv_handle* mpv = detached_;
    detached_ = nullptr;
    if (mpv_) std::swap(mpv, mpv_);  // never detached (the destructor, a close during the cold start)
    if (mpv) {
        Trace("close: player shutdown");
        // Runs off the UI thread (see App shutdown), so a blocking call is fine here.
        const bool forget = NearEnd() || IsMusic(state_.path);
        if (state_.fileLoaded && !holdResume_ && (forget || rememberPosition)) {
            const char* argv[] = {forget ? "delete-watch-later-config" : "write-watch-later-config", nullptr};
            api_.mpv_command(mpv, argv);
        }
        Trace("close: watch-later written");
        api_.mpv_set_wakeup_callback(mpv, nullptr, nullptr);
        api_.mpv_terminate_destroy(mpv);
        Trace("close: mpv destroyed");
    }
    if (state_.ready) state_.ready = false;
    stopped_ = true;
}

// --- crossfade ---------------------------------------------------------------------------------

bool Player::CrossfadeOption(const std::string& name, const std::string& value) {
    if (name == "audio-exclusive") exclusive_ = value == "yes";  // passed on to mpv as well
    if (name == "user-data/moonbow/crossfade") {
        xfSeconds_ = std::atof(value.c_str());
        if (xfSeconds_ <= 0) CancelCrossfade();
        return true;
    }
    if (name == "user-data/moonbow/crossfade-albums") {
        xfAlbums_ = value == "yes";
        return true;
    }
    return false;
}

// On time updates: starts the crossfade worker a few seconds before the fade of a song whose
// next song mpv already has queued (QueueNext).
void Player::CheckCrossfade() {
    if (crossfader_ && crossfader_->Finished()) {
        const bool again = crossfader_->Result() == audio::Crossfader::Outcome::Cancelled;
        crossfader_.reset();      // the worker has ended: no wait
        if (again) xfEntry_ = 0;  // a seek or a pause: try again if there is still time
    }
    if (crossfader_ || xfSeconds_ <= 0 || !mpv_ || !state_.fileLoaded || state_.paused || state_.eof) return;
    if (queuedEntry_ <= 0 || queuedNext_.empty()) return;  // the last song, Repeat One, Stop after each
    if (abLoop_[0] || abLoop_[1]) return;
    if (xfSerial_ == state_.loadSerial && xfEntry_ == queuedEntry_) return;
    const double fade = audio::EffectiveFade(xfSeconds_, state_.duration);
    const audio::ArmDecision decision = audio::DecideArm(state_.position, state_.duration, fade);
    if (decision == audio::ArmDecision::Wait) return;
    xfSerial_ = state_.loadSerial;
    xfEntry_ = queuedEntry_;
    if (decision == audio::ArmDecision::TooLate) return;
    // Songs only: a video never fades into anything. Streams have no reliable end.
    auto isSong = [](const std::wstring& p) {
        return p.find(L"://") == std::wstring::npos && library::KindOf(p) == library::MediaKind::Audio;
    };
    if (state_.hasVideo || !isSong(state_.path) || !isSong(queuedNext_)) return;
    // Exclusive mode and passthrough give the device to one stream.
    if (exclusive_ || spdif_) return;
    audio::CrossfadePlan plan;
    plan.outgoing = state_.path;
    plan.incoming = queuedNext_;
    plan.outgoingAlbum = state_.album;
    plan.duration = state_.duration;
    plan.fade = fade;
    plan.differentAlbumsOnly = xfAlbums_;
    plan.volume = MpvVolume(state_.volume);
    plan.mute = state_.mute;
    plan.speed = state_.speed;
    crossfader_ = std::make_unique<audio::Crossfader>(api_, mpv_, std::move(plan));
}

void Player::CancelCrossfade() {
    if (crossfader_) crossfader_->Cancel();
}

void Player::SyncCrossfade() {
    if (!crossfader_) return;
    crossfader_->SetPaused(state_.paused);
    crossfader_->SetVolume(MpvVolume(state_.volume), state_.mute);
    crossfader_->SetSpeed(state_.speed);
}

// --- visualizer tap ----------------------------------------------------------------------------

void Player::SetSignalTap(bool on, unsigned listener) {
    tapListeners_ = on ? tapListeners_ | listener : tapListeners_ & ~listener;
    on = tapListeners_ != 0;
    if (on == signalTap_) return;
    signalTap_ = on;
    if (state_.ready && state_.volume <= 0) SetDouble("volume", MpvVolume(0));
}

double Player::OutputGain() const {
    if (state_.mute) return 0;
    const double v = MpvVolume(state_.volume) / 100.0;
    return v * v * v;
}
