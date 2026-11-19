#include "app/player/SubWords.h"

#include "Util.h"
#include "app/Actions.h"
#include "app/App.h"
#include "app/I18n.h"
#include "app/MainWindow.h"
#include "app/dialogs/DialogKit.h"
#include "app/player/ControlBar.h"
#include "app/player/SubStyle.h"
#include "app/player/SubText.h"
#include "ui/Host.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace app {

using namespace ui;

namespace {

constexpr float kHitPad = 6;  // around a block: still the words, not the video

const PlayerState& PS() {
    return TheApp().player().State();
}

const Config& Cfg() {
    return TheApp().config();
}

const Track* SelectedSub() {
    for (const Track& t : PS().tracks)
        if (t.selected && t.type == L"sub") return &t;
    return nullptr;
}

std::string Quote(const std::wstring& s) {
    std::string u = WideToUtf8(s);
    for (char& c : u)
        if (c == '"' || c == '\n') c = c == '"' ? '\'' : ' ';
    return "\"" + u + "\"";
}

}  // namespace

SubtitleWords::SubtitleWords(MainWindow* window) : window_(window) {
    visible = false;
    interactive = true;
    id = "subtitle-words";
    onContextMenu = [this](const MouseEvent& e) {
        const int w = WordAt(e.x, e.y);
        if (w < 0) {
            if (fallbackMenu) fallbackMenu(e);
            return;
        }
        if (selA_ < 0 || w < std::min(selA_, selB_) || w > std::max(selA_, selB_)) Select(w, w);
        OpenMenu();
    };
    enabled_ = Cfg().GetBool("subtitles.wordPicker");
    enabledToken_ = TheApp().config().Subscribe("subtitles.wordPicker", [this](const std::string&) {
        enabled_ = Cfg().GetBool("subtitles.wordPicker");
        Reevaluate();
    });
    playerConn_ = Connect(TheApp().playerChanged, [this](unsigned m) { OnPlayer(m); });
    screenConn_ = Connect(TheApp().screenChanged, [this] { Reevaluate(); });
}

SubtitleWords::~SubtitleWords() {
    TheApp().config().Unsubscribe(enabledToken_);
    if (styleToken_) TheApp().config().Unsubscribe(styleToken_);
    if (stepTimer_) window_->CancelTimer(stepTimer_);
    StopObserving();
}

// --- state ---------------------------------------------------------------------------------

void SubtitleWords::OnPlayer(unsigned m) {
    // Time ticks while playing are most of what arrives here: nothing to do for them.
    if (!(m & (kChangeState | kChangeFile | kChangeTracks | kChangeVideoSize | kChangeError))) return;
    if (m & kChangeFile) {
        dismissed_ = false;
        keyboardWanted_ = false;
        assText_.clear();
        secondaryText_.clear();
        key_.clear();
    }
    if ((m & kChangeTracks) && !observers_.empty()) {
        // Another subtitle track: it may be .ass where the last one was not (other text form).
        const Track* sub = SelectedSub();
        if (!sub || sub->id != observedSid_) StopObserving();
    }
    if ((m & kChangeState) && !PS().paused && ui::AnimFloat::Now() >= stepUntil_) {
        dismissed_ = false;  // resumed: the next pause brings the words back
        keyboardWanted_ = false;
    }
    Reevaluate();
}

bool SubtitleWords::Eligible() const {
    const PlayerState& s = PS();
    if (!enabled_ || !TheApp().PlayerShown() || window_->PictureInPicture() || !s.fileLoaded || !s.hasVideo)
        return false;
    const Track* sub = SelectedSub();
    return sub && !subtext::IsBitmapCodec(sub->codec) && !panelOpen_;
}

void SubtitleWords::Reevaluate(bool inLayout) {
    const bool stepping = ui::AnimFloat::Now() < stepUntil_;
    const bool armed = Eligible() && (PS().paused || stepping) && !dismissed_;
    if (armed) StartObserving();
    else StopObserving();

    // Shown while mpv would show the line (the user didn't hide subtitles) and it has words.
    const bool userVisible = mpvVisible_ || hid_;
    const bool known = textKnown_ && visibilityKnown_;
    if (armed && known && !inLayout) {
        const Rect before = bounds_;
        Rebuild();
        // PlayerView arranges the overlay at Bounds() in its layout: bigger words, a lift over
        // the bar or a new line while paused need one, or clicks keep the old area.
        if (bounds_.x != before.x || bounds_.y != before.y || bounds_.w != before.w || bounds_.h != before.h)
            InvalidateLayout();
    }
    const bool show = armed && known && userVisible && !words_.empty();
    if (!(stepping && !PS().paused)) SetMpvHidden(show);

    if (visible != show) {
        visible = show;
        if (!show) ResetInteraction();
        if (!inLayout) InvalidateLayout();
    }
    if (keyboardWanted_ && armed && known) {
        keyboardWanted_ = false;
        if (show) EnterKeyboard();
        else ShowToast(window_, Tr(L"No subtitle on screen right now"), "subtitles");
    }
}

