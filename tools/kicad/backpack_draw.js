// MashTouch - drawing primitives for the backpack schematic generator.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Everything placed is recorded in "model" (for the KiCad sheet) and drawn
// into "g" (for the SVG preview). Both use the same coordinates: px, with
// 10 px = 1.27 mm, so the 20 px pin pitch is KiCad's 2.54 mm grid.
'use strict';

const g = [];          // svg elements
const model = [];      // placed items, for the KiCad output
const fields = {};     // ref -> {ref: [x, y, anchor], val: [...]}
const parts = {};      // ref -> {pins: {num: [x, y]}}
const wires = [];      // {net, pts}
const powers = [];     // {net, x, y} power symbols and PWR_FLAGs
const ncs = [];        // {ref, pin, x, y}
const f = n => +n.toFixed(2);
const esc = s => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');

function text(x, y, s, o = {}) {
  const a = o.anchor || 'start';
  const cls = o.cls || 't';
  g.push(`<text class="${cls}" x="${f(x)}" y="${f(y)}" text-anchor="${a}">${esc(s)}</text>`);
  if (o.sheet) model.push({t: 'text', x, y, s, anchor: a, cls});
}
// a part's reference or value text
function lab(ref, which, x, y, s, anchor, cls) {
  text(x, y, s, {anchor, cls});
  (fields[ref] = fields[ref] || {})[which] = [x, y, anchor];
}
function line(pts, cls = 'w') {
  g.push(`<polyline class="${cls}" points="${pts.map(p => f(p[0]) + ',' + f(p[1])).join(' ')}"/>`);
}
// transform for symbol-local coordinates: mirror (x -> -x) then rotate
function xf(x0, y0, rot, mir) {
  const r = rot * Math.PI / 180, c = Math.round(Math.cos(r)), s = Math.round(Math.sin(r));
  return (x, y) => {
    if (mir) x = -x;
    return [x0 + x * c - y * s, y0 + x * s + y * c];
  };
}
function group(x, y, rot, mir, body) {
  g.push(`<g transform="translate(${f(x)} ${f(y)}) rotate(${rot})${mir ? ' scale(-1 1)' : ''}">${body}</g>`);
}
function addPart(ref, pins) {
  if (parts[ref]) throw new Error('duplicate part ' + ref);
  parts[ref] = {pins};
  return parts[ref];
}
const P = (ref, pin) => {
  const p = parts[ref] && parts[ref].pins[pin];
  if (!p) throw new Error(`no pin ${ref}.${pin}`);
  return p;
};

// two-pin parts, drawn horizontally from -20 (local pin 1) to +20; in KiCad
// they are the Device:*_Small symbols, whose pins are 5.08 mm apart too
const BODY = {
  R: 'M-20 0H-12L-10 -4L-6 4L-2 -4L2 4L6 -4L10 4L12 0H20',
  C: 'M-20 0H-3M-3 -8V8M3 -8V8M3 0H20',
  CP: 'M-20 0H-3M-3 -8V8M5 -8Q2 0 5 8M3 0H20M-10 -9H-6M-8 -11V-7',
  L: 'M-20 0H-12A3 3 0 0 1 -6 0A3 3 0 0 1 0 0A3 3 0 0 1 6 0A3 3 0 0 1 12 0H20',
  F: 'M-20 0H-12M12 0H20M-12 -4H12V4H-12ZM-12 0H12',
  PTC: 'M-20 0H-12M12 0H20M-12 -4H12V4H-12ZM-8 7L8 -7H12',
  D: 'M-20 0H-6M6 0H20M-6 -6V6L6 0Z M6 -6V6',
  DS: 'M-20 0H-6M6 0H20M-6 -6V6L6 0Z M6 -6V6M6 -6H9V-4M6 6H3V4',
  DZ: 'M-20 0H-6M6 0H20M-6 -6V6L6 0Z M6 -6V6M6 -6H9M6 6H3',
  TVS: 'M-20 0H-10M10 0H20M-10 -6V6L0 0ZM10 -6V6L0 0ZM0 -6V6M0 -6H3M0 6H-3',
  LED: 'M-20 0H-6M6 0H20M-6 -6V6L6 0Z M6 -6V6M0 -9L5 -15M3 -15H5V-13M5 -8L10 -14M8 -14H10V-12',
};
const FILLED = {D: 1, DS: 1, DZ: 1, LED: 1, TVS: 1};
// dir: 'r' pin 1 left, 'l' pin 1 right, 'd' pin 1 top, 'u' pin 1 bottom
const DIR = {r: 0, d: 90, l: 180, u: 270};
function two(ref, kind, x, y, dir, value, o = {}) {
  const map = o.pins || ['1', '2'];
  const t = xf(x, y, DIR[dir], false);
  const pins = {};
  pins[map[0]] = t(-20, 0);
  pins[map[1]] = t(20, 0);
  addPart(ref, pins);
  model.push({t: 'two', ref, kind, x, y});
  const cls = FILLED[kind] ? 'sf' : 's';
  group(x, y, DIR[dir], false, `<path class="${cls}" d="${BODY[kind]}"/>`);
  const l = o.lab || (dir === 'r' || dir === 'l' ? 'a' : 'r');
  if (l === 'a') { lab(ref, 'ref', x, y - 21, ref, 'middle', 'ref'); lab(ref, 'val', x, y - 10, value, 'middle', 'val'); }
  if (l === 'b') { lab(ref, 'ref', x, y + 19, ref, 'middle', 'ref'); lab(ref, 'val', x, y + 30, value, 'middle', 'val'); }
  if (l === 'r') { lab(ref, 'ref', x + 11, y - 2, ref, 'start', 'ref'); lab(ref, 'val', x + 11, y + 9, value, 'start', 'val'); }
  if (l === 'l') { lab(ref, 'ref', x - 11, y - 2, ref, 'end', 'ref'); lab(ref, 'val', x - 11, y + 9, value, 'end', 'val'); }
}
// MOSFET: local gate (-30,0), drain (0,-30), source (0,30)
const FET_BODY = [[[-10, -9], [-10, 9]], [[-5, -11], [-5, -5]], [[-5, -3], [-5, 3]], [[-5, 5], [-5, 11]],
  [[-5, -8], [0, -8], [0, -10]], [[-5, 8], [0, 8], [0, 10]], [[-5, 0], [0, 0], [0, 8]]];
