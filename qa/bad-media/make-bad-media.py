"""Builds a folder of media files that real libraries contain and players choke on.

Usage: python qa/bad-media/make-bad-media.py <empty folder inside %TEMP%>   (needs ffmpeg on PATH)

Then run-bad-media.ps1 scans the folder in a hidden test instance and opens every file. What is in it:
  empty/garbage   0-byte files and random bytes under media extensions
  truncated       real files cut in the middle, the header only, an MP4 without its index (moov at the end)
  disguised       one format under another extension
  tags            hand-made ID3v2 edge cases (huge frame sizes, unsync, v2.2, UTF-16 without BOM, CP-1251),
                  a 20 MB cover, 300 tag frames, broken FLAC and MP4 atoms
  streams         1x1 and 8000x16 video, 40 ms clips, 24 audio + 24 subtitle tracks, 300 chapters,
                  a start time near 2^33, an HEVC 10-bit clip
  subtitles       a 2 MB single line, 20000 ASS events, a binary .srt, a .lrc with 50000 stamps
  playlists       an .m3u that lists itself, 10000 entries, one 40000-character line
  names           emoji, RTL and combining marks, trailing dots and spaces, '#%&', a path over 260
                  characters, 40 nested folders, a junction that points at its own parent
"""
import os
import random
import shutil
import struct
import subprocess
import sys

if len(sys.argv) != 2:
    sys.exit(__doc__)
OUT = os.path.abspath(sys.argv[1])
TEMP = os.path.abspath(os.environ.get("TEMP", ""))
if not OUT.lower().startswith(TEMP.lower() + os.sep):
    sys.exit("The folder must be inside %TEMP%: " + OUT)
os.makedirs(OUT, exist_ok=True)
FFMPEG = shutil.which("ffmpeg") or sys.exit("ffmpeg not found")
rnd = random.Random(1)


def long_path(p):
    return "\\\\?\\" + p  # names Explorer and plain Win32 calls refuse (trailing dots, > 260 chars)


def write(rel, data):
    path = os.path.normpath(os.path.join(OUT, rel))
    os.makedirs(long_path(os.path.dirname(path)), exist_ok=True)
    with open(long_path(path), "wb") as f:
        f.write(data if isinstance(data, bytes) else data.encode("utf-8"))
    return path


def ff(*args):
    r = subprocess.run([FFMPEG, "-v", "error", "-y", *args])
    if r.returncode:
        print("skipped (ffmpeg failed):", args[-1])


def path(rel):
    p = os.path.normpath(os.path.join(OUT, rel))
    os.makedirs(os.path.dirname(p), exist_ok=True)
    return p