void SubtitleWords::StartObserving() {
    Player& p = TheApp().player();
    if (!observers_.empty() || !p.Ready()) return;
    textKnown_ = visibilityKnown_ = false;
    const Track* sub = SelectedSub();
    observedSid_ = sub ? sub->id : 0;
    ReadAssHeader();
    // The callbacks only record values: unobserving from inside one would destroy the function
    // that is running, so whatever follows from a value runs after the event batch.
    auto text = [this, &p](const char* name, std::wstring* field, bool primary) {
        observers_.push_back(p.Observe(name, MPV_FORMAT_STRING, [this, field, primary](const mpv_event_property& e) {
            const char* s = e.format == MPV_FORMAT_STRING && e.data ? *static_cast<char**>(e.data) : nullptr;
            *field = s ? Utf8ToWide(s) : std::wstring();
            if (primary) textKnown_ = true;
            Schedule();
        }));
    };
    // Whole event lines for a native .ass track (their style and margins), else the text with
    // tags. mpv answers "none" while it has no playback time (paused during a seek): no line yet.
    text(assMode_ ? "sub-text/ass-full" : "sub-text/ass", &assText_, true);
    text("secondary-sub-text", &secondaryText_, false);
    observers_.push_back(p.Observe("sub-visibility", MPV_FORMAT_FLAG, [this](const mpv_event_property& e) {
        const bool v = e.format == MPV_FORMAT_FLAG && e.data && *static_cast<int*>(e.data) != 0;
        visibilityKnown_ = true;
        mpvVisible_ = v;
        if (hid_ && !v) hidConfirmed_ = true;
        if (hid_ && v && hidConfirmed_) {
            // Someone turned subtitles on while the overlay had them hidden (the Settings panel,
            // Shift+V): mpv shows them now; step aside until playback resumes.
            hid_ = hidConfirmed_ = false;
            dismissed_ = true;
        }
        Schedule();
    }));
    auto num = [this, &p](const char* name, double* field) {
        observers_.push_back(p.Observe(name, MPV_FORMAT_DOUBLE, [this, field](const mpv_event_property& e) {
            if (e.format != MPV_FORMAT_DOUBLE || !e.data) return;
            *field = *static_cast<double*>(e.data);
            Schedule();
        }));
    };
    num("sub-pos", &subPos_);
    num("sub-scale", &subScale_);
    num("secondary-sub-pos", &secondaryPos_);
    // With the control bar over the picture MainWindow lifts the subtitles above it.
    observers_.push_back(p.Observe("sub-margin-y", MPV_FORMAT_INT64, [this](const mpv_event_property& e) {
        if (e.format != MPV_FORMAT_INT64 || !e.data) return;
        marginY_ = *static_cast<int64_t*>(e.data);
        Schedule();
    }));
    observers_.push_back(p.Observe("secondary-sid", MPV_FORMAT_STRING, [this](const mpv_event_property& e) {
        const char* s = e.format == MPV_FORMAT_STRING && e.data ? *static_cast<char**>(e.data) : nullptr;
        secondarySid_ = s ? std::atoll(s) : 0;
        Schedule();
    }));
    // The style can change from the Settings window while paused.
    styleToken_ = TheApp().config().Subscribe("subtitles.", [this](const std::string&) {
        styleDirty_ = true;
        Schedule();
    });
    ReadStyle();
}

void SubtitleWords::Schedule() {
    if (scheduled_) return;
    scheduled_ = true;
    window_->Post([this] {
        scheduled_ = false;
        if (styleDirty_ && !observers_.empty()) {
            styleDirty_ = false;
            ReadStyle();
            key_.clear();
        }
        Reevaluate();
    });
}

void SubtitleWords::StopObserving() {
    if (observers_.empty()) return;
    Player& p = TheApp().player();
    for (uint64_t o : observers_) p.Unobserve(o);
    observers_.clear();
    if (styleToken_) TheApp().config().Unsubscribe(styleToken_);
    styleToken_ = 0;
    textKnown_ = visibilityKnown_ = false;
    assText_.clear();
    secondaryText_.clear();
    blocks_.clear();
    words_.clear();
    key_.clear();
    textKey_.clear();
}

void SubtitleWords::SetMpvHidden(bool hide) {
    Player& p = TheApp().player();
    if (hide) {
        if (!hid_) {
            p.SetProperty("sub-visibility", false);
            hid_ = true;
            hidConfirmed_ = false;
        }
        const bool secondary = secondarySid_ != 0 && !secondaryText_.empty();
        if (secondary && !hidSecondary_) {
            p.SetProperty("secondary-sub-visibility", false);
            hidSecondary_ = true;
        }
        return;
    }
    if (hid_) p.SetProperty("sub-visibility", true);
    if (hidSecondary_) p.SetProperty("secondary-sub-visibility", true);
    hid_ = hidConfirmed_ = hidSecondary_ = false;
}

void SubtitleWords::Frame() {
    // Cheap: only while paused, and only a pointer comparison or two.
    if (!enabled_ || (!PS().paused && !panelOpen_)) return;
    const bool open = PlayerPanelOpen(PanelKind::Settings) || PlayerPanelOpen(PanelKind::Subtitles) ||
                      PlayerPanelOpen(PanelKind::Tracks) || PlayerPanelOpen(PanelKind::Video);
    if (open == panelOpen_) return;
    panelOpen_ = open;
    window_->Post([this] { Reevaluate(); });
}

void SubtitleWords::ReleaseKeepHidden() {
    if (hidSecondary_) TheApp().player().SetProperty("secondary-sub-visibility", true);
    // mpv keeps them hidden: the overlay goes with them, and comes back when they are shown.
    hid_ = hidConfirmed_ = hidSecondary_ = false;
    mpvVisible_ = false;
    Reevaluate();
}

void SubtitleWords::ExpectFrameStep() {
    if (observers_.empty()) return;
    stepUntil_ = ui::AnimFloat::Now() + 600;
    if (stepTimer_) window_->CancelTimer(stepTimer_);
    stepTimer_ = window_->StartTimer(650, [this] {
        stepTimer_ = 0;
        Reevaluate();
    });
}

bool SubtitleWords::ClaimVideoClick(float x, float y, const Rect& band) {
    if (PS().paused || !band.Contains(x, y) || !Eligible()) return false;
    if (Cfg().GetString("controls.singleClick") == "playPause") return false;  // that click pauses anyway
    // Rare (a click), so one synchronous read: is a line on screen at all?
    if (TheApp().player().GetPropertyString("sub-text").empty()) return false;
    TheApp().player().SetPause(true);
    return true;
}

void SubtitleWords::StartKeyboard() {
    const PlayerState& s = PS();
    if (!s.fileLoaded) return;
    if (!enabled_) {
        ShowToast(window_, Tr(L"Picking words is off in Settings › Subtitles"), "subtitles");
        return;
    }
    const Track* sub = SelectedSub();
    if (!sub || !s.hasVideo) {
        ShowToast(window_, Tr(L"Turn on subtitles first"), "subtitles");
        return;
    }
    if (subtext::IsBitmapCodec(sub->codec)) {
        ShowToast(window_, Tr(L"These subtitles are pictures: they have no words to pick"), "subtitles");
        return;
    }
    if (visible && !words_.empty()) {
        EnterKeyboard();
        return;
    }
    dismissed_ = false;
    keyboardWanted_ = true;
    if (!s.paused) TheApp().player().SetPause(true);
    Reevaluate();
}

