#include "app/player/DetachedPanels.h"

#include "Util.h"
#include "app/Actions.h"
#include "app/App.h"
#include "app/I18n.h"
#include "app/StandInArt.h"
#include "app/player/AudioFeed.h"
#include "app/player/ControlBar.h"
#include "app/player/DetachedWindows.h"
#include "app/player/LiveState.h"
#include "app/player/Lyrics.h"
#include "app/player/NowPlaying.h"
#include "app/player/PlayerView.h"
#include "app/player/VizPaint.h"
#include "ui/Controls.h"
#include "ui/Host.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace app {

using namespace ui;

namespace {

std::wstring CoverPath() {
    const QueueItem* item = TheApp().queue().CurrentItem();
    return item ? item->path : TheApp().player().State().path;
}

// The window's menu (on top, title bar, close) after the panel's own entries.
void ShowPanelMenu(Widget* w, const MouseEvent& e, std::vector<MenuItem> items = {}) {
    AppendPanelWindowMenu(w->GetHost(), items);
    if (!items.empty()) ShowContextMenu(w->GetHost(), {e.x, e.y}, std::move(items));
}

// --- player controls ----------------------------------------------------------------------------

class ControlsPanel : public Widget {
public:
    explicit ControlsPanel(std::function<void(const std::wstring&)> caption) : caption_(std::move(caption)) {
        width = Length::Grow();
        height = Length::Grow();
        dir = Dir::Column;
        pad = {12, 6, 12, 8};
        gap = 2;
        Add<SeekBar>();
        auto* row = Add<Widget>();
        row->dir = Dir::Row;
        row->align = Align::Center;
        row->width = Length::Grow();
        row->gap = 4;
        time_ = row->Add<Label>(L"", TextStyle::MonoSmall, Tone::Secondary);
        time_->width = Length::Px(120);
        row->Add<Widget>()->width = Length::Grow();
        auto* prev = row->Add<IconButton>("previous", Tr(L"Previous"), 36, 22);
        prev->onClick = [] { Run(Action::Previous); };
        play_ = row->Add<IconButton>("play", Tr(L"Play"), 44, 26);
        play_->tone = Tone::Primary;
        play_->onClick = [] { Run(Action::PlayPause); };
        auto* next = row->Add<IconButton>("next", Tr(L"Next"), 36, 22);
        next->onClick = [] { Run(Action::Next); };
        row->Add<Widget>()->width = Length::Grow();
        mute_ = row->Add<IconButton>("volume_value-high", Tr(L"Mute"), 32, 20);
        mute_->onClick = [] { TheApp().player().ToggleMute(); };
        // shortcut: 0-100 %; a volume above 100 (Settings > Audio > Maximum volume) shows as full.
        volume_ = row->Add<Slider>(0.0f, 100.0f, 100.0f, 1.0f);
        volume_->width = Length::Px(96);
        volume_->format = [](float v) { return std::to_wstring(static_cast<int>(std::lround(v))) + L" %"; };
        volume_->onChange = [](float v) { TheApp().player().SetVolume(v); };
        onContextMenu = [this](const MouseEvent& e) { ShowPanelMenu(this, e); };
        conn_ = Connect(TheApp().playerChanged, [this](unsigned) { Refresh(); });
        Refresh();
    }

private:
    void Refresh() {
        const PlayerState& s = TheApp().player().State();
        const PlayingTags& tags = CurrentTags();
        std::wstring name = tags.title.empty() ? (s.path.empty() ? std::wstring(Tr(L"Player controls")) : FileNameOf(s.path))
                                               : tags.title;
        if (!tags.artist.empty()) name += L" · " + tags.artist;
        if (caption_) caption_(name);
        const bool hours = s.duration >= 3600;
        const double pos = s.eof && s.duration > 0 ? s.duration : s.position;
        time_->SetText(s.fileLoaded ? FormatTime(pos, hours) + L" / " + FormatTime(s.duration, hours) : L"");
        const bool playing = s.fileLoaded && !s.paused && !s.eof;
        play_->icon = playing ? "pause" : "play";
        play_->tooltip = WithKey(Tr(playing ? L"Pause" : L"Play"), Action::PlayPause);
        mute_->icon = VolumeIcon(s.volume, s.mute);
        if (!volume_->IsPressed()) volume_->SetValue(static_cast<float>(std::min(s.volume, 100.0)));
        play_->Invalidate();
        mute_->Invalidate();
    }

