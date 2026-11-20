# Security

## Reporting a vulnerability

**Please do not open a public issue.** Report privately through
[Security → Report a vulnerability](https://github.com/Piwqust/moonbow/security/advisories/new) in this repository;
only the maintainers see it.

Include:

- the Moonbow version (**About Moonbow** in the logo menu) and the Windows version;
- what happens, and how to reproduce it. If a file triggers it, describe the file or attach a minimal one.

> [!WARNING]
> Do not send your OpenSubtitles or TMDB keys, not even in a private report. Replace them with `***`.

Писать можно и по-русски.

## What counts

| In Moonbow | Elsewhere |
| --- | --- |
| A media file, playlist, subtitle, lyrics file, skin or tag that makes Moonbow's own code run something, crash in a way that looks exploitable, or read or write files it should not | Flaws in mpv or FFmpeg decoders: report them to [mpv](https://github.com/mpv-player/mpv/security) or [FFmpeg](https://ffmpeg.org/security.html) |
| The installer or uninstaller replacing, deleting or running files outside its own folders, or gaining privileges | Flaws in yt-dlp: report them to [yt-dlp](https://github.com/yt-dlp/yt-dlp/security) |
| Online features (LRCLIB, OpenSubtitles, TMDB) sending more than they should, or trusting a server's answer in a way that harms the computer | The services themselves |
| Saved API keys readable by another user or program | |

If Moonbow ships a libmpv build with a known flaw, report that here too: the fix is a new Moonbow build.

## Supported versions

Only the latest version is supported. Fixes ship in a new version, which Moonbow 0.2 and later install by
themselves (Settings → General → Updates); on 0.1.0, install the new release over the old one.

The update feed (`moonbow-update.json` on each release) is signed with ECDSA P-256, and Moonbow runs a downloaded
setup only when the feed's signature checks out against the key built into it and the file's size and SHA-256 match
the feed (`src/setup/UpdateFeed.cpp`). A way to make Moonbow install something else counts as a vulnerability.