// --- layout ----------------------------------------------------------------------------------

void SubtitleWords::ReadStyle() {
    style_ = ReadSubtitleStyle(Cfg());
    for (Block& b : blocks_) b.scene.Reset();
}

// A native .ass track is drawn with its own styles (sub-ass-extradata), unless Settings apply
// the Moonbow style to it. Read once per track: a synchronous read, but only on a pause.
void SubtitleWords::ReadAssHeader() {
    const Track* sub = SelectedSub();
    const bool native = sub && (sub->codec == L"ass" || sub->codec == L"ssa") && !Cfg().GetBool("subtitles.assOverride");
    const uint64_t key = native ? PS().loadSerial * 4096 + static_cast<uint64_t>(sub->id) : 0;
    if (key == headerKey_) return;
    headerKey_ = key;
    header_ = native ? subtext::ParseAssHeader(Utf8ToWide(TheApp().player().GetPropertyString("sub-ass-extradata")))
                     : subtext::AssHeader{};
    assMode_ = native && !header_.styles.empty();
}

// libass sizes a font by its line (ascent + descent), DirectWrite by the em.
void SubtitleWords::FontRatios(const std::wstring& name, bool bold, float& em, float& ascent) {
    em = 0.89f, ascent = 0.8f;
    for (const FontMetrics& f : fontCache_)
        if (f.bold == bold && f.name == name) {
            em = f.em, ascent = f.ascent;
            return;
        }
    if (IDWriteFactory5* dw = gfx::Gpu::Get().DWrite()) {
        gfx::ComPtr<IDWriteFontCollection> fonts;
        UINT32 index = 0;
        BOOL exists = FALSE;
        gfx::ComPtr<IDWriteFontFamily> family;
        gfx::ComPtr<IDWriteFont> font;
        if (SUCCEEDED(dw->GetSystemFontCollection(&fonts)) && SUCCEEDED(fonts->FindFamilyName(name.c_str(), &index, &exists)) &&
            exists && SUCCEEDED(fonts->GetFontFamily(index, &family)) &&
            SUCCEEDED(family->GetFirstMatchingFont(bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                                                   DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font))) {
            DWRITE_FONT_METRICS m{};
            font->GetMetrics(&m);
            const float line = static_cast<float>(m.ascent) + static_cast<float>(m.descent);
            if (line > 0 && m.designUnitsPerEm > 0) {
                em = static_cast<float>(m.designUnitsPerEm) / line;
                ascent = static_cast<float>(m.ascent) / line;
            }
        }
    }
    if (fontCache_.size() > 8) fontCache_.erase(fontCache_.begin());
    fontCache_.push_back({name, bold, em, ascent});
}

void SubtitleWords::Place(const Rect& video) {
    video_ = video;
    if (!observers_.empty() && textKnown_) Rebuild();
    Reevaluate(true);
}

