#!/usr/bin/env node
// Reads Moonbow's C++ sources and writes the JSON the site renders (src/data/app/):
//   settings.json  every row of the Settings window       (src/core/SettingsSchema.cpp)
//   actions.json   every command and its default keys     (src/app/Actions.cpp, strings/*.inc)
//   formats.json   the file types Moonbow opens           (library scanner, file associations, dialogs)
//
// Plain Node 20+, no dependencies: `npm run extract` from site/.
//
// The settings schema is C++ code, not a table: rows come from loops (equalizer bands, crop
// sides, compressor sliders), helper functions (the language lists) and `if`s that look at the
// machine. So this script tokenizes the C++ and runs the small subset of it the schema is
// written in: declarations, brace lists, structs, range-for and counted loops, ifs, calls, and
// the schema's own Builder and ItemRef classes. A statement it cannot read becomes a warning,
// never a silent gap.

import { existsSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const SITE = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const REPO = resolve(SITE, '..');
const OUT_DIR = join(SITE, 'src', 'data', 'app');

// Calls in the schema that look at the machine it runs on. The site describes a fresh install
// on a current Windows.
const RUNTIME = {
  'audio::ProcessLoopbackSupported': { value: true, why: 'process loopback is available (Windows 10 2004 or later)' },
  YtDlpPath: { value: '', why: 'yt-dlp is not installed (Moonbow neither ships nor downloads it)' },
};

const warnings = [];
const notes = [];
const warn = (msg) => warnings.push(msg);

// =============================================================================================
// Tokenizer
// =============================================================================================

// Longest first. No '>>': two '>' tokens, so nested template argument lists close one by one.
const PUNCT = ['...', '<<=', '::', '->', '++', '--', '<<', '<=', '>=', '==', '!=', '&&', '||',
  '+=', '-=', '*=', '/=', '%=', '&=', '|=', '^='];

const SIMPLE_ESCAPES = { n: '\n', t: '\t', r: '\r', a: '\x07', b: '\b', f: '\f', v: '\v', '\\': '\\', '"': '"', "'": "'", '?': '?' };

function codePoint(n) {
  return Number.isFinite(n) && n >= 0 && n <= 0x10ffff ? String.fromCodePoint(n) : '�';
}

// A "..." or '...' literal starting at src[j]; escapes decoded.
function readQuoted(src, j, quote) {
  let i = j + 1;
  let out = '';
  let lines = 0;
  while (i < src.length && src[i] !== quote) {
    const ch = src[i];
    if (ch === '\n') break;  // unterminated; the tokenizer goes on with the next line
    if (ch !== '\\') {
      out += ch;
      i++;
      continue;
    }
    const e = src[i + 1];
    if (e === '\n' || (e === '\r' && src[i + 2] === '\n')) {  // line continuation
      i += e === '\r' ? 3 : 2;
      lines++;
    } else if (e in SIMPLE_ESCAPES) {
      out += SIMPLE_ESCAPES[e];
      i += 2;
    } else if (e === 'x') {
      let k = i + 2;
      while (k < src.length && /[0-9A-Fa-f]/.test(src[k])) k++;
      out += codePoint(parseInt(src.slice(i + 2, k), 16));
      i = k;
    } else if (e === 'u' || e === 'U') {
      const len = e === 'u' ? 4 : 8;
      out += codePoint(parseInt(src.substr(i + 2, len), 16));
      i += 2 + len;
    } else if (/[0-7]/.test(e)) {
      let k = i + 1;
      while (k < i + 4 && /[0-7]/.test(src[k])) k++;
      out += codePoint(parseInt(src.slice(i + 1, k), 8));
      i = k;
    } else {
      out += e;
      i += 2;
    }
  }
  return { value: out, end: i + 1, lines };
}

// R"delim(...)delim" starting at the quote.
function readRaw(src, j) {
  const open = src.indexOf('(', j);
  const delim = src.slice(j + 1, open);
  const close = src.indexOf(')' + delim + '"', open);
  const value = src.slice(open + 1, close);
  return { value, end: close + delim.length + 2, lines: (value.match(/\n/g) || []).length };
}

function parseNumber(text) {
  const t = text.replaceAll("'", '');
  if (/^0[xX]/.test(t)) return parseInt(t.replace(/[uUlL]+$/, ''), 16);
  if (/^0[bB]/.test(t)) return parseInt(t.slice(2).replace(/[uUlL]+$/, ''), 2);
  const plain = t.replace(/[uUlLfF]+$/, '');
  if (/^0[0-7]+$/.test(plain)) return parseInt(plain, 8);
  return parseFloat(plain);
}

// Tokens: {t: 'id' | 'num' | 'str' | 'chr' | 'p', v, line, file}. Comments and preprocessor
// lines are dropped; adjacent string literals are joined, as the compiler does.
function tokenize(src, file) {
  const toks = [];
  const n = src.length;
  let i = 0;
  let line = 1;
  let lineStart = true;
  while (i < n) {
    const ch = src[i];
    if (ch === '\n') {
      line++;
      i++;
      lineStart = true;
      continue;
    }
    if (ch === ' ' || ch === '\t' || ch === '\r' || ch === '\f' || ch === '\v' || ch === '﻿') {
      i++;
      continue;
    }
    if (ch === '/' && src[i + 1] === '/') {
      while (i < n && src[i] !== '\n') i++;
      continue;
    }
    if (ch === '/' && src[i + 1] === '*') {
      const end = src.indexOf('*/', i + 2);
      const stop = end < 0 ? n : end + 2;
      for (let k = i; k < stop; k++) if (src[k] === '\n') line++;
      i = stop;
      continue;
    }
    if (ch === '#' && lineStart) {
      while (i < n && src[i] !== '\n') {
        if (src[i] === '\\' && (src[i + 1] === '\n' || (src[i + 1] === '\r' && src[i + 2] === '\n'))) {
          i += src[i + 1] === '\r' ? 3 : 2;
          line++;
          continue;
        }
        i++;
      }
      continue;
    }
    lineStart = false;
    const tokLine = line;
    if (/[A-Za-z_]/.test(ch)) {
      let j = i + 1;
      while (j < n && /[A-Za-z0-9_]/.test(src[j])) j++;
      const word = src.slice(i, j);
      const raw = word.endsWith('R');
      if (/^(?:L|u8|u|U)?R?$/.test(word) && (src[j] === '"' || (src[j] === "'" && !raw))) {
        if (src[j] === '"') {
          const lit = raw ? readRaw(src, j) : readQuoted(src, j, '"');
          toks.push({ t: 'str', v: lit.value, line: tokLine, file });
          line += lit.lines;
          i = lit.end;
        } else {
          const lit = readQuoted(src, j, "'");
          toks.push({ t: 'chr', v: lit.value.codePointAt(0) ?? 0, line: tokLine, file });
          i = lit.end;
        }
        continue;
      }
      toks.push({ t: 'id', v: word, line: tokLine, file });
      i = j;
      continue;
    }
    if (/[0-9]/.test(ch) || (ch === '.' && /[0-9]/.test(src[i + 1] ?? ''))) {
      let j = i + 1;
      const hex = /^0[xX]/.test(src.slice(i, i + 2));
      while (j < n) {
        const c = src[j];
        if (/[0-9A-Za-z_.]/.test(c)) j++;
        else if (c === "'" && /[0-9A-Fa-f]/.test(src[j + 1] ?? '')) j++;
        else if ((c === '+' || c === '-') && !hex && /[eE]/.test(src[j - 1])) j++;
        else break;
      }
      toks.push({ t: 'num', v: parseNumber(src.slice(i, j)), line: tokLine, file });
      i = j;
      continue;
    }
    if (ch === '"') {
      const lit = readQuoted(src, i, '"');
      toks.push({ t: 'str', v: lit.value, line: tokLine, file });
      line += lit.lines;
      i = lit.end;
      continue;
    }
    if (ch === "'") {
      const lit = readQuoted(src, i, "'");
      toks.push({ t: 'chr', v: lit.value.codePointAt(0) ?? 0, line: tokLine, file });
      i = lit.end;
      continue;
    }
    const p = PUNCT.find((s) => src.startsWith(s, i)) ?? ch;
    toks.push({ t: 'p', v: p, line: tokLine, file });
    i += p.length;
  }
  const merged = [];
  for (const t of toks) {
    const last = merged[merged.length - 1];
    if (t.t === 'str' && last && last.t === 'str') last.v += t.v;
    else merged.push(t);
  }
  return annotate(merged);
}

// Brace depth of every token, not counting namespace braces (so namespace-scope declarations
// sit at depth 0), and parenthesis depth.
function annotate(toks) {
  const stack = [];
  let depth = 0;
  let pdepth = 0;
  for (let k = 0; k < toks.length; k++) {
    const t = toks[k];
    t.depth = depth;
    t.pdepth = pdepth;
    if (t.t !== 'p') continue;
    if (t.v === '{') {
      const ns = isNamespaceOpen(toks, k);
      stack.push(ns);
      if (!ns) depth++;
    } else if (t.v === '}') {
      if (!stack.pop() && depth > 0) depth--;
      t.depth = depth;
    } else if (t.v === '(') {
      pdepth++;
    } else if (t.v === ')') {
      pdepth = Math.max(0, pdepth - 1);
      t.pdepth = pdepth;
    }
  }
  return toks;
}

function isNamespaceOpen(toks, k) {
  if (toks[k - 1]?.t === 'str' && toks[k - 2]?.v === 'extern') return true;
  let j = k - 1;
  while (j >= 0 && ((toks[j].t === 'id' && toks[j].v !== 'namespace') || toks[j].v === '::')) j--;
  return j >= 0 && toks[j].t === 'id' && toks[j].v === 'namespace';
}

const PAIRS = { '(': ')', '[': ']', '{': '}' };

function matchClose(toks, k, end = toks.length) {
  const open = toks[k].v;
  const close = PAIRS[open];
  let depth = 0;
  for (let j = k; j < end; j++) {
    const t = toks[j];
    if (t.t !== 'p') continue;
    if (t.v === open) depth++;
    else if (t.v === close && --depth === 0) return j;
  }
  throw new ParseError(`unbalanced "${open}"`, toks[k]);
}

function tokText(toks, a, b, max = 110) {
  let s = '';
  for (let k = a; k < b && k < toks.length; k++) {
    const t = toks[k];
    const v = t.t === 'str' ? JSON.stringify(t.v.length > 30 ? t.v.slice(0, 30) + '…' : t.v)
      : t.t === 'chr' ? `'${String.fromCodePoint(t.v)}'` : String(t.v);
    s += (s && /^[\w"']/.test(v) && /[\w"')\]]$/.test(s) ? ' ' : '') + v;
    if (s.length > max) return s.slice(0, max) + '…';
  }
  return s;
}

// =============================================================================================
// Parser: C++ statements and expressions into a small AST
// =============================================================================================

class ParseError extends Error {
  constructor(msg, tok) {
    super(msg);
    this.tok = tok;
  }
}

const EOF = { t: 'eof', v: '<end>', line: 0, file: '' };

class Cursor {
  constructor(toks, i = 0, end = toks.length) {
    this.toks = toks;
    this.i = i;
    this.end = end;
  }
  peek(o = 0) {
    const k = this.i + o;
    return k < this.end ? this.toks[k] : EOF;
  }
  next() {
    const t = this.peek();
    if (t !== EOF) this.i++;
    return t;
  }
  isP(v, o = 0) {
    const t = this.peek(o);
    return t.t === 'p' && t.v === v;
  }
  isId(v, o = 0) {
    const t = this.peek(o);
    return t.t === 'id' && (v === undefined || t.v === v);
  }
  expectP(v) {
    if (!this.isP(v)) throw new ParseError(`expected "${v}", found "${this.peek().v}"`, this.peek());
    return this.next();
  }
  expectId() {
    if (!this.isId()) throw new ParseError(`expected a name, found "${this.peek().v}"`, this.peek());
    return this.next().v;
  }
}

const SPECIFIERS = new Set(['const', 'static', 'constexpr', 'consteval', 'constinit', 'inline', 'volatile',
  'mutable', 'thread_local', 'extern', 'register', 'typename']);
const BUILTIN = new Set(['unsigned', 'signed', 'long', 'short', 'int', 'char', 'double', 'float', 'bool', 'void',
  'wchar_t', 'char8_t', 'char16_t', 'char32_t', 'auto']);
const NOT_TYPE = new Set(['return', 'if', 'else', 'for', 'while', 'do', 'switch', 'case', 'default', 'break',
  'continue', 'goto', 'new', 'delete', 'this', 'true', 'false', 'nullptr', 'sizeof', 'throw', 'try', 'catch',
  'operator', 'template', 'typedef', 'using', 'namespace', 'public', 'private', 'protected', 'static_cast',
  'const_cast', 'reinterpret_cast', 'dynamic_cast', 'co_return', 'co_await', 'struct', 'class', 'enum', 'union']);
const CONTAINERS = new Set(['vector', 'array', 'initializer_list', 'deque', 'list', 'span']);
const NUMERIC = new Set(['int', 'double', 'float', 'long', 'short', 'unsigned', 'signed', 'size_t', 'UINT', 'DWORD',
  'uint8_t', 'uint16_t', 'uint32_t', 'uint64_t', 'int8_t', 'int16_t', 'int32_t', 'int64_t', 'char', 'wchar_t']);
const STRINGS = new Set(['string', 'wstring', 'string_view', 'wstring_view']);

// At '<': skips a balanced template argument list; false when it was not one (a comparison).
function skipAngles(c) {
  let depth = 0;
  while (c.i < c.end) {
    const t = c.peek();
    if (t.t === 'p') {
      if (t.v === '<') depth++;
      else if (t.v === '>') {
        depth--;
        if (depth === 0) {
          c.next();
          return true;
        }
      } else if (t.v === '(' || t.v === '[') {
        c.i = matchClose(c.toks, c.i, c.end) + 1;
        continue;
      } else if ([';', '{', '}', ')', ']', '&&', '||'].includes(t.v)) return false;
    }
    c.next();
  }
  return false;
}

// A type ("const std::vector<Choice>&"); null, with the cursor unmoved, when there is none.
function parseType(c) {
  const start = c.i;
  while (c.isId() && SPECIFIERS.has(c.peek().v)) c.next();
  if (c.isId('struct') || c.isId('class') || c.isId('enum')) c.next();
  if (c.isP('::')) c.next();
  if (!c.isId() || NOT_TYPE.has(c.peek().v)) {
    c.i = start;
    return null;
  }
  if (BUILTIN.has(c.peek().v)) {
    while (c.isId() && (BUILTIN.has(c.peek().v) || SPECIFIERS.has(c.peek().v))) c.next();
  } else {
    c.next();
    for (;;) {
      if (c.isP('<') && !skipAngles(c)) {
        c.i = start;
        return null;
      }
      if (c.isP('::') && c.isId(undefined, 1)) {
        c.next();
        c.next();
        continue;
      }
      break;
    }
  }
  while (c.isP('*') || c.isP('&') || c.isP('&&') || (c.isId() && SPECIFIERS.has(c.peek().v))) c.next();
  return { start, end: c.i };
}

// What a declaration's type means to the interpreter: its base name ("Choice", "wstring"),
// pointer/reference, and for containers the element type.
function typeInfo(toks, start, end) {
  const slice = toks.slice(start, end);
  const info = { text: slice.map((t) => t.v).join(' '), base: null, pointer: false, ref: false, elem: null };
  let depth = 0;
  for (let k = 0; k < slice.length; k++) {
    const t = slice[k];
    if (t.t === 'p') {
      if (t.v === '<') depth++;
      else if (t.v === '>') depth--;
      else if (depth === 0 && t.v === '*') info.pointer = true;
      else if (depth === 0 && (t.v === '&' || t.v === '&&')) info.ref = true;
      continue;
    }
    if (depth !== 0 || t.t !== 'id' || SPECIFIERS.has(t.v)) continue;
    info.base = t.v;
    info.elem = null;
    if (CONTAINERS.has(t.v) && slice[k + 1]?.v === '<') {
      let j = k + 2;
      for (let d = 0; j < slice.length; j++) {
        const v = slice[j].v;
        if (v === '<') d++;
        else if (v === '>' && d-- === 0) break;
        else if (v === ',' && d === 0) break;
      }
      info.elem = typeInfo(slice, k + 2, j);
    }
  }
  return info;
}

const typeNamed = (name) => ({ text: name, base: name.split('::').pop(), pointer: false, ref: false, elem: null });

const node = (k, tok, fields) => ({ k, tok, ...fields });

const BIN_PREC = { '||': 1, '&&': 2, '|': 3, '^': 4, '&': 5, '==': 6, '!=': 6, '<': 7, '>': 7, '<=': 7, '>=': 7,
  '<<': 8, '+': 9, '-': 9, '*': 10, '/': 10, '%': 10 };
const ASSIGN_OPS = new Set(['=', '+=', '-=', '*=', '/=', '%=', '|=', '&=', '^=']);
const TEMPLATED = new Set(['static_cast', 'const_cast', 'reinterpret_cast', 'dynamic_cast']);

function parseAssign(c) {
  const lhs = parseTernary(c);
  const t = c.peek();
  if (t.t === 'p' && ASSIGN_OPS.has(t.v)) {
    c.next();
    return node('assign', lhs.tok, { op: t.v, target: lhs, value: parseAssign(c) });
  }
  return lhs;
}

function parseTernary(c) {
  const start = c.i;
  const cond = parseBin(c, 1);
  if (!c.isP('?')) return cond;
  const text = tokText(c.toks, start, c.i);
  c.next();
  const a = parseAssign(c);
  c.expectP(':');
  const b = parseAssign(c);
  return node('tern', cond.tok, { cond, a, b, text });
}

function parseBin(c, minPrec) {
  let left = parseUnary(c);
  for (;;) {
    const t = c.peek();
    const prec = t.t === 'p' ? BIN_PREC[t.v] : undefined;
    if (!prec || prec < minPrec) return left;
    c.next();
    left = node('bin', left.tok, { op: t.v, l: left, r: parseBin(c, prec + 1) });
  }
}

function parseUnary(c) {
  const t = c.peek();
  if (t.t === 'p' && ['!', '-', '+', '~', '*', '&'].includes(t.v)) {
    c.next();
    return node('unary', t, { op: t.v, e: parseUnary(c) });
  }
  if (t.t === 'p' && (t.v === '++' || t.v === '--')) {
    c.next();
    return node('incdec', t, { op: t.v, post: false, e: parseUnary(c) });
  }
  return parsePostfix(c);
}

function parsePostfix(c) {
  let e = parsePrimary(c);
  for (;;) {
    const t = c.peek();
    if (t.t !== 'p') return e;
    if (t.v === '(') {
      c.next();
      const args = [];
      while (!c.isP(')')) {
        args.push(parseAssign(c));
        if (!c.isP(',')) break;
        c.next();
      }
      c.expectP(')');
      e = node('call', e.tok, { fn: e, args });
    } else if (t.v === '[') {
      c.next();
      const idx = parseAssign(c);
      c.expectP(']');
      e = node('index', e.tok, { obj: e, idx });
    } else if (t.v === '.' || t.v === '->') {
      c.next();
      e = node('member', e.tok, { obj: e, name: c.expectId() });
    } else if (t.v === '++' || t.v === '--') {
      c.next();
      e = node('incdec', e.tok, { op: t.v, post: true, e });
    } else {
      return e;
    }
  }
}

function parseBraceList(c, type) {
  const open = c.expectP('{');
  const items = [];
  while (!c.isP('}')) {
    if (c.isP('.') && c.isId(undefined, 1)) throw new ParseError('designated initializers are not supported', c.peek());
    items.push(parseAssign(c));
    if (!c.isP(',')) break;
    c.next();
  }
  c.expectP('}');
  return node('list', open, { items, type });
}

function parsePrimary(c) {
  const t = c.peek();
  if (t.t === 'num' || t.t === 'str') {
    c.next();
    return node('lit', t, { v: t.v });
  }
  if (t.t === 'chr') {
    c.next();
    return node('lit', t, { v: new Char(t.v) });
  }
  if (t.t === 'p') {
    if (t.v === '(') {
      c.next();
      const e = parseAssign(c);
      c.expectP(')');
      return e;
    }
    if (t.v === '{') return parseBraceList(c, null);
    if (t.v === '::' && c.isId(undefined, 1)) c.next();
    else throw new ParseError(`unexpected "${t.v}"`, t);
  }
  const id = c.peek();
  if (id.t !== 'id') throw new ParseError(`unexpected "${id.v}"`, id);
  if (id.v === 'true' || id.v === 'false') {
    c.next();
    return node('lit', id, { v: id.v === 'true' });
  }
  if (id.v === 'nullptr' || id.v === 'NULL') {
    c.next();
    return node('lit', id, { v: null });
  }
  if (id.v === 'this') {
    c.next();
    return node('this', id, {});
  }
  if (['new', 'delete', 'sizeof', 'throw', 'operator', 'alignof', 'typeid', 'decltype', 'co_await'].includes(id.v)) {
    throw new ParseError(`"${id.v}" is not supported`, id);
  }
  let name = c.next().v;
  for (;;) {
    if (c.isP('<') && (TEMPLATED.has(name) || name.startsWith('std::'))) {
      const save = c.i;
      if (skipAngles(c)) continue;
      c.i = save;
      break;
    }
    if (c.isP('::') && c.isId(undefined, 1)) {
      c.next();
      name += '::' + c.next().v;
      continue;
    }
    break;
  }
  if (c.isP('{')) return parseBraceList(c, name);
  return node('name', id, { name });
}

function parseExprAt(toks, start, end = toks.length) {
  return parseAssign(new Cursor(toks, start, end));
}

// Skips one statement after a parse error: to its ';', or past its block.
function skipStatement(c) {
  try {
    while (c.i < c.end) {
      const t = c.next();
      if (t.t !== 'p') continue;
      if (t.v === ';') return;
      if (t.v === '(' || t.v === '[') {
        c.i = matchClose(c.toks, c.i - 1, c.end) + 1;
      } else if (t.v === '{') {
        c.i = matchClose(c.toks, c.i - 1, c.end) + 1;
        const n = c.peek();
        if (!(n.t === 'p' && [';', ',', ')', '.', '->'].includes(n.v))) return;
      }
    }
  } catch {
    c.i = c.end;
  }
}

// A declaration ("const Param kCompressor[] = {...};", "auto& [a, b] = x;"); null, with the
// cursor unmoved, when the statement is not one.
function tryParseDecl(c) {
  const start = c.i;
  const tr = parseType(c);
  if (!tr) return null;
  const decls = [];
  for (;;) {
    while (c.isP('*') || c.isP('&') || c.isP('&&') || c.isId('const')) c.next();
    const d = { name: null, names: null, isArray: false, init: null };
    if (c.isP('[')) {
      c.next();
      d.names = [];
      while (c.isId()) {
        d.names.push(c.next().v);
        if (c.isP(',')) c.next();
      }
      if (!c.isP(']')) {
        c.i = start;
        return null;
      }
      c.next();
    } else if (c.isId() && !NOT_TYPE.has(c.peek().v)) {
      d.name = c.next().v;
    } else {
      c.i = start;
      return null;
    }
    while (c.isP('[')) {
      c.i = matchClose(c.toks, c.i, c.end) + 1;
      d.isArray = true;
    }
    if (c.isP('=')) {
      c.next();
      d.init = parseAssign(c);
    } else if (c.isP('{')) {
      d.init = parseBraceList(c, null);
    } else if (!c.isP(';') && !c.isP(',')) {
      c.i = start;
      return null;
    }
    decls.push(d);
    if (!c.isP(',')) break;
    c.next();
  }
  c.expectP(';');
  return { k: 'decl', ti: typeInfo(c.toks, tr.start, tr.end), decls };
}

function parseStatements(toks, start, end) {
  const c = new Cursor(toks, start, end);
  const out = [];
  while (c.i < end) out.push(parseStatement(c));
  return out;
}

function parseStatement(c) {
  const start = c.i;
  const t = c.peek();
  const done = (s) => Object.assign(s, { tok: t, text: tokText(c.toks, start, c.i) });
  try {
    if (t.t === 'p' && t.v === ';') {
      c.next();
      return done({ k: 'empty' });
    }
    if (t.t === 'p' && t.v === '{') {
      const close = matchClose(c.toks, c.i, c.end);
      const body = parseStatements(c.toks, c.i + 1, close);
      c.i = close + 1;
      return { k: 'block', body, tok: t, text: '{…}' };
    }
    if (t.t === 'id') {
      switch (t.v) {
        case 'if': {
          c.next();
          if (c.isId('constexpr')) c.next();
          c.expectP('(');
          const cs = c.i;
          const cond = parseAssign(c);
          const condText = tokText(c.toks, cs, c.i);
          c.expectP(')');
          const head = tokText(c.toks, start, c.i);
          const then = parseStatement(c);
          let otherwise = null;
          if (c.isId('else')) {
            c.next();
            otherwise = parseStatement(c);
          }
          return { k: 'if', cond, condText, then, else: otherwise, tok: t, text: head };
        }
        case 'for': {
          c.next();
          const open = c.i;
          c.expectP('(');
          const close = matchClose(c.toks, open, c.end);
          let colon = -1;
          for (let k = c.i, d = 0; k < close; k++) {
            const v = c.toks[k];
            if (v.t !== 'p') continue;
            if (v.v === '(' || v.v === '[' || v.v === '{') d++;
            else if (v.v === ')' || v.v === ']' || v.v === '}') d--;
            else if (d === 0 && v.v === ';') break;
            else if (d === 0 && v.v === ':') {
              colon = k;
              break;
            }
          }
          const head = tokText(c.toks, start, close + 1);
          if (colon >= 0) {
            const tr = parseType(c);
            if (!tr) throw new ParseError('cannot read the loop variable', c.peek());
            const ti = typeInfo(c.toks, tr.start, tr.end);
            let name = null;
            let names = null;
            if (c.isP('[')) {
              c.next();
              names = [];
              while (!c.isP(']')) {
                names.push(c.expectId());
                if (c.isP(',')) c.next();
              }
              c.next();
            } else {
              name = c.expectId();
            }
            c.expectP(':');
            const range = parseAssign(c);
            c.expectP(')');
            return { k: 'forRange', ti, name, names, range, body: parseStatement(c), tok: t, text: head };
          }
          let init = null;
          if (c.isP(';')) c.next();
          else init = parseSimple(c);
          let cond = null;
          let condText = '';
          if (!c.isP(';')) {
            const cs = c.i;
            cond = parseAssign(c);
            condText = tokText(c.toks, cs, c.i);
          }
          c.expectP(';');
          const step = c.isP(')') ? null : parseAssign(c);
          c.expectP(')');
          return { k: 'for', init, cond, condText, step, body: parseStatement(c), tok: t, text: head };
        }
        case 'while': {
          c.next();
          c.expectP('(');
          const cs = c.i;
          const cond = parseAssign(c);
          const condText = tokText(c.toks, cs, c.i);
          c.expectP(')');
          return { k: 'while', cond, condText, body: parseStatement(c), tok: t, text: `while (${condText})` };
        }
        case 'return': {
          c.next();
          const e = c.isP(';') ? null : parseAssign(c);
          c.expectP(';');
          return done({ k: 'return', e });
        }
        case 'break':
        case 'continue':
          c.next();
          c.expectP(';');
          return done({ k: t.v });
        case 'struct':
        case 'class':
          if (c.isId(undefined, 1) && c.isP('{', 2)) {
            const name = c.peek(1).v;
            const open = c.i + 2;
            const def = parseStructBody(c.toks, open, name);
            c.i = matchClose(c.toks, open, c.end) + 1;
            if (c.isP(';')) c.next();
            return { k: 'struct', name, def, tok: t, text: `struct ${name}` };
          }
          break;
        case 'using':
        case 'typedef':
        case 'static_assert':
          skipStatement(c);
          return done({ k: 'empty' });
        case 'public':
        case 'private':
        case 'protected':
          if (c.isP(':', 1)) {
            c.i += 2;
            return done({ k: 'empty' });
          }
          break;
        case 'switch':
        case 'do':
        case 'try':
        case 'goto':
          throw new ParseError(`"${t.v}" is not supported`, t);
        default:
          break;
      }
    }
    return done(parseSimple(c));
  } catch (err) {
    if (!(err instanceof ParseError)) throw err;
    c.i = start;
    skipStatement(c);
    return done({ k: 'bad', err: err.message });
  }
}

// A declaration or an expression statement, with its ';'.
function parseSimple(c) {
  if (c.isId()) {
    const decl = tryParseDecl(c);
    if (decl) return decl;
  }
  const e = parseAssign(c);
  c.expectP(';');
  return { k: 'expr', e };
}

// --- structs, functions ----------------------------------------------------------------------

function skipToComma(c) {
  while (c.i < c.end && !c.isP(',')) {
    if (c.isP('(') || c.isP('[') || c.isP('{')) c.i = matchClose(c.toks, c.i, c.end) + 1;
    else c.next();
  }
}

function parseParams(toks, open, close) {
  const c = new Cursor(toks, open + 1, close);
  const out = [];
  if (c.isId('void') && open + 2 === close) return out;
  while (c.i < close) {
    const tr = parseType(c);
    if (!tr) throw new ParseError('cannot read a parameter', c.peek());
    const ti = typeInfo(toks, tr.start, tr.end);
    let name = null;
    if (c.isId()) name = c.next().v;
    while (c.isP('[')) {
      c.i = matchClose(toks, c.i, close) + 1;
      ti.pointer = true;
    }
    let def = null;
    if (c.isP('=')) {
      c.next();
      const s = c.i;
      skipToComma(c);
      def = { start: s, end: c.i };
    }
    out.push({ name, ti, def, toks });
    if (c.isP(',')) c.next();
    else if (c.i < close) throw new ParseError('unexpected token in parameters', c.peek());
  }
  return out;
}

function parseFieldDecl(toks, start, end) {
  const c = new Cursor(toks, start, end);
  const tr = parseType(c);
  if (!tr) return [];
  const ti = typeInfo(toks, tr.start, tr.end);
  const out = [];
  while (c.i < end) {
    let pointer = ti.pointer;
    while (c.isP('*') || c.isP('&') || c.isP('&&') || c.isId('const')) {
      if (c.isP('*')) pointer = true;
      c.next();
    }
    if (!c.isId()) break;
    const name = c.next().v;
    let isArray = false;
    while (c.isP('[')) {
      c.i = matchClose(toks, c.i, end) + 1;
      isArray = true;
    }
    let init = null;
    if (c.isP('=')) {
      c.next();
      const s = c.i;
      skipToComma(c);
      init = { start: s, end: c.i };
    } else if (c.isP('{')) {
      const s = c.i;
      c.i = matchClose(toks, c.i, end) + 1;
      init = { start: s, end: c.i };
    }
    out.push({ name, ti: pointer === ti.pointer ? ti : { ...ti, pointer }, isArray, init, toks });
    if (!c.isP(',')) break;
    c.next();
  }
  return out;
}

// Fields (in order, with their default initializers) and methods (with bodies) of a struct or class.
function parseStructBody(toks, open, name) {
  const close = matchClose(toks, open);
  const def = { name, fields: [], methods: new Map() };
  let k = open + 1;
  while (k < close) {
    const t = toks[k];
    if (t.t === 'p' && t.v === ';') {
      k++;
      continue;
    }
    if (t.t === 'id' && ['public', 'private', 'protected'].includes(t.v) && toks[k + 1]?.v === ':') {
      k += 2;
      continue;
    }
    const nested = t.t === 'id' && (t.v === 'struct' || t.v === 'class') && toks[k + 1]?.t === 'id' && toks[k + 2]?.v === '{';
    if (nested || (t.t === 'id' && ['using', 'friend', 'static_assert', 'typedef', 'enum', 'template'].includes(t.v))) {
      const c = new Cursor(toks, k, close);
      skipStatement(c);
      k = c.i;
      continue;
    }
    let j = k;
    let paren = -1;
    let eq = false;
    let body = null;
    while (j < close) {
      const v = toks[j];
      if (v.t === 'p') {
        if (v.v === '(') {
          if (paren < 0 && !eq) paren = j;
          j = matchClose(toks, j) + 1;
          continue;
        }
        if (v.v === '[') {
          j = matchClose(toks, j) + 1;
          continue;
        }
        if (v.v === '=' && paren < 0) eq = true;
        if (v.v === '{') {
          const end = matchClose(toks, j);
          if (paren >= 0) {
            body = { start: j + 1, end };
            j = end + 1;
            break;
          }
          j = end + 1;
          continue;
        }
        if (v.v === ';') break;
      }
      j++;
    }
    if (paren >= 0) {
      const mname = toks[paren - 1];
      if (body && mname.t === 'id' && mname.v !== name) {
        const m = { name: mname.v, toks, params: parseParams(toks, paren, matchClose(toks, paren)), body, ret: typeInfo(toks, k, paren - 1) };
        if (!def.methods.has(m.name)) def.methods.set(m.name, []);
        def.methods.get(m.name).push(m);
      }
    } else if (!(t.t === 'id' && t.v === 'static')) {
      def.fields.push(...parseFieldDecl(toks, k, j));
    }
    k = j < close && toks[j].t === 'p' && toks[j].v === ';' ? j + 1 : j;
  }
  return def;
}

function scanStructs(unit, toks) {
  for (let k = 0; k + 2 < toks.length; k++) {
    const t = toks[k];
    if (t.t !== 'id' || (t.v !== 'struct' && t.v !== 'class') || toks[k - 1]?.v === 'enum') continue;
    const nameTok = toks[k + 1];
    if (nameTok.t !== 'id') continue;
    let open = -1;
    if (toks[k + 2].v === '{') open = k + 2;
    else if (toks[k + 2].v === ':' || toks[k + 2].v === 'final') {
      for (let j = k + 3; j < toks.length; j++) {
        if (toks[j].t === 'p' && toks[j].v === '{') {
          open = j;
          break;
        }
        if (toks[j].v === ';') break;
      }
    }
    if (open < 0 || unit.structs.has(nameTok.v)) continue;
    try {
      unit.structs.set(nameTok.v, parseStructBody(toks, open, nameTok.v));
    } catch {
      // A struct this extractor never builds may use syntax it cannot read.
    }
  }
}

// Free functions with bodies, at namespace scope.
function scanFuncs(unit, toks) {
  for (let k = 1; k + 1 < toks.length; k++) {
    const t = toks[k];
    if (t.t !== 'id' || t.depth !== 0 || t.pdepth !== 0 || NOT_TYPE.has(t.v)) continue;
    if (toks[k + 1].t !== 'p' || toks[k + 1].v !== '(') continue;
    const prev = toks[k - 1];
    if (!((prev.t === 'id' && !NOT_TYPE.has(prev.v)) || (prev.t === 'p' && ['>', '*', '&', '&&'].includes(prev.v)))) continue;
    let close;
    try {
      close = matchClose(toks, k + 1);
    } catch {
      continue;
    }
    let b = close + 1;
    while (toks[b]?.t === 'id' && ['const', 'noexcept', 'override', 'final'].includes(toks[b].v)) b++;
    if (toks[b]?.t !== 'p' || toks[b].v !== '{') continue;
    const end = matchClose(toks, b);
    let params = null;
    try {
      params = parseParams(toks, k + 1, close);
    } catch {
      // unreadable parameters: the function cannot be called, which a caller reports
    }
    if (!unit.funcs.has(t.v)) unit.funcs.set(t.v, []);
    unit.funcs.get(t.v).push({ name: t.v, toks, params, body: { start: b + 1, end }, tok: t });
    k = end;
  }
}

function scanAliases(unit, toks) {
  for (let k = 0; k + 3 < toks.length; k++) {
    if (toks[k].t === 'id' && toks[k].v === 'using' && toks[k + 1].t === 'id' && toks[k + 2].v === '=') {
      let j = k + 3;
      while (j < toks.length && toks[j].v !== ';') j++;
      unit.aliases.set(toks[k + 1].v, typeInfo(toks, k + 3, j));
    }
  }
}

function loadUnit(paths) {
  const files = paths.map((p) => ({ path: p, toks: tokenize(readFileSync(join(REPO, p), 'utf8'), p) }));
  const unit = { files, structs: new Map(), funcs: new Map(), aliases: new Map() };
  for (const f of files) {
    scanStructs(unit, f.toks);
    scanFuncs(unit, f.toks);
    scanAliases(unit, f.toks);
  }
  return unit;
}

// =============================================================================================
// Interpreter
// =============================================================================================

class Sym {  // an enumerator or a name nothing defines here: ItemType::Toggle, VK_SPACE
  constructor(name) {
    this.name = name;
  }
}
class Char {  // 'S'
  constructor(code) {
    this.code = code;
  }
  get ch() {
    return String.fromCodePoint(this.code);
  }
}
class Unknown {  // a value that depends on code outside the units read here
  constructor(why) {
    this.why = why;
  }
}
class Obj {  // a struct instance
  constructor(type) {
    this.type = type;
    this.f = Object.create(null);
  }
}

const BREAK = { brk: true };
const CONTINUE = { cont: true };
const IDENTITY = new Set(['std::move', 'std::forward', 'std::as_const', 'static_cast', 'const_cast', 'reinterpret_cast']);

function truthy(v) {
  if (v instanceof Unknown || v instanceof Sym || v === undefined) return undefined;
  if (typeof v === 'boolean') return v;
  if (typeof v === 'number') return v !== 0;
  if (v === null) return false;
  if (v instanceof Char) return v.code !== 0;
  return true;  // strings (pointers), lists, objects
}

const num = (v) => (typeof v === 'number' ? v : v instanceof Char ? v.code : typeof v === 'boolean' ? Number(v) : undefined);

function equal(a, b) {
  if (a instanceof Sym && b instanceof Sym) {
    return a.name === b.name || a.name.endsWith('::' + b.name) || b.name.endsWith('::' + a.name);
  }
  if (a instanceof Sym || b instanceof Sym) return undefined;
  const x = a instanceof Char ? a.code : a;
  const y = b instanceof Char ? b.code : b;
  if (x === null || y === null) return x === y;
  if (typeof x === typeof y && typeof x !== 'object') return x === y;
  return undefined;
}

function binop(op, a, b) {
  if (a instanceof Unknown || b instanceof Unknown) return new Unknown(op);
  if (op === '==' || op === '!=') {
    const r = equal(a, b);
    return r === undefined ? new Unknown(op) : op === '==' ? r : !r;
  }
  if (op === '+' && (typeof a === 'string' || typeof b === 'string')) {
    const s = (x) => (typeof x === 'string' ? x : x instanceof Char ? x.ch : undefined);
    const sa = s(a);
    const sb = s(b);
    return sa === undefined || sb === undefined ? new Unknown('+') : sa + sb;
  }
  const x = num(a);
  const y = num(b);
  if (x === undefined || y === undefined) return new Unknown(op);
  switch (op) {
    case '+': return x + y;
    case '-': return x - y;
    case '*': return x * y;
    case '/': return x / y;
    case '%': return x % y;
    case '<': return x < y;
    case '>': return x > y;
    case '<=': return x <= y;
    case '>=': return x >= y;
    case '&': return x & y;
    case '|': return x | y;
    case '^': return x ^ y;
    case '<<': return x << y;
    default: return new Unknown(op);
  }
}

function describe(v) {
  if (v instanceof Obj) return v.type;
  if (v instanceof Sym) return v.name;
  if (v instanceof Unknown) return `an unknown value (${v.why})`;
  if (Array.isArray(v)) return 'a list';
  return JSON.stringify(v);
}

const where = (n) => (n?.tok?.file ? `${n.tok.file}:${n.tok.line}` : '?');

class Scope {
  constructor(parent = null) {
    this.parent = parent;
    this.vars = new Map();
  }
  find(name) {
    for (let s = this; s; s = s.parent) if (s.vars.has(name)) return s;
    return null;
  }
}

class Interp {
  constructor(unit) {
    this.unit = unit;
    this.globals = new Map();
    this.runtimeUsed = new Set();
    this.depth = 0;
  }

  // --- values ---

  list(items, elem) {
    items.elem = elem ?? null;
    return items;
  }

  defaultFor(ti) {
    if (!ti) return new Unknown('no initializer');
    if (ti.elem) return this.list([], ti.elem);
    if (ti.pointer) return null;
    if (ti.base === 'bool') return false;
    if (NUMERIC.has(ti.base)) return 0;
    if (STRINGS.has(ti.base) || STRINGS.has(this.unit.aliases.get(ti.base)?.base)) return '';
    if (ti.base === 'optional') return null;
    if (this.unit.structs.has(ti.base) && !ti.ref) return this.construct(ti.base, []);
    return new Unknown(`default ${ti.text}`);
  }

  coerce(v, ti, isArray = false) {
    if (!ti) return v;
    if (isArray) return Array.isArray(v) ? this.list(v.map((x) => this.coerceOne(x, ti)), ti) : v;
    return this.coerceOne(v, ti);
  }

  coerceOne(v, ti) {
    if (!ti || ti.ref || !Array.isArray(v)) return v;  // a reference binds to the very object
    if (ti.elem) return this.list(v.map((x) => this.coerceOne(x, ti.elem)), ti.elem);
    if (!ti.pointer && this.unit.structs.has(ti.base)) return this.construct(ti.base, v);
    const scalar = ti.pointer || ti.base === 'bool' || NUMERIC.has(ti.base) || STRINGS.has(ti.base);
    if (scalar && v.length <= 1) return v.length ? v[0] : this.defaultFor(ti);
    return v;
  }

  // Aggregate initialization: Param{"audio.compAttack", L"Attack", ...}.
  construct(type, items) {
    const def = this.unit.structs.get(type);
    const o = new Obj(type);
    def.fields.forEach((fd, i) => {
      if (i < items.length) o.f[fd.name] = this.coerce(items[i], fd.ti, fd.isArray);
      else if (fd.init) {
        const v = this.eval(parseExprAt(fd.toks, fd.init.start, fd.init.end), { scope: null, self: null, quiet: true });
        o.f[fd.name] = this.coerce(v, fd.ti, fd.isArray);
      } else o.f[fd.name] = fd.isArray ? new Unknown('array field') : this.defaultFor(fd.ti);
    });
    return o;
  }

  // A namespace-scope constant (kEqBandKeys, kNone ...), evaluated on first use.
  global(name) {
    if (this.globals.has(name)) return this.globals.get(name);
    this.globals.set(name, undefined);
    for (const f of this.unit.files) {
      const toks = f.toks;
      for (let k = 1; k + 1 < toks.length; k++) {
        const t = toks[k];
        if (t.t !== 'id' || t.v !== name || t.depth !== 0 || t.pdepth !== 0) continue;
        const prev = toks[k - 1];
        if (!((prev.t === 'id' && !NOT_TYPE.has(prev.v)) || (prev.t === 'p' && ['*', '&', '>'].includes(prev.v)))) continue;
        let m = k + 1;
        let isArray = false;
        while (toks[m]?.t === 'p' && toks[m].v === '[') {
          m = matchClose(toks, m) + 1;
          isArray = true;
        }
        if (toks[m]?.t !== 'p' || (toks[m].v !== '=' && toks[m].v !== '{')) continue;
        let s = k - 1;
        while (s >= 0 && !(toks[s].t === 'p' && [';', '{', '}'].includes(toks[s].v))) s--;
        const ti = typeInfo(toks, s + 1, k);
        const expr = parseExprAt(toks, toks[m].v === '=' ? m + 1 : m);
        const v = this.coerce(this.eval(expr, { scope: null, self: null, quiet: true }), ti, isArray);
        this.globals.set(name, v);
        return v;
      }
    }
    return undefined;
  }

  lookup(name, ctx) {
    const s = ctx.scope?.find(name);
    if (s) return s.vars.get(name);
    if (ctx.self instanceof Obj && name in ctx.self.f) return ctx.self.f[name];
    const bare = name.startsWith('::') ? name.slice(2) : name;
    if (!bare.includes('::')) {
      const g = this.global(bare);
      if (g !== undefined) return g;
    }
    return new Sym(bare);
  }

  store(target, v, ctx) {
    if (target.k === 'name') {
      const s = ctx.scope?.find(target.name);
      if (s) s.vars.set(target.name, v);
      else if (ctx.self instanceof Obj && target.name in ctx.self.f) ctx.self.f[target.name] = v;
      else throw new Error(`assignment to "${target.name}", which is not a variable here`);
    } else if (target.k === 'member') {
      const o = this.eval(target.obj, ctx);
      if (o instanceof Obj) o.f[target.name] = v;
    } else if (target.k === 'index') {
      const o = this.eval(target.obj, ctx);
      const i = num(this.eval(target.idx, ctx));
      if (Array.isArray(o) && i !== undefined) o[i] = v;
    } else if (target.k === 'unary' && target.op === '*') {
      this.store(target.e, v, ctx);
    }
  }

  // --- expressions ---

  eval(e, ctx) {
    switch (e.k) {
      case 'lit':
        return e.v;
      case 'this':
        return ctx.self ?? new Unknown('this');
      case 'name':
        return this.lookup(e.name, ctx);
      case 'list': {
        const items = e.items.map((x) => this.eval(x, ctx));
        return e.type ? this.coerceOne(items, typeNamed(e.type)) : items;
      }
      case 'member': {
        const o = this.eval(e.obj, ctx);
        if (o instanceof Obj && e.name in o.f) return o.f[e.name];
        return new Unknown(`${describe(o)}.${e.name}`);
      }
      case 'index': {
        const o = this.eval(e.obj, ctx);
        const i = num(this.eval(e.idx, ctx));
        if (Array.isArray(o) && i !== undefined && i >= 0 && i < o.length) return o[i];
        if (typeof o === 'string' && i !== undefined && i < o.length) return new Char(o.codePointAt(i));
        return new Unknown('[]');
      }
      case 'call':
        return this.call(e, ctx);
      case 'unary': {
        const v = this.eval(e.e, ctx);
        if (e.op === '!') {
          const t = truthy(v);
          return t === undefined ? new Unknown('!') : !t;
        }
        if (e.op === '-') return num(v) === undefined ? new Unknown('-') : -num(v);
        if (e.op === '~') return num(v) === undefined ? new Unknown('~') : ~num(v);
        return v;  // + * &
      }
      case 'incdec': {
        const cur = num(this.eval(e.e, ctx));
        if (cur === undefined) throw new Error(`cannot apply ${e.op} here`);
        const next = e.op === '++' ? cur + 1 : cur - 1;
        this.store(e.e, next, ctx);
        return e.post ? cur : next;
      }
      case 'bin': {
        if (e.op === '&&' || e.op === '||') {
          const and = e.op === '&&';
          const a = truthy(this.eval(e.l, ctx));
          if (a === !and) return a;
          const b = truthy(this.eval(e.r, ctx));
          if (a === undefined) return b === !and ? b : new Unknown(e.op);
          return b === undefined ? new Unknown(e.op) : b;
        }
        return binop(e.op, this.eval(e.l, ctx), this.eval(e.r, ctx));
      }
      case 'tern':
        return this.eval(this.decide(e.cond, ctx, e.text) ? e.a : e.b, ctx);
      case 'assign': {
        let v = this.eval(e.value, ctx);
        if (e.op !== '=') v = binop(e.op.slice(0, -1), this.eval(e.target, ctx), v);
        this.store(e.target, v, ctx);
        return v;
      }
      default:
        throw new Error(`unsupported expression (${e.k})`);
    }
  }

  decide(expr, ctx, text) {
    const v = truthy(this.eval(expr, ctx));
    if (v !== undefined) return v;
    if (!ctx.quiet) warn(`${where(expr)}: cannot tell whether "${text}" holds; assumed it does not`);
    return false;
  }

  isStringType(name) {
    const base = name.split('::').pop();
    return STRINGS.has(base) || STRINGS.has(this.unit.aliases.get(name)?.base);
  }

  call(e, ctx) {
    if (e.fn.k === 'member') {
      const recv = this.eval(e.fn.obj, ctx);
      return this.callMethod(recv, e.fn.name, e.args.map((a) => this.eval(a, ctx)), ctx, e);
    }
    if (e.fn.k !== 'name') return new Unknown('call');
    const name = e.fn.name.startsWith('::') ? e.fn.name.slice(2) : e.fn.name;
    const args = e.args.map((a) => this.eval(a, ctx));
    if (IDENTITY.has(name) || this.isStringType(name)) return args.length ? args[0] : '';
    if (name in RUNTIME) {
      this.runtimeUsed.add(name);
      return RUNTIME[name].value;
    }
    if (ctx.self instanceof Obj) {
      const m = this.methodDef(ctx.self.type, name, args.length);
      if (m) return this.enter(m, args, ctx.self, true);
    }
    const fn = pickOverload(this.unit.funcs.get(name), args.length);
    if (fn) return this.enter(fn, args, null, ctx.quiet);
    const type = name.split('::').pop();
    if (this.unit.structs.has(type)) return this.construct(type, args);
    return new Unknown(`${name}()`);
  }

  callMethod(recv, name, args, ctx, e) {
    if (recv instanceof Obj) {
      const m = this.methodDef(recv.type, name, args.length);
      if (m) return this.enter(m, args, recv, true);
      if (!ctx.quiet) warn(`${where(e)}: ${recv.type} has no method ${name}() with ${args.length} arguments`);
      return new Unknown(`${recv.type}.${name}()`);
    }
    if (Array.isArray(recv)) {
      switch (name) {
        case 'push_back':
        case 'emplace_back':
          recv.push(recv.elem ? this.coerceOne(args[0], recv.elem) : args[0]);
          return undefined;
        case 'back':
          return recv.length ? recv[recv.length - 1] : new Unknown('back() of an empty list');
        case 'front':
          return recv.length ? recv[0] : new Unknown('front() of an empty list');
        case 'size':
          return recv.length;
        case 'empty':
          return recv.length === 0;
        case 'clear':
          recv.length = 0;
          return undefined;
        case 'at':
          return recv[num(args[0])];
        default:
          break;
      }
    }
    if (typeof recv === 'string') {
      if (name === 'empty') return recv.length === 0;
      if (name === 'size' || name === 'length') return recv.length;
      if (name === 'c_str' || name === 'data') return recv;
    }
    return new Unknown(`${name}()`);
  }

  methodDef(type, name, argc) {
    return pickOverload(this.unit.structs.get(type)?.methods.get(name), argc);
  }

  // Runs a function or method body. Methods (Builder, ItemRef ...) run quietly: they are the
  // schema's machinery, and what matters is reported where the schema calls them.
  enter(def, args, self, quiet) {
    if (!def.params) throw new Error(`cannot read the parameters of ${def.name}()`);
    if (this.depth > 200) throw new Error('calls nest too deep');
    const ctx = { scope: new Scope(), self, quiet };
    def.params.forEach((p, i) => {
      let v;
      if (i < args.length) v = args[i];
      else if (p.def) v = this.eval(parseExprAt(p.toks, p.def.start, p.def.end), ctx);
      else v = new Unknown(`missing argument ${p.name}`);
      if (p.name) ctx.scope.vars.set(p.name, this.coerceOne(v, p.ti));
    });
    def.ast ??= parseStatements(def.toks, def.body.start, def.body.end);
    this.depth++;
    try {
      const r = this.run(def.ast, ctx);
      return r && 'ret' in r ? r.ret : undefined;
    } finally {
      this.depth--;
    }
  }

  // --- statements ---

  run(stmts, ctx) {
    for (const s of stmts) {
      let r;
      if (ctx.quiet) {
        r = this.exec(s, ctx);
      } else {
        try {
          r = this.exec(s, ctx);
        } catch (err) {
          warn(`${where(s)}: could not run \`${s.text}\`: ${err.message}`);
          continue;
        }
      }
      if (r) return r;
    }
    return null;
  }

  exec(s, ctx) {
    switch (s.k) {
      case 'empty':
        return null;
      case 'bad':
        if (!ctx.quiet) warn(`${where(s)}: skipped \`${s.text}\`, which the extractor cannot read (${s.err})`);
        return null;
      case 'block':
        return this.run(s.body, { ...ctx, scope: new Scope(ctx.scope) });
      case 'expr':
        this.eval(s.e, ctx);
        return null;
      case 'decl':
        for (const d of s.decls) {
          const v = this.coerce(d.init ? this.eval(d.init, ctx) : this.defaultFor(d.isArray ? null : s.ti), s.ti, d.isArray);
          if (d.names) this.bindNames(ctx.scope, d.names, v);
          else ctx.scope.vars.set(d.name, v);
        }
        return null;
      case 'struct':
        if (!this.unit.structs.has(s.name)) this.unit.structs.set(s.name, s.def);
        return null;
      case 'if':
        if (this.decide(s.cond, ctx, s.condText)) return this.exec(s.then, ctx);
        return s.else ? this.exec(s.else, ctx) : null;
      case 'for': {
        const loop = { ...ctx, scope: new Scope(ctx.scope) };
        if (s.init) this.exec(s.init, loop);
        for (let guard = 0; ; guard++) {
          if (guard > 100000) throw new Error('the loop does not end');
          if (s.cond && !this.decide(s.cond, loop, s.condText)) break;
          const r = this.exec(s.body, { ...loop, scope: new Scope(loop.scope) });
          if (r === BREAK) break;
          if (r && r !== CONTINUE) return r;
          if (s.step) this.eval(s.step, loop);
        }
        return null;
      }
      case 'forRange': {
        const range = this.eval(s.range, ctx);
        if (!Array.isArray(range)) throw new Error(`cannot loop over ${describe(range)}`);
        for (const el of range) {
          const scope = new Scope(ctx.scope);
          if (s.names) this.bindNames(scope, s.names, el);
          else scope.vars.set(s.name, this.coerceOne(el, s.ti));
          const r = this.exec(s.body, { ...ctx, scope });
          if (r === BREAK) break;
          if (r && r !== CONTINUE) return r;
        }
        return null;
      }
      case 'while':
        for (let guard = 0; this.decide(s.cond, ctx, s.condText); guard++) {
          if (guard > 100000) throw new Error('the loop does not end');
          const r = this.exec(s.body, { ...ctx, scope: new Scope(ctx.scope) });
          if (r === BREAK) break;
          if (r && r !== CONTINUE) return r;
        }
        return null;
      case 'return':
        return { ret: s.e ? this.eval(s.e, ctx) : undefined };
      case 'break':
        return BREAK;
      case 'continue':
        return CONTINUE;
      default:
        throw new Error(`unsupported statement (${s.k})`);
    }
  }

  bindNames(scope, names, v) {
    const parts = v instanceof Obj ? Object.values(v.f) : Array.isArray(v) ? v : null;
    if (!parts) throw new Error(`cannot unpack ${describe(v)}`);
    names.forEach((n, i) => scope.vars.set(n, parts[i]));
  }
}

function pickOverload(defs, argc) {
  if (!defs) return null;
  return defs.find((d) => !d.params || (argc <= d.params.length && argc >= d.params.filter((p) => !p.def).length)) ?? null;
}

// =============================================================================================
// Translations: src/app/strings/*.inc, {L"English", L"Русский"} pairs
// =============================================================================================

function loadTranslations() {
  // The order of the #include lines in I18n.cpp: the app keeps the first pair for a phrase.
  const i18n = readFileSync(join(REPO, 'src/app/I18n.cpp'), 'utf8');
  let files = [...i18n.matchAll(/#include\s+"([^"]+\.inc)"/g)].map((m) => `src/${m[1]}`);
  if (!files.length) {
    files = readdirSync(join(REPO, 'src/app/strings')).filter((f) => f.endsWith('.inc')).sort().map((f) => `src/app/strings/${f}`);
  }
  const map = new Map();
  for (const p of files) {
    if (!existsSync(join(REPO, p))) {
      warn(`translations: ${p} (included by I18n.cpp) does not exist`);
      continue;
    }
    const toks = tokenize(readFileSync(join(REPO, p), 'utf8'), p);
    for (let k = 0; k + 4 < toks.length; k++) {
      const [a, en, comma, ru, b] = toks.slice(k, k + 5);
      if (a.v === '{' && en.t === 'str' && comma.v === ',' && ru.t === 'str' && b.v === '}' && !map.has(en.v)) map.set(en.v, ru.v);
    }
  }
  return { map, files };
}

// =============================================================================================
// settings.json
// =============================================================================================

const textOr = (v, what, key) => {
  if (typeof v === 'string') return v;
  warn(`setting ${key}: ${what} is ${describe(v)}, not text`);
  return '';
};

function extractSettings() {
  const schemaPath = 'src/core/SettingsSchema.cpp';
  const unit = loadUnit([schemaPath, 'src/core/SettingsSchema.h']);
  const interp = new Interp(unit);
  for (const need of ['Builder', 'ItemRef', 'Item', 'Section', 'Category']) {
    if (!unit.structs.has(need)) throw new Error(`${schemaPath}: cannot find the ${need} class`);
  }

  // The schema runs Build*(b) on one Builder, in this order (struct Schema's constructor).
  let buildFns = null;
  const toks = unit.files[0].toks;
  for (let k = 0; k + 2 < toks.length && !buildFns; k++) {
    if (toks[k].v !== 'Builder' || toks[k + 1].t !== 'id' || toks[k + 2].v !== ';') continue;
    const b = toks[k + 1].v;
    const calls = [];
    for (let j = k + 3; j + 4 < toks.length; j += 5) {
      const [fn, open, arg, close, semi] = toks.slice(j, j + 5);
      if (fn.t === 'id' && open.v === '(' && arg.v === b && close.v === ')' && semi.v === ';') calls.push(fn.v);
      else break;
    }
    if (calls.length) buildFns = calls;
  }
  if (!buildFns) {
    buildFns = [...unit.funcs.values()].flat().filter((f) => f.params?.length === 1 && f.params[0].ti.base === 'Builder').map((f) => f.name);
    warn(`${schemaPath}: did not find "Builder b; Build...(b);", running every function that takes a Builder`);
  }

  const builder = interp.construct('Builder', []);
  for (const name of buildFns) {
    const fn = pickOverload(unit.funcs.get(name), 1);
    if (!fn) {
      warn(`${schemaPath}: cannot find ${name}(Builder&)`);
      continue;
    }
    interp.enter(fn, [builder], null, false);
  }

  const hasChoices = (it) => {
    if (interp.methodDef('Item', 'HasChoices', 0)) return interp.enter(interp.methodDef('Item', 'HasChoices', 0), [], it, true) === true;
    return ['Choice', 'Segmented', 'AccentPicker'].includes(String(it.f.type?.name).split('::').pop());
  };
  const option = (ch) => {
    if (ch instanceof Obj) return { ru: ch.f.labelRu, en: ch.f.label };
    if (Array.isArray(ch)) return { ru: ch[2], en: ch[1] };
    return { ru: new Unknown('choice'), en: new Unknown('choice') };
  };

  const pages = [];
  const keys = new Map();
  let count = 0;
  let valueCount = 0;
  let hiddenCount = 0;
  const perPage = [];
  for (const cat of builder.f.cats) {
    const page = { id: cat.f.id, ru: cat.f.labelRu, en: cat.f.label, group: cat.f.group, sections: [] };
    let shown = 0;
    let hidden = 0;
    for (const sec of cat.f.sections) {
      const rows = [];
      for (const it of sec.f.items) {
        const key = textOr(it.f.key, 'the key', '?');
        keys.set(key, (keys.get(key) ?? 0) + 1);
        if (it.f.hidden === true) {
          hidden++;
          continue;
        }
        const typeName = it.f.type instanceof Sym ? it.f.type.name.split('::').pop() : null;
        if (!typeName) warn(`setting ${key}: unknown kind ${describe(it.f.type)}`);
        const kind = typeName ? typeName[0].toLowerCase() + typeName.slice(1) : 'unknown';
        const row = {
          key,
          kind,
          ru: textOr(it.f.labelRu, 'the Russian label', key),
          en: textOr(it.f.label, 'the English label', key),
          descRu: textOr(it.f.descriptionRu, 'the Russian description', key),
          descEn: textOr(it.f.description, 'the English description', key),
        };
        if (hasChoices(it)) {
          const choices = Array.isArray(it.f.choices) ? it.f.choices : [];
          if (!choices.length) warn(`setting ${key}: a ${kind} without options`);
          row.options = choices.map(option).map((o) => ({ ru: textOr(o.ru, 'an option', key), en: textOr(o.en, 'an option', key) }));
        }
        row.basic = it.f.basic === true;
        row.advanced = it.f.advanced === true;
        if (it.f.dynamicChoices === true) row.dynamic = true;  // filled at runtime (audio devices)
        if (!row.en || !row.ru) warn(`setting ${key}: empty label`);
        rows.push(row);
        shown++;
        if (kind !== 'button' && kind !== 'info') valueCount++;
      }
      // The Settings window leaves out a section with no visible rows.
      if (rows.length) page.sections.push({ id: sec.f.id, ru: sec.f.titleRu, en: sec.f.title, settings: rows });
    }
    count += shown;
    hiddenCount += hidden;
    perPage.push({ id: page.id, ru: page.ru, shown, hidden });
    if (page.sections.length) pages.push(page);
  }
  for (const [k, n] of keys) if (n > 1) warn(`setting ${k} is built ${n} times`);

  // Cross-check: every b.<Kind>("literal.key") in the source should have produced a row.
  const builderVar = unit.funcs.get(buildFns[0])?.[0]?.params?.[0]?.name ?? 'b';
  const itemMethods = [...unit.structs.get('Builder').methods.values()].flat().filter((m) => m.ret.base === 'ItemRef').map((m) => m.name);
  const src = readFileSync(join(REPO, schemaPath), 'utf8');
  const literal = [...src.matchAll(new RegExp(`\\b${builderVar}\\.(?:${itemMethods.join('|')})\\(\\s*"([A-Za-z0-9_.]+)"`, 'g'))].map((m) => m[1]);
  const notBuilt = literal.filter((k) => !keys.has(k));
  if (notBuilt.length) notes.push(`in the schema but not built under the runtime assumptions: ${notBuilt.join(', ')}`);

  // What tools/dev/check-settings.py counts (STATUS.md quotes its number).
  let checkScriptCount = null;
  try {
    const py = readFileSync(join(REPO, 'tools/dev/check-settings.py'), 'utf8');
    const m = /re\.finditer\(r'([^']+)'/.exec(py);
    if (m) checkScriptCount = [...src.matchAll(new RegExp(m[1], 'g'))].length;
  } catch {
    // only for the summary
  }

  return {
    data: { count, valueCount, hiddenCount, pages },
    perPage,
    total: [...keys.values()].reduce((a, b) => a + b, 0),
    itemMethods,
    literalCount: literal.length,
    checkScriptCount,
    runtimeUsed: interp.runtimeUsed,
  };
}

// =============================================================================================
// actions.json
// =============================================================================================

// How KeyText() in Actions.cpp spells a key: modifier prefixes, the names in its switch, F1-F24,
// letters and digits as themselves.
function keyTextRules(unit) {
  const def = unit.funcs.get('KeyText')?.[0];
  if (!def) throw new Error('src/app/Actions.cpp: cannot find KeyText()');
  const t = def.toks;
  const rules = { mods: [], names: new Map(), fkeys: false, alnum: false };
  for (let k = def.body.start; k < def.body.end; k++) {
    const v = (o) => t[k + o]?.v;
    // if (mods.ctrl) s += L"Ctrl + ";
    if (v(0) === 'if' && v(1) === '(' && t[k + 2].t === 'id' && v(3) === '.' && t[k + 4].t === 'id' && v(5) === ')' &&
        t[k + 6].t === 'id' && v(7) === '+=' && t[k + 8].t === 'str') {
      rules.mods.push({ field: v(4), text: t[k + 8].v });
    }
    // case VK_SPACE: return s + tr(L"Space");   case VK_LEFT: return s + L"←";
    if (v(0) === 'case' && t[k + 1].t === 'id' && v(2) === ':' && v(3) === 'return' && t[k + 4].t === 'id' && v(5) === '+') {
      if (t[k + 6].t === 'str') rules.names.set(v(1), { text: t[k + 6].v, translated: false });
      else if (t[k + 6].t === 'id' && v(7) === '(' && t[k + 8].t === 'str' && v(9) === ')') rules.names.set(v(1), { text: t[k + 8].v, translated: true });
    }
    if (t[k].t === 'id' && t[k].v === 'VK_F24') rules.fkeys = true;
    if (t[k].t === 'chr' && t[k].v === 'Z'.codePointAt(0)) rules.alnum = true;
  }
  if (!rules.mods.length || !rules.names.size) throw new Error('src/app/Actions.cpp: cannot read KeyText()');
  return rules;
}

function extractActions(tr) {
  const unit = loadUnit(['src/app/Actions.cpp', 'src/app/Actions.h', 'src/ui/Widget.h']);
  const interp = new Interp(unit);
  const all = pickOverload(unit.funcs.get('AllActions'), 0);
  if (!all) throw new Error('src/app/Actions.cpp: cannot find AllActions()');
  const table = interp.enter(all, [], null, false);
  if (!Array.isArray(table)) throw new Error(`src/app/Actions.cpp: AllActions() gave ${describe(table)}`);
  const rules = keyTextRules(unit);

  const translate = (en, what) => {
    if (tr.map.has(en)) return tr.map.get(en);
    warn(`actions: no Russian for ${what} "${en}" in src/app/strings/*.inc (the app shows the English)`);
    return en;
  };
  const keyText = (vk, mods, ru) => {
    let s = '';
    for (const m of rules.mods) if (mods instanceof Obj && mods.f[m.field] === true) s += m.text;
    if (vk instanceof Sym) {
      const n = rules.names.get(vk.name);
      if (n) return s + (ru && n.translated ? translate(n.text, 'the key') : n.text);
      const f = /^VK_F(\d+)$/.exec(vk.name);
      if (f && rules.fkeys) return s + 'F' + f[1];
    } else if (vk instanceof Char && rules.alnum && /^[0-9A-Z]$/.test(vk.ch)) {
      return s + vk.ch;
    }
    warn(`actions: KeyText() has no name for ${describe(vk)} (Windows names it at runtime)`);
    return s + (vk instanceof Sym ? vk.name : describe(vk));
  };

  const groups = [];
  let count = 0;
  for (const a of table) {
    if (!(a instanceof Obj)) {
      warn(`actions: an entry of the table is ${describe(a)}`);
      continue;
    }
    if (a.f.action instanceof Sym && a.f.action.name.endsWith('::None')) continue;
    const groupEn = a.f.group;
    if (typeof groupEn !== 'string' || !groupEn) continue;  // as the shortcuts dialog
    let g = groups.find((x) => x.en === groupEn);
    if (!g) {
      g = { id: groupEn.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, ''), ru: translate(groupEn, 'the group'), en: groupEn, actions: [] };
      groups.push(g);
    }
    const bindings = (Array.isArray(a.f.keys) ? a.f.keys : []).filter((b) => Array.isArray(b) && num(b[0]) !== 0);
    g.actions.push({
      id: a.f.id,
      ru: translate(a.f.label, 'the command'),
      en: a.f.label,
      keys: bindings.map(([vk, mods]) => keyText(vk, mods, false)),
      keysRu: bindings.map(([vk, mods]) => keyText(vk, mods, true)),
    });
    count++;
  }
  return { count, groups };
}

// =============================================================================================
// formats.json
// =============================================================================================

function sourceFiles(dir) {
  const out = [];
  for (const e of readdirSync(join(REPO, dir), { withFileTypes: true })) {
    const p = `${dir}/${e.name}`;
    if (e.isDirectory()) out.push(...sourceFiles(p));
    else if (/\.(cpp|h|inc)$/.test(e.name)) out.push(p);
  }
  return out;
}

function extractFormats() {
  const kinds = ['video', 'audio', 'subtitles', 'playlists'];
  const sets = Object.fromEntries(kinds.map((k) => [k, new Set()]));
  const origin = Object.fromEntries(kinds.map((k) => [k, new Map()]));  // ext -> first source
  const add = (kind, ext, from) => {
    const e = ext.toLowerCase().replace(/^\*?\./, '');
    if (!/^[a-z0-9]+$/.test(e)) {
      warn(`formats: "${ext}" in ${from} is not a plain extension`);
      return;
    }
    sets[kind].add(e);
    if (!origin[kind].has(e)) origin[kind].set(e, from);
  };
  const kindOf = (name) => (/video/i.test(name) ? 'video' : /audio/i.test(name) ? 'audio'
    : /subtitle/i.test(name) ? 'subtitles' : /playlist/i.test(name) ? 'playlists' : null);

  // 1. Lists in the library scanner and the file associations (kVideoExts, kAudio ...).
  for (const p of ['src/library/MediaTypes.cpp', 'src/app/FileAssociations.cpp']) {
    const unit = loadUnit([p]);
    const interp = new Interp(unit);
    const toks = unit.files[0].toks;
    let found = 0;
    for (let k = 0; k + 2 < toks.length; k++) {
      const t = toks[k];
      if (t.t !== 'id' || t.depth !== 0 || !/^k[A-Z]/.test(t.v) || toks[k + 1].v !== '[') continue;
      const kind = kindOf(t.v);
      const v = kind && interp.global(t.v);
      if (!Array.isArray(v) || !v.every((x) => typeof x === 'string')) continue;
      for (const ext of v) add(kind, ext, `${p} ${t.v}`);
      found++;
    }
    if (!found) warn(`formats: no extension lists found in ${p}`);
  }
  const known = new Map();
  for (const k of kinds) for (const e of sets[k]) if (!known.has(e)) known.set(e, k);

  // 2. Open-file dialog filters anywhere in src: {Tr(L"Subtitles"), L"*.srt;*.ass"}.
  const used = [];
  const ignored = [];
  for (const p of sourceFiles('src')) {
    const toks = tokenize(readFileSync(join(REPO, p), 'utf8'), p);
    for (let k = 0; k + 4 < toks.length; k++) {
      if (toks[k].v !== '{') continue;
      let j = k + 1;
      let name;
      if (toks[j].t === 'str') {
        name = toks[j].v;
        j++;
      } else if (toks[j].t === 'id' && toks[j + 1]?.v === '(' && toks[j + 2]?.t === 'str' && toks[j + 3]?.v === ')') {
        name = toks[j + 2].v;
        j += 4;
      } else continue;
      if (toks[j]?.v !== ',' || toks[j + 1]?.t !== 'str' || toks[j + 2]?.v !== '}') continue;
      const spec = toks[j + 1].v;
      if (!/^\*\.[A-Za-z0-9]+(;\*\.[A-Za-z0-9]+)*$/.test(spec)) continue;
      const exts = spec.split(';').map((s) => s.slice(2).toLowerCase());
      const at = `${p}:${toks[k].line} "${name}"`;
      let kind = null;
      if (/subtitle/i.test(name)) kind = 'subtitles';
      else if (/^audio/i.test(name)) kind = 'audio';
      else if (/playlist|m3u/i.test(name)) kind = 'playlists';
      else if (/video|media/i.test(name)) kind = 'mixed';  // classified by the lists above
      if (!kind) {
        ignored.push(`${at}: ${exts.join(' ')}`);
        continue;
      }
      used.push(at);
      for (const e of exts) {
        const k2 = kind === 'mixed' ? known.get(e) : kind;
        if (!k2) warn(`formats: ${at} lists .${e}, which no list says is video, audio or a playlist`);
        else add(k2, e, at);
      }
    }
  }
  const data = Object.fromEntries(kinds.map((k) => [k, [...sets[k]].sort()]));
  const extra = [];
  for (const k of kinds) for (const [e, from] of origin[k]) if (from.includes('"')) extra.push(`.${e} (${k}, from ${from})`);
  return { data, used, ignored, extra };
}

// =============================================================================================
// main
// =============================================================================================

const pad = (s, n) => String(s).padEnd(n);

function main() {
  const fatal = [];
  const tr = loadTranslations();
  const settings = extractSettings();
  const actions = extractActions(tr);
  const formats = extractFormats();

  const s = settings.data;
  if (s.pages.length < 5 || s.count < 50) fatal.push(`settings look broken: ${s.pages.length} pages, ${s.count} rows`);
  if (actions.count < 20) fatal.push(`actions look broken: ${actions.count} commands`);
  for (const [k, list] of Object.entries(formats.data)) if (!list.length) fatal.push(`formats: no ${k} extensions`);

  console.log(`Moonbow app data from ${REPO}`);
  console.log(`\nsettings.json: ${s.count} visible rows on ${s.pages.length} pages (${s.valueCount} hold a value, ` +
    `the rest are buttons and info rows); ${s.hiddenCount} hidden; ${settings.total} built in all`);
  for (const p of settings.perPage) console.log(`  ${pad(p.id, 12)} ${pad(p.ru, 20)} ${String(p.shown).padStart(3)}${p.hidden ? `  (+${p.hidden} hidden)` : ''}`);
  if (settings.checkScriptCount !== null) {
    console.log(`  tools/dev/check-settings.py counts ${settings.checkScriptCount} rows: b.<Kind>("literal.key") calls only, ` +
      `hidden ones included, rows made in loops or with b.Add() left out`);
  }
  console.log(`\nactions.json: ${actions.count} commands in ${actions.groups.length} groups`);
  for (const g of actions.groups) {
    const keyed = g.actions.filter((a) => a.keys.length).length;
    console.log(`  ${pad(g.id, 18)} ${pad(g.ru, 20)} ${String(g.actions.length).padStart(3)}  (${keyed} with a default key)`);
  }
  console.log(`\nformats.json: ${Object.entries(formats.data).map(([k, v]) => `${k} ${v.length}`).join(', ')}`);
  console.log(`  dialog filters read: ${formats.used.length}; left out: ${formats.ignored.length}`);
  for (const i of formats.ignored) console.log(`    left out ${i}`);
  if (formats.extra.length) console.log(`  only in a dialog filter: ${formats.extra.join('; ')}`);

  console.log('\nRuntime assumptions:');
  for (const name of settings.runtimeUsed) console.log(`  ${name}(): ${RUNTIME[name].why}`);
  for (const n of notes) console.log(`Note: ${n}`);
  if (warnings.length) {
    console.log(`\nWarnings (${warnings.length}):`);
    for (const w of warnings) console.log(`  ${w}`);
  }

  if (fatal.length) {
    console.error(`\nNot written: ${fatal.join('; ')}`);
    process.exit(1);
  }
  mkdirSync(OUT_DIR, { recursive: true });
  const write = (name, data) => writeFileSync(join(OUT_DIR, name), JSON.stringify(data, null, 2) + '\n', 'utf8');
  write('settings.json', s);
  write('actions.json', actions);
  write('formats.json', formats.data);
  console.log(`\nWrote settings.json, actions.json, formats.json to ${OUT_DIR}`);
}

try {
  main();
} catch (err) {
  console.error(`extract-app-data: ${err.stack ?? err}`);
  process.exit(1);
}