    std::function<void(const std::wstring&)> caption_;
    Label* time_ = nullptr;
    IconButton* play_ = nullptr;
    IconButton* mute_ = nullptr;
    Slider* volume_ = nullptr;
    ScopedConnection conn_;
};

// --- the seek bar alone -------------------------------------------------------------------------

class SeekPanel : public Widget {
public:
    SeekPanel() {
        width = Length::Grow();
        height = Length::Grow();
        dir = Dir::Row;
        align = Align::Center;
        pad = {14, 0, 14, 0};
        gap = 12;
        pos_ = Add<Label>(L"", TextStyle::MonoSmall, Tone::Secondary);
        auto* bar = Add<SeekBar>();
        bar->width = Length::Grow();
        dur_ = Add<Label>(L"", TextStyle::MonoSmall, Tone::Secondary);
        onContextMenu = [this](const MouseEvent& e) { ShowPanelMenu(this, e); };
        conn_ = Connect(TheApp().playerChanged, [this](unsigned m) {
            if (m & (kChangeTime | kChangeFile | kChangeState)) Refresh();
        });
        Refresh();
    }

private:
    void Refresh() {
        const PlayerState& s = TheApp().player().State();
        const bool hours = s.duration >= 3600;
        const double pos = s.eof && s.duration > 0 ? s.duration : s.position;
        pos_->SetText(s.fileLoaded ? FormatTime(pos, hours) : L"");
        dur_->SetText(s.fileLoaded ? FormatTime(s.duration, hours) : L"");
    }

    Label* pos_ = nullptr;
    Label* dur_ = nullptr;
    ScopedConnection conn_;
};

// --- the cover, alone or with what plays ------------------------------------------------------

class CoverPanel : public Widget {
public:
    explicit CoverPanel(bool withText) : withText_(withText) {
        width = Length::Grow();
        height = Length::Grow();
        onContextMenu = [this](const MouseEvent& e) { ShowPanelMenu(this, e); };
        conn_ = Connect(TheApp().playerChanged, [this](unsigned m) {
            if (m & (kChangeFile | kChangeMeta)) Invalidate();
        });
        images_ = Connect(TheApp().images().ready, [this] { Invalidate(); });
    }

    void Paint(Painter& p) override {
        p.Fill(rect, P().bgBase);
        const std::wstring path = CoverPath();
        const PlayingTags& tags = CurrentTags();
        const std::wstring title =
            tags.title.empty() ? (path.empty() ? std::wstring(Tr(L"Nothing is playing")) : FileNameOf(path)) : tags.title;
        std::wstring second = tags.artist;
        if (!tags.album.empty()) second += (second.empty() ? L"" : L" · ") + tags.album;

        // The cover as large as the panel allows; with text, beside it in a wide panel, under it
        // otherwise.
        const float margin = std::clamp(std::min(rect.w, rect.h) * 0.06f, 8.0f, 32.0f);
        Rect area = rect.Inset(margin);
        Rect cover, text;
        const bool beside = withText_ && area.w >= area.h * 1.8f;
        if (!withText_) {
            const float s = std::floor(std::min(area.w, area.h));
            cover = {std::round(area.Cx() - s / 2), std::round(area.Cy() - s / 2), s, s};
        } else if (beside) {
            const float s = std::floor(area.h);
            cover = {area.x, area.y, s, s};
            text = {cover.Right() + margin, area.y, area.Right() - cover.Right() - margin, area.h};
        } else {
            const float textH = second.empty() ? 40.0f : 66.0f;
            const float s = std::floor(std::max(0.0f, std::min(area.w, area.h - textH - margin)));
            cover = {std::round(area.Cx() - s / 2), area.y, s, s};
            text = {area.x, cover.Bottom() + margin, area.w, textH};
        }
        if (cover.w >= 16) PaintCover(p, cover, path);
        if (!withText_ || text.w < 40) return;
        const HAlign textAlign = beside ? HAlign::Leading : HAlign::Center;
        const float block = second.empty() ? 32.0f : 58.0f;
        const float y = beside ? std::round(text.Cy() - block / 2) : text.y;
        p.Text(title, {text.x, y, text.w, 32}, TextStyle::Title, P().textPrimary, textAlign);
        if (!second.empty()) p.Text(second, {text.x, y + 34, text.w, 22}, TextStyle::Body, P().textSecondary, textAlign);
    }

private:
    void PaintCover(Painter& p, const Rect& r, const std::wstring& path) {
        const float radius = std::clamp(r.w * 0.03f, 6.0f, 16.0f);
        ID2D1Bitmap* bmp = nullptr;
        if (!path.empty() && GetHost()) bmp = TheApp().images().Get(path, static_cast<int>(r.w * GetHost()->Scale()));
        if (bmp) {
            p.Image(bmp, r, radius, true);
            p.ImageOutline(r, radius);
        } else if (!path.empty() && TheApp().images().Loading(path, static_cast<int>(r.w * GetHost()->Scale()))) {
            p.Fill(r, P().bgPanel, radius);  // on its way
        } else {
            const Color ink = P().textPrimary;
            PaintStandInArt(p, r, radius, ArtSeed(path), {P().bgPanel, ink, ink.Fade(0.55f), ink.Fade(0.12f)});
        }
    }