void SubtitleWords::Rebuild() {
    Host* host = GetHost();
    const float scale = host ? host->Scale() : 1.0f;
    wchar_t num[160];
    swprintf_s(num, L"|%g,%g,%g,%g|%g|%g|%g|%lld|%lld|%g|%lld,%lld|%d", video_.x, video_.y, video_.w, video_.h, subPos_,
               subScale_, secondaryPos_, static_cast<long long>(marginY_), static_cast<long long>(secondarySid_), scale,
               static_cast<long long>(PS().videoW), static_cast<long long>(PS().videoH), assMode_ ? 1 : 0);
    std::wstring text = assText_ + L'\x1f' + secondaryText_;
    std::wstring key = text + num;
    if (key == key_) return;
    key_ = std::move(key);
    // The picked words are indices into words_: they mean nothing once the line changed, or when
    // the same line has other words now (a block too small to point at is left out).
    const bool newText = text != textKey_;
    const size_t oldWords = words_.size();
    auto dropStalePick = [&] {
        if (newText || words_.size() != oldWords) DropPick();
    };
    textKey_ = std::move(text);
    blocks_.clear();
    words_.clear();
    bounds_ = {};
    IDWriteFactory5* dw = gfx::Gpu::Get().DWrite();
    if (!dw || video_.Empty()) {
        dropStalePick();
        return;
    }
    LARGE_INTEGER t0{}, t1{}, freq{};
    QueryPerformanceCounter(&t0);

    // Where mpv draws: in the window (with "Show subtitles in the black bars") or in the picture.
    Rect pic = video_;
    if (PS().videoW > 0 && PS().videoH > 0) {
        const float ar = static_cast<float>(PS().videoW) / static_cast<float>(PS().videoH);
        if (video_.w / video_.h > ar) pic = video_.Center(video_.h * ar, video_.h);
        else pic = video_.Center(video_.w, video_.w / ar);
    }
    const Rect ref = Cfg().GetBool("subtitles.useBlackBars") ? video_ : pic;
    const float sizeScale = static_cast<float>(subScale_ > 0 ? subScale_ : 1);
    auto snap = [scale](float v) { return std::round(v * scale) / scale; };

    // The Moonbow style: mpv's sizes are pixels of a 720-line frame, of the window ("Relative")
    // or of the picture. The lift over the control bar is in sub-margin-y.
    const float unit = (Cfg().GetString("subtitles.sizeMode") == "pixels" ? pic.h : video_.h) / 720.0f;
    Look plain;
    plain.font = style_.font;
    plain.line = style_.size * sizeScale * unit;
    plain.bold = style_.bold;
    plain.text = style_.text;
    plain.outlineColor = style_.outlineColor;
    plain.back = style_.back;
    plain.outline = style_.outline * unit;
    plain.shadow = style_.shadow ? 2 * unit : 0;
    plain.background = style_.background == "lines" ? Look::Lines : style_.background == "box" ? Look::Box : Look::None;
    const float plainV = static_cast<float>(marginY_) * unit, plainH = 25 * unit;

    // Events of one vertical alignment stack into one block, as libass stacks them.
    struct Group {
        std::wstring text;
        int align = 2;
        bool secondary = false;
        std::vector<uint8_t> marks;  // italic / bold per character, empty when none
        Look look;
        float marginV = 0, marginL = 0, marginR = 0;
    };
    std::vector<Group> groups;
    auto add = [&](Group g) {
        const int row = (g.align - 1) / 3;  // 0 bottom, 1 middle, 2 top
        for (Group& o : groups)
            if (!o.secondary && !g.secondary && (o.align - 1) / 3 == row) {
                if (!g.marks.empty() || !o.marks.empty()) {
                    o.marks.resize(o.text.size(), 0);
                    o.marks.push_back(0);
                    if (g.marks.empty()) o.marks.resize(o.marks.size() + g.text.size(), 0);
                    else o.marks.insert(o.marks.end(), g.marks.begin(), g.marks.end());
                }
                o.text += L'\n' + g.text;
                return;
            }
        groups.push_back(std::move(g));
    };
    if (assMode_) {
        // libass scales the script's PlayRes to the picture.
        const float sy = pic.h / static_cast<float>(header_.playResY), sx = pic.w / static_cast<float>(header_.playResX);
        for (subtext::Event& ev : subtext::ParseFullEvents(assText_)) {
            const subtext::AssStyle* st = header_.Find(ev.style);
            Group g;
            g.align = ev.align ? ev.align : st ? st->align : 2;
            g.text = std::move(ev.text);
            g.marks = std::move(ev.marks);
            g.look = plain;
            if (st) {
                g.look.font = st->font;
                g.look.line = st->size * sy * sizeScale;
                g.look.bold = st->bold;
                g.look.italic = st->italic;
                g.look.text = Color::Hex(st->primary, st->primaryAlpha);
                g.look.outlineColor = Color::Hex(st->outlineColor);
                g.look.back = Color::Hex(st->back, st->backAlpha);
                g.look.outline = st->outline * sy;
                g.look.shadow = st->shadow * sy;
                g.look.background = st->borderStyle == 3 ? Look::Lines : Look::None;
            }
            g.marginV = static_cast<float>(ev.marginV ? ev.marginV : st ? st->marginV : 10) * sy;
            g.marginL = static_cast<float>(ev.marginL ? ev.marginL : st ? st->marginL : 10) * sx;
            g.marginR = static_cast<float>(ev.marginR ? ev.marginR : st ? st->marginR : 10) * sx;
            add(std::move(g));
        }
    } else {
        for (subtext::Event& ev : subtext::ParseEvents(assText_)) {
            Group g;
            g.align = ev.align;
            g.text = std::move(ev.text);
            g.marks = std::move(ev.marks);
            g.look = plain;
            g.marginV = plainV;
            g.marginL = g.marginR = plainH;
            add(std::move(g));
        }
    }
    if (secondarySid_ != 0) {
        Group g;
        g.text = subtext::PlainText(secondaryText_);
        g.align = 8;
        g.secondary = true;
        g.look = plain;
        g.marginV = plainV;
        g.marginL = g.marginR = plainH;
        if (!g.text.empty()) add(std::move(g));
    }

    for (Group& g : groups) {
        if (g.look.line < 4) continue;  // a tiny picture: nothing to point at
        Block b;
        b.text = g.text;
        b.align = g.align;
        b.secondary = g.secondary;
        b.rtl = subtext::IsRtl(b.text);
        b.look = g.look;
        float em = 0.89f, ascent = 0.8f;
        FontRatios(b.look.font, b.look.bold, em, ascent);
        gfx::ComPtr<IDWriteTextFormat> format;
        if (FAILED(dw->CreateTextFormat(b.look.font.c_str(), nullptr,
                                        b.look.bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                                        b.look.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
                                        DWRITE_FONT_STRETCH_NORMAL, b.look.line * em, L"", &format)))
            continue;
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, b.look.line, b.look.line * ascent);
        const float wrapW = std::max(80.0f, ref.w - g.marginL - g.marginR);
        if (FAILED(dw->CreateTextLayout(b.text.c_str(), static_cast<UINT32>(b.text.size()), format.Get(), wrapW, 100000,
                                        &b.layout)))
            continue;
        // <i> and <b> of SRT, \i1 and \b1 of ASS: libass draws them, so does the overlay.
        for (size_t i = 0; i < g.marks.size();) {
            const uint8_t m = g.marks[i];
            size_t e = i + 1;
            while (e < g.marks.size() && g.marks[e] == m) ++e;
            const DWRITE_TEXT_RANGE range{static_cast<UINT32>(i), static_cast<UINT32>(e - i)};
            if (m & subtext::kItalic) b.layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, range);
            if (m & subtext::kBold) b.layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, range);
            i = e;
        }
        const int col = (g.align - 1) % 3;  // 0 left, 1 center, 2 right
        // Leading is the right edge of a right-to-left layout.
        const int leading = b.rtl ? 2 : 0;
        b.layout->SetTextAlignment(col == 1          ? DWRITE_TEXT_ALIGNMENT_CENTER
                                   : col == leading ? DWRITE_TEXT_ALIGNMENT_LEADING
                                                    : DWRITE_TEXT_ALIGNMENT_TRAILING);
        if (b.rtl) b.layout->SetReadingDirection(DWRITE_READING_DIRECTION_RIGHT_TO_LEFT);
        DWRITE_TEXT_METRICS m{};
        b.layout->GetMetrics(&m);
        b.ox = ref.x + g.marginL;
        const int row = (g.align - 1) / 3;
        if (g.secondary) {
            // secondary-sub-pos: 0 % is the top edge; past the middle it hangs from below.
            const float at = ref.y + static_cast<float>(std::clamp(secondaryPos_, 0.0, 150.0)) / 100 * ref.h;
            b.oy = secondaryPos_ <= 50 ? at + g.marginV : at - g.marginV - m.height;
        } else if (row == 0) {
            const float raised = static_cast<float>(100 - std::clamp(subPos_, 0.0, 150.0)) / 100 * ref.h;
            b.oy = ref.Bottom() - g.marginV - raised - m.height;
        } else if (row == 2) {
            b.oy = ref.y + g.marginV;
        } else {
            b.oy = ref.Cy() - m.height / 2;
        }
        b.ox = snap(b.ox);
        b.oy = snap(b.oy);
        b.box = {b.ox + m.left, b.oy + m.top, m.width, m.height};
        // Line boxes, for "Box behind each line" (and BorderStyle 3 of .ass).
        UINT32 count = 0;
        b.layout->HitTestTextRange(0, static_cast<UINT32>(b.text.size()), b.ox, b.oy, nullptr, 0, &count);
        if (count) {
            std::vector<DWRITE_HIT_TEST_METRICS> hm(count);
            if (SUCCEEDED(b.layout->HitTestTextRange(0, static_cast<UINT32>(b.text.size()), b.ox, b.oy, hm.data(), count,
                                                     &count)))
                for (UINT32 i = 0; i < count; ++i) {
                    const Rect r{hm[i].left, hm[i].top, hm[i].width, hm[i].height};
                    if (!b.lines.empty() && std::abs(b.lines.back().y - r.y) < 0.5f) {
                        Rect& l = b.lines.back();  // one line split in bidi runs
                        const float right = std::max(l.Right(), r.Right());
                        l.x = std::min(l.x, r.x);
                        l.w = right - l.x;
                    } else {
                        b.lines.push_back(r);
                    }
                }
        }
        blocks_.push_back(std::move(b));
    }
    // Top to bottom, so arrows and the dump read the way the screen does.
    std::stable_sort(blocks_.begin(), blocks_.end(), [](const Block& a, const Block& b) { return a.box.y < b.box.y; });

    for (size_t bi = 0; bi < blocks_.size(); ++bi) {
        Block& b = blocks_[bi];
        for (const subtext::Span& sp : subtext::SegmentWords(b.text)) {
            DWRITE_HIT_TEST_METRICS hm[4];
            UINT32 count = 0;
            if (FAILED(b.layout->HitTestTextRange(sp.start, sp.length, b.ox, b.oy, hm, 4, &count)) || !count) continue;
            Rect r{hm[0].left, hm[0].top, hm[0].width, hm[0].height};
            for (UINT32 i = 1; i < count; ++i) {
                const float right = std::max(r.Right(), hm[i].left + hm[i].width);
                const float bottom = std::max(r.Bottom(), hm[i].top + hm[i].height);
                r.x = std::min(r.x, hm[i].left);
                r.y = std::min(r.y, hm[i].top);
                r.w = right - r.x;
                r.h = bottom - r.y;
            }
            words_.push_back({static_cast<int>(bi), sp.start, sp.length, r});
        }
        const Rect hit = b.box.Inset(-kHitPad);
        if (bounds_.Empty()) {
            bounds_ = hit;
        } else {
            const float right = std::max(bounds_.Right(), hit.Right()), bottom = std::max(bounds_.Bottom(), hit.Bottom());
            bounds_.x = std::min(bounds_.x, hit.x);
            bounds_.y = std::min(bounds_.y, hit.y);
            bounds_.w = right - bounds_.x;
            bounds_.h = bottom - bounds_.y;
        }
    }
    bounds_ = bounds_.Intersect(video_);
    dropStalePick();
    // Stepping frames from the keyboard: the focus lands on the new line's first word.
    if (keyboard_ && focus_ < 0 && !words_.empty()) {
        focus_ = anchor_ = FirstWord();
        selA_ = selB_ = focus_;
    }
    QueryPerformanceCounter(&t1);
    QueryPerformanceFrequency(&freq);
    buildUs_ = static_cast<int>((t1.QuadPart - t0.QuadPart) * 1000000 / std::max<LONGLONG>(1, freq.QuadPart));
    ++builds_;
    Invalidate();
}

