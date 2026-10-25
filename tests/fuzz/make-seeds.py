"""Writes the fuzz seeds into tests/fuzz/seeds/<target>/: realistic inputs that the mutator starts from.

Usage: python tests/fuzz/make-seeds.py   (media seeds for "tags" need ffmpeg on PATH)
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SEEDS = os.path.join(HERE, "seeds")
ROOT = os.path.dirname(os.path.dirname(HERE))


def write(rel, text, enc="utf-8"):
    path = os.path.join(SEEDS, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    data = text.encode(enc) if isinstance(text, str) else text
    with open(path, "wb") as f:
        f.write(data)


def j(obj):
    return json.dumps(obj, ensure_ascii=False)


def text_seeds():
    write("json/lrclib-get.json", j({"id": 1, "trackName": "Song", "artistName": "Сава Ветров", "duration": 181.5,
                                     "instrumental": False, "plainLyrics": "Line one\nLine two",
                                     "syncedLyrics": "[00:01.20] Line one\n[00:05.00] Line two"}))
    write("json/lrclib-search.json", j([{"id": 1, "duration": 180, "instrumental": True, "plainLyrics": None,
                                         "syncedLyrics": None}, {"id": 2, "duration": 200, "syncedLyrics": "[00:00.00]x"}]))
    write("json/os-search.json", j({"total_count": 1, "data": [{"id": "9", "type": "subtitle", "attributes": {
        "language": "ru", "download_count": 5, "hearing_impaired": False, "ai_translated": False,
        "machine_translated": False, "from_trusted": True, "foreign_parts_only": False, "moviehash_match": True,
        "ratings": 7.5, "votes": 3, "release": "Show.S01E02.1080p",
        "feature_details": {"title": "Show", "movie_name": "Show - S01E02", "year": 2020, "season_number": 1,
                            "episode_number": 2},
        "files": [{"file_id": 123, "file_name": "show.srt"}]}}]}))
    write("json/os-download.json", j({"link": "https://example.org/a.srt", "file_name": "a.srt", "requests": 3,
                                      "remaining": 17, "message": "ok", "reset_time": "23 hours",
                                      "reset_time_utc": "2026-10-22T00:00:00.000Z"}))
    write("json/os-login.json", j({"user": {"allowed_downloads": 20, "remaining_downloads": 19, "vip": False},
                                   "base_url": "api.opensubtitles.com", "token": "abc", "status": 200}))
    write("json/tmdb-movie.json", j({"page": 1, "results": [{"id": 603, "title": "The Matrix",
                                                              "original_title": "The Matrix", "release_date": "1999-03-30",
                                                              "overview": "…", "poster_path": "/a.jpg",
                                                              "backdrop_path": "/b.jpg", "vote_average": 8.2}]}))
    write("json/tmdb-tv.json", j({"results": [{"id": 1399, "name": "Тихий берег", "original_name": "Quiet Shore",
                                               "first_air_date": "2011-04-17", "poster_path": None}]}))
    write("json/tmdb-episode.json", j({"id": 63056, "name": "Winter", "overview": "x", "air_date": "2011-04-17",
                                       "episode_number": 1, "season_number": 1, "still_path": "/s.jpg"}))
    write("json/values.json", j({"version": 1, "values": {"general.language": "ru", "x": [1, 2.5e10, -0.0, True, None,
                                                                                            {"y": "\u0000\U0001F600"}]}}))
    write("json/nested.json", "[" * 40 + '{"a":"\\u00e9\\ud83d\\ude00\\n"}' + "]" * 40)

    write("lyrics/synced.lrc", "[ar:Сава Ветров]\n[ti:Песня]\n[offset:+250]\n[00:12.00][01:30.5]Припев\n"
                               "[00:15.30]<00:15.30>Слово <00:16.10>за <00:16.80>словом\n[1:02:03.456]long\n")
    write("lyrics/plain.lrc", "Just text\nno stamps\n\n")
    write("lyrics/cp1251.lrc", "[00:01.00]Привет\r\n[00:02.00]мир\r\n", "cp1251")
    write("lyrics/utf16.lrc", "\ufeff[00:01.00]UTF-16 строка\r\n", "utf-16-le")

    write("ass/events.ass",
          "[Script Info]\nPlayResX: 1920\nPlayResY: 1080\n\n[V4+ Styles]\n"
          "Format: Name, Fontname, Fontsize, PrimaryColour, Alignment, MarginL, MarginR, MarginV\n"
          "Style: Default,Arial,48,&H00FFFFFF,2,10,10,10\n\n[Events]\n"
          "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
          r"Dialogue: 0,0:00:01.00,0:00:03.00,Default,,0,0,0,,{\an8\i1}Наверху{\i0}\Nвторая {\b1}строка{\b0}" "\n"
          r"Dialogue: 0,0:00:04.00,0:00:05.00,Default,,0,0,0,,{\p1}m 0 0 l 10 10{\p0}text\h\n" "\n")
    write("ass/sub-text.txt", r"1,0,Default,,0000,0000,0000,,Hello {\a6}world" "\n"
                              r"2,0,Default,,0,0,0,,{\pos(10,20)}Second\Nline" "\n")

    write("m3u/ext.m3u8", "\ufeff#EXTM3U\r\n#EXTINF:123,Artist - Title\r\nmusic\\song.mp3\r\n#EXTINF:-1,Radio\r\n"
                          "https://example.org/stream\r\nC:\\Media\\film.mkv\r\n..\\..\\up.flac\r\n"
                          "\\\\server\\share\\x.mp4\r\nfile:///C:/Media/a%20b.mp3\r\n")
    write("m3u/cp1251.m3u", "#EXTM3U\r\n#EXTINF:5,Песня\r\nПесня.mp3\r\n", "cp1251")

    names = ["Show.S01E02.1080p.WEB-DL", "Тихий берег - 1x03 - Пилот", "show_s2e10_final", "[Group] Anime - 07 [1080p]",
             "Movie (1999)", "Series.2020.E05", "Intro", "Opening", "Ending credits", "Part 1 of 3"]
    for i, n in enumerate(names):
        write(f"episode/{i}.txt", n)

    for name in os.listdir(os.path.join(ROOT, "res", "skins")):
        shutil.copy(os.path.join(ROOT, "res", "skins", name), os.path.join(SEEDS, "skin", name))


def media_seeds():
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        print("ffmpeg not found: no media seeds for 'tags'")
        return
    tmp = tempfile.mkdtemp(prefix="moonbow-seeds-")
    cover = os.path.join(tmp, "cover.png")
    run = lambda *a: subprocess.run([ffmpeg, "-v", "error", "-y", *a], check=True)
    run("-f", "lavfi", "-i", "color=c=gray:s=8x8", "-frames:v", "1", cover)
    tone = ["-f", "lavfi", "-i", "sine=frequency=440:duration=0.3"]
    meta = ["-metadata", "title=Песня", "-metadata", "artist=Сава Ветров", "-metadata", "album=Альбом",
            "-metadata", "date=2024", "-metadata", "track=3/12", "-metadata", "disc=1/2", "-metadata", "genre=Rock",
            "-metadata", "lyrics=[00:01.00]Строка"]
    with_cover = ["-i", cover, "-map", "0:a", "-map", "1:v", "-disposition:v", "attached_pic"]
    out = lambda n: os.path.join(SEEDS, "tags", n)
    os.makedirs(os.path.join(SEEDS, "tags"), exist_ok=True)
    run(*tone, *with_cover, "-c:a", "libmp3lame", "-b:a", "32k", "-c:v", "png", "-id3v2_version", "3", *meta, out("id3v23.mp3"))
    run(*tone, "-c:a", "libmp3lame", "-b:a", "32k", "-id3v2_version", "4", "-write_id3v1", "1", *meta, out("id3v24-v1.mp3"))
    run(*tone, *with_cover, "-c:a", "flac", "-c:v", "png", *meta, out("cover.flac"))
    run(*tone, "-c:a", "libvorbis", *meta, out("vorbis.ogg"))
    run(*tone, "-c:a", "libopus", "-b:a", "16k", *meta, out("opus.opus"))
    run(*tone, *with_cover, "-c:a", "aac", "-b:a", "32k", "-c:v", "png", *meta, out("cover.m4a"))
    run(*tone, "-c:a", "pcm_s16le", "-ar", "8000", *meta, out("info.wav"))
    run(*tone, "-c:a", "pcm_s16be", "-ar", "8000", "-write_id3v2", "1", *meta, out("id3.aiff"))
    run(*tone, "-c:a", "libopus", "-b:a", "16k", *meta, out("tags.mka"))
    run("-f", "lavfi", "-i", "color=c=black:s=32x32:d=0.2", "-c:v", "libx264", "-pix_fmt", "yuv420p",
        "-metadata", "title=Видео", out("video.mp4"))
    shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    text_seeds()
    media_seeds()
    for d in sorted(os.listdir(SEEDS)):
        files = os.listdir(os.path.join(SEEDS, d))
        print(f"{d}: {len(files)} files, {sum(os.path.getsize(os.path.join(SEEDS, d, f)) for f in files)} bytes")
