#!/usr/bin/env bash
# Test clips for the cross-player comparison (qa/perf/compare/bench.ps1).
# Every frame carries, in the middle of the picture:
#   - a square that cycles magenta, cyan, yellow, red, green, blue, one colour per second;
#   - under it a row of 12 black/white cells: the frame number in binary, lowest bit on the left.
# A measuring tool that only sees the window's pixels can so tell when the first frame is
# shown, which frame it is, and where a seek landed. Usage: bash make-media.sh <out dir>
set -e
OUT="${1:?out dir}"
mkdir -p "$OUT"
cd "$OUT"
F="ffmpeg -hide_banner -loglevel error -y"

IDX='mod(floor(T)\,6)'
SQ_R="255*(eq($IDX\,0)+eq($IDX\,2)+eq($IDX\,3))"
SQ_G="255*(eq($IDX\,1)+eq($IDX\,2)+eq($IDX\,4))"
SQ_B="255*(eq($IDX\,0)+eq($IDX\,1)+eq($IDX\,5))"

# clip <name> <width> <height> <rate> <seconds> <k = height/1080> <video encoder options...>
clip() {
  local name="$1" w="$2" h="$3" rate="$4" dur="$5" k="$6"; shift 6
  local sq=$((440*k)) pad=$((20*k)) cell=$((60*k))
  $F -f lavfi -i "testsrc2=size=${w}x${h}:rate=${rate},noise=alls=14:allf=t+u" \
     -f lavfi -i "nullsrc=size=1x1:rate=${rate},format=rgb24,geq=r='${SQ_R}':g='${SQ_G}':b='${SQ_B}',scale=${sq}:${sq}:flags=neighbor,pad=$((sq+2*pad)):$((sq+2*pad)):${pad}:${pad}:black" \
     -f lavfi -i "nullsrc=size=12x1:rate=${rate},format=gray,geq=lum='255*mod(floor(N/pow(2\,X))\,2)',scale=$((12*cell)):${cell}:flags=neighbor,pad=$((12*cell+2*pad)):$((cell+2*pad)):${pad}:${pad}:black" \
     -f lavfi -i "sine=frequency=440:sample_rate=48000,volume=-70dB" \
     -filter_complex "[0][1]overlay=(W-w)/2:(H-h)/2[a];[a][2]overlay=(W-w)/2:$((800*k))[v]" \
     -map "[v]" -map 3:a -t "$dur" "$@" \
     -color_primaries bt709 -color_trc bt709 -colorspace bt709 -color_range tv \
     -c:a aac -b:a 128k "$name"
}

# 1080p film-like: H.264 High, 23.976 fps, about 8 Mbit/s, 2 minutes
[ -f bench-1080p-h264.mkv ] || clip bench-1080p-h264.mkv 1920 1080 24000/1001 120 1 \
  -pix_fmt yuv420p -c:v libx264 -preset medium -b:v 8M -maxrate 12M -bufsize 16M

# 4K stress: HEVC Main10, 60 fps, about 45 Mbit/s, 1 minute
[ -f bench-2160p60-hevc10.mkv ] || clip bench-2160p60-hevc10.mkv 3840 2160 60 60 2 \
  -pix_fmt p010le -c:v hevc_nvenc -profile:v main10 -preset p5 -b:v 45M -maxrate 60M -bufsize 90M

# Colour accuracy: 3 s of magenta (to find the square), then ten flat patches, 2 s each
if [ ! -f colors-1080p.mkv ]; then
  PATCHES="FF00FF:3 000000:2 101010:2 808080:2 EBEBEB:2 FFFFFF:2 BF4040:2 40BF40:2 4040BF:2 BFBF40:2 40BFBF:2"
  in=""; chain=""; n=0
  for p in $PATCHES; do
    in="$in -f lavfi -i color=c=0x${p%%:*}:s=440x440:r=24:d=${p##*:}"
    chain="$chain[$((n+1)):v]"; n=$((n+1))
  done
  $F -f lavfi -i "color=c=0x404040:s=1920x1080:r=24:d=23" $in \
     -filter_complex "${chain}concat=n=${n}:v=1:a=0,pad=480:480:20:20:black[sq];[0][sq]overlay=(W-w)/2:(H-h)/2[v]" \
     -map "[v]" -pix_fmt yuv420p -c:v libx264 -preset slow -crf 10 \
     -color_primaries bt709 -color_trc bt709 -colorspace bt709 -color_range tv colors-1080p.mkv
  # What a correct BT.709 limited-range decode shows in the middle of the square, one line per patch
  : > colors-reference.txt
  t=1
  for p in $PATCHES; do
    rgb=$(ffmpeg -hide_banner -loglevel error -ss "$t" -i colors-1080p.mkv -frames:v 1 \
      -vf "crop=64:64:928:508,scale=1:1:flags=area:in_color_matrix=bt709:in_range=tv:out_range=pc,format=rgb24" \
      -f rawvideo - | od -An -tu1 | tr -s ' ' | sed 's/^ //')
    echo "${p%%:*} $rgb" >> colors-reference.txt
    t=$((t + ${p##*:}))
  done
fi
ls -la
ffprobe -hide_banner -loglevel error -show_entries format=duration,bit_rate:stream=codec_name,profile,width,height,r_frame_rate,pix_fmt -of compact bench-1080p-h264.mkv bench-2160p60-hevc10.mkv 2>&1 | head -20
cat colors-reference.txt