// Shadow, outline and text of one block at the origin, recorded once per line and style so a
// repaint (hover, selection) only replays it. The outline is the text stamped around a circle,
// as in the Settings preview.
bool SubtitleWords::Record(ID2D1DeviceContext5* ctx, Block& b) {
    // One recording context per device, kept: a new one per line cost a few MB each time.
    if (!recorder_) {
        gfx::ComPtr<ID2D1Device> device;
        ctx->GetDevice(&device);
        if (!device || FAILED(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &recorder_))) return false;
    }
    ID2D1DeviceContext* rec = recorder_.Get();
    gfx::ComPtr<ID2D1CommandList> list;
    if (FAILED(rec->CreateCommandList(&list))) return false;
    float dpiX = 96, dpiY = 96;
    ctx->GetDpi(&dpiX, &dpiY);
    rec->SetDpi(dpiX, dpiY);
    rec->SetTarget(list.Get());
    rec->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    rec->BeginDraw();
    const Look& k = b.look;
    const bool lineBoxes = k.background == Look::Lines;  // outline and shadow become the box
    const float r = lineBoxes ? 0.0f : k.outline;
    gfx::ComPtr<ID2D1SolidColorBrush> brush;
    rec->CreateSolidColorBrush(k.text.D2D(), &brush);
    auto stamp = [&](float ox, float oy, Color c) {
        brush->SetColor(c.D2D());
        if (r > 0) {
            const int n = r > 2 ? 24 : 16;
            for (float rr : {r, r * 0.55f})
                for (int i = 0; i < n; ++i) {
                    const float a = 6.2831853f * static_cast<float>(i) / static_cast<float>(n);
                    rec->DrawTextLayout({ox + rr * std::cos(a), oy + rr * std::sin(a)}, b.layout.Get(), brush.Get());
                }
        }
        rec->DrawTextLayout({ox, oy}, b.layout.Get(), brush.Get());
    };
    if (k.shadow > 0 && !lineBoxes) {
        const float s = std::max(1.0f, k.shadow);
        stamp(s, s, Color::Hex(0x000000, 0.5f));
    }
    if (r > 0) stamp(0, 0, k.outlineColor);
    brush->SetColor(k.text.D2D());
    rec->DrawTextLayout({0, 0}, b.layout.Get(), brush.Get());
    const HRESULT hr = rec->EndDraw();
    rec->SetTarget(nullptr);
    if (FAILED(hr) || FAILED(list->Close())) return false;
    b.scene = list;
    return true;
}

