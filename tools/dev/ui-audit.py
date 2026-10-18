#!/usr/bin/env python3
"""Deterministic layout audit of a Lumen widget-tree dump (METHOD.md, layer 1).

    python tools/dev/ui-audit.py <tree.txt> [--hitmap hitmap.txt] [--allow qa/round5/audit-allow.json]
                                 [--json out.json] [--state normal|maximized|fullscreen] [--context NAME]

The tree comes from tools/dev/dump-tree.ps1. Both formats are read:
- v1: `<indent><Type>[ #id] [x,y wxh][ hidden][ disabled]`, one window, popups after `--- popup`;
- v2: a `=== window title=... client=WxH scale=S theme=... fullscreen=0|1 maximized=0|1 hwnd=N main=0|1 dpi=D`
  line per window, and the same widget lines plus ` interactive`, ` focus`, ` hot`, ` pressed`, state tokens
  (` scroll offset=Y/MAX`, ` on`, ` selected=N`, ...), one ` text="..."` per painted string followed by that
  string's ` trunc` / ` clip` / ` overflow` / ` faded` / ` box=[x,y wxh]`, and ` tip="..."`.
A v1 dump has no interactive/text/scroll flags, so they are inferred from the widget type (lists in the
allowlist file). Rects are window DIPs. Hidden propagates to the whole subtree.

Checks: overlap, overlap-cross, clip-parent, clip-window, offscreen, text-trunc, text-clip, text-overflow, target-size,
zero-size, hitmap, hitmap-odd. Prints a markdown report; --json writes every finding, allowed ones
included with the allowlist reason. Exit code 0 (findings are data, not failures).
"""
from __future__ import annotations

import argparse
import json
import math
import os
import re
import sys
from dataclasses import dataclass, field

# --- tree model -----------------------------------------------------------------------------------


@dataclass
class Node:
    raw_type: str
    type: str
    id: str
    x: float
    y: float
    w: float
    h: float
    flags: set
    text: str | None  # the first visible painted string
    depth: int
    line: int
    layer: int  # 0 = window root, 1.. = popups in dump order
    parent: "Node | None" = None
    children: list = field(default_factory=list)
    visible: bool = True  # effective (hidden propagates)
    interactive: bool = False
    has_text: bool = False
    scroll: bool = False
    vis_rect: tuple | None = None  # rect clipped by scrolling ancestors, None when scrolled out
    texts: list = field(default_factory=list)  # v2: [{"text", "flags": set, "box": rect|None}] in paint order
    tip: str | None = None

    @property
    def rect(self):
        return (self.x, self.y, self.w, self.h)

    def label(self):
        s = self.type + (f"#{self.id}" if self.id else "")
        if self.text:
            t = self.text.replace("\n", " ")
            s += f' "{t[:40]}{"…" if len(t) > 40 else ""}"'
        return s

    def path(self):
        parts = []
        n = self
        while n is not None:
            seg = n.type + (f"#{n.id}" if n.id else "")
            if n.parent is not None:
                same = [c for c in n.parent.children if c.type == n.type]
                if len(same) > 1 and not n.id:
                    seg += f"[{same.index(n)}]"
            parts.append(seg)
            n = n.parent
        return "/".join(reversed(parts))


@dataclass
class Window:
    title: str = ""
    client: tuple | None = None  # (w, h) DIPs
    scale: float | None = None
    theme: str = ""
    fullscreen: bool | None = None
    maximized: bool | None = None
    header: bool = False
    layers: list = field(default_factory=list)  # list of root Nodes (0 = window, then popups)
    nodes: list = field(default_factory=list)


LINE_RE = re.compile(r"^(?P<indent> *)(?P<body>.*?) \[(?P<x>-?[\d.]+),(?P<y>-?[\d.]+) (?P<w>-?[\d.]+)x(?P<h>-?[\d.]+)\](?P<rest>.*)$")
FLAG_RE = re.compile(r'\s+(?:(?P<k>\w+)="(?P<v>(?:[^"\\]|\\.)*)"|(?P<f>[\w-]+))')
HEADER_RE = re.compile(r'(\w+)=("(?:[^"\\]|\\.)*"|\S+)')


