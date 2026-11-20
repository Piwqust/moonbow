// Phone versions of the screenshots: the part of each window that carries the point, so its UI text
// stays readable at 390 px instead of a whole window shrunk to 4 px letters. Shot.astro shows these
// below 600 px. Regions are in the captured pixels (2542x1631 main window, 200 %).
//   node tools/shots/crops.mjs
import sharp from 'sharp';
import fs from 'node:fs';

const dir = 'src/assets/shots/';
const out = `${dir}crops/`;
fs.mkdirSync(out, { recursive: true });
const regions = {
  'player-seek': [760, 560, 1100, 1071],   // the frame preview above the seek bar
  'player-stats': [0, 90, 1150, 720],      // the statistics overlay
  meters: [30, 90, 920, 820],              // the left VU meter, whole
  subtitles: [1180, 150, 1362, 1481],      // the subtitles panel
  'music-lyrics': [700, 100, 1250, 1150],   // the lyrics, the current line lit
  'library-home': [0, 0, 1160, 1150],      // sidebar and the first "Продолжить" card
  series: [0, 0, 1350, 1150],              // seasons grid
  'home-light': [0, 0, 1350, 1150],
  palette: [560, 60, 1400, 1000],          // the command palette
  settings: [470, 470, 1626, 1260],        // the subtitle sample on a real frame
  album: [0, 0, 1400, 1000],
};
for (const [name, [left, top, width, height]] of Object.entries(regions)) {
  const src = `${dir}${name}.jpg`;
  if (!fs.existsSync(src)) { console.warn(`no ${src}`); continue; }
  const m = await sharp(src).metadata();
  const w = Math.min(width, m.width - left), h = Math.min(height, m.height - top);
  await sharp(src).extract({ left, top, width: w, height: h }).jpeg({ quality: 90, chromaSubsampling: '4:4:4', mozjpeg: true }).toFile(`${out}${name}.jpg`);
  console.log(`${name}: ${w}x${h}`);
}