void SubtitleWords::Paint(Painter& p) {
    ID2D1DeviceContext5* ctx = p.Ctx();
    if (!ctx || blocks_.empty()) return;
    const uint32_t gen = gfx::Gpu::Get().Generation();
    const float scale = GetHost() ? GetHost()->Scale() : 1.0f;
    if (gen != generation_ || scale != builtScale_) {
        if (gen != generation_) recorder_.Reset();
        generation_ = gen;
        builtScale_ = scale;
        for (Block& b : blocks_) b.scene.Reset();
    }
    const int lo = selA_ < 0 ? -1 : std::min(selA_, selB_), hi = selA_ < 0 ? -1 : std::max(selA_, selB_);
    const Color pick = Color::Hex(0x000000, 0.55f);
    for (size_t bi = 0; bi < blocks_.size(); ++bi) {
        Block& b = blocks_[bi];
        const Look& k = b.look;
        if (k.background == Look::Box) {
            p.Fill(b.box.Inset(Insets::Sym(-(k.outline + k.line * 0.2f), -(k.outline + k.line * 0.08f))), k.back, 3);
        } else if (k.background == Look::Lines) {
            const float boxPad = std::max(1.0f, k.outline);
            for (const Rect& l : b.lines) {
                if (k.shadow > 0) {
                    const float s = std::max(1.0f, k.shadow);
                    p.Fill(l.Inset(-boxPad).Offset(s, s), Color::Hex(0x000000, 0.5f));
                }
                p.Fill(l.Inset(-boxPad), k.outlineColor);
            }
        }
        // The picked words sit on a dark pill, readable over any picture.
        for (int i = lo; i >= 0 && i <= hi; ++i)
            if (words_[static_cast<size_t>(i)].block == static_cast<int>(bi))
                p.Fill(words_[static_cast<size_t>(i)].r.Inset(Insets::Sym(-4, 0)), pick, 4);
        if (!b.scene && !Record(ctx, b)) continue;
        ctx->DrawImage(b.scene.Get(), D2D1::Point2F(b.ox, b.oy));
    }
    // Hover: an underline in the text color with an outline, so it shows on light scenes too.
    if (hover_ >= 0 && hover_ < static_cast<int>(words_.size())) {
        const Word& w = words_[static_cast<size_t>(hover_)];
        const Look& k = blocks_[static_cast<size_t>(w.block)].look;
        const float t = std::max(2.0f, std::round(k.line * 0.07f));
        const Rect u{w.r.x, w.r.Bottom() - t * 0.5f, w.r.w, t};
        p.Fill(u.Inset(-1), k.outlineColor.WithAlpha(0.8f), 1);
        p.Fill(u, k.text.WithAlpha(1), 1);
    }
    // The keyboard focus: a ring in the text color on a dark one, visible over any picture (the
    // theme's focus color is made for the panels, not for video).
    if (keyboard_ && focus_ >= 0 && focus_ < static_cast<int>(words_.size()) && (IsFocused() || menu_)) {
        const Word& w = words_[static_cast<size_t>(focus_)];
        const Rect ring = w.r.Inset(Insets::Sym(-6, -2));
        p.Stroke(ring.Inset(-1), Color::Hex(0x000000, 0.7f), 7, 3);
        p.Stroke(ring, blocks_[static_cast<size_t>(w.block)].look.text.WithAlpha(1), 6, 1.5f);
    }
}

// --- input -----------------------------------------------------------------------------------

Widget* SubtitleWords::HitTest(float x, float y) {
    if (!visible) return nullptr;
    for (const Block& b : blocks_)
        if (b.box.Inset(-kHitPad).Contains(x, y)) return this;
    return nullptr;
}

int SubtitleWords::WordAt(float x, float y, int block) const {
    int best = -1;
    float bestD = 1e9f;
    for (size_t i = 0; i < words_.size(); ++i) {
        const Word& w = words_[i];
        if (block >= 0 && w.block != block) continue;
        // Half the gap between words belongs to each side; above and below, the line itself.
        const Rect r = w.r.Inset(Insets::Sym(-4, 0));
        if (r.Contains(x, y)) return static_cast<int>(i);
        if (block >= 0 && y >= w.r.y && y < w.r.Bottom()) {
            // Dragging: the nearest word of the line under the pointer.
            const float d = x < r.x ? r.x - x : x - r.Right();
            if (d < bestD) bestD = d, best = static_cast<int>(i);
        }
    }
    return best;
}

void SubtitleWords::Select(int a, int b) {
    selA_ = a;
    selB_ = b;
    Invalidate();
}

bool SubtitleWords::OnMouseDown(const MouseEvent& e) {
    if (e.button != MouseButton::Left) return true;
    const int w = WordAt(e.x, e.y);
    pressed_ = w;
    clickPending_ = false;
    if (w < 0) return true;  // between words: neither a word nor a click on the video
    if (keyboard_) focus_ = w;
    if (e.mods.shift && anchor_ >= 0 && words_[static_cast<size_t>(anchor_)].block == words_[static_cast<size_t>(w)].block) {
        Select(anchor_, w);
    } else {
        anchor_ = w;
        Select(w, w);
        dragging_ = true;
    }
    return true;
}

bool SubtitleWords::OnMouseMove(const MouseEvent& e) {
    window_->PointerMoved(e.x, e.y);
    if (dragging_ && anchor_ >= 0) {
        const int w = WordAt(e.x, e.y, words_[static_cast<size_t>(anchor_)].block);
        if (w >= 0 && w != selB_) Select(anchor_, w);
    }
    const int h = WordAt(e.x, e.y);
    cursor = h >= 0 ? IDC_HAND : IDC_ARROW;
    if (h != hover_) {
        hover_ = h;
        Invalidate();
    }
    return true;
}

