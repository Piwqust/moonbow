#pragma once
// Scrobbling: the songs played in Moonbow go to Last.fm and/or ListenBrainz under the user's own
// account (privacy.lastfmSubmit, privacy.listenbrainzSubmit; both off by default).
//
// - A song counts once it has played for half its length or 4 minutes, whichever comes first,
//   and only when it is at least 30 s long and has an artist and a title (Last.fm's rules, which
//   ListenBrainz follows too). Seeking ahead is not listening. Streams and videos are left out.
// - "Now playing" goes out when a song starts.
// - Listens that could not be sent (offline, the service down) wait in <data>\scrobbles.json and
//   go out with the next one: up to 14 days old (Last.fm refuses older ones), at most 1000.
// - Last.fm: the user name and password are traded once for a session key
//   (auth.getMobileSession), which is kept encrypted; the password field is cleared then. The API
//   key and shared secret are the user's own (privacy.lastfmApiKey, privacy.lastfmApiSecret).
// - ListenBrainz: the user token from listenbrainz.org/settings, kept encrypted.
//
// UI thread only.
namespace app {

void InitScrobbler();

}  // namespace app
