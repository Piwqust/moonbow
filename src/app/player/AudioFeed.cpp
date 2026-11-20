#include "app/player/AudioFeed.h"

#include "app/App.h"
#include "ui/Anim.h"
#include "ui/Host.h"
#include "ui/Widget.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace app {

namespace {

constexpr UINT kFrameMessage = WM_APP + 1;
constexpr size_t kHistoryMax = 3000;  // 5 min at 10 points per second
constexpr wchar_t kClass[] = L"MoonbowAudioFeed";

float Db(float v) {
    return v > 1e-9f ? 20.0f * std::log10(v) : -200.0f;
}

// Drawn now: every widget up to the root is visible and its window is on screen.
bool OnScreen(const ui::Widget* w) {
    for (const ui::Widget* x = w; x; x = x->Parent())
        if (!x->visible) return false;
    const ui::Host* host = w->GetHost();
    return host && host->Hwnd() && IsWindowVisible(host->Hwnd()) && !IsIconic(host->Hwnd());
}

}  // namespace

AudioFeed& AudioFeed::Get() {
    // Never destroyed: its capture goes when the last view detaches, before the app ends.
    static AudioFeed* feed = new AudioFeed();
    return *feed;
}

AudioFeed::AudioFeed() {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = &AudioFeed::WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);
    notify_ = CreateWindowExW(0, kClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, inst, nullptr);
}

LRESULT CALLBACK AudioFeed::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == kFrameMessage) {
        Get().Notify();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void AudioFeed::Attach(ui::Widget* view) {
    for (const View& v : views_)
        if (v.widget == view) return;
    views_.push_back({view});
    if (!loaded_) {
        loaded_ = true;
        LoadOptions();
        // Never unsubscribed: the feed lives as long as the settings do.
        TheApp().config().Subscribe("interface.meters", [this](const std::string&) { LoadOptions(); });
        TheApp().config().Subscribe("audio.vuReference", [this](const std::string&) { LoadOptions(); });
        TheApp().playerChanged.Connect([this](unsigned m) { OnPlayerChanged(m); });
    }
    // While anyone listens, volume 0 still carries a (silent) signal to measure (Player::SetSignalTap).
    if (views_.size() == 1) TheApp().player().SetSignalTap(true);
}

void AudioFeed::Detach(ui::Widget* view) {
    std::erase_if(views_, [view](const View& v) { return v.widget == view; });
    if (!views_.empty()) return;
    capture_.reset();
    live_ = false;
    TheApp().player().SetSignalTap(false);
}

void AudioFeed::Notify() {
    const double now = ui::AnimFloat::Now();
    // A view that has not drawn for a quarter second is not waited for (a window that stopped
    // painting must not stop the others).
    for (const View& v : views_)
        if (v.pending && now - v.askedAt < 250 && OnScreen(v.widget)) return;
    for (View& v : views_)
        if (OnScreen(v.widget)) {
            v.pending = true;
            v.askedAt = now;
            v.widget->Invalidate();
        }
}

// The song started, paused or changed, or the capture parked itself after a long frame (a busy UI
// thread): a paint of the views on screen starts it, parks it or wakes it (Pull). While it runs,
// its frames repaint them.
void AudioFeed::OnPlayerChanged(unsigned m) {
    if (views_.empty()) return;
    const PlayerState& s = TheApp().player().State();
    const bool playing = s.fileLoaded && !s.paused && !s.eof && !s.idle;
    const bool wake = (m & (kChangeState | kChangeFile | kChangeVolume)) ||
                      ((m & kChangeTime) && (Stalled() || (playing && !capture_)));
    if (!wake) return;
    for (const View& v : views_)
        if (OnScreen(v.widget)) v.widget->Invalidate();
}

void AudioFeed::LoadOptions() {
    Config& c = TheApp().config();
    opt_.vuPeak = c.GetBool("interface.metersVuPeak");
    opt_.balance = c.GetBool("interface.metersBalance");
    opt_.gonioDots = c.GetString("interface.metersGonio") == "dots";
    opt_.spectrumLine = c.GetString("interface.metersSpectrumStyle") == "line";
    opt_.spectrumPeaks = c.GetBool("interface.metersSpectrumPeaks");
    const std::string target = c.GetString("interface.metersTarget");
    opt_.target = target == "off" ? audio::kLoudnessNone : static_cast<float>(std::atof(target.c_str()));
    opt_.historyN = std::clamp<size_t>(static_cast<size_t>(std::atoi(c.GetString("interface.metersHistory").c_str())) * 10,
                                       100, kHistoryMax);
    opt_.floor = std::min(-20.0f, static_cast<float>(std::atof(c.GetString("interface.metersFloor").c_str())));
    const std::string peakHold = c.GetString("interface.metersPeakHold");
    opt_.holdMs = peakHold == "hold" ? -1.0 : peakHold == "off" ? 0.0 : std::atof(peakHold.c_str());
    reference_ = static_cast<float>(std::atof(c.GetString("audio.vuReference").c_str()));
    for (const View& v : views_) v.widget->Invalidate();
}

void AudioFeed::Pull(const ui::Widget* view) {
    for (View& v : views_)
        if (v.widget == view) v.pending = false;
    const PlayerState& s = TheApp().player().State();
    const bool playing = s.fileLoaded && !s.paused && !s.eof && !s.idle;
    if (playing && !capture_ && !views_.empty() && notify_)
        capture_ = std::make_unique<audio::LoopbackCapture>(notify_, kFrameMessage);
    if (s.loadSerial != serial_) {  // a new song: its own loudness and loudest moments
        serial_ = s.loadSerial;
        Reset();
    }
    live_ = false;
    if (capture_) {
        // Windows' "Animation effects" off: the first frame with sound in it stays, still (WCAG 2.3.3).
        const bool still = ui::AnimFloat::ReducedMotion() && frame_.level > 0.05f;
        capture_->SetGain(TheApp().player().OutputGain());
        capture_->SetRunning(playing && !still);
        if (!still) capture_->Take(frame_);
        live_ = playing && !capture_->Failed();
    }
    const double now = ui::AnimFloat::Now();
    for (int ch = 0; ch < 2; ++ch) {
        const float db = live_ ? Db(frame_.peak[ch]) : -200.0f;
        // The peak readout holds the highest value for a moment, like a desk's peak meter.
        if (db >= hold[ch].db || (opt_.holdMs >= 0 && now - hold[ch].at > opt_.holdMs)) hold[ch] = {db, now};
        if (db > max[ch].db) max[ch] = {db, now};
    }
    // The loudness graph takes a point every 100 ms while the sound plays.
    if (live_ && now - historyAt_ >= 100) {
        historyAt_ = now;
        history.push_back(frame_.shortTerm > audio::kLoudnessNone ? frame_.shortTerm : frame_.momentary);
        if (history.size() > kHistoryMax) history.erase(history.begin());
    }
}

void AudioFeed::ResetLoudness() {
    history.clear();
    if (capture_) capture_->ResetLoudness();
    frame_.integrated = frame_.range = audio::kLoudnessNone;
    for (const View& v : views_) v.widget->Invalidate();
}

void AudioFeed::ResetClips() {
    for (Hold& h : max) h = {};
    for (const View& v : views_) v.widget->Invalidate();
}

void AudioFeed::Reset() {
    ResetClips();
    for (Hold& h : hold) h = {};
    ResetLoudness();
}

}  // namespace app
