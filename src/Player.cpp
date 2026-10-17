#include "Player.h"

#include "Trace.h"
#include "Util.h"

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
};

const mpv_node* MapGet(const mpv_node& map, const char* key) {
    if (map.format != MPV_FORMAT_NODE_MAP) return nullptr;
    for (int i = 0; i < map.u.list->num; ++i)
        if (std::strcmp(map.u.list->keys[i], key) == 0) return &map.u.list->values[i];
    return nullptr;
}

std::wstring NodeString(const mpv_node* n) {
    return n && n->format == MPV_FORMAT_STRING ? Utf8ToWide(n->u.string) : std::wstring();
}

int64_t NodeInt(const mpv_node* n) {
    return n && n->format == MPV_FORMAT_INT64 ? n->u.int64 : 0;
}

bool NodeFlag(const mpv_node* n) {
    return n && n->format == MPV_FORMAT_FLAG && n->u.flag;
}

}  // namespace

std::wstring Track::Label() const {
    std::wstring label;
    if (!title.empty())
        label = title;
    else if (!lang.empty())
        label = lang;
    else
        label = L"Дорожка " + std::to_wstring(id);
    if (!title.empty() && !lang.empty()) label += L" (" + lang + L")";
    if (!codec.empty()) label += L" · " + codec;
    if (external) label += L" · внешняя";
    return label;
}

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

    mpv_ = api_.mpv_create();
    if (!mpv_) {
        error_ = L"mpv_create не удалось";
        return false;
    }

    auto opt = [this](const char* name, const std::string& value) {
        api_.mpv_set_option_string(mpv_, name, value.c_str());
    };

    api_.mpv_set_option(mpv_, "wid", MPV_FORMAT_INT64, &wid);
    opt("vo", "gpu-next");
    opt("hwdec", "auto-safe");
    opt("keep-open", "yes");
    opt("idle", "yes");
    opt("osc", "no");
    opt("osd-level", "0");
    opt("input-default-bindings", "no");
    opt("input-vo-keyboard", "no");
    opt("input-cursor", "no");
    opt("cursor-autohide", "no");
    opt("load-scripts", "no");
    opt("ytdl", "no");
    opt("terminal", "no");
    opt("volume-max", "100");
    opt("save-position-on-quit", "yes");

    std::wstring appData = KnownFolderPath(FOLDERID_RoamingAppData, L"Lumen");
    if (!appData.empty()) {
        opt("config-dir", WideToUtf8(appData));
        opt("config", "yes");
        std::wstring watchLater = appData + L"\\watch_later";
        CreateDirectoryW(watchLater.c_str(), nullptr);
        opt("watch-later-dir", WideToUtf8(watchLater));
    }
    std::wstring shots = KnownFolderPath(FOLDERID_Pictures, L"Lumen");
    if (!shots.empty()) opt("screenshot-dir", WideToUtf8(shots));

    Trace("mpv initialize");
    int err = api_.mpv_initialize(mpv_);
    Trace("mpv initialized");
    if (err < 0) {
        error_ = L"mpv_initialize: " + Utf8ToWide(api_.mpv_error_string(err));
        return false;
    }
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

    api_.mpv_observe_property(mpv_, kPropTimePos, "time-pos", MPV_FORMAT_DOUBLE);
    api_.mpv_observe_property(mpv_, kPropDuration, "duration", MPV_FORMAT_DOUBLE);
    api_.mpv_observe_property(mpv_, kPropPause, "pause", MPV_FORMAT_FLAG);
    api_.mpv_observe_property(mpv_, kPropVolume, "volume", MPV_FORMAT_DOUBLE);
    api_.mpv_observe_property(mpv_, kPropMute, "mute", MPV_FORMAT_FLAG);
    api_.mpv_observe_property(mpv_, kPropTitle, "media-title", MPV_FORMAT_STRING);
    api_.mpv_observe_property(mpv_, kPropPath, "path", MPV_FORMAT_STRING);
    api_.mpv_observe_property(mpv_, kPropTrackList, "track-list", MPV_FORMAT_NODE);
    api_.mpv_observe_property(mpv_, kPropIdle, "idle-active", MPV_FORMAT_FLAG);
    api_.mpv_observe_property(mpv_, kPropWidth, "dwidth", MPV_FORMAT_INT64);
    api_.mpv_observe_property(mpv_, kPropHeight, "dheight", MPV_FORMAT_INT64);
    api_.mpv_observe_property(mpv_, kPropSpeed, "speed", MPV_FORMAT_DOUBLE);
    api_.mpv_observe_property(mpv_, kPropEof, "eof-reached", MPV_FORMAT_FLAG);
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
            case MPV_EVENT_PROPERTY_CHANGE:
                changes |= OnProperty(static_cast<mpv_event_property*>(ev->data), ev->reply_userdata);
                break;
            case MPV_EVENT_FILE_LOADED:
                Trace("file loaded");
                state_.fileLoaded = true;
                state_.lastError.clear();
                changes |= kChangeFile | kChangeState;
                break;
            case MPV_EVENT_END_FILE: {
                auto* end = static_cast<mpv_event_end_file*>(ev->data);
                if (end->reason == MPV_END_FILE_REASON_ERROR) {
                    state_.lastError = L"Не удалось открыть файл: " + Utf8ToWide(api_.mpv_error_string(end->error));
                    changes |= kChangeError;
                }
                break;
            }
            default:
                break;
        }
    }
    return changes;
}

