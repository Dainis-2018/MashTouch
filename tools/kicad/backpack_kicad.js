// MashTouch - write the backpack drawing model as a KiCad schematic.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Used by backpack_overview.js --kicad. Standard parts use the KiCad
// Device:*_Small, Connector:TestPoint and power symbols; MOSFETs, ICs,
// J8, the terminals and the jumpers get project symbols (backpack.kicad_sym)
// drawn with the same pin positions as the overview. Symbol UUIDs and fields
// come from the existing schematic, so the PCB stays linked.
'use strict';
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const S = require('./sexpr');

const K = 0.127, X0 = 25.4, Y0 = 20.32;          // px -> mm, on the 1.27 mm grid
const r4 = v => +(+v).toFixed(4);
const mx = px => r4(X0 + px * K), my = py => r4(Y0 + py * K);
const q = s => ({s: String(s)});
const uuid = () => q(crypto.randomUUID());
const font = (size, extra = []) => ['effects', ['font', ['size', size, size], ...extra]];
const hidden = size => ['effects', ['font', ['size', size, size]], ['hide', 'yes']];
const FS = 1.0;                                   // text size for fields, pins, notes

const LIB2 = {
  R: 'Device:R_Small', C: 'Device:C_Small', CP: 'Device:C_Polarized_Small', L: 'Device:L_Small',
  F: 'Device:Fuse_Small', PTC: 'Device:Polyfuse_Small', D: 'Device:D_Small', DS: 'Device:D_Schottky_Small',
  DZ: 'Device:D_Zener_Small', TVS: 'Device:D_TVS_Small', LED: 'Device:LED_Small',
};
const CUSTOM_NAME = {
  J8: 'J8_Panel_2x16', U1: 'AP63205WU', U2: 'UCC27511A', U3: 'UCC27511A', U4: 'MAX31865_CH1', U5: 'MAX31865_CH2',
  U6: 'TCA9535PWR', J1: 'Terminal_OUT1-4', J2: 'Terminal_PWR1-2', J3: 'Terminal_12V', J4: 'Terminal_RTD_1-Wire',
  J5: 'Terminal_Level', J7: 'JST_PH_Buttons', J9: 'UART_Header', JP1: 'Jumper_OUT_V', JP2: 'Jumper_3_Bridged12_Down',
  JP3: 'Jumper_3_Bridged12_Down',
};
const PREFIX = ref => ref.replace(/\d+$/, '');
// KiCad pin names have no spaces: "OUT1 +V" -> "OUT1+V", "RXD (44)" -> "RXD(44)", "GPIO11 SCK" -> "GPIO11/SCK"
const pinName = n => String(n).replace(/~(\w+)/, '~{$1}').replace(/ ([+−-])/g, '$1').replace(/ \(/g, '(').replace(/ /g, '/');

// rotate a screen vector (y down) counter-clockwise by ang degrees, after an optional x mirror
function tf(v, ang, mir) {
  let [x, y] = v;
  if (mir) x = -x;
  const a = ang * Math.PI / 180, c = Math.round(Math.cos(a)), s = Math.round(Math.sin(a));
  return [x * c + y * s, -x * s + y * c];
}
// find angle/mirror that puts the symbol's pins (library coords, y up) on the wanted points (mm)
function solve(symPins, want, allowMirror) {
  for (const mir of allowMirror ? [false, true] : [false]) {
    for (const ang of [0, 90, 180, 270]) {
      const first = symPins[0];
      const v0 = tf([first.x, -first.y], ang, mir);
      const at = [r4(want[first.num][0] - v0[0]), r4(want[first.num][1] - v0[1])];
      const ok = symPins.every(p => {
        const v = tf([p.x, -p.y], ang, mir);
        return Math.abs(at[0] + v[0] - want[p.num][0]) < 1e-3 && Math.abs(at[1] + v[1] - want[p.num][1]) < 1e-3;
      });
      if (ok) return {at, ang, mir};
    }
  }
  return null;
}

function onSeg(p, a, b) {
  return (a[0] === b[0] && p[0] === a[0] && p[1] >= Math.min(a[1], b[1]) && p[1] <= Math.max(a[1], b[1])) ||
         (a[1] === b[1] && p[1] === a[1] && p[0] >= Math.min(a[0], b[0]) && p[0] <= Math.max(a[0], b[0]));
}

// ------------------------------------------------------------ custom symbols
const stroke = (w, type = 'default') => ['stroke', ['width', w], ['type', type]];
function symDef(name, prefix, graphics, pins, o = {}) {
  return ['symbol', q(name),
    ...(o.hideNums ? [['pin_numbers', ['hide', 'yes']]] : []),
    ['pin_names', ['offset', 0.508], ...(o.hideNames ? [['hide', 'yes']] : [])],
    ['exclude_from_sim', 'no'], ['in_bom', 'yes'], ['on_board', 'yes'],
    ['property', q('Reference'), q(prefix), ['at', 0, 2.54, 0], font(FS)],
    ['property', q('Value'), q(name), ['at', 0, 0, 0], font(FS)],
    ['property', q('Footprint'), q(''), ['at', 0, 0, 0], hidden(1.27)],
    ['property', q('Datasheet'), q(''), ['at', 0, 0, 0], hidden(1.27)],
    ['property', q('Description'), q(o.descr || ''), ['at', 0, 0, 0], hidden(1.27)],
    ['symbol', q(name + '_0_1'), ...graphics],
    ['symbol', q(name + '_1_1'), ...pins.map(p => ['pin', 'passive', 'line', ['at', r4(p.x), r4(p.y), p.ang], ['length', 2.54],
      ['name', q(pinName(p.name)), font(FS)], ['number', q(p.n), font(FS)]])],
    ['embedded_fonts', 'no']];
}
const ANG = {r: 0, l: 180, u: 90, d: 270};
function customFromModel(m) {
  // anchor at the top-left of the first rectangle, on the grid
  const ax = m.rects[0][0] - (m.rects[0][0] % 10), ay = m.rects[0][1] - (m.rects[0][1] % 10);
  const sx = px => r4((px - ax) * K), sy = py => r4(-(py - ay) * K);
  const gr = m.rects.map(r => ['rectangle', ['start', sx(r[0]), sy(r[1])], ['end', sx(r[2]), sy(r[3])], stroke(0.254), ['fill', ['type', 'background']]]);
  for (const [x, y] of m.pads || []) gr.push(['circle', ['center', sx(x), sy(y)], ['radius', 0.3], stroke(0.15), ['fill', ['type', 'none']]]);
  if (m.shunt) gr.push(['rectangle', ['start', sx(m.shunt[0]), sy(m.shunt[1])], ['end', sx(m.shunt[2]), sy(m.shunt[3])], stroke(0.1), ['fill', ['type', 'outline']]]);
  const pins = m.pins.map(p => ({n: p.n, name: m.names ? p.name : '', x: sx(p.x), y: sy(p.y), ang: ANG[p.dir]}));
  return {ax, ay, gr, pins, sig: JSON.stringify([gr, pins])};
}
function fetDef(name, ch, pins) {
  const sx = x => r4(x * K), sy = y => r4(-y * K);
  const pl = pts => ['polyline', ['pts', ...pts.map(([x, y]) => ['xy', sx(x), sy(y)])], stroke(0.2), ['fill', ['type', 'none']]];
  const D = require('./backpack_draw');
  const gr = [['circle', ['center', sx(-3), 0], ['radius', r4(15 * K)], stroke(0.2), ['fill', ['type', 'background']]],
    ...D.FET_BODY.map(pl),
    ['polyline', ['pts', ...(ch === 'n' ? [[-5, 0], [0, -3], [0, 3], [-5, 0]] : [[0, 0], [-5, -3], [-5, 3], [0, 0]]).map(([x, y]) => ['xy', sx(x), sy(y)])],
      stroke(0.15), ['fill', ['type', 'outline']]]];
  const pp = [{n: pins.G, name: 'G', x: sx(-30), y: 0, ang: 0}, {n: pins.D, name: 'D', x: 0, y: sy(-30), ang: 270}, {n: pins.S, name: 'S', x: 0, y: sy(30), ang: 90}];
  return symDef(name, 'Q', gr, pp, {hideNames: true, hideNums: true, descr: (ch === 'n' ? 'N' : 'P') + '-channel MOSFET'});
}

// ---------------------------------------------------------------- the sheet
function emit(D, opt) {
  const old = S.parse(fs.readFileSync(opt.sch, 'utf8'));
  const ROOT = S.str(S.find(old, 'uuid')[1]);
  const oldSym = {};
  for (const x of S.findAll(old, 'symbol')) {
    const ref = S.str(x.find(p => S.head(p) === 'property' && S.str(p[1]) === 'Reference')[2]);
    if (!ref.startsWith('#')) oldSym[ref] = x;
  }
  const libSyms = new Map();           // lib_id -> definition (for lib_symbols)
  const custom = new Map();            // name -> {def, sig}
  const items = [];
  const pinPoints = [];                // [x, y] mm of every pin, for splitting wires
  let pwrN = 0, flgN = 0;

  const useLib = id => { if (!libSyms.has(id)) libSyms.set(id, S.symbol(id)); return libSyms.get(id); };
  const useCustom = (name, def, sig) => {
    const c = custom.get(name);
    if (c && c.sig !== sig) throw new Error('custom symbol ' + name + ' drawn two ways');
    if (!c) custom.set(name, {def, sig});
    const d = JSON.parse(JSON.stringify(def));
    d[1] = q('backpack:' + name);
    libSyms.set('backpack:' + name, d);
  };
  const fieldAt = (ref, which, symAng, size) => {
    const fp = D.fields[ref] && D.fields[ref][which];
    if (!fp) return null;
    const [x, y, anchor] = fp;
    const txt = which === 'ref' ? ref : null;
    return {x, y, anchor, symAng, txt};
  };
  // a visible field: centred text (KiCad flips left/right justification on
  // rotated or mirrored symbols, centred text stays put)
  const visField = (name, value, fp, len, ang) => {
    const cw = 6.3;                    // px per character at 1.0 mm
    let cx = fp.x;
    if (fp.anchor === 'start') cx = fp.x + len * cw / 2;
    if (fp.anchor === 'end') cx = fp.x - len * cw / 2;
    return ['property', q(name), q(value), ['at', mx(cx), my(fp.y - 3.5), ang % 180 === 90 ? 90 : 0], font(FS)];
  };
  function placeSymbol(libId, ref, at, ang, mir, pinNums, o = {}) {
    const os = oldSym[ref];
    if (!os && !ref.startsWith('#')) throw new Error('part not in the old schematic: ' + ref);
    const props = [];
    const oldProps = os ? S.findAll(os, 'property') : [];
    const get = k => { const p = oldProps.find(p => S.str(p[1]) === k); return p ? S.str(p[2]) : undefined; };
    const value = o.value !== undefined ? o.value : get('Value');
    const fr = o.refField || (D.fields[ref] && D.fields[ref].ref ? {x: D.fields[ref].ref[0], y: D.fields[ref].ref[1], anchor: D.fields[ref].ref[2]} : null);
    const fv = o.valField !== undefined ? o.valField : (D.fields[ref] && D.fields[ref].val ? {x: D.fields[ref].val[0], y: D.fields[ref].val[1], anchor: D.fields[ref].val[2]} : null);
    props.push(fr && !o.hideRef ? visField('Reference', ref, fr, ref.length, ang) : ['property', q('Reference'), q(ref), ['at', at[0], at[1], 0], hidden(FS)]);
    props.push(fv ? visField('Value', value, fv, String(value).length, ang) : ['property', q('Value'), q(value), ['at', at[0], at[1], 0], hidden(FS)]);
    const keep = os ? oldProps.filter(p => !['Reference', 'Value'].includes(S.str(p[1]))) : [
      ['property', q('Footprint'), q(''), ['at', 0, 0, 0], hidden(1.27)], ['property', q('Datasheet'), q(''), ['at', 0, 0, 0], hidden(1.27)]];
    for (const p of keep) props.push(['property', p[1], p[2], ['at', at[0], at[1], 0], hidden(1.27)]);
    const flags = k => { const e = os && S.find(os, k); return e ? e[1] : (k === 'exclude_from_sim' || k === 'dnp' ? 'no' : (ref.startsWith('#') ? 'no' : 'yes')); };
    items.push(['symbol', ['lib_id', q(libId)], ['at', at[0], at[1], ang], ...(mir ? [['mirror', 'y']] : []), ['unit', 1],
      ['exclude_from_sim', flags('exclude_from_sim')], ['in_bom', flags('in_bom')], ['on_board', flags('on_board')], ['dnp', flags('dnp')],
      ['uuid', os ? S.find(os, 'uuid')[1] : uuid()], ...props,
      ...pinNums.map(n => ['pin', q(n), ['uuid', uuid()]]),
      ['instances', ['project', q('backpack'), ['path', q('/' + ROOT), ['reference', q(ref)], ['unit', 1]]]]]);
  }

  for (const m of D.model) {
    if (m.t === 'two') {
      const id = LIB2[m.kind];
      const sp = S.pins(useLib(id));
      const want = {};
      for (const p of sp) {
        const pt = D.parts[m.ref].pins[p.num];
        if (!pt) throw new Error(`${m.ref}: no pin ${p.num}`);
        want[p.num] = [mx(pt[0]), my(pt[1])];
      }
      const s = solve(sp, want, false);
      if (!s) throw new Error('no orientation for ' + m.ref);
      placeSymbol(id, m.ref, s.at, s.ang, false, sp.map(p => p.num));
      Object.values(want).forEach(p => pinPoints.push(p));
    } else if (m.t === 'fet') {
      const order = ['G', 'D', 'S'].sort((a, b) => m.pins[a] - m.pins[b]).join('');
      const name = (m.ch === 'n' ? 'NMOS_' : 'PMOS_') + order;
      const def = fetDef(name, m.ch, m.pins);
      useCustom(name, def, JSON.stringify(def));
      const sp = S.pins(def);
      const want = {};
      for (const p of sp) { const pt = D.parts[m.ref].pins[p.num]; want[p.num] = [mx(pt[0]), my(pt[1])]; }
      const s = solve(sp, want, true);
      if (!s) throw new Error('no orientation for ' + m.ref);
      placeSymbol('backpack:' + name, m.ref, s.at, s.ang, s.mir, sp.map(p => p.num));
      Object.values(want).forEach(p => pinPoints.push(p));
    } else if (m.t === 'custom') {
      const name = CUSTOM_NAME[m.ref];
      if (!name) throw new Error('no symbol name for ' + m.ref);
      const c = customFromModel(m);
      const def = symDef(name, PREFIX(m.ref), c.gr, c.pins, {hideNames: !m.names});
      useCustom(name, def, c.sig);
      placeSymbol('backpack:' + name, m.ref, [mx(c.ax), my(c.ay)], 0, false, c.pins.map(p => p.n));
      m.pins.forEach(p => pinPoints.push([mx(p.x), my(p.y)]));
    } else if (m.t === 'tp') {
      const ang = {u: 0, d: 180, l: 90, r: 270}[m.dir];
      useLib('Connector:TestPoint');
      placeSymbol('Connector:TestPoint', m.ref, [mx(m.x), my(m.y)], ang, false, ['1']);
      pinPoints.push([mx(m.x), my(m.y)]);
    } else if (m.t === 'hole') {
      useLib('Mechanical:MountingHole');
      placeSymbol('Mechanical:MountingHole', m.ref, [mx(m.x), my(m.y)], 0, false, [],
        {refField: {x: m.x, y: m.y - 12, anchor: 'middle'}, valField: null});
    } else if (m.t === 'pwr' || m.t === 'flag') {
      const id = m.t === 'flag' ? 'power:PWR_FLAG' : 'power:' + m.net;
      useLib(id);
      const gnd = m.net === 'GND';
      const ang = m.t === 'flag' ? 0 : gnd ? {d: 0, l: 270, r: 90, u: 180}[m.dir] : {u: 0, d: 180, l: 90, r: 270}[m.dir];
      const ref = m.t === 'flag' ? '#FLG' + String(++flgN).padStart(3, '0') : '#PWR' + String(++pwrN).padStart(3, '0');
      let valField = null;
      if (m.t === 'flag') valField = {x: m.x, y: m.y - 24, anchor: 'middle'};
      else if (!gnd) {
        const v = {u: [0, -14], d: [0, 22], l: [-13, 3.5], r: [13, 3.5]}[m.dir];
        valField = {x: m.x + v[0], y: m.y + v[1], anchor: m.dir === 'l' ? 'end' : m.dir === 'r' ? 'start' : 'middle'};
      }
      placeSymbol(id, ref, [mx(m.x), my(m.y)], ang, false, ['1'], {value: m.t === 'flag' ? 'PWR_FLAG' : m.net, hideRef: true, valField});
      pinPoints.push([mx(m.x), my(m.y)]);
    } else if (m.t === 'frame') {
      items.push(['rectangle', ['start', mx(m.x), my(m.y)], ['end', mx(m.x + m.w), my(m.y + m.h)],
        ['stroke', ['width', 0.15], ['type', 'dash'], ['color', 132, 132, 132, 1]], ['fill', ['type', 'none']], ['uuid', uuid()]]);
    } else if (m.t === 'text') {
      const size = {title: 1.5, head: 2.2, sub: 1.27}[m.cls] || FS;
      const bold = ['title', 'head', 'sub'].includes(m.cls) ? [['bold', 'yes']] : [];
      const just = {start: 'left', middle: null, end: 'right'}[m.anchor];
      items.push(['text', q(m.s), ['exclude_from_sim', 'no'], ['at', mx(m.x), my(m.y), 0],
        ['effects', ['font', ['size', size, size], ...bold], ['justify', ...(just ? [just] : []), 'bottom']], ['uuid', uuid()]]);
    }
  }
  for (const n of D.ncs) items.push(['no_connect', ['at', mx(n.x), my(n.y)], ['uuid', uuid()]]);

  // net labels: one per named signal net, on its longest horizontal wire
  const labels = [];
  const byNet = {};
  D.wires.forEach(wr => (byNet[wr.net] = byNet[wr.net] || []).push(wr));
  // a label point must not touch anything of another net (it would join them)
  const busy = (p, net) => D.wires.some(wr => wr.net !== net && wr.pts.some((a, i) => i && onSeg(p, wr.pts[i - 1], a))) ||
    Object.values(D.parts).some(pt => Object.values(pt.pins).some(q2 => q2[0] === p[0] && q2[1] === p[1]));
  const boxes = [];                    // placed label text boxes, px
  const hit = b => boxes.some(o => b[0] < o[2] && o[0] < b[2] && b[1] < o[3] && o[1] < b[3]);
  for (const [net, ws] of Object.entries(byNet)) {
    if (['+12V', '+5V', '+3V3', 'GND'].includes(net)) continue;
    const L = net.length * 5.8 + 2;    // text length, px
    let best = null;
    for (const wr of ws) for (let i = 1; i < wr.pts.length; i++) {
      const [a, b] = [wr.pts[i - 1], wr.pts[i]];
      const horiz = a[1] === b[1];
      const along = horiz ? 0 : 1;
      const lo = Math.min(a[along], b[along]), hi = Math.max(a[along], b[along]);
      for (let v = lo; v <= hi; v += 10) {
        // horizontal text runs right from the point, vertical text runs up
        const p = horiz ? [v, a[1]] : [a[0], hi - (v - lo)];
        const end = v === lo || v === hi;
        if (D.wires.some(o => o.net !== net && o.pts.some((c, k) => k && onSeg(p, o.pts[k - 1], c)))) continue;
        if (!end && Object.values(D.parts).some(pt => Object.values(pt.pins).some(q2 => q2[0] === p[0] && q2[1] === p[1]))) continue;
        const box = horiz ? [p[0], p[1] - 13, p[0] + L, p[1] + 1] : [p[0] - 13, p[1] - L, p[0] + 1, p[1]];
        if (hit(box)) continue;
        const fits = (horiz ? p[0] + L <= hi : p[1] - L >= lo) ? 2000 : 0;
        const score = fits + (horiz ? 1000 : 0) + Math.min(hi - lo, 300) - (end ? 50 : 0);
        if (!best || score > best.score) best = {p, horiz, score, box};
      }
    }
    if (!best) throw new Error('no place for the label of ' + net);
    boxes.push(best.box);
    const at = best.p, ang = best.horiz ? 0 : 90;
    labels.push({net, at, ang});
    items.push(['label', q(net), ['at', mx(at[0]), my(at[1]), ang], ['fields_autoplaced', 'yes'],
      ['effects', ['font', ['size', 0.9, 0.9]], ['justify', 'left', 'bottom']], ['uuid', uuid()]]);
  }

  // wires: split every segment where a pin, label, flag or another wire's end touches it
  const pts = [...pinPoints, ...labels.map(l => [mx(l.at[0]), my(l.at[1])])];
  const segs = [];
  for (const wr of D.wires) for (let i = 1; i < wr.pts.length; i++) segs.push([[mx(wr.pts[i - 1][0]), my(wr.pts[i - 1][1])], [mx(wr.pts[i][0]), my(wr.pts[i][1])]]);
  segs.forEach(s => pts.push(s[0], s[1]));
  const key = p => p[0].toFixed(3) + ',' + p[1].toFixed(3);
  const uniq = [...new Map(pts.map(p => [key(p), p])).values()];
  const pieces = [];
  for (const [a, b] of segs) {
    const along = a[0] === b[0] ? 1 : 0;
    const lo = Math.min(a[along], b[along]), hi = Math.max(a[along], b[along]);
    const cuts = uniq.filter(p => p[1 - along] === a[1 - along] && p[along] > lo + 1e-6 && p[along] < hi - 1e-6).map(p => p[along]);
    const stops = [lo, ...cuts.sort((x, y) => x - y), hi];
    for (let i = 1; i < stops.length; i++) {
      const p1 = along ? [a[0], stops[i - 1]] : [stops[i - 1], a[1]];
      const p2 = along ? [a[0], stops[i]] : [stops[i], a[1]];
      pieces.push([p1, p2]);
    }
  }
  for (const [p1, p2] of pieces) items.push(['wire', ['pts', ['xy', r4(p1[0]), r4(p1[1])], ['xy', r4(p2[0]), r4(p2[1])]], ['stroke', ['width', 0], ['type', 'default']], ['uuid', uuid()]]);
  const deg = new Map();
  const bump = (p, n = 1) => deg.set(key(p), (deg.get(key(p)) || 0) + n);
  pieces.forEach(([p1, p2]) => { bump(p1); bump(p2); });
  pinPoints.forEach(p => { if (deg.has(key(p))) bump(p); });
  for (const [k, n] of deg) if (n >= 3) {
    const [x, y] = k.split(',').map(Number);
    items.push(['junction', ['at', x, y], ['diameter', 0], ['color', 0, 0, 0, 0], ['uuid', uuid()]]);
  }

  // header texts
  items.unshift(
    ['text', q('MashTouch I/O backpack: MCU-less board on J8 of the Waveshare ESP32-S3-Touch-LCD-3.5'), ['exclude_from_sim', 'no'], ['at', mx(20), my(34), 0],
      ['effects', ['font', ['size', 2.2, 2.2], ['bold', 'yes']], ['justify', 'left', 'bottom']], ['uuid', uuid()]],
    ['text', q('J8 pin numbers follow the Waveshare pinout image (pin 1 BAT at the USB-C end, odd pins on the outer row). Power nets are symbols; each net label names the wire it sits on.'),
      ['exclude_from_sim', 'no'], ['at', mx(20), my(54), 0], ['effects', ['font', ['size', FS, FS]], ['justify', 'left', 'bottom']], ['uuid', uuid()]]);

  const sch = ['kicad_sch', S.find(old, 'version'), S.find(old, 'generator'), S.find(old, 'generator_version'), ['uuid', q(ROOT)],
    ['paper', q('A3')], S.find(old, 'title_block'), ['lib_symbols', ...libSyms.values()], ...items,
    ['sheet_instances', ['path', q('/'), ['page', q('1')]]], ['embedded_fonts', 'no']];
  fs.writeFileSync(opt.sch, S.write(sch) + '\n');

  // project symbol library and its table entry
  const libHead = S.parse(fs.readFileSync(path.join(S.LIB_DIR || 'C:/Program Files/KiCad/10.0/share/kicad/symbols', 'Device.kicad_sym'), 'utf8'));
  const lib = ['kicad_symbol_lib', S.find(libHead, 'version'), ['generator', q('kicad_symbol_editor')], S.find(libHead, 'generator_version'),
    ...[...custom.values()].map(c => c.def)];
  fs.writeFileSync(path.join(path.dirname(opt.sch), 'backpack.kicad_sym'), S.write(lib) + '\n');
  const tbl = path.join(path.dirname(opt.sch), 'sym-lib-table');
  fs.writeFileSync(tbl, '(sym_lib_table\n\t(version 7)\n\t(lib (name "backpack")(type "KiCad")(uri "${KIPRJMOD}/backpack.kicad_sym")(options "")(descr "MashTouch backpack symbols"))\n)\n');
  return {symbols: items.filter(i => i[0] === 'symbol').length, custom: custom.size, wires: pieces.length, labels: labels.length};
}

module.exports = {emit};
