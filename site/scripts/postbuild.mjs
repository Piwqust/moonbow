// After `astro build`:
// 1. Measures each page's code (HTML + script + fonts, gzip) and writes the number into the page,
//    where the footer says how much it weighs ({{page-kb}}). The number is the real one for that file.
// 2. Copies dist/ to dist-local/ with relative links, so the site opens by double click
//    (dist-local/index.html), with no server. dist/ stays as it is for hosting.
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';

const dist = path.resolve('dist');
const local = path.resolve('dist-local');

const walk = (dir) => fs.readdirSync(dir, { withFileTypes: true })
  .flatMap((e) => (e.isDirectory() ? walk(path.join(dir, e.name)) : [path.join(dir, e.name)]));
const gz = (buf) => zlib.gzipSync(buf, { level: 9 }).length;

// ---- 1. Page weight
const pages = walk(dist).filter((f) => f.endsWith('.html'));
const report = [];
for (const file of pages) {
  let html = fs.readFileSync(file, 'utf8');
  const refs = new Set();
  for (const m of html.matchAll(/<script[^>]+src="(\/[^"]+)"/g)) refs.add(m[1]);
  for (const m of html.matchAll(/<link[^>]+rel="preload"[^>]+href="(\/[^"]+)"/g)) refs.add(m[1]);
  for (const m of html.matchAll(/url\((?:'|")?(\/fonts\/[^'")]+)/g)) refs.add(m[1]);
  let bytes = 0;
  for (const r of refs) {
    const f = path.join(dist, r);
    if (!fs.existsSync(f)) continue;
    const buf = fs.readFileSync(f);
    bytes += r.endsWith('.woff2') ? buf.length : gz(buf); // woff2 is compressed already
  }
  // The HTML's own gzip size includes the number it will show: measure with a stand-in of 3 digits.
  bytes += gz(Buffer.from(html.replaceAll('{{page-kb}}', '000')));
  const kb = Math.round(bytes / 1024);
  html = html.replaceAll('{{page-kb}}', String(kb));
  fs.writeFileSync(file, html);
  report.push(`${path.relative(dist, file).replace(/\\/g, '/').padEnd(26)} ${kb} KB (code, gzip)`);
}
console.log(report.join('\n'));

// ---- Sitemap and robots.txt from SITE_URL (works under a sub-path, unlike Astro's integration).
const config = fs.readFileSync('astro.config.mjs', 'utf8');
const siteUrl = (config.match(/SITE_URL = '([^']+)'/) || [])[1].replace(/\/$/, '');
const urls = pages
  .map((f) => path.relative(dist, f).replace(/\\/g, '/'))
  .filter((p) => p !== '404.html')
  .map((p) => `${siteUrl}/${p.replace(/index\.html$/, '')}`);
fs.writeFileSync(path.join(dist, 'sitemap.xml'), `<?xml version="1.0" encoding="UTF-8"?>\n<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">\n${urls.map((u) => `  <url><loc>${u}</loc></url>`).join('\n')}\n</urlset>\n`);
fs.writeFileSync(path.join(dist, 'robots.txt'), `User-agent: *\nAllow: /\nSitemap: ${siteUrl}/sitemap.xml\n`);

// ---- 2. dist-local: relative links for file://
fs.rmSync(local, { recursive: true, force: true });
fs.cpSync(dist, local, { recursive: true });

function target(rel, from) {
  // rel: a root-relative URL ("/features/", "/assets/site.js", "/#gate"). from: the page's folder in dist.
  const m = rel.match(/^([^?#]*)([?#].*)?$/);
  let p = m[1] || '/';
  const tail = m[2] || '';
  if (p.endsWith('/')) p += 'index.html';
  else if (!path.extname(p) && fs.existsSync(path.join(dist, p, 'index.html'))) p += '/index.html';
  let out = path.relative(from, path.join(dist, p)).replace(/\\/g, '/');
  if (!out.startsWith('.')) out = `./${out}`;
  return out + tail;
}
for (const file of walk(local).filter((f) => f.endsWith('.html') || f.endsWith('.css'))) {
  const from = path.dirname(path.join(dist, path.relative(local, file)));
  let text = fs.readFileSync(file, 'utf8');
  text = text
    // Font preloads use CORS, which file:// pages can't do; @font-face still loads the same files.
    .replace(/<link rel="preload"[^>]*as="font"[^>]*>/g, '')
    .replace(/(\s(?:href|src|poster|data-src|data-preview))="(\/(?!\/)[^"]*)"/g, (_, a, u) => `${a}="${target(u, from)}"`)
    .replace(/(\ssrcset)="([^"]+)"/g, (_, a, list) => `${a}="${list.split(',').map((part) => {
      const [u, d] = part.trim().split(/\s+/);
      return (u.startsWith('/') && !u.startsWith('//') ? target(u, from) : u) + (d ? ` ${d}` : '');
    }).join(', ')}"`)
    .replace(/url\((['"]?)(\/(?!\/)[^'")]+)\1\)/g, (_, q, u) => `url(${q}${target(u, from)}${q})`);
  fs.writeFileSync(file, text);
}
console.log(`dist-local: open ${path.join(local, 'index.html')}`);
