// Demo library for the site's screenshots: films, a series and albums made from placeholder photos
// (Unsplash via Lorem Picsum, see credits.json) and synthesized audio. Nothing here is real media.
//   node tools/shots/make-media.mjs [--photos <dir>] [--out <dir>]
// Defaults: photos in %TEMP%\moonbow-site\photos (photos.mjs get ...), media in %TEMP%\moonbow-site\media.
// Needs ffmpeg on PATH. Every ffmpeg runs with windowsHide: no console window flashes.
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';

const args = process.argv.slice(2);
const opt = (k, d) => { const i = args.indexOf(`--${k}`); return i > -1 ? args[i + 1] : d; };
const root = path.join(os.tmpdir(), 'moonbow-site');
const photos = opt('photos', path.join(root, 'photos'));
const out = opt('out', path.join(root, 'media'));
const tmp = path.join(root, 'work');
fs.mkdirSync(tmp, { recursive: true });

function ff(argv, what) {
  const r = spawnSync('ffmpeg', ['-hide_banner', '-loglevel', 'error', '-y', ...argv], { windowsHide: true, encoding: 'utf8' });
  if (r.status !== 0) throw new Error(`ffmpeg failed (${what}):\n${r.stderr}`);
}
const photo = (id) => path.join(photos, `${id}.jpg`);
const hms = (s) => [Math.floor(s / 3600), Math.floor(s / 60) % 60, s % 60].map((n) => String(n).padStart(2, '0')).join(':');
const exists = (f) => fs.existsSync(f) && fs.statSync(f).size > 0;

// ---- Films: one still each, 1 frame/s, no audio (library cards and the series page only need a frame).
const films = [
  { title: 'Последний трамвай', year: 2019, photo: 351, sec: 6482 },
  { title: 'Глубина резкости', year: 2021, photo: 223, sec: 5560 },
  { title: 'Короткие дни', year: 2017, photo: 559, sec: 5822 },
  { title: 'Высокая вода', year: 2022, photo: 1035, sec: 7311 },
  { title: 'Полярная ночь', year: 2020, photo: 1022, sec: 6105 },
  { title: 'Туман над рельсами', year: 2018, photo: 227, sec: 5398 },
  { title: 'Город не спит', year: 2023, photo: 579, sec: 6890 },
];
function still(img, sec, file) {
  if (exists(file)) return;
  ff(['-loop', '1', '-framerate', '1', '-i', img, '-t', String(sec), '-vf', 'scale=1920:1080,format=yuv420p', '-r', '1',
    '-c:v', 'libx264', '-preset', 'ultrafast', '-tune', 'stillimage', '-crf', '30', '-g', '600', '-movflags', '+faststart', file], file);
}
const filmDir = path.join(out, 'Видео', 'Фильмы');
fs.mkdirSync(filmDir, { recursive: true });
for (const f of films) still(photo(f.photo), f.sec, path.join(filmDir, `${f.title} (${f.year}).mp4`));

// ---- Series. "Дом у моря" S01E04 is the hero file: 24 fps, five scenes, chapters, two audio tracks,
// Russian and English subtitles. The other episodes are stills like the films.
const series = [
  { name: 'Дом у моря', season: 1, eps: [870, 1019, 896, 0, 385, 266], sec: [2771, 2650, 2904, 2770, 2588, 3012] },
  { name: 'Северный ветер', season: 2, eps: [901, 412, 961], sec: [2340, 2512, 2398] },
];
for (const s of series) {
  const dir = path.join(out, 'Видео', 'Сериалы', s.name);
  fs.mkdirSync(dir, { recursive: true });
  s.eps.forEach((id, i) => {
    if (!id) return;
    const ep = `S${String(s.season).padStart(2, '0')}E${String(i + 1).padStart(2, '0')}`;
    still(photo(id), s.sec[i], path.join(dir, `${s.name} ${ep}.mp4`));
  });
}

