"""Lists settings that nothing reads: no mpv mapping and no code outside the schema.

Such a row changes settings.json and nothing else, so users conclude the setting is broken.
Usage: python tools/dev/check-settings.py
"""
import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parents[2]
schema_path = root / "src/core/SettingsSchema.cpp"
schema = schema_path.read_text(encoding="utf-8")

# Each item starts with b.<Kind>("key", ...) and its builder chain runs until the next "b." call.
starts = [(m.start(), m.group(1)) for m in re.finditer(r'\bb\.(?:Toggle|Pick|Seg|Number|Slider|Text|Path|Color|Accent|Button|Info|Item)\(\s*"([A-Za-z0-9_.]+)"', schema)]
items = []
for i, (pos, key) in enumerate(starts):
    end = starts[i + 1][0] if i + 1 < len(starts) else len(schema)
    chain = schema[pos:end]
    stop = re.search(r'\n\s*b\.(?!\w+\(\s*"' + re.escape(key) + ')', chain)
    if stop:
        chain = chain[:stop.start()]
    items.append((key, chain))

sources = []
for path in (root / "src").rglob("*.*"):
    if path.suffix in (".cpp", ".h", ".inc") and path != schema_path:
        sources.append(path.read_text(encoding="utf-8", errors="replace"))
code = "\n".join(sources)
# Keys read by the schema's own mpv builders (Build* functions) count as used.
builders = schema[: starts[0][0]] if starts else ""

unused = []
for key, chain in items:
    if re.search(r'\.(Mpv|MpvBool|MpvBuild|MpvChoice)\(', chain) or ".Hidden(" in chain:
        continue
    if re.search(r'Button\(|Info\(', chain[:40]):
        continue
    needle = '"' + key + '"'
    # main.cpp looks for '\"library.singleInstance\"' in the raw settings text.
    if needle in code or needle in builders or needle.replace('"', '\\"') in code:
        continue
    # prefix lookups like GetString("audio.eq" + ...) are rare; report them for a manual look
    unused.append(key)

for k in unused:
    print(k)
print(f"{len(unused)} of {len(items)} settings have no reader", file=sys.stderr)
