// The share picture (public/og.png, 1200x630): the poster line and the real hero window, set in the
// site's own fonts. Run once after the hero screenshot changes: node tools/og.mjs
import { chromium } from 'playwright';
import { pathToFileURL } from 'node:url';
import path from 'node:path';
import fs from 'node:fs';
import os from 'node:os';

const f = (p) => pathToFileURL(path.resolve(p)).href;
const html = `<!doctype html><html><head><meta charset="utf-8"><style>
@font-face { font-family: Onest; src: url(${f('public/fonts/onest-cyrillic.woff2')}); unicode-range: U+0400-045F; font-weight: 100 900; }
@font-face { font-family: Onest; src: url(${f('public/fonts/onest-latin.woff2')}); font-weight: 100 900; }
@font-face { font-family: Mono; src: url(${f('public/fonts/geist-mono-latin.woff2')}); }
@font-face { font-family: Mono; src: url(${f('public/fonts/geist-mono-cyrillic.woff2')}); unicode-range: U+0400-045F; }
html, body { margin: 0; width: 1200px; height: 630px; background: #0b0c10; color: #eef1f6; font-family: Onest; overflow: hidden; }
.brand { position: absolute; left: 64px; top: 56px; display: flex; align-items: center; gap: 12px; font-weight: 500; font-size: 30px; letter-spacing: -0.04em; }
h1 { position: absolute; left: 60px; top: 200px; margin: 0; font-weight: 400; font-size: 56px; line-height: 0.98; letter-spacing: -0.045em; }
.meta { position: absolute; left: 64px; bottom: 56px; font: 18px Mono; color: #8e939c; }
.shot { position: absolute; left: 580px; top: 120px; width: 720px; border-radius: 12px; overflow: hidden; box-shadow: 0 0 0 1px rgba(255,255,255,.08), 0 40px 120px -30px #000; }
.shot img { display: block; width: 100%; }
</style></head><body>
<div class="brand"><svg width="44" height="22" viewBox="0 0 42 21" fill="none"><path d="M2 21A19 19 0 0 1 40 21" stroke="#d2c8f2" stroke-width="3.6"/><path d="M8 21A13 13 0 0 1 34 21" stroke="#c6dcf4" stroke-width="3.6"/><path d="M14 21A7 7 0 0 1 28 21" stroke="#f6cbbb" stroke-width="3.6"/></svg>moonbow</div>
<h1>Двойной щелчок.<br>Восемь кадров.<br>Фильм идёт.</h1>
<div class="meta">Плеер для Windows 10 и 11</div>
<div class="shot"><img src="${f('src/assets/shots/hero.jpg')}"></div>
</body></html>`;

const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: 1200, height: 630 } });
// A file page, so the fonts and the screenshot (file:// URLs) are allowed to load.
const tmp = path.join(os.tmpdir(), 'moonbow-og.html');
fs.writeFileSync(tmp, html);
await page.goto(pathToFileURL(tmp).href, { waitUntil: 'load' });
await page.evaluate(() => document.fonts.ready);
await page.screenshot({ path: 'public/og.png' });
await browser.close();
fs.rmSync(tmp, { force: true });
console.log('public/og.png');