def unescape(s: str) -> str:
    out, i = [], 0
    while i < len(s):
        c = s[i]
        if c == "\\" and i + 1 < len(s):
            n = s[i + 1]
            out.append({"n": "\n", "r": "\r", "t": "\t", '"': '"', "\\": "\\"}.get(n, n))
            i += 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


def short_type(raw: str) -> str:
    t = raw.strip()
    if t.startswith("class "):
        t = t[6:]
    t = t.replace("`anonymous namespace'::", "")
    if "'::" in t:  # local classes: "`public: __cdecl X::X(...)'::`2'::QueueButton"
        t = t.rsplit("'::", 1)[1]
    return t.rsplit("::", 1)[-1]


def parse_rest(rest: str):
    """Widget flags, painted strings (each with its own trunc/clip/overflow/faded/box) and the tooltip."""
    flags, texts, tip = set(), [], None
    i, n = 0, len(rest)
    while i < n:
        if rest[i] == " ":
            i += 1
            continue
        m = re.match(r'(\w+)="', rest[i:])
        if m:  # key="escaped value"
            j = i + m.end()
            while j < n and rest[j] != '"':
                j += 2 if rest[j] == "\\" else 1
            val = unescape(rest[i + m.end():j])
            i = j + 1
            if m.group(1) == "text":
                texts.append({"text": val, "flags": set(), "box": None})
            elif m.group(1) == "tip":
                tip = val
            else:
                flags.add(m.group(1) + "=" + val)
            continue
        m = re.match(r"box=\[(-?[\d.]+),(-?[\d.]+) (-?[\d.]+)x(-?[\d.]+)\]", rest[i:])
        if m:
            if texts:
                texts[-1]["box"] = tuple(float(v) for v in m.groups())
            i += m.end()
            continue
        j = rest.find(" ", i)
        j = n if j < 0 else j
        tok = rest[i:j]
        i = j
        if texts and tok in ("trunc", "clip", "overflow", "faded"):
            texts[-1]["flags"].add(tok)
        elif tok in ("trunc", "clip"):  # early v2 drafts: one text, flags after it or alone
            flags.add(tok)
        else:
            flags.add(tok)
    return flags, texts, tip


def parse_tree(text: str) -> list[Window]:
    windows: list[Window] = []
    win: Window | None = None
    stack: list[Node] = []
    layer = 0
    for lineno, line in enumerate(text.splitlines(), 1):
        line = line.rstrip("\r")
        if not line.strip():
            continue
        if line.startswith("=== window"):
            win = Window(header=True)
            for k, v in HEADER_RE.findall(line[10:]):
                if v.startswith('"'):
                    v = unescape(v[1:-1])
                if k == "title":
                    win.title = v
                elif k == "client":
                    m = re.match(r"(\d+(?:\.\d+)?)x(\d+(?:\.\d+)?)", v)
                    if m:
                        win.client = (float(m.group(1)), float(m.group(2)))
                elif k == "scale":
                    try:
                        win.scale = float(v)
                    except ValueError:
                        pass
                elif k == "theme":
                    win.theme = v
                elif k == "fullscreen":
                    win.fullscreen = v == "1"
                elif k == "maximized":
                    win.maximized = v == "1"
            windows.append(win)
            stack, layer = [], 0
            continue
        if line.startswith("--- popup"):
            if win is None:
                win = Window()
                windows.append(win)
            layer = len(win.layers)
            stack = []
            continue
        m = LINE_RE.match(line)
        if not m:
            continue
        if win is None:
            win = Window()
            windows.append(win)
        body = m.group("body")
        nid = ""
        im = re.search(r" #(\S+)$", body)
        if im:
            nid = im.group(1)
            body = body[: im.start()]
        flags, texts, tip = parse_rest(m.group("rest"))
        visible_texts = [t["text"] for t in texts if "faded" not in t["flags"] and t["text"].strip()]
        depth = len(m.group("indent")) // 2
        node = Node(body, short_type(body), nid, float(m.group("x")), float(m.group("y")), float(m.group("w")),
                    float(m.group("h")), flags, visible_texts[0] if visible_texts else None, depth, lineno, layer)
        node.texts, node.tip = texts, tip
        while stack and stack[-1].depth >= depth:
            stack.pop()
        if stack:
            node.parent = stack[-1]
            stack[-1].children.append(node)
        else:
            if layer >= len(win.layers):
                win.layers.append(node)
            else:  # a second root in the same layer: keep it as its own layer
                win.layers.append(node)
                layer = len(win.layers) - 1
                node.layer = layer
        stack.append(node)
        win.nodes.append(node)
    return windows


