import collections
import pathlib
import re

root = pathlib.Path(__file__).resolve().parent.parent / "src"
order = [l.split('"')[1] for l in (root / "app/I18n.cpp").read_text(encoding="utf-8").splitlines()
         if '#include "app/strings/' in l]
pat = re.compile(r'\{L"((?:[^"\\]|\\.)*)",\s*L"((?:[^"\\]|\\.)*)"\}')
d = collections.defaultdict(list)
for f in order:
    for m in pat.finditer((root / f).read_text(encoding="utf-8")):
        d[m.group(1)].append((f.split("/")[-1], m.group(2)))
n = 0
for k, v in d.items():
    if len(set(x[1] for x in v)) > 1:
        n += 1
        print(repr(k), v)
print("conflicts:", n, "keys:", len(d))