const hero = path.join(out, 'Видео', 'Сериалы', 'Дом у моря', 'Дом у моря S01E04.mp4');
if (!exists(hero)) {
  const scenes = [ // [photo, seconds]: the seek preview shows another scene than the playing one
    [870, 600], [1019, 540], [499, 560], [537, 520], [845, 550],
  ];
  const total = scenes.reduce((a, [, s]) => a + s, 0); // 2770 s = 46:10
  // Each scene is one 10-second piece encoded once and repeated by the concat demuxer (no re-encode):
  // a 46-minute 24 fps file in seconds instead of minutes.
  const parts = scenes.flatMap(([id, sec], i) => {
    const f = path.join(tmp, `scene${i}.mp4`);
    ff(['-loop', '1', '-framerate', '24', '-i', photo(id), '-t', '10', '-vf', 'scale=1920:1080,format=yuv420p',
      '-c:v', 'libx264', '-preset', 'veryfast', '-tune', 'stillimage', '-crf', '22', '-g', '240', f], `scene ${i}`);
    return Array(sec / 10).fill(f);
  });
  const list = path.join(tmp, 'scenes.txt');
  fs.writeFileSync(list, parts.map((p) => `file '${p.replace(/\\/g, '/')}'`).join('\n'));
  // Chapters (ffmetadata), in milliseconds.
  const names = ['Маяк', 'Шторм', 'Пирс', 'Звездопад', 'Прибой'];
  let t = 0;
  const meta = [';FFMETADATA1', 'title=Дом у моря · Маяк', 'show=Дом у моря', 'episode_id=S01E04', ''];
  scenes.forEach(([, sec], i) => { meta.push('[CHAPTER]', 'TIMEBASE=1/1000', `START=${t * 1000}`, `END=${(t + sec) * 1000}`, `title=${names[i]}`, ''); t += sec; });
  const metaFile = path.join(tmp, 'chapters.txt');
  fs.writeFileSync(metaFile, meta.join('\n'));
  // Subtitles: a line every ~6 s for the whole episode, so any captured moment has one.
  const ru = ['Свет на маяке зажигают в шесть.', 'Ты опять не спал?', 'Шторм обещали к ночи.', 'Смотри, вода уходит.',
    'Отец говорил, что лодки помнят дорогу.', 'Завтра будет тихо.', 'Я досмотрю и приду.', 'Слышишь? Ветер стих.'];
  const en = ['They light the lighthouse at six.', 'You didn\'t sleep again?', 'The storm is due tonight.', 'Look, the tide is going out.',
    'Father said boats remember the way.', 'Tomorrow will be quiet.', 'I\'ll finish this and come.', 'Hear that? The wind has dropped.'];
  const srt = (lines) => {
    const rows = [];
    const ts = (s) => `${hms(Math.floor(s))},${String(Math.round((s % 1) * 1000)).padStart(3, '0')}`;
    for (let i = 0, s = 2; s < total - 4; i++, s += 6) rows.push(`${i + 1}\n${ts(s)} --> ${ts(s + 4.2)}\n${lines[i % lines.length]}\n`);
    return rows.join('\n');
  };
  const subRu = path.join(tmp, 'ru.srt');
  const subEn = path.join(tmp, 'en.srt');
  fs.writeFileSync(subRu, srt(ru));
  fs.writeFileSync(subEn, srt(en));
  // Two quiet audio tracks: sea-like brown noise, different levels.
  ff(['-f', 'concat', '-safe', '0', '-i', list, '-i', metaFile,
    '-f', 'lavfi', '-t', String(total), '-i', 'anoisesrc=color=brown:amplitude=0.08:sample_rate=48000',
    '-f', 'lavfi', '-t', String(total), '-i', 'anoisesrc=color=pink:amplitude=0.03:sample_rate=48000',
    '-i', subRu, '-i', subEn,
    '-map', '0:v', '-map', '2:a', '-map', '3:a', '-map', '4:s', '-map', '5:s', '-map_metadata', '1', '-map_chapters', '1',
    '-c:v', 'copy', '-c:a', 'aac', '-b:a', '48k', '-ac', '2', '-c:s', 'mov_text',
    '-metadata:s:a:0', 'language=rus', '-metadata:s:a:0', 'title=Русский',
    '-metadata:s:a:1', 'language=eng', '-metadata:s:a:1', 'title=English',
    '-metadata:s:s:0', 'language=rus', '-metadata:s:s:0', 'title=Русские',
    '-metadata:s:s:1', 'language=eng', '-metadata:s:s:1', 'title=English',
    '-movflags', '+faststart', hero], 'hero');
}

