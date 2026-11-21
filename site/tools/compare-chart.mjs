// The "next to VLC and MPC-HC" chart for README.md / README.ru.md, as SVG for GitHub's light and
// dark themes: docs/images/compare-{light,dark}.svg and docs/images/ru/compare-{light,dark}.svg.
// One cell is one frame of cinema (1/24 s), the same picture as on the site's Speed page.
// Numbers: `versus` in src/data/site.ts (medians from qa/perf/compare/results-2026-11-21).
//   node tools/compare-chart.mjs
import fs from 'node:fs';
import path from 'node:path';

const players = [
  { name: 'Moonbow', version: '0.2.0', window: 70, first: 420, me: true },
  { name: 'VLC', version: '3.0.24', window: 1094, first: 1583 },
  { name: 'MPC-HC', version: '2.8.5', window: 1518, first: 2398 },
];
const text = {
  en: { s: 's', frame: 'frame', dec: '.', none: 'no window yet', wait: 'window, no film', go: 'first frame of the film',
        title: 'From launch to the first frame of a 1080p film, one cell per 1/24 second' },
  ru: { s: 'с', frame: 'кадр', dec: ',', none: 'окна ещё нет', wait: 'окно есть, фильма нет', go: 'первый кадр фильма',
        title: 'От запуска до первого кадра фильма 1080p, одна ячейка на 1/24 секунды' },
};
const themes = {
  dark: { ink: '#e6edf3', soft: '#9198a1', line: '#3d444d', wait: '#656c76', after: '#21262d' },
  light: { ink: '#1f2328', soft: '#59636e', line: '#c8d1da', wait: '#9aa4af', after: '#e6eaef' },
};

const FRAME = 1000 / 24, CELLS = 60, X0 = 150, PITCH = 11, CW = 9, CH = 24, ROW = 52, TOP = 12;
const frameOf = (ms) => Math.floor(ms / FRAME) + 1;
const sans = "-apple-system,BlinkMacSystemFont,'Segoe UI',Helvetica,Arial,sans-serif";
const mono = "ui-monospace,SFMono-Regular,'Cascadia Mono',Consolas,monospace";

function chart(lang, theme) {
  const t = text[lang], c = themes[theme];
  const num = (ms) => (ms / 1000).toFixed(2).replace('.', t.dec);
  const out = [];
  players.forEach((p, i) => {
    const y = TOP + i * ROW, w = frameOf(p.window), f = frameOf(p.first);
    out.push(`<text x="0" y="${y + 17}" font-family="${sans}" font-size="15" font-weight="${p.me ? 600 : 400}" fill="${p.me ? c.ink : c.soft}">${p.name}<tspan font-family="${mono}" font-size="12" font-weight="400" fill="${c.soft}" dx="7">${p.version}</tspan></text>`);
    for (let n = 1; n <= CELLS; n++) {
      const x = X0 + (n - 1) * PITCH;
      if (n === f) out.push(`<rect x="${x}" y="${y}" width="${CW}" height="${CH}" fill="${c.ink}"/>`);
      else if (n >= w && n < f) out.push(`<rect x="${x}" y="${y}" width="${CW}" height="${CH}" fill="${c.wait}"/>`);
      else out.push(`<rect x="${x + 0.5}" y="${y + 0.5}" width="${CW - 1}" height="${CH - 1}" fill="none" stroke="${n < w ? c.line : c.after}"/>`);
    }
    const vx = X0 + CELLS * PITCH + 12;
    out.push(`<text x="${vx}" y="${y + 11}" font-family="${mono}" font-size="14" font-weight="600" fill="${p.me ? c.ink : c.soft}">${num(p.first)} ${t.s}</text>`);
    out.push(`<text x="${vx}" y="${y + 25}" font-family="${mono}" font-size="11" fill="${c.soft}">${t.frame} ${String(f).padStart(2, '0')}</text>`);
  });
  const ay = TOP + players.length * ROW - 12;
  for (let k = 0; k <= 5; k++) {
    const label = k === 0 ? '0' : `${String(k / 2).replace('.', t.dec)} ${t.s}`;
    out.push(`<text x="${X0 + k * 12 * PITCH}" y="${ay}" font-family="${mono}" font-size="11" fill="${c.soft}">${label}</text>`);
  }
  const ly = ay + 22;
  let lx = X0;
  for (const [kind, label] of [['none', t.none], ['wait', t.wait], ['go', t.go]]) {
    out.push(kind === 'none' ? `<rect x="${lx + 0.5}" y="${ly + 0.5}" width="8" height="13" fill="none" stroke="${c.line}"/>`
      : `<rect x="${lx}" y="${ly}" width="9" height="14" fill="${kind === 'go' ? c.ink : c.wait}"/>`);
    out.push(`<text x="${lx + 16}" y="${ly + 11}" font-family="${mono}" font-size="11" fill="${c.soft}">${label}</text>`);
    lx += 16 + label.length * 6.7 + 26;
  }
  const height = ly + 22;
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 900 ${height}" width="900" height="${height}" role="img" aria-label="${t.title}">\n<title>${t.title}</title>\n${out.join('\n')}\n</svg>\n`;
}

const root = path.resolve('../docs/images');
for (const lang of ['en', 'ru']) {
  const dir = lang === 'en' ? root : path.join(root, lang);
  fs.mkdirSync(dir, { recursive: true });
  for (const theme of ['light', 'dark']) {
    const file = path.join(dir, `compare-${theme}.svg`);
    fs.writeFileSync(file, chart(lang, theme));
    console.log(`${path.relative(root, file)} ${(fs.statSync(file).size / 1024).toFixed(1)} KB`);
  }
}