# --- inference & preparation ----------------------------------------------------------------------

def finalize(win: Window, allow: dict):
    v2 = win.header or any(("interactive" in n.flags or n.text is not None or "scroll" in n.flags) for n in win.nodes)
    inter = set(allow.get("inferInteractive", []))
    texty = set(allow.get("inferText", []))
    scrolly = set(allow.get("inferScroll", ["ScrollView"]))
    for n in win.nodes:
        n.visible = "hidden" not in n.flags and (n.parent.visible if n.parent else True)
        if v2:
            n.interactive = "interactive" in n.flags
            n.has_text = bool(n.text)
            n.scroll = "scroll" in n.flags or n.type in scrolly
        else:
            n.interactive = n.type in inter or (n.type.endswith("Button") and n.type not in allow.get("notInteractive", []))
            n.has_text = n.type in texty
            n.scroll = n.type in scrolly
    if win.client is None and win.layers:
        r = win.layers[0]
        win.client = (r.x + r.w, r.y + r.h)
    for n in win.nodes:
        n.vis_rect = clip_by_scrollers(n)
    return v2


def inter_rect(a, b):
    x1, y1 = max(a[0], b[0]), max(a[1], b[1])
    x2, y2 = min(a[0] + a[2], b[0] + b[2]), min(a[1] + a[3], b[1] + b[3])
    if x2 <= x1 or y2 <= y1:
        return None
    return (x1, y1, x2 - x1, y2 - y1)


def clip_by_scrollers(n: Node):
    r = n.rect
    p = n.parent
    while p is not None:
        if p.scroll:
            r = inter_rect(r, p.rect)
            if r is None:
                return None
        p = p.parent
    return r


def overlay_root(n: Node, overlay: set):
    """The nearest ancestor (or self) whose type is an overlay, None for in-flow widgets."""
    p = n
    while p is not None:
        if p.type in overlay:
            return p
        p = p.parent
    return None


def is_leaf(n: Node):
    return not any(c.visible for c in n.children)


def has_area(n: Node):
    return n.w > 0 and n.h > 0


# --- allowlist ------------------------------------------------------------------------------------

def rx(pattern, value):
    if pattern is None:
        return True
    return re.fullmatch(pattern, value or "") is not None


def ancestors(n: Node):
    p = n.parent
    while p is not None:
        yield p
        p = p.parent


def match_rule(rule: dict, f: dict, node: Node, other: Node | None, win: Window, context: str):
    if rule.get("check", "*") not in ("*", f["check"]) and not rx(rule.get("check"), f["check"]):
        return False
    if not rx(rule.get("context"), context):
        return False
    if not rx(rule.get("window"), win.title):
        return False

    def node_ok(prefix, nd: Node | None):
        if nd is None:
            return all(rule.get(prefix + k) is None for k in ("type", "id", "text", "parent", "ancestor", "path"))
        if not rx(rule.get(prefix + "type"), nd.type):
            return False
        if not rx(rule.get(prefix + "id"), nd.id):
            return False
        if rule.get(prefix + "text") is not None and not rx(rule.get(prefix + "text"), nd.text or ""):
            return False
        if not rx(rule.get(prefix + "parent"), nd.parent.type if nd.parent else ""):
            return False
        if rule.get(prefix + "ancestor") is not None and not any(rx(rule[prefix + "ancestor"], a.type) for a in ancestors(nd)):
            return False
        if rule.get(prefix + "path") is not None and re.search(rule[prefix + "path"], nd.path()) is None:
            return False
        return True

    if other is None:
        return node_ok("", node) and all(rule.get("other" + k) is None for k in ("type", "id", "text", "parent", "ancestor", "path"))
    # Pair checks: the rule may name either side.
    a = node_ok("", node) and (node_ok("other", other) if any(k.startswith("other") for k in rule) else True)
    b = node_ok("", other) and (node_ok("other", node) if any(k.startswith("other") for k in rule) else True)
    return a or b


