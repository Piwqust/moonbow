#pragma once
// Things the player shows over the video: the statistics panel (Ctrl+Shift+I), the
// "Next episode" card and the "Skip intro / credits" button.
#include "app/Signal.h"
#include "ui/Controls.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace app {

class MainWindow;

// Technical details of the playing file (general.developerMode). Painted by PlayerView, so
// clicks go through to the video. mpv properties are observed only while it is shown and the
// text is rebuilt at most twice a second; hidden it costs nothing.
class StatsOverlay {
public:
    StatsOverlay(MainWindow* window, std::function<void()> repaint);
    ~StatsOverlay();
    void SetShown(bool shown);
    bool Shown() const { return shown_; }
    void Paint(ui::Painter& p, const ui::Rect& video);

private:
    struct Values;
    void Observe();
    void Unobserve();
    void Rebuild();

    MainWindow* window_;
    std::function<void()> repaint_;
    std::unique_ptr<Values> v_;
    std::vector<uint64_t> observers_;
    std::vector<std::pair<std::wstring, std::wstring>> lines_;
    float labelW_ = 0, width_ = 0;
    bool shown_ = false, dirty_ = false;
    uintptr_t timer_ = 0;
    ScopedConnection playerConn_;
};

// "Next episode: S01E03 · in 12 s" with Play now / Cancel over the last 30 seconds (or the
// end credits chapter) of a series episode whose next file is in the queue or the folder.
// Also carries the chosen audio and subtitle languages over to the next episode, and keeps them
// for the series (LiveState::RememberShowLanguages), so a later session opens with them too.
class NextEpisodeCard : public ui::Widget {
public:
    explicit NextEpisodeCard(MainWindow* window);
    ~NextEpisodeCard() override;
    // The player's card: Next plays its episode when the queue has nothing after this file.
    // For a song it is "Up next", the queue's next item, 10 s before the end.
    static NextEpisodeCard* Instance();
    bool Wanted() const { return wanted_; }
    bool HasNext();  // takes the folder search's result when it has just finished
    void PlayNext();
    void Paint(ui::Painter& p) override;

private:
    struct FolderJob;
    void OnPlayer(unsigned changes);
    void OnNewFile();
    void Resolve();
    void Update();
    void UpdateSong(long long secs);
    void OnEnded();
    bool AutoNext() const;  // the episode starts by itself when this one ends
    void CarryLanguages();
    void SayLanguages(const std::wstring& audioLang, const std::wstring& text);
    void SetWanted(bool wanted);

    MainWindow* window_;
    ui::Label* title_ = nullptr;
    ui::Label* name_ = nullptr;
    ui::Button* play_ = nullptr;
    ui::Button* cancel_ = nullptr;
    uint64_t serial_ = 0;
    std::wstring curPath_, nextPath_;
    bool fromQueue_ = false, dismissed_ = false, wanted_ = false;
    bool song_ = false;  // a song: the queue's next item, whatever it is
    long long shownSecs_ = -1;
    std::shared_ptr<FolderJob> job_;
    // Track choice of the file at carryPath_, applied to its next episode.
    std::wstring carryPath_, audioLang_, audioTitle_, subLang_, subTitle_;
    bool hadSubs_ = false, subsOff_ = false;
    uint32_t choiceBase_[2] = {};  // Player::TrackChoices (audio, subtitles) when carryPath_ loaded
    ScopedConnection playerConn_, queueConn_, langConn_;
};

// "Skip intro" / "Skip credits" while inside a chapter named Opening, Intro, OP, Credits, ED,
// «Заставка» or «Титры»; for an episode without an intro chapter, inside the intro found by sound
// (IntroFinder.h).
class SkipChapterButton : public ui::Button {
public:
    explicit SkipChapterButton(NextEpisodeCard* card);
    bool Wanted() const { return wanted_; }

private:
    void Update();
    NextEpisodeCard* card_;
    int kind_ = 0;  // episodes::ChapterKind
    double skipTo_ = -1;  // the end of an intro found by sound; -1: skip by chapters
    bool wanted_ = false;
    ScopedConnection playerConn_, langConn_;
};

}  // namespace app