    bool withText_;
    ScopedConnection conn_, images_;
};

// --- the music view's visualization ------------------------------------------------------------

// Bars, the scrolling wave or the oscilloscope; the choice is the window's (ui.json).
class VizPanel : public Widget {
public:
    VizPanel() {
        width = Length::Grow();
        height = Length::Grow();
        style_ = TheApp().UiValue("windows.vizStyle", "bars");
        AudioFeed::Get().Attach(this);
        onContextMenu = [this](const MouseEvent& e) {
            std::vector<MenuItem> items;
            for (const auto& [value, label] : {std::pair<const char*, const wchar_t*>{"bars", L"Spectrum"},
                                               {"wave", L"Waveform"},
                                               {"scope", L"Oscilloscope"}}) {
                MenuItem m{Tr(label)};
                m.radio = true;
                m.checked = style_ == value;
                const std::string v = value;
                m.action = [this, v] {
                    style_ = v;
                    TheApp().SetUiValue("windows.vizStyle", v);
                    Invalidate();
                };
                items.push_back(std::move(m));
            }
            items.push_back(MenuItem::Separator());
            ShowPanelMenu(this, e, std::move(items));
        };
    }
    ~VizPanel() override { AudioFeed::Get().Detach(this); }

    void Paint(Painter& p) override {
        p.Fill(rect, P().bgBase);
        AudioFeed& feed = AudioFeed::Get();
        feed.Pull(this);
        const float margin = std::clamp(std::min(rect.w, rect.h) * 0.06f, 8.0f, 32.0f);
        const Rect r = rect.Inset(margin);
        const Color ink = P().textPrimary;
        const bool live = feed.IsLive();
        if (style_ == "wave") {
            if (live) viz_.Waveform(p, r, feed.Frame(), ink);
            else p.Fill({r.x, std::round(r.Cy()), r.w, 1}, ink.Fade(0.3f));
        } else if (style_ == "scope") {
            if (live) viz_.Scope(p, r, feed.Frame(), ink.Fade(0.86f), ink.Fade(0.4f));
            else p.Fill({r.x, std::round(r.Cy()), r.w, 1}, ink.Fade(0.3f));
        } else {
            viz_.Spectrum(p, r, feed.Frame(), ink, false, false, !live);
        }
    }

private:
    std::string style_;
    VizPaint viz_;
};

// --- lyrics ------------------------------------------------------------------------------------

class LyricsPanel : public Widget {
public:
    LyricsPanel() {
        width = Length::Grow();
        height = Length::Grow();
        interactive = true;
        clipChildren = true;
        onContextMenu = [this](const MouseEvent& e) {
            std::vector<MenuItem> items = LyricsStyleItems();
            items.push_back(MenuItem::Separator());
            ShowPanelMenu(this, e, std::move(items));
        };
        conn_ = Connect(TheApp().playerChanged, [this](unsigned m) {
            if (m & (kChangeFile | kChangeMeta)) scroll_ = 0;
            if (m & (kChangeTime | kChangeFile | kChangeMeta | kChangeState)) Invalidate();
        });
    }