# --- checks ---------------------------------------------------------------------------------------

TOL = 1.0  # DIP tolerance everywhere ("> 1 DIP")


def fmt_rect(r):
    if r is None:
        return "-"
    return f"{r[0]:g},{r[1]:g} {r[2]:g}x{r[3]:g}"


def check_tree(win: Window, allow: dict, emit):
    overlay = set(allow.get("overlayTypes", []))
    min_target = float(allow.get("minTarget", 24))
    cw, ch = win.client or (0, 0)
    visible = [n for n in win.nodes if n.visible]

    def carrier(n):
        return (n.has_text or n.interactive) and n.type not in set(allow.get("notCarrier", []))

    # overlap: visible sibling leaves with text or input.
    for parent in [n for n in visible]:
        kids = [c for c in parent.children if c.visible and has_area(c) and is_leaf(c) and carrier(c) and c.vis_rect]
        for i in range(len(kids)):
            for j in range(i + 1, len(kids)):
                a, b = kids[i], kids[j]
                r = inter_rect(a.vis_rect, b.vis_rect)
                if r and r[2] > TOL and r[3] > TOL:
                    if a.type in overlay or b.type in overlay:
                        continue
                    emit("overlap", "high", a, b, f"{a.label()} ∩ {b.label()} = {fmt_rect(r)}")

    # overlap-cross: leaves in different branches of the same layer (cousins), scrolled-out parts removed.
    by_layer: dict[int, list[Node]] = {}
    for n in visible:
        if has_area(n) and is_leaf(n) and carrier(n) and n.vis_rect:
            by_layer.setdefault(n.layer, []).append(n)
    for layer, leaves in by_layer.items():
        leaves.sort(key=lambda n: n.vis_rect[0])
        for i, a in enumerate(leaves):
            ax2 = a.vis_rect[0] + a.vis_rect[2]
            for b in leaves[i + 1:]:
                if b.vis_rect[0] >= ax2:
                    break
                if a.parent is b.parent:
                    continue  # reported by "overlap"
                r = inter_rect(a.vis_rect, b.vis_rect)
                if not r or r[2] <= TOL or r[3] <= TOL:
                    continue
                if a.type in overlay or b.type in overlay:
                    continue
                if overlay_root(a, overlay) is not overlay_root(b, overlay):
                    continue  # one of them sits in an overlay (mini player, chip, toast...) over the other
                emit("overlap-cross", "medium", a, b, f"{a.label()} ∩ {b.label()} = {fmt_rect(r)}")

    # clip-parent / clip-window / offscreen: report the topmost offender only.
    reported: set[int] = set()

    def under_reported(n):
        return any(id(a) in reported for a in ancestors(n))

    for n in visible:
        if not has_area(n) or n.parent is None or under_reported(n):
            continue
        p = n.parent
        if p.scroll or not has_area(p):
            continue
        if n.vis_rect is None:
            continue  # scrolled out of view
        px, py, pw, ph = p.rect
        over = []
        if n.x < px - TOL:
            over.append(f"left {px - n.x:g}")
        if n.y < py - TOL:
            over.append(f"top {py - n.y:g}")
        if n.x + n.w > px + pw + TOL:
            over.append(f"right {n.x + n.w - px - pw:g}")
        if n.y + n.h > py + ph + TOL:
            over.append(f"bottom {n.y + n.h - py - ph:g}")
        if over:
            reported.add(id(n))
            emit("clip-parent", "medium", n, p, f"{n.label()} {fmt_rect(n.rect)} outside {p.label()} {fmt_rect(p.rect)} by {', '.join(over)}")

    reported_w: set[int] = set()
    for n in visible:
        if not has_area(n) or n.vis_rect is None or cw <= 0:
            continue
        if any(id(a) in reported_w for a in ancestors(n)):
            continue
        r = n.vis_rect
        inside = inter_rect(r, (0, 0, cw, ch))
        if inside is None:
            if n.interactive or n.has_text:
                reported_w.add(id(n))
                emit("offscreen", "medium", n, None, f"{n.label()} {fmt_rect(r)} is visible but outside the client {cw:g}x{ch:g}")
            continue
        over = []
        if r[0] < -TOL:
            over.append(f"left {-r[0]:g}")
        if r[1] < -TOL:
            over.append(f"top {-r[1]:g}")
        if r[0] + r[2] > cw + TOL:
            over.append(f"right {r[0] + r[2] - cw:g}")
        if r[1] + r[3] > ch + TOL:
            over.append(f"bottom {r[1] + r[3] - ch:g}")
        if over:
            reported_w.add(id(n))
            emit("clip-window", "high" if (n.interactive or n.has_text) else "medium", n, None,
                 f"{n.label()} {fmt_rect(r)} outside the client {cw:g}x{ch:g} by {', '.join(over)}")

    # Text flags (v2): one finding per widget and flag, faded (auto-hidden) strings skipped.
    def quote(ts):
        shown = ", ".join('"' + t.replace("\n", " ")[:50] + '"' for t in ts[:3])
        return shown + (f" and {len(ts) - 3} more" if len(ts) > 3 else "")

    for n in visible:
        if not has_area(n) or n.vis_rect is None:
            continue
        live = [t for t in n.texts if "faded" not in t["flags"] and t["text"].strip()]
        clip = [t["text"] for t in live if "clip" in t["flags"]] or ([n.text or ""] if "clip" in n.flags else [])
        trunc = [t["text"] for t in live if "trunc" in t["flags"] and "clip" not in t["flags"]] or (
            [n.text or ""] if "trunc" in n.flags and "clip" not in n.flags else [])
        over = [t["text"] for t in live if "overflow" in t["flags"]]
        where = f"{n.type}{'#' + n.id if n.id else ''} {fmt_rect(n.rect)}"
        if clip:
            emit("text-clip", "high", n, None, f"{where}: {len(clip)} string(s) taller than their box: {quote(clip)}")
        if over:
            emit("text-overflow", "high", n, None, f"{where}: {len(over)} string(s) drawn past the widget: {quote(over)}")
        if trunc:
            if n.tip:
                emit("text-trunc", "low", n, None, f"{where}: {len(trunc)} string(s) ellipsized, tooltip \"{n.tip[:60]}\": {quote(trunc)}")
            else:
                emit("text-trunc", "medium", n, None, f"{where}: {len(trunc)} string(s) ellipsized, no tooltip: {quote(trunc)}")

    # Target size, with the WCAG 2.5.8 spacing exception.
    targets = [n for n in visible if n.interactive and n.vis_rect and has_area(n) and n.type not in set(allow.get("notTarget", []))]
    for n in targets:
        if n.w >= min_target - 0.01 and n.h >= min_target - 0.01:
            continue
        cx, cy = n.x + n.w / 2, n.y + n.h / 2
        crowded = None
        root = overlay_root(n, overlay)
        for o in targets:
            if o is n or o.layer != n.layer or o in ancestors(n) or n in ancestors(o):
                continue
            if overlay_root(o, overlay) is not root:
                continue  # one floats over the other (mini player over the grid): not neighbours
            if o.w < min_target or o.h < min_target:
                d = math.hypot(o.x + o.w / 2 - cx, o.y + o.h / 2 - cy)
                if d < min_target:
                    crowded = o
                    break
            else:
                dx = max(o.x - cx, 0, cx - (o.x + o.w))
                dy = max(o.y - cy, 0, cy - (o.y + o.h))
                if math.hypot(dx, dy) < min_target / 2:
                    crowded = o
                    break
        if crowded is None:
            continue  # spaced out enough
        emit("target-size", "medium", n, crowded, f"{n.label()} is {n.w:g}x{n.h:g} DIP (< {min_target:g}) and within {min_target:g} DIP of {crowded.label()}")

    for n in visible:
        if n.interactive and (n.w <= 0 or n.h <= 0) and (n.parent is None or has_area(n.parent)):
            emit("zero-size", "medium", n, None, f"{n.label()} is visible and interactive but {n.w:g}x{n.h:g}")


