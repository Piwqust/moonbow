// Music view shown instead of the video for audio files. Placeholder until the
// design's version (blurred cover backdrop, waveform) lands.
#include "Util.h"
#include "app/App.h"
#include "app/player/PlayerView.h"

namespace app {

using namespace ui;

namespace {

class MusicView : public Widget {
public:
    MusicView() {
        conn_ = Connect(TheApp().playerChanged, [this](unsigned m) {
            if (m & (kChangeMeta | kChangeFile)) Invalidate();
        });
    }
    void Paint(Painter& p) override {
        p.Fill(rect, P().videoBg);
        const float side = std::min(rect.w, rect.h) * 0.5f;
        const Rect cover = rect.Center(side, side).Offset(0, -24);
        p.Fill(cover, P().bgCard, 12);
        p.Icon("music", cover.Center(side * 0.3f, side * 0.3f), P().textTertiary);
        const PlayerState& s = TheApp().player().State();
        std::wstring line = s.title;
        if (!s.artist.empty()) line += L" - " + s.artist;
        p.Text(line, {rect.x, cover.Bottom() + 24, rect.w, 24}, TextStyle::Large, P().textSecondary, HAlign::Center);
    }

private:
    ScopedConnection conn_;
};

}  // namespace

std::unique_ptr<Widget> CreateMusicView() {
    return std::make_unique<MusicView>();
}

}  // namespace app