TONE = ["-f", "lavfi", "-i", "sine=frequency=330:duration=3"]
CLIP = ["-f", "lavfi", "-i", "testsrc2=s=320x180:d=3", "-f", "lavfi", "-i", "sine=duration=3"]
H264 = ["-c:v", "libx264", "-preset", "ultrafast", "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "64k"]

# --- real files to cut and disguise ------------------------------------------------------------
ref = os.path.join(OUT, "_ref")
os.makedirs(ref, exist_ok=True)
ff(*CLIP, *H264, os.path.join(ref, "clip.mp4"))                                    # moov at the end
ff(*CLIP, *H264, "-movflags", "+faststart", os.path.join(ref, "faststart.mp4"))
ff(*CLIP, *H264, os.path.join(ref, "clip.mkv"))
ff(*TONE, "-c:a", "libmp3lame", "-metadata", "title=Ref", os.path.join(ref, "tone.mp3"))
ff(*TONE, "-c:a", "flac", os.path.join(ref, "tone.flac"))
ff("-f", "lavfi", "-i", "color=c=gray:s=64x64", "-frames:v", "1", os.path.join(ref, "pic.png"))
refs = {n: open(os.path.join(ref, n), "rb").read() for n in os.listdir(ref)}

# --- empty and garbage --------------------------------------------------------------------------
for ext in ["mp4", "mkv", "avi", "mp3", "flac", "m4a", "ogg", "wav", "srt", "ass", "lrc", "m3u", "webm", "ts"]:
    write(f"empty/empty.{ext}", b"")
    write(f"garbage/random.{ext}", bytes(rnd.getrandbits(8) for _ in range(65536)))
write("garbage/zeros.mkv", bytes(1 << 20))
write("garbage/text.mp4", "This is not a video.\n" * 1000)

# --- truncated ------------------------------------------------------------------------------------
for name, data in refs.items():
    if name.endswith(".png"):
        continue
    stem, ext = os.path.splitext(name)
    write(f"truncated/{stem}-half{ext}", data[: len(data) // 2])
    write(f"truncated/{stem}-1k{ext}", data[:1024])
    write(f"truncated/{stem}-tail{ext}", data[len(data) // 2:])

# --- disguised --------------------------------------------------------------------------------
write("disguised/mp3-as.mkv", refs["tone.mp3"])
write("disguised/png-as.mp4", refs["pic.png"])
write("disguised/mkv-as.mp3", refs["clip.mkv"])
write("disguised/flac-as.m4a", refs["tone.flac"])


# --- tags -------------------------------------------------------------------------------------
def synchsafe(n):
    return bytes([(n >> 21) & 0x7F, (n >> 14) & 0x7F, (n >> 7) & 0x7F, n & 0x7F])


def id3(frames, version=3, flags=0, size=None):
    body = b"".join(frames)
    return b"ID3" + bytes([version, 0, flags]) + synchsafe(len(body) if size is None else size) + body


def frame(fid, data, version=3, size=None):
    n = len(data) if size is None else size
    return fid.encode() + (synchsafe(n) if version == 4 else struct.pack(">I", n)) + b"\0\0" + data


audio = refs["tone.mp3"]
audio = audio[audio.index(b"\xff\xfb"):] if b"\xff\xfb" in audio else audio  # MPEG frames without the old tag
write("tags/frame-size-huge.mp3", id3([frame("TIT2", b"\x00Title", size=0x7FFFFFF0)]) + audio)
write("tags/tag-size-huge.mp3", id3([frame("TIT2", b"\x00Title")], size=0x0FFFFFFF) + audio)
write("tags/frame-size-zero.mp3", id3([frame("TIT2", b"", size=0), frame("TPE1", b"\x00Artist")]) + audio)
write("tags/utf16-no-bom.mp3", id3([frame("TIT2", b"\x01" + "Без BOM".encode("utf-16-le"))]) + audio)
write("tags/utf16-odd.mp3", id3([frame("TIT2", b"\x01\xff\xfeA\x00B")]) + audio)
write("tags/cp1251.mp3", id3([frame("TIT2", b"\x00" + "Песня".encode("cp1251")),
                               frame("TPE1", b"\x00" + "Исполнитель".encode("cp1251"))]) + audio)
write("tags/unsync.mp3", id3([frame("TIT2", b"\x00\xff\x00\xe0Unsync")], flags=0x80) + audio)
write("tags/ext-header-bad.mp3", id3([struct.pack(">I", 0xFFFFFF) + b"\0\0" + frame("TIT2", b"\x00X")], flags=0x40) + audio)
write("tags/v22.mp3", b"ID3\x02\x00\x00" + synchsafe(13) + b"TT2" + b"\x00\x00\x07" + b"\x00Old v2" + audio)
write("tags/v24-synchsafe.mp3", id3([frame("TIT2", b"\x03" + "v2.4 UTF-8".encode(), version=4)], version=4) + audio)
write("tags/many-frames.mp3", id3([frame("TXXX", b"\x00key%d\x00value%d" % (i, i)) for i in range(300)]) + audio)
write("tags/apic-truncated.mp3", id3([frame("APIC", b"\x00image/png\x00\x03\x00" + refs["pic.png"][:20], size=5000)]) + audio)
write("tags/sylt-bad.mp3", id3([frame("SYLT", b"\x00eng\x02\x01" + b"line\x00" + b"\xff\xff")]) + audio)
write("tags/uslt-huge.mp3", id3([frame("USLT", b"\x00eng\x00" + ("строка\n" * 200000).encode("utf-8"))]) + audio)
write("tags/id3v1-only.mp3", audio + b"TAG" + "Песня".encode("cp1251").ljust(30, b"\0") + b"\xff" * 30 + b"\0" * 64 + b"\xff")
write("tags/two-id3.mp3", id3([frame("TIT2", b"\x00First")]) + id3([frame("TIT2", b"\x00Second")]) + audio)
flac = refs["tone.flac"]
write("tags/flac-block-huge.flac", flac[:4] + b"\x84\xff\xff\xff" + flac[8:])  # last VORBIS_COMMENT block, 16 MB length
write("tags/flac-no-streaminfo.flac", b"fLaC" + b"\x84\x00\x00\x08" + struct.pack("<I", 1) + b"\0\0\0\0" + flac[42:])
mp4 = refs["faststart.mp4"]
i = mp4.find(b"moov")
if i > 4:
    write("tags/mp4-moov-huge.mp4", mp4[: i - 4] + struct.pack(">I", 0xFFFFFFF0) + mp4[i:])
    write("tags/mp4-moov-zero.mp4", mp4[: i - 4] + struct.pack(">I", 0) + mp4[i:])
    write("tags/mp4-atom-tiny.mp4", mp4[: i - 4] + struct.pack(">I", 3) + mp4[i:])
cover = path("_ref/big-cover.png")
ff("-f", "lavfi", "-i", "nullsrc=s=2600x2600,geq=random(1)*255:128:128", "-frames:v", "1", cover)
ff(*TONE, "-i", cover, "-map", "0:a", "-map", "1:v", "-c:a", "libmp3lame", "-c:v", "png",
   "-disposition:v", "attached_pic", path("tags/cover-20mb.mp3"))

# --- streams --------------------------------------------------------------------------------------
ff("-f", "lavfi", "-i", "color=c=red:s=2x2:d=2", "-vf", "scale=1:1", "-c:v", "libx264", "-pix_fmt", "yuv444p", path("streams/1x1.mkv"))
ff("-f", "lavfi", "-i", "testsrc2=s=8000x16:d=2", "-c:v", "libx264", "-pix_fmt", "yuv420p", path("streams/8000x16.mp4"))
ff("-f", "lavfi", "-i", "testsrc2=s=320x180:d=0.04", "-c:v", "libx264", path("streams/40ms.mp4"))
ff("-f", "lavfi", "-i", "testsrc2=s=320x180:d=3", "-c:v", "libx265", "-pix_fmt", "yuv420p10le", "-tag:v", "hvc1",
   path("streams/hevc-10bit.mp4"))
ff(*CLIP, *H264, "-output_ts_offset", "95000", path("streams/start-26h.ts"))
srt = write("_ref/one.srt", "1\n00:00:00,000 --> 00:00:02,000\nSub\n")
many = ["-f", "lavfi", "-i", "testsrc2=s=320x180:d=3"]
for _ in range(24):
    many += ["-f", "lavfi", "-i", "sine=duration=3"]
for _ in range(24):
    many += ["-i", srt]
maps = ["-map", "0:v"] + sum([["-map", f"{k}:a"] for k in range(1, 25)], []) + sum([["-map", f"{k}:s"] for k in range(25, 49)], [])
langs = sum([[f"-metadata:s:a:{k}", f"language={['rus', 'eng', 'jpn', 'deu'][k % 4]}",
              f"-metadata:s:s:{k}", f"title=Субтитры {k}"] for k in range(24)], [])
ff(*many, *maps, "-c:v", "libx264", "-c:a", "libopus", "-b:a", "16k", "-c:s", "srt", *langs, path("streams/48-tracks.mkv"))
chapters = ";FFMETADATA1\n" + "".join(f"[CHAPTER]\nTIMEBASE=1/1000\nSTART={k * 10}\nEND={k * 10 + 10}\ntitle=Глава {k}\n"
                                      for k in range(300))
meta = write("_ref/chapters.txt", chapters)
ff(*CLIP, "-i", meta, "-map_metadata", "2", "-map", "0", "-map", "1", *H264, path("streams/300-chapters.mkv"))

# --- subtitles and lyrics next to a video ---------------------------------------------------------
shutil.copy(os.path.join(ref, "faststart.mp4"), path("subtitles/long-line.mp4"))
write("subtitles/long-line.srt", "1\n00:00:00,000 --> 00:00:03,000\n" + "длинная строка " * 140000 + "\n")
shutil.copy(os.path.join(ref, "faststart.mp4"), path("subtitles/many-events.mp4"))
ass = ["[Script Info]\nScriptType: v4.00+\n\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"]
ass += [f"Dialogue: 0,0:00:00.{k % 100:02d},0:00:03.00,Default,,0,0,0,,{{\\an{k % 9 + 1}}}Event {k}\n" for k in range(20000)]
write("subtitles/many-events.ass", "".join(ass))
shutil.copy(os.path.join(ref, "faststart.mp4"), path("subtitles/binary-sub.mp4"))
write("subtitles/binary-sub.srt", bytes(rnd.getrandbits(8) for _ in range(30000)))
shutil.copy(os.path.join(ref, "tone.mp3"), path("subtitles/many-stamps.mp3"))
write("subtitles/many-stamps.lrc", "".join(f"[{k // 6000:02d}:{k // 100 % 60:02d}.{k % 100:02d}]строка {k}\n" for k in range(50000)))

# --- playlists ------------------------------------------------------------------------------------
write("playlists/self.m3u", "#EXTM3U\nself.m3u\n" + os.path.join(OUT, "playlists", "self.m3u") + "\n")
write("playlists/10000.m3u8", "#EXTM3U\n" + "".join(f"#EXTINF:3,Track {k}\n../truncated/tone-half.mp3\n" for k in range(10000)))
write("playlists/long-line.m3u", "#EXTM3U\n#EXTINF:1," + "x" * 40000 + "\n" + "y" * 40000 + ".mp3\n")
write("playlists/garbage.m3u", bytes(rnd.getrandbits(8) for _ in range(20000)))

# --- names ----------------------------------------------------------------------------------------
tone = refs["tone.mp3"]
for name in ["😀 эмодзи 🎵.mp3", "עברית RTL ‏mixed‎.mp3", "comb̵i̵n̵i̵n̵g̵.mp3", "a#b%c&d;e'f.mp3",
             "trailing dot..mp3.", "trailing space.mp3 ", "CON_like.mp3", "  leading spaces.mp3"]:
    write("names/" + name, tone)
write("names/" + "длинное имя " * 18 + ".mp3", tone)
deep = "names/deep/" + "/".join(f"level{k:02d}" for k in range(40))
write(deep + "/deep.mp3", tone)
write("names/very-long/" + "/".join(["папка-с-длинным-именем-" + str(k) for k in range(12)]) + "/song.mp3", tone)
shutil.rmtree(ref)  # reference copies are not test cases
count = sum(len(files) for _, _, files in os.walk(long_path(OUT)))
loop = os.path.join(OUT, "names", "loop")
os.makedirs(loop, exist_ok=True)
subprocess.run(["cmd", "/c", "mklink", "/J", os.path.join(loop, "back-to-names"), os.path.join(OUT, "names")],
               check=True, stdout=subprocess.DEVNULL)

print(f"{count} files in {OUT}")
