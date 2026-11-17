// MashTouch - KiCad helper: S-expression reader and library symbol pins.
// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const fs = require('fs');
const path = require('path');

// Parse an S-expression text into nested arrays; strings stay {s: "..."},
// atoms are plain strings.
function parse(text) {
  let i = 0;
  const n = text.length;
  function skip() {
    while (i < n && /\s/.test(text[i])) i++;
  }
  function item() {
    skip();
    if (text[i] === '(') {
      i++;
      const list = [];
      for (;;) {
        skip();
        if (text[i] === ')') { i++; return list; }
        list.push(item());
      }
    }
    if (text[i] === '"') {
      i++;
      let s = '';
      while (text[i] !== '"') {
        if (text[i] === '\\') { s += text[i + 1]; i += 2; } else s += text[i++];
      }
      i++;
      return {s};
    }
    const start = i;
    while (i < n && !/[\s()]/.test(text[i])) i++;
    return text.slice(start, i);
  }
  return item();
}

// Serialise back (strings quoted, lists indented one level per depth).
function write(x, depth = 0) {
  if (typeof x === 'string') return x;
  if (typeof x === 'number') return String(+x.toFixed(4));
  if (x && x.s !== undefined) return '"' + x.s.replace(/\\/g, '\\\\').replace(/"/g, '\\"').replace(/\n/g, '\\n') + '"';
  const simple = x.every(e => !Array.isArray(e));
  if (simple) return '(' + x.map(e => write(e)).join(' ') + ')';
  const pad = '\t'.repeat(depth + 1);
  return '(' + x.map((e, k) => (k && Array.isArray(e) ? '\n' + pad : (k ? ' ' : '')) + write(e, depth + 1)).join('') + ')';
}

const head = l => Array.isArray(l) ? l[0] : null;
const str = v => (v && v.s !== undefined ? v.s : v);
const find = (l, h) => l.find(e => head(e) === h);
const findAll = (l, h) => l.filter(e => head(e) === h);

const LIB_DIR = process.env.KICAD_SYMBOL_DIR || 'C:/Program Files/KiCad/10.0/share/kicad/symbols';
const libCache = new Map();
function library(lib) {
  if (!libCache.has(lib)) libCache.set(lib, parse(fs.readFileSync(path.join(LIB_DIR, lib + '.kicad_sym'), 'utf8')));
  return libCache.get(lib);
}

// The symbol definition (with "extends" resolved: the parent's units are used,
// the child's properties win). Returns a deep copy named "Lib:Name".
function symbol(libName) {
  const [lib, name] = libName.split(':');
  const root = library(lib);
  const own = root.find(e => head(e) === 'symbol' && str(e[1]) === name);
  if (!own) throw new Error('no symbol ' + libName);
  let def = JSON.parse(JSON.stringify(own));
  const ext = find(def, 'extends');
  if (ext) {
    const parent = JSON.parse(JSON.stringify(symbol(lib + ':' + str(ext[1]))));
    const props = new Map(findAll(def, 'property').map(p => [str(p[1]), p]));
    def = parent.map(e => {
      if (head(e) === 'property' && props.has(str(e[1]))) return props.get(str(e[1]));
      if (head(e) === 'symbol') {  // rename units ParentName_1_1 -> Name_1_1
        const unitName = str(e[1]).replace(/^.*?(_\d+_\d+)$/, name + '$1');
        return [e[0], {s: unitName}, ...e.slice(2)];
      }
      return e;
    });
    def[1] = {s: name};
    for (const [k, p] of props) if (!findAll(def, 'property').some(q => str(q[1]) === k)) def.splice(2, 0, p);
  }
  def[1] = {s: lib + ':' + name};
  return def;
}

// Pins of a symbol: [{num, name, type, x, y, angle, len, unit}] in symbol
// coordinates (y up, as stored in the library).
function pins(def) {
  const out = [];
  for (const u of findAll(def, 'symbol')) {
    const m = /_(\d+)_(\d+)$/.exec(str(u[1]));
    const unit = m ? +m[1] : 0;
    for (const p of findAll(u, 'pin')) {
      const at = find(p, 'at');
      out.push({
        num: str(find(p, 'number')[1]), name: str(find(p, 'name')[1]), type: p[1],
        x: +at[1], y: +at[2], angle: +(at[3] || 0), len: +find(p, 'length')[1], unit,
      });
    }
  }
  return out;
}

function property(def, key) {
  const p = findAll(def, 'property').find(q => str(q[1]) === key);
  return p ? str(p[2]) : undefined;
}

module.exports = {parse, write, head, str, find, findAll, symbol, pins, property};

if (require.main === module) {
  for (const n of process.argv.slice(2)) {
    const d = symbol(n);
    console.log(`== ${n}  fp=${property(d, 'Footprint')}`);
    for (const p of pins(d)) console.log(`  u${p.unit} ${p.num.padEnd(4)} ${p.name.padEnd(12)} ${p.type.padEnd(14)} (${p.x}, ${p.y}) a${p.angle}`);
  }
}
