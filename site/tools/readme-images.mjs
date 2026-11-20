// Images for README.md / README.ru.md and the release notes, in docs/images/:
//   <lang>/<name>.jpg      screenshots, 1600 px wide (en: English interface, ru: Russian)
//   startup-frames.png     the startup as a strip of film frames with the measured times
//   social-preview.png     1280x640 for Settings > Social preview (there is no API to upload it)
// English screenshots come from `capture.ps1 -Language en -OutDir %TEMP%\moonbow-site\shots-en`,
// Russian ones are the site's own (src/assets/shots).
//   node tools/readme-images.mjs
import sharp from 'sharp';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { chromium } from 'playwright';

const out = path.resolve('../docs/images');
const names = {
  hero: 'player', 'library-home': 'library', 'music-lyrics': 'music', series: 'series',
  settings: 'subtitle-styles', 'player-seek': 'seek-preview', 'player-stats': 'statistics',
};
const sources = {
  en: (n) => path.join(os.tmpdir(), 'moonbow-site', 'shots-en', `${n}.png`),
  ru: (n) => path.resolve('src/assets/shots', `${n}.jpg`),
};
for (const [lang, src] of Object.entries(sources)) {
  fs.mkdirSync(path.join(out, lang), { recursive: true });
  for (const [from, to] of Object.entries(names)) {
    const file = src(from);
    if (!fs.existsSync(file)) { console.warn(`missing ${file}`); continue; }
    const dst = path.join(out, lang, `${to}.jpg`);
    await sharp(file).resize({ width: 1600 }).jpeg({ quality: 82, chromaSubsampling: '4:4:4', mozjpeg: true }).toFile(dst);
    console.log(`${lang}/${to}.jpg ${(fs.statSync(dst).size / 1024).toFixed(0)} KB`);
  }
}

// The film strip and the social preview are drawn in HTML with the site's fonts.
const f = (p) => pathToFileURL(path.resolve(p)).href;
const frames = ['none', 'window', 'player', 'player', 'player', 'player', 'player', 'fade0', 'fade1', 'fade2', 'fade3', 'full'];
const frameFile = (s) => (s === 'none' ? '' : f(fs.existsSync(`src/assets/frames/${s}.png`) ? `src/assets/frames/${s}.png` : `src/assets/frames/${s}.jpg`));
const trace = [[28, 'wWinMain'], [39, 'config loaded'], [60, 'window shown'], [103, 'first frame'], [105, 'mpv ready'],
  [237, 'hardware device'], [251, 'device switch'], [295, 'file loaded'], [328, 'playback started']];