bool SubtitleWords::OnMouseUp(const MouseEvent& e) {
    if (e.button != MouseButton::Left) return true;
    dragging_ = false;
    clickPending_ = false;
    if (pressed_ < 0 || e.cancelled) return true;
    if (HitTest(e.x, e.y) == this) clickPending_ = true;  // OnClick follows and opens the menu
    else if (selA_ != selB_) OpenMenu();                  // a drag that ended off the line
    return true;
}

void SubtitleWords::OnClick() {
    if (!clickPending_) return;
    clickPending_ = false;
    if (selA_ >= 0) OpenMenu();
}

bool SubtitleWords::OnDoubleClick(const MouseEvent& e) {
    // The second click of a double click that paused the video (the words came up under it)
    // still means what it means on the video: fullscreen, by default.
    pressed_ = -1;
    dragging_ = clickPending_ = false;
    if (forwardDoubleClick) return forwardDoubleClick(e);
    return true;
}

bool SubtitleWords::OnWheel(const WheelEvent& e) {
    VideoWheel(e.delta, e.horizontal);
    return true;
}

void SubtitleWords::OnMouseLeave() {
    if (hover_ < 0) return;
    hover_ = -1;
    Invalidate();
}

void SubtitleWords::DropPick() {
    Host* host = GetHost();
    if (menu_ && host && host->IsPopupOpen(menu_)) host->ClosePopup(menu_);
    menu_ = nullptr;
    hover_ = anchor_ = selA_ = selB_ = focus_ = pressed_ = -1;
    dragging_ = clickPending_ = false;
}

void SubtitleWords::ResetInteraction() {
    DropPick();
    cursor = IDC_ARROW;
    Host* host = GetHost();
    if (keyboard_) {
        keyboard_ = false;
        focusable = false;
        if (host && host->Focus() == this) host->SetFocus(nullptr);
    }
}

int SubtitleWords::FirstWord() const {
    // The dialogue: the first word of the lowest block (a sign at the top comes second).
    if (words_.empty()) return -1;
    const int last = words_.back().block;
    for (size_t i = 0; i < words_.size(); ++i)
        if (words_[i].block == last) return static_cast<int>(i);
    return 0;
}

void SubtitleWords::EnterKeyboard() {
    Host* host = GetHost();
    if (!host || words_.empty()) return;
    keyboard_ = true;
    focusable = true;
    if (focus_ < 0 || focus_ >= static_cast<int>(words_.size())) focus_ = FirstWord();
    anchor_ = focus_;
    Select(focus_, focus_);
    host->SetFocus(this, true);
}

void SubtitleWords::OnFocusChanged(bool focused) {
    Invalidate();
    if (focused || !keyboard_) return;
    Host* host = GetHost();
    if (openingMenu_ || (menu_ && host && host->IsPopupOpen(menu_))) return;  // the word menu gives it back
    keyboard_ = false;
    focusable = false;
}

// Next word in reading order on the screen: right-to-left lines run the other way.
int SubtitleWords::Step(int from, int step) const {
    if (from < 0 || words_.empty()) return FirstWord();
    if (blocks_[static_cast<size_t>(words_[static_cast<size_t>(from)].block)].rtl) step = -step;
    const int to = from + step;
    return to < 0 || to >= static_cast<int>(words_.size()) ? from : to;
}

// The nearest word on the line above (dir -1) or below (+1).
int SubtitleWords::LineStep(int from, int down) const {
    if (from < 0) return FirstWord();
    const Rect& cur = words_[static_cast<size_t>(from)].r;
    float lineY = down < 0 ? -1e9f : 1e9f;
    for (const Word& w : words_) {
        const float cy = w.r.Cy();
        if (down < 0 && cy < cur.y && cy > lineY) lineY = cy;
        if (down > 0 && cy > cur.Bottom() && cy < lineY) lineY = cy;
    }
    if (std::abs(lineY) > 1e8f) return from;
    int best = from;
    float bestD = 1e9f;
    for (size_t i = 0; i < words_.size(); ++i) {
        const Rect& r = words_[i].r;
        if (std::abs(r.Cy() - lineY) > 1) continue;
        const float d = std::abs(r.Cx() - cur.Cx());
        if (d < bestD) bestD = d, best = static_cast<int>(i);
    }
    return best;
}

bool SubtitleWords::OnKeyDown(const KeyEvent& e) {
    if (!keyboard_ || words_.empty()) return false;
    const bool plain = !e.mods.ctrl && !e.mods.alt;
    int next = -1;
    switch (e.key) {
        case VK_LEFT:
        case VK_RIGHT:
            if (!plain) return false;
            next = Step(focus_, e.key == VK_RIGHT ? 1 : -1);
            break;
        case VK_UP:
        case VK_DOWN:
            if (!plain) return false;
            next = LineStep(focus_, e.key == VK_DOWN ? 1 : -1);
            break;
        case VK_HOME: next = 0; break;
        case VK_END: next = static_cast<int>(words_.size()) - 1; break;
        case VK_RETURN:
        case VK_APPS:
            if (selA_ < 0) Select(focus_, focus_);
            OpenMenu();
            return true;
        case VK_ESCAPE:
            dismissed_ = true;
            Reevaluate();
            return true;
        case 'C':
            if (!e.mods.ctrl || e.mods.alt || e.mods.shift) return false;
            Copy(SelectedText());
            return true;
        case VK_SPACE: {
            // The host would press the focused widget; Space stays the window's key (play).
            const Action a = ActionForKey(e);
            if (a != Action::None) Run(a);
            return true;
        }
        default: return false;
    }
    if (next < 0) return true;
    // Shift extends the selection within the line's block; a plain move picks one word.
    const bool extend = e.mods.shift && anchor_ >= 0 &&
                        words_[static_cast<size_t>(anchor_)].block == words_[static_cast<size_t>(next)].block;
    focus_ = next;
    if (!extend) anchor_ = next;
    Select(anchor_, next);
    return true;
}