    void Paint(Painter& p) override {
        p.Fill(rect, P().bgBase);
        rows_.clear();
        int line = -1;
        const Lyrics& l = MusicLyrics(&line);
        const float side = std::clamp(rect.w * 0.06f, 12.0f, 40.0f);
        const Rect r{rect.x + side, rect.y + 16, rect.w - 2 * side, rect.h - 32};
        if (l.Empty()) {
            p.Text(Tr(L"No lyrics for this song"), r, TextStyle::Body, P().textTertiary, HAlign::Center);
            return;
        }
        p.PushClip(rect);
        const float space = 10;
        auto lineH = [&](size_t i, TextStyle st) {
            return std::max(20.0f, p.Measure(l.lines[i].text, st, r.w, gfx::kTextWrap).height);
        };
        auto draw = [&](size_t i, float y, TextStyle st, Color c) {
            const float h = lineH(i, st);
            const Rect row{r.x, y, r.w, h};
            p.Text(l.lines[i].text, row, st, c, HAlign::Center, VAlign::Top, gfx::kTextWrap);
            rows_.push_back({row, static_cast<int>(i)});
            return h;
        };
        if (l.synced) {
            // The sung line in the middle, larger; the ones around it fade with distance.
            const int cur = std::max(0, line);
            const float hc = lineH(static_cast<size_t>(cur), TextStyle::Section);
            float y = std::round(r.Cy() - hc / 2);
            draw(static_cast<size_t>(cur), y, TextStyle::Section, line >= 0 ? P().textPrimary : P().textSecondary);
            float up = y - space, down = y + hc + space;
            for (int k = 1; up > r.y - 40 || down < r.Bottom() + 40; ++k) {
                const Color c = P().textSecondary.Fade(std::max(0.25f, 1.0f - 0.15f * static_cast<float>(k)));
                if (cur - k >= 0 && up > r.y - 40) {
                    const float h = lineH(static_cast<size_t>(cur - k), TextStyle::Body);
                    up -= h;
                    draw(static_cast<size_t>(cur - k), up, TextStyle::Body, c);
                    up -= space;
                }
                if (cur + k < static_cast<int>(l.lines.size()) && down < r.Bottom() + 40) {
                    down += draw(static_cast<size_t>(cur + k), down, TextStyle::Body, c) + space;
                }
                if (cur - k < 0 && cur + k >= static_cast<int>(l.lines.size())) break;
            }
        } else {
            // Unsynced: the text from the top, scrolled with the wheel.
            float y = r.y - scroll_;
            total_ = 0;
            for (size_t i = 0; i < l.lines.size(); ++i) {
                const float h = lineH(i, TextStyle::Body);
                if (y + h > rect.y && y < rect.Bottom()) draw(i, y, TextStyle::Body, P().textSecondary);
                y += h + space;
                total_ += h + space;
            }
        }
        p.PopClip();
    }

    bool OnMouseDown(const MouseEvent& e) override {
        if (e.button != MouseButton::Left) return false;
        int line = -1;
        const Lyrics& l = MusicLyrics(&line);
        if (!l.synced) return false;
        for (const auto& [row, i] : rows_)
            if (row.Contains(e.x, e.y) && i >= 0 && i < static_cast<int>(l.lines.size())) {
                TheApp().player().SeekAbsolute(l.lines[static_cast<size_t>(i)].time + Live().LyricsDelay(), true);
                return true;
            }
        return false;
    }

    bool OnWheel(const WheelEvent& e) override {
        int line = -1;
        if (MusicLyrics(&line).synced || e.horizontal) return false;
        const float maxScroll = std::max(0.0f, total_ - (rect.h - 32));
        scroll_ = std::clamp(scroll_ - e.delta * 64, 0.0f, maxScroll);
        Invalidate();
        return true;
    }

private:
    std::vector<std::pair<Rect, int>> rows_;  // drawn lines, for clicks
    float scroll_ = 0, total_ = 0;
    ScopedConnection conn_;
};

}  // namespace

Widget* AddControlsPanel(Widget* parent, std::function<void(const std::wstring&)> caption) {
    return parent->Add<ControlsPanel>(std::move(caption));
}

Widget* AddSeekPanel(Widget* parent) {
    return parent->Add<SeekPanel>();
}

Widget* AddCoverPanel(Widget* parent, bool withText) {
    return parent->Add<CoverPanel>(withText);
}

Widget* AddVizPanel(Widget* parent) {
    return parent->Add<VizPanel>();
}

Widget* AddLyricsPanel(Widget* parent) {
    return parent->Add<LyricsPanel>();
}

}  // namespace app
