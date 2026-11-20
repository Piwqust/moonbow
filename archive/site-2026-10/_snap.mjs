import { chromium } from 'playwright';
const [url, out, w, h, y] = process.argv.slice(2);
const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: +w, height: +h } });
await p.goto(url, { waitUntil: 'networkidle' }); await p.waitForTimeout(1500);
if (+y) { await p.mouse.wheel(0, +y); await p.waitForTimeout(1800); }
await p.screenshot({ path: out }); await b.close();
