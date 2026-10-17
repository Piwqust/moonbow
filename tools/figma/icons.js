// Figma bridge job: exports variants of the "Icon" component set as SVG strings.
// START/COUNT are replaced by tools/figma/export-icons.ps1. A variant whose export
// does not finish within 3 s is reported as skipped instead of stalling the job.
const set = await figma.getNodeByIdAsync('42:356');
const out = [];
const withTimeout = (p, ms) => Promise.race([p, new Promise((_, rej) => setTimeout(() => rej(new Error('timeout')), ms))]);
for (const c of set.children.slice(START, START + COUNT)) {
  const props = Object.fromEntries(c.name.split(',').map((kv) => kv.trim().split('=').map((s) => s.trim())));
  const parts = [props.name];
  for (const k of ['direction', 'state', 'value', 'EQ']) if (props[k] && props[k] !== 'default') parts.push(k === 'state' || k === 'direction' ? props[k] : k + '-' + props[k]);
  if (props['two-tone'] === 'yes') parts.push('twotone');
  const key = parts.join('_').toLowerCase().replace(/[^a-z0-9_-]+/g, '-').replace(/-+/g, '-');
  try {
    const svg = await withTimeout(c.exportAsync({ format: 'SVG_STRING' }), 3000);
    out.push({ key, id: c.id, svg });
  } catch (e) {
    out.push({ key, id: c.id, error: String(e.message || e) });
  }
}
return out;