// --- actions ---------------------------------------------------------------------------------

std::wstring SubtitleWords::SelectedText() const {
    if (selA_ < 0 || selB_ < 0 || words_.empty()) return {};
    const Word& a = words_[static_cast<size_t>(std::min(selA_, selB_))];
    const Word& b = words_[static_cast<size_t>(std::max(selA_, selB_))];
    if (a.block != b.block) return {};
    const std::wstring& text = blocks_[static_cast<size_t>(a.block)].text;
    std::wstring out = text.substr(a.start, b.start + b.length - a.start);
    for (wchar_t& c : out)
        if (c == L'\n' || c == 0x00A0) c = L' ';
    return out;
}

std::wstring SubtitleWords::PhraseText() const {
    // The block the picked words are in: the dialogue, not a sign at the top or the other track.
    if (selA_ >= 0 && selA_ < static_cast<int>(words_.size()))
        return subtext::Phrase({blocks_[static_cast<size_t>(words_[static_cast<size_t>(selA_)].block)].text});
    std::vector<std::wstring> parts;
    for (const Block& b : blocks_)
        if (!b.secondary) parts.push_back(b.text);
    if (parts.empty())
        for (const Block& b : blocks_) parts.push_back(b.text);
    return subtext::Phrase(parts);
}

Rect SubtitleWords::SelectionRect() const {
    if (selA_ < 0) return bounds_;
    const Rect& a = words_[static_cast<size_t>(std::min(selA_, selB_))].r;
    const Rect& b = words_[static_cast<size_t>(std::max(selA_, selB_))].r;
    const float x = std::min(a.x, b.x), y = std::min(a.y, b.y);
    return {x, y, std::max(a.Right(), b.Right()) - x, std::max(a.Bottom(), b.Bottom()) - y};
}

void SubtitleWords::OpenMenu() {
    Host* host = GetHost();
    const std::wstring sel = SelectedText();
    if (!host || sel.empty()) return;
    const bool several = selA_ != selB_;
    const std::wstring phrase = PhraseText();
    std::vector<MenuItem> items;
    MenuItem copy{several ? Tr(L"Copy words") : Tr(L"Copy word"), keyboard_ ? L"Ctrl + C" : L""};
    copy.action = [this, sel] { Copy(sel); };
    items.push_back(std::move(copy));
    MenuItem copyPhrase{Tr(L"Copy phrase")};
    copyPhrase.action = [this, phrase] { Copy(phrase); };
    copyPhrase.enabled = phrase != sel;
    items.push_back(std::move(copyPhrase));
    items.push_back(MenuItem::Separator());
    MenuItem look{Tr(L"Look up in dictionary")};
    look.action = [this, sel] { Lookup("subtitles.dictionaryUrl", sel); };
    items.push_back(std::move(look));
    MenuItem translate{Tr(L"Translate")};
    // A lone word is translated as itself; with "Copy phrase" disabled the line is that word.
    translate.action = [this, sel] { Lookup("subtitles.translateUrl", sel); };
    items.push_back(std::move(translate));
    // Above the whole block, at the picked words: below them it would cover the control bar,
    // and over the block the rest of the line the user is reading.
    Rect anchor = SelectionRect().Inset(Insets::Sym(-4, 0));
    if (selA_ >= 0) {
        const Rect& box = blocks_[static_cast<size_t>(words_[static_cast<size_t>(selA_)].block)].box;
        anchor.h = anchor.Bottom() - (box.y - 2);
        anchor.y = box.y - 2;
    }
    openingMenu_ = true;  // the menu takes focus before ShowMenu returns it
    menu_ = ShowMenu(host, anchor, std::move(items), Place::Above, 200);
    openingMenu_ = false;
}

void SubtitleWords::Copy(const std::wstring& text) {
    if (text.empty()) return;
    lastCopy_ = text;
    if (!Host::Automation()) dlg::SetClipboardText(window_->Hwnd(), text);
    ShowToast(window_, Tr(L"Copied to clipboard"), "check_active");
}

void SubtitleWords::Lookup(const char* key, const std::wstring& text) {
    const std::wstring lang = CurrentLanguage() == Language::Russian ? L"ru" : L"en";
    const std::wstring url = subtext::LookupUrl(Cfg().GetWString(key), text, lang);
    if (url.empty()) {
        ShowToast(window_, Tr(L"The link in Settings › Subtitles must start with http:// or https:// and have no spaces"),
                  "warning", 3000);
        return;
    }
    lastUrl_ = url;
    // The browser is a native window: background test runs record the link instead (ShellOpen).
    // A failure (a link too long for the browser's handler, no browser) says so.
    const HINSTANCE r = Host::ShellOpen(window_->Hwnd(), L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (!Host::Automation() && reinterpret_cast<INT_PTR>(r) <= 32)
        ShowToast(window_, Tr(L"Couldn't open the link in the browser"), "warning", 3000);
}

void SubtitleWords::DebugInfo(std::string& out) const {
    out += " words=" + std::to_string(words_.size()) + " builds=" + std::to_string(builds_) +
           " build-us=" + std::to_string(buildUs_);
    if (hid_) out += " mpv-subs-off";  // not "hidden": the dump tools read that word as invisible
    if (keyboard_) out += " keyboard focus=" + std::to_string(focus_);
    if (hover_ >= 0) out += " hover=" + std::to_string(hover_);
    if (selA_ >= 0) out += " sel=" + std::to_string(std::min(selA_, selB_)) + "-" + std::to_string(std::max(selA_, selB_));
    if (selA_ >= 0) out += " picked=" + Quote(SelectedText());
    if (!words_.empty()) {
        std::vector<std::wstring> all;
        for (const Block& b : blocks_) all.push_back(b.text);
        out += " blocks=" + std::to_string(blocks_.size()) + " text=" + Quote(subtext::Phrase(all));
        if (assMode_) out += " ass-styles";
    }
    if (!lastCopy_.empty()) out += " copied=" + Quote(lastCopy_);
    if (!lastUrl_.empty()) out += " opened=" + Quote(lastUrl_);
}

}  // namespace app