const FR = 1000 / 24;
const cells = frames.map((s, i) => {
  const ev = trace.filter(([ms]) => ms >= i * FR && ms < (i + 1) * FR).map(([ms, l]) => `<b>${ms} ms</b> ${l}`).join('<br>');
  return `<div class="cell${i === 7 ? ' go' : ''}"><div class="img">${s === 'none' ? '' : `<img src="${frameFile(s)}">`}</div>
    <div class="no">${String(i + 1).padStart(2, '0')}<span>${(i * FR / 1000).toFixed(3)} s</span></div><div class="ev">${ev}</div></div>`;
}).join('');
const fonts = `
@font-face { font-family: Onest; src: url(${f('public/fonts/onest-latin.woff2')}); font-weight: 100 900; }
@font-face { font-family: Mono; src: url(${f('public/fonts/geist-mono-latin.woff2')}); font-weight: 100 900; }`;
const stripHtml = `<!doctype html><meta charset="utf-8"><style>${fonts}
html, body { margin: 0; background: #0b0c10; color: #eef1f6; font-family: Onest; }
.wrap { width: 1600px; padding: 40px 40px 34px; box-sizing: border-box; }
h1 { margin: 0 0 6px; font-weight: 400; font-size: 34px; letter-spacing: -0.03em; }
p { margin: 0 0 26px; color: #8e939c; font: 15px Mono; }
.strip { display: grid; grid-template-columns: repeat(6, 1fr); gap: 18px 14px; padding: 18px 26px; background: #050506; border-radius: 6px; position: relative; }
.strip::before, .strip::after { content: ''; position: absolute; top: 0; bottom: 0; width: 26px; background: linear-gradient(#050506 18%, #1a1c22 18%, #1a1c22 82%, #050506 82%) 50% 0 / 10px 22px repeat-y; }
.strip::before { left: 0; } .strip::after { right: 0; }
.img { aspect-ratio: 2542 / 1631; background: #000; border-radius: 3px; overflow: hidden; box-shadow: 0 0 0 1px rgb(255 255 255 / .08); }
.img img { width: 100%; height: 100%; object-fit: cover; display: block; }
.no { display: flex; justify-content: space-between; align-items: baseline; margin-top: 8px; font: 300 26px Mono; letter-spacing: -0.04em; }
.no span { font-size: 12px; color: #8e939c; letter-spacing: 0; }
.ev { min-height: 34px; margin-top: 4px; font: 12px/1.45 Mono; color: #a7abb3; }
.ev b { color: #eef1f6; font-weight: 500; }
.go .no { color: #fff; } .go .img { box-shadow: 0 0 0 2px #eef1f6; }
</style><div class="wrap"><h1>Double-click to film in eight frames of cinema</h1>
<p>Moonbow startup, one cell per 1/24 s. Median of 20 launches with a 1080p file; times from the process start (MOONBOW_TRACE). Frames 8, 10 and 11 are blends of their neighbours along the player's 200 ms fade.</p>
<div class="strip">${cells}</div></div>`;
const socialHtml = `<!doctype html><meta charset="utf-8"><style>${fonts}
html, body { margin: 0; width: 1280px; height: 640px; background: #0b0c10; color: #eef1f6; font-family: Onest; overflow: hidden; }
.brand { position: absolute; left: 72px; top: 64px; display: flex; align-items: center; gap: 14px; font-weight: 500; font-size: 34px; letter-spacing: -0.04em; }
h1 { position: absolute; left: 68px; top: 200px; margin: 0; font-weight: 400; font-size: 60px; line-height: 1; letter-spacing: -0.045em; }
.meta { position: absolute; left: 72px; bottom: 64px; font: 20px Mono; color: #8e939c; }
.shot { position: absolute; left: 640px; top: 128px; width: 720px; border-radius: 12px; overflow: hidden; box-shadow: 0 0 0 1px rgba(255,255,255,.08), 0 40px 120px -30px #000; }
.shot img { display: block; width: 100%; }
</style>
<div class="brand"><svg width="50" height="25" viewBox="0 0 42 21" fill="none"><path d="M2 21A19 19 0 0 1 40 21" stroke="#d2c8f2" stroke-width="3.6"/><path d="M8 21A13 13 0 0 1 34 21" stroke="#c6dcf4" stroke-width="3.6"/><path d="M14 21A7 7 0 0 1 28 21" stroke="#f6cbbb" stroke-width="3.6"/></svg>moonbow</div>
<h1>Double-click.<br>Eight frames.<br>The film is on.</h1>
<div class="meta">Video and music player for Windows 10 and 11</div>
<div class="shot"><img src="${f(sources.en('hero'))}"></div>`;

const browser = await chromium.launch();
async function render(html, file, viewport, full) {
  const page = await browser.newPage({ viewport, deviceScaleFactor: 1 });
  const tmp = path.join(os.tmpdir(), `moonbow-readme-${path.basename(file)}.html`);
  fs.writeFileSync(tmp, html);
  await page.goto(pathToFileURL(tmp).href, { waitUntil: 'load' });
  await page.evaluate(() => document.fonts.ready);
  await page.screenshot({ path: file, fullPage: full });
  await page.close();
  fs.rmSync(tmp, { force: true });
  console.log(`${path.basename(file)} ${(fs.statSync(file).size / 1024).toFixed(0)} KB`);
}
await render(stripHtml, path.join(out, 'startup-frames.png'), { width: 1600, height: 600 }, true);
await render(socialHtml, path.join(out, 'social-preview.png'), { width: 1280, height: 640 }, false);
await browser.close();
