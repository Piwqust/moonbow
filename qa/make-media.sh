#!/usr/bin/env bash
# Synthetic media for reviewer agents (never touch the user's own files).
set -e
OUT="$1"
mkdir -p "$OUT/Videos/Series S01" "$OUT/Music/Artist A - Album One" "$OUT/Music/Другой исполнитель" "$OUT/Mixed"
cd "$OUT"
F="ffmpeg -hide_banner -loglevel error -y"

# Chapters metadata
cat > chapters.txt <<'EOF'
;FFMETADATA1
title=Chaptered Demo
[CHAPTER]
TIMEBASE=1/1000
START=0
END=20000
title=Opening
[CHAPTER]
TIMEBASE=1/1000
START=20000
END=60000
title=Middle part
[CHAPTER]
TIMEBASE=1/1000
START=60000
END=90000
title=Ending
EOF

cat > subs-en.srt <<'EOF'
1
00:00:01,000 --> 00:00:04,000
Hello, this is an English subtitle.

2
00:00:05,000 --> 00:00:09,000
Second line, with <i>italics</i>.
EOF
cat > subs-ru.srt <<'EOF'
1
00:00:01,000 --> 00:00:04,000
Привет, это русские субтитры.

2
00:00:05,000 --> 00:00:09,000
Вторая строка субтитров.
EOF

# 90 s 1080p with chapters, 2 audio tracks, 2 embedded subtitle tracks
$F -f lavfi -i "testsrc2=size=1920x1080:rate=30:duration=90" \
   -f lavfi -i "sine=frequency=440:duration=90" -f lavfi -i "sine=frequency=660:duration=90" \
   -i subs-en.srt -i subs-ru.srt -i chapters.txt \
   -map 0:v -map 1:a -map 2:a -map 3 -map 4 -map_metadata 5 -map_chapters 5 \
   -c:v libx264 -preset ultrafast -crf 30 -c:a aac -b:a 96k -c:s srt \
   -metadata:s:a:0 language=eng -metadata:s:a:0 title="English 440 Hz" \
   -metadata:s:a:1 language=rus -metadata:s:a:1 title="Русский 660 Hz" \
   -metadata:s:s:0 language=eng -metadata:s:s:1 language=rus \
   "Videos/Chaptered Demo (1080p).mkv"

# 10 min 720p for long seeking
$F -f lavfi -i "testsrc=size=1280x720:rate=25:duration=600" -f lavfi -i "sine=frequency=330:duration=600" \
   -c:v libx264 -preset ultrafast -crf 34 -c:a aac -b:a 64k "Videos/Long Seek Test 10min.mp4"

# vertical phone video
$F -f lavfi -i "smptebars=size=720x1280:rate=30:duration=15" -f lavfi -i "anoisesrc=d=15:a=0.05" \
   -c:v libx264 -preset ultrafast -crf 30 -c:a aac "Videos/Vertical phone clip.mp4"

# 4K short
$F -f lavfi -i "mandelbrot=size=3840x2160:rate=30" -t 8 -c:v libx264 -preset ultrafast -crf 32 "Videos/4K Mandelbrot.mp4"

# unicode names + external subtitle sidecar
$F -f lavfi -i "testsrc2=size=1280x720:rate=24:duration=30" -f lavfi -i "sine=frequency=520:duration=30" \
   -c:v libx264 -preset ultrafast -crf 32 -c:a aac "Videos/Фильм — тест «кавычки» 日本語.mp4"
cp subs-ru.srt "Videos/Фильм — тест «кавычки» 日本語.srt"

# series episodes (auto-next)
for i in 1 2 3; do
  $F -f lavfi -i "testsrc2=size=1280x720:rate=30:duration=12" -f lavfi -i "sine=frequency=$((300+i*100)):duration=12" \
     -vf "drawbox=x=0:y=0:w=200*$i:h=40:color=white@0.8:t=fill" \
     -c:v libx264 -preset ultrafast -crf 32 -c:a aac "Videos/Series S01/Series S01E0$i.mp4"
done

# audio-only no video, webm vp9 (no audio)
$F -f lavfi -i "testsrc=size=640x360:rate=30:duration=10" -c:v libvpx-vp9 -deadline realtime -b:v 300k "Mixed/silent vp9.webm"

# cover art
$F -f lavfi -i "color=c=0x334455:size=600x600,drawtext=text='ALBUM':fontcolor=white:fontsize=96:x=(w-tw)/2:y=(h-th)/2" -frames:v 1 cover.jpg 2>/dev/null || \
$F -f lavfi -i "color=c=0x334455:size=600x600" -frames:v 1 cover.jpg

# tagged album with cover
for i in 1 2 3 4; do
  $F -f lavfi -i "sine=frequency=$((200+i*110)):duration=$((40+i*10))" -i cover.jpg -map 0 -map 1 \
     -c:a libmp3lame -b:a 128k -c:v mjpeg -disposition:v attached_pic \
     -metadata title="Track Number $i" -metadata artist="Artist A" -metadata album="Album One" -metadata track="$i/4" \
     -metadata date=2024 -metadata genre="Electronic" \
     "Music/Artist A - Album One/0$i Track Number $i.mp3"
done
# flac, cyrillic tags, no cover
for i in 1 2; do
  $F -f lavfi -i "sine=frequency=$((250+i*90)):duration=35" -c:a flac \
     -metadata title="Песня $i" -metadata artist="Другой исполнитель" -metadata album="Русский альбом" -metadata track="$i" \
     "Music/Другой исполнитель/Песня $i.flac"
done
# untagged wav + opus
$F -f lavfi -i "sine=frequency=880:duration=20" "Mixed/untagged beep.wav"
$F -f lavfi -i "sine=frequency=600:duration=25" -c:a libopus "Mixed/voice memo.opus"
# a broken file
head -c 50000 "Videos/Long Seek Test 10min.mp4" > "Mixed/broken truncated.mp4"
echo "not a video" > "Mixed/fake.mkv"
# m3u playlist
printf '#EXTM3U\n../Videos/Series S01/Series S01E01.mp4\n../Music/Artist A - Album One/01 Track Number 1.mp3\n' > "Mixed/mixed list.m3u"
rm -f chapters.txt subs-en.srt subs-ru.srt cover.jpg
find . -type f | sort