// ---- Music: synthesized songs with dynamics (intro, verses, choruses, bridge, outro) so the
// waveform seek bar and the visualizers have something to draw. Covers are square crops.
const albums = [
  { artist: 'Lunar Drift', album: 'Glass Hours', year: 2024, genre: 'Ambient', photo: 903, root: 196,
    tracks: ['Glass Hours', 'Low Orbit', 'Night Ferry', 'Slow Signal', 'Moonbow', 'Tidal'] },
  { artist: 'Тишина в эфире', album: 'Ночной эфир', year: 2022, genre: 'Электроника', photo: 992, root: 220,
    tracks: ['Позывной', 'Помехи', 'Длинная волна', 'До рассвета'] },
  { artist: 'Мира Сол', album: 'Каньон', year: 2023, genre: 'Инди', photo: 564, root: 247,
    tracks: ['Тёплый камень', 'Эхо', 'Песок', 'Южный ветер', 'Каньон'] },
  { artist: 'Glass Coast', album: 'Low Tide', year: 2021, genre: 'Dream pop', photo: 1002, root: 174,
    tracks: ['Low Tide', 'Saltwater', 'Harbour Lights', 'Undertow'] },
  { artist: 'Северный свет', album: 'Аврора', year: 2020, genre: 'Пост-рок', photo: 974, root: 165,
    tracks: ['Аврора', 'Холодный фронт', 'Белые ночи'] },
  { artist: 'Полоса прибоя', album: 'Пурпур', year: 2025, genre: 'Синти-поп', photo: 888, root: 233,
    tracks: ['Пурпур', 'Самолёты', 'Выше облаков', 'Посадка'] },
  { artist: 'Тихий час', album: 'Сирень', year: 2019, genre: 'Лоу-фай', photo: 950, root: 185,
    tracks: ['Сирень', 'Мост', 'Вечер'] },
  { artist: 'Мост', album: 'Красный пролёт', year: 2024, genre: 'Рок', photo: 503, root: 147,
    tracks: ['Красный пролёт', 'Тросы', 'Ветер с залива'] },
];
function song(file, rootHz, sec, seed) {
  if (exists(file)) return;
  // Chord tones (major/minor triad on the root), a pulse at a per-song tempo, a section envelope.
  const minor = seed % 2 === 1;
  const f = [rootHz, rootHz * (minor ? 1.1892 : 1.2599), rootHz * 1.4983, rootHz * 2];
  const bpm = 84 + (seed * 13) % 48;
  const beat = 60 / bpm;
  const L = sec;
  // Sections: intro, verse, chorus, verse, chorus, bridge, chorus, outro.
  const env = `(0.18+0.22*gte(t,${(L * 0.08).toFixed(1)})+0.3*gte(t,${(L * 0.25).toFixed(1)})*lt(t,${(L * 0.38).toFixed(1)})` +
    `+0.3*gte(t,${(L * 0.52).toFixed(1)})*lt(t,${(L * 0.64).toFixed(1)})-0.25*gte(t,${(L * 0.64).toFixed(1)})*lt(t,${(L * 0.74).toFixed(1)})` +
    `+0.35*gte(t,${(L * 0.74).toFixed(1)})*lt(t,${(L * 0.9).toFixed(1)}))*min(1,(${L}-t)/6)*min(1,t/2)`;
  const pulse = `(0.55+0.45*exp(-mod(t,${beat.toFixed(4)})*7))`;
  const tones = f.map((hz, i) => `${(0.32 / (i + 1)).toFixed(3)}*sin(2*PI*${hz.toFixed(2)}*t+${i}*sin(2*PI*0.2*t))`).join('+');
  const hat = `0.07*gte(mod(t,${(beat / 2).toFixed(4)}),${(beat / 2 - 0.03).toFixed(4)})*(random(0)-0.5)`;
  const kick = `0.5*exp(-mod(t,${beat.toFixed(4)})*22)*sin(2*PI*55*t)`;
  const expr = `${env}*(${pulse}*(${tones})+${kick}+${hat})`;
  const wav = path.join(tmp, `song${seed}.wav`);
  ff(['-f', 'lavfi', '-i', `aevalsrc='${expr}':s=22050:d=${sec}`, '-af', 'volume=1.4,alimiter=limit=0.95', wav], `song ${seed}`);
  return wav;
}
let seed = 0;
for (const a of albums) {
  const dir = path.join(out, 'Музыка', a.artist, a.album);
  fs.mkdirSync(dir, { recursive: true });
  const cover = path.join(dir, 'cover.jpg');
  if (!exists(cover)) ff(['-i', photo(a.photo), '-vf', 'crop=ih:ih,scale=1000:1000', '-q:v', '3', cover], `cover ${a.album}`);
  a.tracks.forEach((title, i) => {
    seed++;
    const sec = 168 + ((seed * 37) % 110);
    const file = path.join(dir, `${String(i + 1).padStart(2, '0')} ${title}.mp3`);
    if (exists(file)) return;
    const wav = song(file, a.root * (1 + (i % 3) * 0.125), sec, seed);
    ff(['-i', wav, '-i', cover, '-map', '0:a', '-map', '1:v', '-c:a', 'libmp3lame', '-b:a', '128k', '-ac', '2', '-ar', '44100', '-c:v', 'mjpeg',
      '-disposition:v', 'attached_pic', '-id3v2_version', '3',
      '-metadata', `title=${title}`, '-metadata', `artist=${a.artist}`, '-metadata', `album_artist=${a.artist}`,
      '-metadata', `album=${a.album}`, '-metadata', `track=${i + 1}/${a.tracks.length}`, '-metadata', `date=${a.year}`,
      '-metadata', `genre=${a.genre}`, file], `mp3 ${title}`);
    fs.rmSync(wav, { force: true });
  });
}