unsigned Player::OnProperty(const mpv_event_property* prop, uint64_t id) {
    const bool has = prop->format != MPV_FORMAT_NONE && prop->data;
    auto dbl = [&] { return has ? *static_cast<double*>(prop->data) : 0.0; };
    auto flag = [&] { return has && *static_cast<int*>(prop->data) != 0; };
    auto str = [&] { return has ? Utf8ToWide(*static_cast<char**>(prop->data)) : std::wstring(); };
    auto i64 = [&] { return has ? *static_cast<int64_t*>(prop->data) : int64_t{0}; };

    switch (id) {
        case kPropTimePos: state_.position = dbl(); return kChangeTime;
        case kPropDuration: state_.duration = dbl(); return kChangeTime;
        case kPropPause: state_.paused = flag(); return kChangeState;
        case kPropVolume: state_.volume = has ? dbl() : 100.0; return kChangeVolume;
        case kPropMute: state_.mute = flag(); return kChangeVolume;
        case kPropTitle: state_.title = str(); return kChangeState;
        case kPropPath: state_.path = str(); return kChangeFile;
        case kPropIdle:
            state_.idle = flag();
            if (state_.idle) {
                state_.fileLoaded = false;
                state_.position = state_.duration = 0;
            }
            return kChangeState;
        case kPropWidth: state_.videoW = i64(); return kChangeVideoSize;
        case kPropHeight: state_.videoH = i64(); return kChangeVideoSize;
        case kPropSpeed: state_.speed = has ? dbl() : 1.0; return kChangeState;
        case kPropEof: state_.eof = flag(); return kChangeState;
        case kPropTrackList: {
            state_.tracks.clear();
            if (has && prop->format == MPV_FORMAT_NODE) {
                auto* node = static_cast<mpv_node*>(prop->data);
                if (node->format == MPV_FORMAT_NODE_ARRAY) {
                    for (int i = 0; i < node->u.list->num; ++i) {
                        const mpv_node& t = node->u.list->values[i];
                        Track track;
                        track.id = NodeInt(MapGet(t, "id"));
                        track.type = NodeString(MapGet(t, "type"));
                        track.title = NodeString(MapGet(t, "title"));
                        track.lang = NodeString(MapGet(t, "lang"));
                        track.codec = NodeString(MapGet(t, "codec"));
                        track.selected = NodeFlag(MapGet(t, "selected"));
                        track.external = NodeFlag(MapGet(t, "external"));
                        state_.tracks.push_back(std::move(track));
                    }
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
void Player::Command(std::initializer_list<const char*> args, bool async) {
    if (!mpv_) return;
    const char* argv[8] = {};
    size_t n = 0;
    for (const char* a : args)
        if (n < std::size(argv) - 1) argv[n++] = a;
    if (async)
        api_.mpv_command_async(mpv_, 0, argv);
    else
        api_.mpv_command(mpv_, argv);
}

void Player::SetDouble(const char* name, double value) {
    if (mpv_) api_.mpv_set_property_async(mpv_, 0, name, MPV_FORMAT_DOUBLE, &value);
}

void Player::SetString(const char* name, const std::string& value) {
    if (!mpv_) return;
    const char* v = value.c_str();
    api_.mpv_set_property_async(mpv_, 0, name, MPV_FORMAT_STRING, &v);
}

void Player::Open(const std::wstring& path) {
    if (state_.fileLoaded) SaveWatchLater();
    std::string utf8 = WideToUtf8(path);
    Command({"loadfile", utf8.c_str(), "replace"});
}

void Player::Stop() {
    if (state_.fileLoaded) SaveWatchLater();
    Command({"stop"});
}

void Player::TogglePause() {
    if (!state_.fileLoaded) return;
    if (state_.eof && state_.paused) {
        Command({"seek", "0", "absolute"});
    }
    Command({"cycle", "pause"});
}

void Player::SeekAbsolute(double seconds, bool exact) {
    if (!state_.fileLoaded) return;
    std::string pos = std::to_string(seconds < 0 ? 0 : seconds);
    Command({"seek", pos.c_str(), exact ? "absolute+exact" : "absolute+keyframes"}, true);
}

void Player::SeekRelative(double seconds) {
    if (!state_.fileLoaded) return;
    std::string delta = std::to_string(seconds);
    Command({"seek", delta.c_str(), "relative+exact"}, true);
}

void Player::FrameStep(bool back) {
    if (!state_.fileLoaded) return;
    Command({back ? "frame-back-step" : "frame-step"});
}

void Player::SetVolume(double volume) {
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    state_.volume = volume;  // optimistic, so repeated wheel steps accumulate
    SetDouble("volume", volume);
}

void Player::ToggleMute() {
    Command({"cycle", "mute"});
}

void Player::SetSpeed(double speed) {
    if (speed < 0.1) speed = 0.1;
    if (speed > 4.0) speed = 4.0;
    state_.speed = speed;
    SetDouble("speed", speed);
}

void Player::SetTrack(const char* prop, int64_t id) {
    SetString(prop, id > 0 ? std::to_string(id) : "no");
}

void Player::CycleTrack(const char* prop) {
    Command({"cycle", prop});
}

void Player::Screenshot() {
    if (!state_.fileLoaded) return;
    Command({"screenshot", "video"});
}

void Player::SaveWatchLater() {
    if (state_.fileLoaded) Command({"write-watch-later-config"});
}

void Player::Shutdown() {
    if (init_.joinable()) init_.join();
    if (mpv_) {
        // Runs off the UI thread (see App WM_CLOSE), so a blocking call is fine here.
        if (state_.fileLoaded) Command({"write-watch-later-config"}, false);
        api_.mpv_set_wakeup_callback(mpv_, nullptr, nullptr);
        api_.mpv_terminate_destroy(mpv_);
        mpv_ = nullptr;
    }
    state_.ready = false;
}
