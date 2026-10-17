// Figma bridge job: aggregates colors, fonts, radii and effects used in the file.
// Run: node <bridge>/fig.js run tools/figma/tokens.js
const hex = (c) => '#' + [c.r, c.g, c.b].map((v) => Math.round(v * 255).toString(16).padStart(2, '0')).join('');
const colors = {}, fonts = {}, radii = {}, effects = {}, strokes = {};
const bump = (m, k) => { m[k] = (m[k] || 0) + 1; };
const nodes = figma.currentPage.findAll(() => true);
for (const n of nodes) {
  if ('fills' in n && Array.isArray(n.fills)) {
    for (const f of n.fills) if (f.type === 'SOLID' && f.visible !== false)
      bump(colors, hex(f.color) + (f.opacity != null && f.opacity < 1 ? '@' + f.opacity.toFixed(2) : '') + (n.type === 'TEXT' ? ' text' : ''));
  }
  if ('strokes' in n && Array.isArray(n.strokes)) {
    for (const s of n.strokes) if (s.type === 'SOLID' && s.visible !== false) bump(strokes, hex(s.color) + ' w' + (typeof n.strokeWeight === 'number' ? n.strokeWeight : '?'));
  }
  if (n.type === 'TEXT') {
    const fn = n.fontName === figma.mixed ? 'mixed' : n.fontName.family + ' ' + n.fontName.style;
    const fs = n.fontSize === figma.mixed ? 'mixed' : n.fontSize;
    bump(fonts, fn + ' ' + fs);
  }
  if ('cornerRadius' in n && typeof n.cornerRadius === 'number' && n.cornerRadius > 0) bump(radii, n.cornerRadius);
  if ('effects' in n && n.effects.length) for (const e of n.effects) if (e.visible !== false)
    bump(effects, e.type + (e.radius ? ' r' + e.radius : '') + (e.color ? ' ' + hex(e.color) + '@' + e.color.a.toFixed(2) : '') + (e.offset ? ' o' + e.offset.x + ',' + e.offset.y : ''));
}
const top = (m, k = 60) => Object.entries(m).sort((a, b) => b[1] - a[1]).slice(0, k);
return { nodes: nodes.length, colors: top(colors), strokes: top(strokes, 20), fonts: top(fonts, 40), radii: top(radii, 20), effects: top(effects, 20) };