// Synced lyrics for "Glass Hours": the music screen shows them next to the cover.
const lrc = path.join(out, 'Музыка', 'Lunar Drift', 'Glass Hours', '01 Glass Hours.lrc');
const lines = [
  [4, 'Свет в окне напротив гаснет ровно в час'], [11, 'Город тише, чем казалось в первый раз'],
  [18, 'Я держу в руке стеклянные часы'], [25, 'В них песок, и в нём не видно полосы'],
  [33, 'Белая дуга над чёрною водой'], [40, 'Если долго ждать, она станет цветной'],
  [47, 'Белая дуга, и больше ничего'], [54, 'Только ты и тихий шум у берегов'],
  [63, 'Ночь идёт по кругу, как пластинка'], [70, 'Ты молчишь, и это тоже половинка'],
  [77, 'Я считаю кадры, а не дни'], [84, 'Восемь кадров, и зажглись огни'],
  [93, 'Белая дуга над чёрною водой'], [100, 'Если долго ждать, она станет цветной'],
  [107, 'Белая дуга, и больше ничего'], [114, 'Только ты и тихий шум у берегов'],
  [124, 'Стеклянные часы'], [131, 'Стеклянные часы'], [140, ''],
];
const mmss = (s) => `${String(Math.floor(s / 60)).padStart(2, '0')}:${String(s % 60).padStart(2, '0')}.00`;
fs.writeFileSync(lrc, ['[ar:Lunar Drift]', '[al:Glass Hours]', '[ti:Glass Hours]', ...lines.map(([s, l]) => `[${mmss(s)}]${l}`)].join('\n'), 'utf8');

console.log(`media in ${out}`);