# --- hit map --------------------------------------------------------------------------------------

HIT_RE = re.compile(r"^(?P<code>HT\w+|HT-?\d+)\s+px \[(?P<x1>-?\d+),(?P<y1>-?\d+) \.\. (?P<x2>-?\d+),(?P<y2>-?\d+)\] x(?P<n>\d+)\s+over: ?(?P<over>.*)$")
RESIZE = {"HTLEFT", "HTRIGHT", "HTTOP", "HTBOTTOM", "HTTOPLEFT", "HTTOPRIGHT", "HTBOTTOMLEFT", "HTBOTTOMRIGHT"}


def parse_hitmap(text: str):
    regions, client, scale = [], None, None
    for line in text.splitlines():
        line = line.strip().lstrip("\ufeff")
        m = re.match(r"client (\d+)x(\d+) px, scale ([\d.]+)", line)
        if m:
            client = (int(m.group(1)), int(m.group(2)))
            scale = float(m.group(3))
            continue
        m = HIT_RE.match(line)
        if m:
            regions.append({k: (int(v) if k in ("x1", "y1", "x2", "y2", "n") else v) for k, v in m.groupdict().items()})
    return regions, client, scale


def check_hitmap(win: Window, text: str, step: int, state: str, allow: dict, emit_raw):
    regions, client_px, scale = parse_hitmap(text)
    if not regions and client_px is None:
        return
    scale = scale or win.scale or 1.0
    cw = client_px[0] / scale if client_px else (win.client or (0, 0))[0]
    ch = client_px[1] / scale if client_px else (win.client or (0, 0))[1]
    edge = float(allow.get("resizeBorderDip", 8)) + step / scale
    title_h = float(allow.get("titleBarDip", 40))
    for rule in allow.get("titleBarByWindow", []):
        if re.search(rule.get("window", "$^"), win.title or ""):
            title_h = float(rule.get("dip", title_h))
            break
    inter = [n for n in win.nodes if n.visible and n.interactive and has_area(n) and n.vis_rect]
    framed = state == "normal"
    for r in regions:
        code = r["code"]
        x1, y1, x2, y2 = r["x1"] / scale, r["y1"] / scale, r["x2"] / scale, r["y2"] / scale
        half = step / scale / 2
        bbox = (x1 - half, y1 - half, x2 - x1 + 2 * half, y2 - y1 + 2 * half)
        grid = ((r["x2"] - r["x1"]) // step + 1) * ((r["y2"] - r["y1"]) // step + 1)
        dense = r["n"] >= grid
        over_names = [short_type(s) for s in r["over"].split(" < ") if s.strip()]
        # Which interactive widgets does it sit on?
        # A widget is hit when one of the region's sample points falls inside it, and the region is
        # dense (every sample in its box answered this code) or hitmap.ps1 named the widget.
        sx = [(r["x1"] + k * step) / scale for k in range((r["x2"] - r["x1"]) // step + 1)]
        sy = [(r["y1"] + k * step) / scale for k in range((r["y2"] - r["y1"]) // step + 1)]

        def sampled(nd):
            vx, vy, vw, vh = nd.vis_rect
            return any(vx <= x < vx + vw for x in sx) and any(vy <= y < vy + vh for y in sy)

        hits = [n for n in inter if sampled(n) and (dense or (over_names and over_names[0] == n.type))]
        at_edge = (x1 <= edge or y1 <= edge or x2 >= cw - edge or y2 >= ch - edge) and (
            min(x2 - x1, y2 - y1) <= edge or code in ("HTTOPLEFT", "HTTOPRIGHT", "HTBOTTOMLEFT", "HTBOTTOMRIGHT"))
        expected = False
        why = ""
        if code in RESIZE:
            if framed and at_edge:
                continue  # the real resize border; it may cover the outer pixels of edge controls, like any Windows app
            else:
                why = "resize border away from the real edge" if framed else f"resize border in a {state} window"
        elif code == "HTCAPTION":
            if y2 <= title_h and state != "fullscreen":
                expected = not hits
                why = "caption over a control" if hits else ""
            else:
                why = "caption outside the title bar"
        elif code == "HTMAXBUTTON":
            maxbtn = [n for n in hits if re.fullmatch(allow.get("maxButtonType", "CaptionButton"), n.type)]
            expected = bool(maxbtn) or (y2 <= title_h and x1 >= cw - 4 * 36 - edge)
            why = "" if expected else "maximize-button answer away from the caption buttons"
            hits = [n for n in hits if n not in maxbtn]
            if expected and not hits:
                continue
        else:
            why = f"{code} inside the client area"
        if expected and not hits:
            continue
        desc = f"{code} px [{r['x1']},{r['y1']} .. {r['x2']},{r['y2']}] x{r['n']} over: {r['over']}"
        if hits:
            for n in hits:
                emit_raw("hitmap", "high", n, None, f"{desc} -> {why or 'non-client answer'} over {n.label()} {fmt_rect(n.rect)}", region=desc)
        else:
            emit_raw("hitmap-odd", "low", None, None, f"{desc} -> {why}", region=desc)


# --- report ---------------------------------------------------------------------------------------

CHECK_ORDER = ["hitmap", "overlap", "clip-window", "text-clip", "text-overflow", "target-size", "zero-size", "offscreen", "clip-parent",
               "overlap-cross", "text-trunc", "hitmap-odd"]
CHECK_TITLE = {
    "overlap": "Overlapping sibling leaves (text or input)",
    "overlap-cross": "Overlapping leaves in different branches",
    "clip-parent": "Child outside its parent",
    "clip-window": "Widget outside the client rect",
    "offscreen": "Visible widget entirely off-screen",
    "text-trunc": "Ellipsized text",
    "text-clip": "Text taller than its box (cut)",
    "text-overflow": "Text drawn past its widget",
    "target-size": "Interactive target under 24x24 DIP",
    "zero-size": "Zero-size visible interactive widget",
    "hitmap": "Non-client hit-test answer over an interactive widget",
    "hitmap-odd": "Unexpected non-client region",
}


def md_escape(s: str) -> str:
    return s.replace("|", "\\|").replace("\n", " ")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("tree")
    ap.add_argument("--hitmap")
    ap.add_argument("--hitmap-step", type=int, default=8)
    ap.add_argument("--allow", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "qa", "round5", "audit-allow.json"))
    ap.add_argument("--json")
    ap.add_argument("--state", choices=["normal", "maximized", "fullscreen"], help="window state when the dump has no v2 header")
    ap.add_argument("--context", default="", help="scenario name, matched by allowlist rules with \"context\"")
    ap.add_argument("--window", default=None, help="audit only windows whose title matches this regex")
    ap.add_argument("--quiet", action="store_true", help="no markdown on stdout")
    ap.add_argument("--md", help="also write the markdown report to this file (UTF-8)")
    ap.add_argument("--hitmap-window", default=None, help="regex on the window title the hit map belongs to (default: the first window)")
    args = ap.parse_args(argv)

    allow = {}
    if args.allow and os.path.exists(args.allow):
        with open(args.allow, encoding="utf-8") as f:
            allow = json.load(f)
    with open(args.tree, encoding="utf-8-sig", errors="replace") as f:
        windows = parse_tree(f.read())
    hit_text = None
    if args.hitmap and os.path.exists(args.hitmap):
        with open(args.hitmap, encoding="utf-8-sig", errors="replace") as f:
            hit_text = f.read()

    findings = []
    formats = set()
    for wi, win in enumerate(windows):
        if args.window and not re.search(args.window, win.title):
            continue
        formats.add("v2" if finalize(win, allow) else "v1")
        state = "fullscreen" if win.fullscreen else "maximized" if win.maximized else None
        if state is None and win.header:
            state = "normal"
        state = state or args.state or "normal"

        def emit(check, sev, node, other, detail, region=None, _win=win, _wi=wi):
            f = {"check": check, "severity": sev, "window": _win.title or f"window {_wi}", "detail": detail,
                 "widget": node.path() if node else None, "type": node.type if node else None,
                 "rect": fmt_rect(node.rect) if node else None, "line": node.line if node else None,
                 "other": other.path() if other else None, "otherLine": other.line if other else None,
                 "layer": node.layer if node else None}
            if region:
                f["region"] = region
            for rule in allow.get("rules", []):
                if match_rule(rule, f, node, other, _win, args.context) if node else (
                        rule.get("check") in (check, "*") and rx(rule.get("context"), args.context)
                        and rule.get("type") is None and re.search(rule.get("region") or "", region or "") is not None):
                    f["allowed"] = rule.get("reason", "allowlisted")
                    break
            findings.append(f)

        check_tree(win, allow, emit)
        hit_win = next((i for i, w in enumerate(windows) if args.hitmap_window and re.search(args.hitmap_window, w.title)), 0)
        if hit_text is not None and wi == hit_win:
            check_hitmap(win, hit_text, args.hitmap_step, state, allow, emit)

    # De-duplicate identical findings (same check, widget and detail).
    seen, uniq = set(), []
    for f in findings:
        k = (f["check"], f["window"], f["widget"], f["other"], f["detail"])
        if k not in seen:
            seen.add(k)
            uniq.append(f)
    findings = uniq
    active = [f for f in findings if "allowed" not in f]
    allowed = [f for f in findings if "allowed" in f]

    summary = {c: sum(1 for f in active if f["check"] == c) for c in CHECK_ORDER}
    result = {"tree": os.path.abspath(args.tree), "hitmap": os.path.abspath(args.hitmap) if args.hitmap else None,
              "context": args.context, "format": "+".join(sorted(formats)), "windows": [w.title for w in windows],
              "counts": {k: v for k, v in summary.items() if v}, "allowedCount": len(allowed),
              "findings": active, "allowed": allowed}
    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump(result, f, ensure_ascii=False, indent=1)

    if not args.quiet or args.md:
        out = []
        out.append(f"# UI audit: {os.path.basename(args.tree)}" + (f" ({args.context})" if args.context else ""))
        out.append("")
        out.append(f"Format {result['format']}; {len(active)} findings, {len(allowed)} allowlisted.")
        for c in CHECK_ORDER:
            rows = [f for f in active if f["check"] == c]
            if not rows:
                continue
            out.append("")
            out.append(f"## {CHECK_TITLE[c]} (`{c}`, {len(rows)})")
            out.append("")
            out.append("| # | sev | widget | line | detail |")
            out.append("|---|---|---|---|---|")
            for i, f in enumerate(rows, 1):
                out.append(f"| {i} | {f['severity']} | `{md_escape(f['widget'] or '-')}` | {f['line'] or ''} | {md_escape(f['detail'])} |")
        if allowed:
            out.append("")
            out.append("## Allowlisted")
            out.append("")
            reasons = {}
            for f in allowed:
                reasons.setdefault((f["check"], f["allowed"]), 0)
                reasons[(f["check"], f["allowed"])] += 1
            for (c, why), n in sorted(reasons.items()):
                out.append(f"- `{c}` x{n}: {why}")
        text = "\n".join(out) + "\n"
        if args.md:
            with open(args.md, "w", encoding="utf-8") as f:
                f.write(text)
        if not args.quiet:
            sys.stdout.reconfigure(encoding="utf-8")
            sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