function fet(ref, ch, x, y, rot, mir, value, pins, l = 'r') {
  const t = xf(x, y, rot, mir);
  const pp = {};
  pp[pins.G] = t(-30, 0); pp[pins.D] = t(0, -30); pp[pins.S] = t(0, 30);
  addPart(ref, pp);
  model.push({t: 'fet', ref, ch, x, y, rot, mir, pins});
  const arrow = ch === 'n' ? 'M-5 0L0 -3V3Z' : 'M0 0L-5 -3V3Z';
  const body = FET_BODY.map(p => 'M' + p.map(q => q.join(' ')).join('L')).join('');
  group(x, y, rot, mir,
    `<circle class="s" cx="-3" cy="0" r="15"/><path class="s" d="M-30 0H-10M0 -30V-10M0 30V10${body}"/><path class="sf" d="${arrow}"/>`);
  if (l === 'r') { lab(ref, 'ref', x + 16, y - 4, ref, 'start', 'ref'); lab(ref, 'val', x + 16, y + 7, value, 'start', 'val'); }
  else { lab(ref, 'ref', x - 18, y - 4, ref, 'end', 'ref'); lab(ref, 'val', x - 18, y + 7, value, 'end', 'val'); }
}
// IC box: left/right pin lists (one entry per 20 px row: {n, name} or null)
function ic(ref, value, x, y, w, left, right, o = {}) {
  const rows = Math.max(left.length, right.length);
  const h = (rows + 1) * 20;
  g.push(`<rect class="box" x="${x}" y="${y}" width="${w}" height="${h}"/>`);
  lab(ref, 'ref', x + w / 2, y - 16, ref, 'middle', 'ref');
  lab(ref, 'val', x + w / 2, y - 5, value, 'middle', 'val');
  const pins = {}, kp = [];
  left.forEach((p, i) => {
    if (!p) return;
    const py = y + 20 + i * 20;
    line([[x - 20, py], [x, py]], 's');
    pins[p.n] = [x - 20, py];
    kp.push({n: p.n, name: p.name, x: x - 20, y: py, dir: 'r'});
    text(x + 4, py + 3.5, p.name, {cls: 'pin'});
    text(x - 3, py - 3, p.n, {anchor: 'end', cls: 'num'});
  });
  right.forEach((p, i) => {
    if (!p) return;
    const py = y + 20 + i * 20;
    line([[x + w, py], [x + w + 20, py]], 's');
    pins[p.n] = [x + w + 20, py];
    kp.push({n: p.n, name: p.name, x: x + w + 20, y: py, dir: 'l'});
    text(x + w - 4, py + 3.5, p.name, {anchor: 'end', cls: 'pin'});
    text(x + w + 3, py - 3, p.n, {cls: 'num'});
  });
  addPart(ref, pins);
  model.push({t: 'custom', ref, sym: o.sym || value, rects: [[x, y, x + w, y + h]], pads: [], pins: kp, names: true});
  return pins;
}
// connector / terminal: pins on one side ('l' = pins point left, box to the
// right; 'r' = box to the left; 'u' = pins point up from a box below, each
// pin has its x, and xe is the y of the pin ends)
function conn(ref, value, xe, side, list, o = {}) {
  const pins = {}, kp = [], pads = [];
  let rect;
  if (side === 'u') {
    const xs = list.map(p => p.x);
    rect = [Math.min(...xs) - 14, xe + 20, Math.max(...xs) + 14, xe + 46];
    for (const p of list) {
      line([[p.x, xe], [p.x, xe + 20]], 's');
      pads.push([p.x, xe + 20]);
      pins[p.n] = [p.x, xe];
      kp.push({n: p.n, name: p.label, x: p.x, y: xe, dir: 'd'});
      text(p.x + 3, xe + 8, p.n, {cls: 'num'});
      text(p.x, xe + 38, p.label, {anchor: 'middle', cls: 'pin'});
    }
    lab(ref, 'ref', rect[2] + 6, xe + 32, ref, 'start', 'ref');
    lab(ref, 'val', rect[2] + 6, xe + 43, value, 'start', 'val');
  } else {
    const ys = list.map(p => p.y);
    const y0 = Math.min(...ys) - 14, y1 = Math.max(...ys) + 14;
    const bw = o.w || 64;
    const bx = side === 'l' ? xe + 20 : xe - 20 - bw;
    rect = [bx, y0, bx + bw, y1];
    for (const p of list) {
      const xi = side === 'l' ? xe + 20 : xe - 20;
      line([[xe, p.y], [xi, p.y]], 's');
      pads.push([xi, p.y]);
      pins[p.n] = [xe, p.y];
      kp.push({n: p.n, name: p.label, x: xe, y: p.y, dir: side === 'l' ? 'r' : 'l'});
      text(side === 'l' ? xe + 3 : xe - 3, p.y - 3, p.n, {anchor: side === 'l' ? 'start' : 'end', cls: 'num'});
      text(side === 'l' ? xi + 5 : xi - 5, p.y + 3.5, p.label, {anchor: side === 'l' ? 'start' : 'end', cls: 'pin'});
    }
    const both = (x, y, a) => { lab(ref, 'ref', x, y, ref, a, 'ref'); lab(ref, 'val', x, y + 11, value, a, 'val'); };
    if (o.lab === 'left') both(bx - 6, y0 + 10, 'end');
    else if (o.lab === 'start' || o.lab === 'end') both(o.lab === 'start' ? bx : bx + bw, o.labAbove === false ? y1 + 13 : y0 - 16, o.lab);
    else both(bx + bw / 2, o.labAbove === false ? y1 + 13 : y0 - 16, 'middle');
  }
  g.push(`<rect class="box" x="${rect[0]}" y="${rect[1]}" width="${rect[2] - rect[0]}" height="${rect[3] - rect[1]}"/>`);
  for (const [x, y] of pads) g.push(`<circle class="pad" cx="${f(x)}" cy="${f(y)}" r="2.4"/>`);
  if (o.shunt) g.push(`<rect class="shunt" x="${o.shunt[0]}" y="${o.shunt[1]}" width="${o.shunt[2] - o.shunt[0]}" height="${o.shunt[3] - o.shunt[1]}" rx="3"/>`);
  addPart(ref, pins);
  model.push({t: 'custom', ref, sym: o.sym || ref, rects: [rect], pads, pins: kp, names: true, shunt: o.shunt});
  return pins;
}
// 3-pin jumper header, pins pointing down from a box; xs = x of pins 1, 2, 3
function jumper(ref, value, xs, yEnd, shunt) {
  const x0 = Math.min(...xs) - 10, x1 = Math.max(...xs) + 10;
  const rect = [x0, yEnd - 34, x1, yEnd - 20];
  g.push(`<rect class="box" x="${x0}" y="${yEnd - 34}" width="${x1 - x0}" height="14"/>`);
  const pins = {}, kp = [], pads = [];
  xs.forEach((x, i) => {
    line([[x, yEnd - 20], [x, yEnd]], 's');
    g.push(`<circle class="pad" cx="${x}" cy="${yEnd - 27}" r="2.4"/>`);
    pads.push([x, yEnd - 27]);
    pins[String(i + 1)] = [x, yEnd];
    kp.push({n: String(i + 1), name: String(i + 1), x, y: yEnd, dir: 'u'});
    text(x + 3, yEnd - 8, String(i + 1), {cls: 'num'});
  });
  const a = xs[shunt[0] - 1], b = xs[shunt[1] - 1];
  const sh = [Math.min(a, b) - 5, yEnd - 32, Math.max(a, b) + 5, yEnd - 22];
  g.push(`<rect class="shunt" x="${sh[0]}" y="${sh[1]}" width="${sh[2] - sh[0]}" height="10" rx="3"/>`);
  addPart(ref, pins);
  model.push({t: 'custom', ref, sym: 'Jumper_3_Bridged12_Down', rects: [rect], pads, pins: kp, names: false, shunt: sh});
  lab(ref, 'ref', x1 + 6, yEnd - 30, ref, 'start', 'ref');
  lab(ref, 'val', x1 + 6, yEnd - 19, value, 'start', 'val');
}
// test point: pin at (x, y), circle towards dir
function tp(ref, value, x, y, dir) {
  const d = {u: [0, -1], d: [0, 1], l: [-1, 0], r: [1, 0]}[dir];
  const c = [x + d[0] * 13, y + d[1] * 13];
  line([[x, y], [x + d[0] * 8, y + d[1] * 8]], 's');
  g.push(`<circle class="s" cx="${f(c[0])}" cy="${f(c[1])}" r="5"/>`);
  addPart(ref, {'1': [x, y]});
  model.push({t: 'tp', ref, x, y, dir});
  if (dir === 'u') { lab(ref, 'ref', c[0] - 3, c[1] - 9, ref, 'end', 'ref'); lab(ref, 'val', c[0] + 3, c[1] - 9, value, 'start', 'ref'); }
  else if (dir === 'd') { lab(ref, 'ref', c[0] - 3, c[1] + 16, ref, 'end', 'ref'); lab(ref, 'val', c[0] + 3, c[1] + 16, value, 'start', 'ref'); }
  else if (dir === 'l') { lab(ref, 'ref', c[0] - 8, c[1] + 3.5, ref, 'end', 'ref'); fields[ref].val = null; }
  else { lab(ref, 'ref', c[0] + 8, c[1] + 3.5, ref, 'start', 'ref'); fields[ref].val = null; }
}
// power symbol: GND hangs from (x, y) in dir; supply nets draw a bar
function pwr(net, x, y, dir) {
  dir = dir || (net === 'GND' ? 'd' : 'u');
  powers.push({net, x, y});
  model.push({t: 'pwr', net, x, y, dir});
  const v = {u: [0, -1], d: [0, 1], l: [-1, 0], r: [1, 0]}[dir];
  const px = (a, b) => [x + v[0] * a - v[1] * b, y + v[1] * a + v[0] * b];  // a along, b across
  if (net === 'GND') {
    line([[x, y], px(8, 0)], 'p');
    for (const [a, b] of [[8, 8], [11.5, 5], [15, 2]]) line([px(a, -b), px(a, b)], 'p');
  } else {
    line([[x, y], px(10, 0)], 'p');
    line([px(10, -7), px(10, 7)], 'p');
    const lp = px(dir === 'u' ? 14 : dir === 'd' ? 22 : 13, 0);
    text(lp[0], lp[1] + (dir === 'l' || dir === 'r' ? 3.5 : 0), net, {anchor: dir === 'l' ? 'end' : dir === 'r' ? 'start' : 'middle', cls: 'pwr'});
  }
}
// PWR_FLAG: tells ERC that the net is supplied (from J3, the buck or the panel)
function flag(net, x, y) {
  powers.push({net, x, y});
  model.push({t: 'flag', net, x, y});
  line([[x, y], [x, y - 10]], 'p');
  g.push(`<path class="p" d="M${x} ${y - 10}l-6 -5l6 -5l6 5z"/>`);
  text(x, y - 24, 'PWR_FLAG', {anchor: 'middle', cls: 'val'});
}
function nc(ref, pin) {
  const [x, y] = P(ref, pin);
  ncs.push({ref, pin, x, y});
  line([[x - 4, y - 4], [x + 4, y + 4]], 'nc');
  line([[x - 4, y + 4], [x + 4, y - 4]], 'nc');
}
function w(net, ...pts) {
  for (let i = 1; i < pts.length; i++) {
    if (pts[i][0] !== pts[i - 1][0] && pts[i][1] !== pts[i - 1][1]) throw new Error(`diagonal wire on ${net}: ${pts[i - 1]} -> ${pts[i]}`);
  }
  wires.push({net, pts});
}
// standalone decoupling cap: supply on top, GND below
function decap(ref, value, net, x, y) {
  two(ref, 'C', x, y + 20, 'd', value);
  pwr(net, x, y);
  pwr('GND', x, y + 40);
}
function frame(x, y, w2, h2, title) {
  g.push(`<rect class="frame" x="${x}" y="${y}" width="${w2}" height="${h2}" rx="6"/>`);
  model.push({t: 'frame', x, y, w: w2, h: h2});
  text(x + 10, y + 18, title, {cls: 'title', sheet: true});
}

module.exports = {g, model, fields, parts, wires, powers, ncs, text, line, P, two, fet, ic, conn, jumper, tp, pwr, flag, nc, w, decap, frame, xf, FET_BODY};
