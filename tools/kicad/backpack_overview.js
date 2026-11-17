// MashTouch - layout of the wired one-page backpack schematic (bootstrap).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Places every part of hardware/backpack with real wires (power nets as
// symbols), J8 in the middle, and checks every pin and wire against a KiCad
// netlist. It made the wired backpack.kicad_sch on 2026-11-17 (--kicad, via
// backpack_kicad.js, keeping symbol UUIDs and fields from the sheet it
// replaced). The KiCad schematic is the design source now: edit it in KiCad,
// check edits with tools/kicad/netcheck.js, and don't rerun this with --kicad
// (it would overwrite hand edits).
//
//   node tools/kicad/backpack_overview.js <netlist> [preview.svg] [--kicad]
'use strict';
const fs = require('fs');
const path = require('path');

const ROOT = path.join(__dirname, '..', '..');
const ARGS = process.argv.slice(2).filter(a => !a.startsWith('--'));
const KICAD = process.argv.includes('--kicad');
const NETLIST = ARGS[0] || path.join(ROOT, 'hardware/backpack/backpack.net');
const OUT = ARGS[1];   // optional SVG preview
const W = 2040, H = 1870;

const D = require('./backpack_draw');
const {g, model, fields, parts, wires, powers, ncs, text, P, two, fet, ic, conn, jumper, tp, pwr, flag, nc, w, decap, frame} = D;
const line = D.line;

// ===================================================================== power
frame(20, 70, 1250, 210, '12 V input, reverse-polarity protection, 12 → 5 V buck');
{
  const R = 140;
  conn('J3', '12 V in', 100, 'r', [{n: '2', label: '+12 V', y: R}, {n: '1', label: 'GND', y: R + 40}], {w: 50});
  pwr('GND', 100, R + 40);
  two('F1', 'F', 150, R, 'r', '7.5 A mini blade');
  w('+12V_IN', [100, R], P('F1', '1'));
  fet('Q1', 'p', 230, R, -90, false, 'AOD4185', {G: '1', D: '2', S: '3'}, 'r');
  w('+12V_F', P('F1', '2'), P('Q1', '2'));
  w('+12V', P('Q1', '3'), [660, R]);
  two('D1', 'DZ', 300, R + 50, 'u', 'BZT52C15', {pins: ['2', '1'], lab: 'l'});
  w('+12V', [300, R], P('D1', '1'));
  two('R1', 'R', 230, R + 110, 'd', '10k', {lab: 'l'});
  w('REVP_G', P('Q1', '1'), P('R1', '1'));
  w('REVP_G', P('D1', '2'), [230, P('D1', '2')[1]]);
  pwr('GND', ...P('R1', '2'));
  two('D2', 'TVS', 360, R + 50, 'd', 'SMAJ15CA');
  w('+12V', [360, R], P('D2', '1'));
  pwr('GND', ...P('D2', '2'));
  two('C4', 'CP', 430, R + 50, 'd', '100u 25V');
  w('+12V', [430, R], P('C4', '1'));
  pwr('GND', ...P('C4', '2'));
  tp('TP1', '12V', 490, R, 'u');
  two('C5', 'C', 540, R + 50, 'd', '10u 25V');
  w('+12V', [540, R], P('C5', '1'));
  pwr('GND', ...P('C5', '2'));
  two('C6', 'C', 600, R + 50, 'd', '100n');
  w('+12V', [600, R], P('C6', '1'));
  pwr('GND', ...P('C6', '2'));
  pwr('+12V', 400, R);
  ic('U1', 'AP63205WU', 680, R - 20, 100,
    [{n: '3', name: 'IN'}, {n: '2', name: 'EN'}, {n: '4', name: 'GND'}],
    [{n: '6', name: 'BST'}, {n: '5', name: 'SW'}, {n: '1', name: 'FB'}]);
  w('+12V', P('U1', '2'), [640, R + 20], [640, R]);
  pwr('GND', ...P('U1', '4'));
  two('C7', 'C', 840, R - 20, 'r', '100n', {lab: 'a'});
  w('BUCK_BST', P('U1', '6'), [810, R], [810, R - 20], P('C7', '1'));
  w('BUCK_SW', P('U1', '5'), [880, R + 20]);
  w('BUCK_SW', P('C7', '2'), [870, R - 20], [870, R + 20]);
  two('L1', 'L', 900, R + 20, 'r', '4.7u', {lab: 'a'});
  w('+5V', P('L1', '2'), [1220, R + 20], [1220, 500], [1200, 500]);
  w('+5V', P('U1', '1'), [940, R + 40], [940, R + 20]);
  for (const [ref, x, v] of [['C8', 990, '22u'], ['C9', 1040, '22u']]) {
    two(ref, 'C', x, R + 60, 'd', v);
    w('+5V', [x, R + 20], P(ref, '1'));
    pwr('GND', ...P(ref, '2'));
  }
  two('R2', 'R', 1100, R + 60, 'd', '1k5');
  w('+5V', [1100, R + 20], P('R2', '1'));
  two('D3', 'LED', 1100, R + 110, 'd', 'green', {pins: ['2', '1']});
  w('LED_5V', P('R2', '2'), P('D3', '2'));
  pwr('GND', ...P('D3', '1'));
  tp('TP2', '5V', 1160, R + 20, 'u');
  pwr('+5V', 1200, R + 20);
}

// ===================================================================== J8
const J8X = 1020, J8Y = 480;
{
  const names = {
    1: 'BAT', 2: '5V', 3: 'GND', 4: 'GND', 5: 'GPIO21', 6: 'D− (19)', 7: 'GPIO38', 8: 'D+ (20)', 9: 'GPIO39', 10: 'GPIO11 SCK',
    11: 'GPIO40', 12: 'GPIO10 MOSI', 13: 'GPIO41', 14: 'GPIO9 MISO', 15: 'GPIO42', 16: 'GPIO17 1-Wire', 17: 'GPIO45', 18: 'GPIO18 level',
    19: 'GPIO46', 20: 'BOOT (IO0)', 21: 'GPIO47 CS1', 22: 'RST (EN)', 23: 'GPIO48 CS2', 24: 'PWR', 25: 'RXD (44)', 26: 'SCL (7)',
    27: 'TXD (43)', 28: 'SDA (8)', 29: 'GND', 30: 'GND', 31: '3V3', 32: '3V3'};
  const left = [], right = [];
  for (let n = 1; n <= 32; n++) (n % 2 ? left : right).push({n: String(n), name: names[n]});
  ic('J8', 'to the panel (2×16, 2.54 mm)', J8X, J8Y, 160, left, right);
  text(J8X + 80, J8Y - 32, 'J8 GPIO header', {anchor: 'middle', cls: 'title', sheet: true});
}
const J = n => P('J8', String(n));
nc('J8', '1'); nc('J8', '6'); nc('J8', '8'); nc('J8', '24');
pwr('GND', ...J(3), 'l');
pwr('GND', ...J(29), 'l');
pwr('+3V3', ...J(31), 'l');
pwr('GND', ...J(4), 'r');
pwr('GND', ...J(30), 'r');
pwr('+3V3', ...J(32), 'r');
// test points on short wires, so the nets keep their names
for (const [ref, v, pin, net, dx] of [['TP5', 'GPIO45', 17, 'GPIO45', -40], ['TP6', 'GPIO46', 19, 'GPIO46', -40],
  ['TP7', 'BOOT', 20, 'ESP_BOOT', 10], ['TP8', 'RST', 22, 'ESP_RST', 10]]) {
  const [x, y] = J(pin);
  w(net, [x, y], [x + dx, y]);
  tp(ref, v, x + dx, y, dx < 0 ? 'l' : 'r');
}

// ============================================================== outputs
frame(20, 290, 960, 1110, 'Outputs: OUT1–4 and PWR1–2');
const term = {J1: [], J2: []};
const lanesL = {PWR2: 850, OUT1: 860, OUT2: 870, OUT3: 880, OUT4: 890, PWR1: 900};
const ledBranch = (sig, ys, dref, rref) => {
  const y2 = ys + 40;
  two(dref, 'LED', 810, y2, 'l', 'yellow', {pins: ['2', '1'], lab: 'b'});
  two(rref, 'R', 760, y2, 'l', '1k', {lab: 'b'});
  w(sig, [830, ys], P(dref, '2'));
  w(sig.replace(/$/, '') + '_LK', P(dref, '1'), P(rref, '1'));
  pwr('GND', ...P(rref, '2'));
};
const outCell = (n, yc, pin, refs) => {
  const sig = 'OUT' + n, yv = yc + 10, yl = yc + 50, ys = yc + 80;
  const [rg, rpd, q, df, dl, rl] = refs;
  text(150, yc + 30, `OUT${n}  (GPIO ${37 + n})`, {cls: 'sub', sheet: true});
  two(rg, 'R', 680, ys, 'l', '100');
  two(rpd, 'R', 640, ys + 40, 'd', '10k');
  pwr('GND', ...P(rpd, '2'));
  fet(q, 'n', 580, ys, 0, true, 'AO3400A', {G: '1', S: '2', D: '3'}, 'l');
  pwr('GND', ...P(q, '2'));
  w(sig + '_G', P(rg, '2'), P(q, '1'));
  w(sig + '_G', [640, ys], P(rpd, '1'));
  two(df, 'DS', 500, yc + 30, 'u', 'B5819W', {pins: ['2', '1']});
  w(sig + '_L', P(q, '3'), [100, yl]);
  w('OUT_V', P(df, '1'), [440, yv]);
  w('OUT_V', [440, yv], [100, yv]);
  ledBranch(sig, ys, dl, rl);
  const lane = lanesL[sig];
  w(sig, J(pin), [lane, J(pin)[1]], [lane, ys], P(rg, '1'));
  term.J1.push({y: yv, label: `OUT${n} +V`}, {y: yl, label: `OUT${n} −`});
};
const pwrCell = (n, yc, pin, refs) => {
  const sig = 'PWR' + n, yp = yc + 10, yl = yc + 50, ys = yc + 100;
  const [u, rpd, rh, rgpd, q, df, ptc, dl, rl] = refs;
  text(150, yc + 30, `PWR${n}  (GPIO ${n === 1 ? 42 : 21})`, {cls: 'sub', sheet: true});
  ic(u, 'UCC27511A', 540, ys - 80, 100,
    [null, null, {n: '2', name: 'OUTH'}, {n: '3', name: 'OUTL'}],
    [{n: '1', name: 'VDD'}, null, null, {n: '6', name: 'IN+'}, {n: '5', name: 'IN−'}, {n: '4', name: 'GND'}]);
  w('+12V', P(u, '1'), [680, ys - 60]);
  pwr('+12V', 680, ys - 60);
  w('GND', P(u, '5'), [670, ys + 20], [670, ys + 60]);
  w('GND', P(u, '4'), [670, ys + 40]);
  pwr('GND', 670, ys + 60);
  two(rpd, 'R', 700, ys + 40, 'd', '100k');
  pwr('GND', ...P(rpd, '2'));
  w(sig, [700, ys], P(rpd, '1'));
  two(rh, 'R', 490, ys - 20, 'l', '10');
  w(sig + '_OH', P(u, '2'), P(rh, '1'));
  fet(q, 'n', 380, ys - 20, 0, true, 'AOD4184A', {G: '1', D: '2', S: '3'}, 'l');
  pwr('GND', ...P(q, '3'));
  w(sig + '_G', P(rh, '2'), P(q, '1'));
  w(sig + '_G', P(u, '3'), [460, ys], [460, ys - 20]);
  two(rgpd, 'R', 440, ys + 20, 'd', '10k');
  w(sig + '_G', [440, ys - 20], P(rgpd, '1'));
  pwr('GND', ...P(rgpd, '2'));
  two(df, 'DS', 320, yc + 30, 'u', 'SS34', {pins: ['2', '1']});
  w(sig + '_L', P(q, '2'), [100, yl]);
  two(ptc, 'PTC', 360, yp, 'l', '3 A hold', {lab: 'a'});
  w(sig + '_P', P(ptc, '2'), [100, yp]);
  w('+12V', P(ptc, '1'), [400, yp]);
  pwr('+12V', 400, yp);
  ledBranch(sig, ys, dl, rl);
  const lane = lanesL[sig];
  w(sig, J(pin), [lane, J(pin)[1]], [lane, ys], P(u, '6'));
  term.J2.push({y: yp, label: `PWR${n} +12V`}, {y: yl, label: `PWR${n} −`});
};
outCell(1, 380, 7, ['R3', 'R4', 'Q2', 'D4', 'D5', 'R5']);
outCell(2, 530, 9, ['R6', 'R7', 'Q3', 'D6', 'D7', 'R8']);
outCell(3, 680, 11, ['R9', 'R10', 'Q4', 'D8', 'D9', 'R11']);
outCell(4, 830, 13, ['R12', 'R13', 'Q5', 'D10', 'D11', 'R14']);
pwrCell(1, 990, 15, ['U2', 'R15', 'R16', 'R17', 'Q6', 'D12', 'F2', 'D13', 'R18']);
pwrCell(2, 1180, 5, ['U3', 'R19', 'R20', 'R21', 'Q7', 'D14', 'F3', 'D15', 'R22']);
{
  // OUT +V rail and its select jumper
  const yv1 = 390;
  w('OUT_V', [440, yv1 - 60], [440, 830 + 10]);
  conn('JP1', 'OUT +V select', 380, 'r', [{n: '1', label: '12V', y: yv1 - 80}, {n: '2', label: 'OUT_V', y: yv1 - 60}, {n: '3', label: '5V', y: yv1 - 40}],
    {w: 46, lab: 'left', shunt: [380 - 20 - 46 + 4, yv1 - 85, 380 - 20 - 46 + 12, yv1 - 55]});
  w('OUT_V', P('JP1', '2'), [440, yv1 - 60]);
  pwr('+12V', ...P('JP1', '1'), 'u');
  pwr('+5V', ...P('JP1', '3'), 'd');
  text(470, yv1 - 66, 'shunt 1-2 = 12 V, 2-3 = 5 V', {cls: 'note', sheet: true});
  text(470, yv1 - 52, 'OUT1-4: SSR or relay, switched to GND. PWR1-2: 12 V, 3 A, PWM (fan, pump).', {cls: 'note', sheet: true});
  // terminals, numbered as on the board (labels read left to right on the top edge)
  const j1n = ['8', '7', '6', '5', '4', '3', '2', '1'];
  conn('J1', 'OUT1-4 (3.5 mm)', 100, 'r', term.J1.map((p, i) => Object.assign({n: j1n[i]}, p)), {w: 70, lab: 'start'});
  const j2n = ['4', '3', '2', '1'];
  conn('J2', 'PWR1-2 (3.5 mm)', 100, 'r', term.J2.map((p, i) => Object.assign({n: j2n[i]}, p)), {w: 70, labAbove: false, lab: 'start'});
}

// ===================================================================== RTD
frame(1290, 300, 690, 760, 'PT100 / PT1000: 2× MAX31865 on SPI3');
const SPI = {SCK: 1310, MOSI: 1320, MISO: 1330};
const PE = 1570, J4X = 1760;
const j4 = [];
function rtd(ch, y0, u, rref, cf, jp, rcs, dc1, dc2, csLane) {
  const n = 'RTD' + ch;
  const sp = ch === 1
    ? [{n: '12', name: 'SCLK'}, {n: '11', name: 'SDI'}, {n: '14', name: 'SDO'}]
    : [{n: '14', name: 'SDO'}, {n: '11', name: 'SDI'}, {n: '12', name: 'SCLK'}];
  ic(u, 'MAX31865ATP+', 1430, y0 - 20, 120,
    [...sp, {n: '13', name: '~CS'}, null, {n: '10', name: 'GND'}, {n: '15', name: 'DGND'}, {n: '16', name: 'GND'}, {n: '21', name: 'EP'}],
    [{n: '6', name: 'FORCE2'}, {n: '5', name: 'FORCE+'}, {n: '7', name: 'RTDIN+'}, null, {n: '8', name: 'RTDIN−'}, {n: '9', name: 'FORCE−'},
     {n: '1', name: 'BIAS'}, {n: '2', name: 'REFIN+'}, {n: '3', name: 'REFIN−'}, {n: '4', name: 'ISENSOR'},
     {n: '18', name: '~DRDY'}, {n: '17', name: 'NC'}, {n: '20', name: 'VDD'}, {n: '19', name: 'DVDD'}]);
  const r = k => y0 + 20 * k;
  // SPI taps and chip select
  for (const p of sp) {
    const net = {SCLK: 'SPI_SCK', SDI: 'SPI_MOSI', SDO: 'SPI_MISO'}[p.name];
    const lane = {SCLK: SPI.SCK, SDI: SPI.MOSI, SDO: SPI.MISO}[p.name];
    w(net, [lane, P(u, p.n)[1]], P(u, p.n));
  }
  two(rcs, 'R', 1370, r(3) + 20, 'd', '10k', {lab: 'l'});
  pwr('+3V3', ...P(rcs, '2'), 'd');
  // grounds
  w('GND', P(u, '10'), [1390, r(5)], [1390, r(9)]);
  for (const p of ['15', '16', '21']) w('GND', P(u, p), [1390, P(u, p)[1]]);
  pwr('GND', 1390, r(9));
  // analog side
  jumper(jp, '1-2 = 3-wire', [PE + 60, PE + 40, PE + 20], r(0) - 40, [1, 2]);
  text(PE + 76, r(0) - 10, '2-3 = 2/4-wire', {cls: 'val'});
  w(n + '_F2', P(u, '6'), [PE + 40, r(0)], P(jp, '2'));
  w(n + '_FP', P(u, '5'), [J4X, r(1)]);
  w(n + '_FP', P(jp, '1'), [PE + 60, r(1)]);
  pwr('GND', ...P(jp, '3'));
  w(n + '_SP', P(u, '7'), [J4X, r(2)]);
  w(n + '_SM', P(u, '8'), [J4X, r(4)]);
  w(n + '_FM', P(u, '9'), [J4X, r(5)]);
  two(cf, 'C', PE + 100, r(3), 'd', '100n', {lab: 'r'});
  text(PE + 140, r(3) + 9, '(Cf)', {cls: 'val'});
  two(rref, 'R', PE + 40, r(7), 'd', '430 0.1%');
  w(n + '_BIAS', P(u, '1'), P(rref, '1'));
  w(n + '_BIAS', P(u, '2'), [PE + 20, r(7)], [PE + 20, r(6)]);
  w(n + '_REF', P(u, '3'), P(rref, '2'));
  w(n + '_REF', P(u, '4'), [PE + 20, r(9)], [PE + 20, r(8)]);
  nc(u, '18'); nc(u, '17');
  w('+3V3', P(u, '19'), [PE + 20, r(13)], [PE + 20, r(12)]);
  w('+3V3', P(u, '20'), [PE + 40, r(12)]);
  pwr('+3V3', PE + 40, r(12), 'r');
  decap(dc1, '100n', '+3V3', PE + 100, r(12));
  decap(dc2, '100n', '+3V3', PE + 140, r(12));
  j4.push({n: String(ch * 4 - 3), label: `${n} F+`, y: r(1)}, {n: String(ch * 4 - 2), label: `${n} RTD+`, y: r(2)},
    {n: String(ch * 4 - 1), label: `${n} RTD−`, y: r(4)}, {n: String(ch * 4), label: `${n} F−`, y: r(5)});
  return r(3);
}
const cs1y = rtd(1, 400, 'U4', 'R23', 'C12', 'JP2', 'R24', 'C13', 'C14');
const cs2y = rtd(2, 760, 'U5', 'R25', 'C15', 'JP3', 'R26', 'C16', 'C17');
w('SPI_SCK', J(10), [SPI.SCK, J(10)[1]]);
w('SPI_SCK', [SPI.SCK, 400], [SPI.SCK, 800]);
w('SPI_MOSI', J(12), [SPI.MOSI, J(12)[1]]);
w('SPI_MOSI', [SPI.MOSI, 420], [SPI.MOSI, 780]);
w('SPI_MISO', J(14), [SPI.MISO, J(14)[1]]);
w('SPI_MISO', [SPI.MISO, 440], [SPI.MISO, 760]);
w('RTD_CS1', J(21), [920, J(21)[1]], [920, 920], [1340, 920], [1340, cs1y], P('U4', '13'));
w('RTD_CS2', J(23), [910, J(23)[1]], [910, 910], [1350, 910], [1350, cs2y], P('U5', '13'));
text(1320, 388, 'SPI', {cls: 'note', anchor: 'middle', sheet: true});

// ================================================================= UART
{
  // pins pointing up from a box below J8
  const ye = 840;
  conn('J9', 'UART (optional)', ye, 'u', [{n: '3', label: 'RX', x: 930}, {n: '2', label: 'TX', x: 950}, {n: '1', label: 'GND', x: 970}]);
  w('UART_RX', J(25), [930, J(25)[1]], P('J9', '3'));
  w('UART_TX', J(27), [950, J(27)[1]], P('J9', '2'));
  w('GND', P('J9', '1'), [970, ye - 20], [990, ye - 20]);
  pwr('GND', 990, ye - 20);
}

// ============================================================= 1-Wire, level
frame(1290, 1070, 690, 270, '1-Wire (DS18B20) and level switch');
const LANE = {SDA: 1250, SCL: 1260, LEVEL: 1270, ONEWIRE: 1280};
{
  const yo = 1160;
  w('ONEWIRE', J(16), [LANE.ONEWIRE, J(16)[1]], [LANE.ONEWIRE, yo], [1380, yo]);
  two('R28', 'R', 1340, yo - 20, 'u', '4k7', {lab: 'l'});
  pwr('+3V3', ...P('R28', '2'));
  two('R27', 'R', 1400, yo, 'l', '100', {lab: 'b'});
  w('OW_DQ', P('R27', '1'), [J4X, yo]);
  two('D16', 'DZ', 1460, yo + 20, 'u', 'PESD5V0S1UL', {pins: ['2', '1']});
  pwr('GND', ...P('D16', '2'));
  two('F4', 'PTC', 1520, yo - 20, 'r', '100 mA', {lab: 'a'});
  pwr('+3V3', ...P('F4', '1'));
  w('OW_3V3', P('F4', '2'), [J4X, yo - 20]);
  w('GND', [J4X, yo + 20], [J4X - 20, yo + 20]);
  pwr('GND', J4X - 20, yo + 20);
  j4.push({n: '9', label: '1-Wire 3V3', y: yo - 20}, {n: '10', label: '1-Wire DQ', y: yo}, {n: '11', label: 'GND', y: yo + 20});
  conn('J4', 'RTD1-2, 1-Wire (3.5 mm)', J4X, 'l', j4, {w: 86, lab: 'end'});

  const yl = 1270;
  w('LEVEL', J(18), [LANE.LEVEL, J(18)[1]], [LANE.LEVEL, yl], [1380, yl]);
  two('C18', 'C', 1340, yl + 20, 'd', '100n', {lab: 'l'});
  pwr('GND', ...P('C18', '2'));
  two('R30', 'R', 1400, yl, 'l', '1k', {lab: 'b'});
  w('LEVEL_IN', P('R30', '1'), [J4X, yl]);
  two('R29', 'R', 1480, yl - 20, 'u', '10k', {lab: 'r'});
  pwr('+3V3', ...P('R29', '2'));
  two('D17', 'DZ', 1540, yl + 20, 'u', 'PESD5V0S1UL', {pins: ['2', '1']});
  pwr('GND', ...P('D17', '2'));
  conn('J5', 'Level (3.5 mm)', J4X, 'l', [{n: '2', label: 'Level', y: yl}, {n: '1', label: 'GND', y: yl + 20}], {w: 70});
  w('GND', [J4X, yl + 20], [J4X - 20, yl + 20]);
  pwr('GND', J4X - 20, yl + 20);
  text(1560, yl - 30, 'switch to GND = water present', {cls: 'note', sheet: true});
}

// ============================================================= front panel
frame(1290, 1350, 740, 500, 'Front panel: TCA9535 (0x22) for the two buttons and LED rings');
{
  const yu = 1420, b0 = yu + 20;
  const right = [];
  const nc1 = ['8', '9', '10', '11', '13', '14', '15', '16', '17', '18', '19', '20'];
  const pname = {8: 'P04', 9: 'P05', 10: 'P06', 11: 'P07', 13: 'P10', 14: 'P11', 15: 'P12', 16: 'P13', 17: 'P14', 18: 'P15', 19: 'P16', 20: 'P17'};
  const used = {0: {n: '4', name: 'P00'}, 4: {n: '5', name: 'P01'}, 9: {n: '6', name: 'P02'}, 13: {n: '7', name: 'P03'}};
  let k = 0;
  for (let i = 0; i < 16; i++) right.push(used[i] || {n: nc1[k], name: pname[nc1[k++]]});
  ic('U6', 'TCA9535PWR', 1320, yu, 110,
    [{n: '22', name: 'SCL'}, {n: '23', name: 'SDA'}, {n: '21', name: 'A0'}, {n: '2', name: 'A1'}, {n: '3', name: 'A2'},
     {n: '1', name: '~INT'}, {n: '24', name: 'VCC'}, {n: '12', name: 'GND'}], right);
  for (const n of nc1) nc('U6', n);
  nc('U6', '1');
  w('I2C_SCL', J(26), [LANE.SCL, J(26)[1]], [LANE.SCL, b0], P('U6', '22'));
  w('I2C_SDA', J(28), [LANE.SDA, J(28)[1]], [LANE.SDA, b0 + 20], P('U6', '23'));
  pwr('GND', ...P('U6', '21'), 'l');
  pwr('+3V3', ...P('U6', '2'), 'l');
  pwr('GND', ...P('U6', '3'), 'l');
  pwr('+3V3', ...P('U6', '24'), 'l');
  pwr('GND', ...P('U6', '12'), 'l');
  const XJ7 = 1940;
  const j7 = [];
  const button = (yb, pin, pu, ser, esd, sig, jn, label) => {
    w(sig, P('U6', pin), [1560, yb]);
    two(pu, 'R', 1500, yb - 20, 'u', '10k', {lab: 'l'});
    pwr('+3V3', ...P(pu, '2'));
    two(ser, 'R', 1580, yb, 'l', '100', {lab: 'b'});
    w(sig + '_X', P(ser, '1'), [XJ7, yb]);
    two(esd, 'DZ', 1700, yb + 20, 'u', 'PESD5V0S1UL', {pins: ['2', '1']});
    pwr('GND', ...P(esd, '2'));
    j7.push({n: jn, label, y: yb});
  };
  button(b0, '4', 'R31', 'R32', 'D18', 'BTN_START', '1', 'START');
  button(b0 + 80, '5', 'R35', 'R36', 'D19', 'BTN_PUMP', '2', 'PUMP');
  const led = (yL, pin, pd, q, r0, sig, jn, label) => {
    w(sig, P('U6', pin), [1530, yL]);
    two(pd, 'R', 1490, yL + 20, 'd', '100k', {lab: 'l'});
    pwr('GND', ...P(pd, '2'));
    fet(q, 'n', 1560, yL, 0, false, '2N7002', {G: '1', S: '2', D: '3'}, 'r');
    pwr('GND', ...P(q, '2'));
    two(r0, 'R', 1620, yL - 30, 'l', '0', {lab: 'a'});
    w(sig + '_D', P(q, '3'), P(r0, '2'));
    w(sig + '_K', P(r0, '1'), [XJ7, yL - 30]);
    j7.push({n: jn, label, y: yL - 30});
  };
  led(b0 + 180, '6', 'R33', 'Q8', 'R34', 'LED_START', '3', 'LED START −');
  led(b0 + 260, '7', 'R37', 'Q9', 'R38', 'LED_PUMP', '4', 'LED PUMP −');
  j7.push({n: '5', label: 'LED + (5 V)', y: b0 + 290}, {n: '6', label: 'GND', y: b0 + 320});
  conn('J7', 'Buttons (JST PH 6)', XJ7, 'l', j7, {w: 76, lab: 'end'});
  w('+5V', [XJ7, b0 + 290], [XJ7 - 40, b0 + 290]);
  pwr('+5V', XJ7 - 40, b0 + 290);
  w('GND', [XJ7, b0 + 320], [XJ7 - 20, b0 + 320]);
  pwr('GND', XJ7 - 20, b0 + 320);
  text(1300, yu + 410, 'Buttons switch to GND. RL (0 Ω) is for rings with a built-in resistor; otherwise fit a series resistor.', {cls: 'note', sheet: true});
  decap('C19', '100n', '+3V3', 1280, b0 + 200);
}

// ======================================================== decoupling, misc
frame(20, 1410, 1250, 250, 'Decoupling, test points, mounting');
{
  const y = 1470;
  const list = [['C1', '10u 10V', '+3V3', 'J8 3V3'], ['C2', '100n', '+3V3', 'J8 3V3'], ['C3', '22u 10V', '+5V', 'J8 5V'],
    ['C10', '1u 25V', '+12V', 'U2 VDD'], ['C11', '1u 25V', '+12V', 'U3 VDD']];
  list.forEach(([ref, v, net, note], i) => {
    const x = 80 + i * 110;
    decap(ref, v, net, x, y);
    text(x, y + 80, note, {anchor: 'middle', cls: 'note', sheet: true});
  });
  // PWR_FLAGs: the supplies come from J3, the buck and the panel (3V3)
  [['+12V', 880], ['+5V', 960], ['+3V3', 1040], ['GND', 1120]].forEach(([net, x]) => {
    pwr(net, x, y + 40, net === 'GND' ? 'd' : 'u');
    w(net, [x, y + 40], [x + 30, y + 40]);
    flag(net, x + 30, y + 40);
  });
  tp('TP3', '3V3', 700, y + 20, 'u');
  pwr('+3V3', 700, y + 20, 'd');
  tp('TP4', 'GND', 780, y + 20, 'u');
  pwr('GND', 780, y + 20);
  // mounting holes: no pins, but they are parts of the board
  ['H1', 'H2', 'H3', 'H4'].forEach((ref, i) => {
    const x = 900 + i * 40, yy = y + 85;
    g.push(`<circle class="s" cx="${x}" cy="${yy}" r="6"/>`);
    text(x, yy - 12, ref, {anchor: 'middle', cls: 'ref'});
    model.push({t: 'hole', ref, x, y: yy});
  });
  text(40, y + 120, 'H1–H4: M2 mounting holes on the panel\'s 4 mm standoffs (not connected).', {cls: 'note', sheet: true});
  text(40, y + 140, 'Power nets are drawn as symbols: every +12V, +5V, +3V3 and GND symbol is the same net. +3V3 comes from the panel (J8 pins 31/32).', {cls: 'note', sheet: true});
  text(40, y + 160, 'Every output gate has a pull-down: all outputs are off while the ESP32 is in reset or the panel is unplugged.', {cls: 'note', sheet: true});
  text(40, y + 180, 'Drawn by tools/kicad/backpack_overview.js from hardware/backpack (the KiCad schematic is the design source; this drawing is checked against its netlist).', {cls: 'note', sheet: true});
}

// ================================================================== check
function readNetlist(file) {
  const s = fs.readFileSync(file, 'utf8');
  const pins = {};
  for (const blk of s.split(/\n\t\t\(net\b/).slice(1)) {
    const name = blk.match(/\(name "([^"]*)"\)/)[1].replace(/^\//, '');
    for (const m of blk.matchAll(/\(ref "([^"]+)"\)\s*\(pin "([^"]+)"\)/g)) pins[m[1] + '.' + m[2]] = name;
  }
  return pins;
}
function check(net) {
  const errs = [];
  const key = p => p[0] + ',' + p[1];
  const segs = [];
  wires.forEach((wr, wi) => { for (let i = 1; i < wr.pts.length; i++) segs.push({wi, a: wr.pts[i - 1], b: wr.pts[i], net: wr.net}); });
  const onSeg = (p, s) => {
    const [x, y] = p, [x1, y1] = s.a, [x2, y2] = s.b;
    return (x1 === x2 && x === x1 && y >= Math.min(y1, y2) && y <= Math.max(y1, y2)) ||
           (y1 === y2 && y === y1 && x >= Math.min(x1, x2) && x <= Math.max(x1, x2));
  };
  // union-find over items: wires, pins, power symbols
  const parent = {};
  const find = a => (parent[a] === undefined ? (parent[a] = a) : parent[a] === a ? a : (parent[a] = find(parent[a])));
  const unite = (a, b) => { parent[find(a)] = find(b); };
  const pinList = [];
  for (const [ref, pt] of Object.entries(parts)) for (const [n, p] of Object.entries(pt.pins)) pinList.push({ref, n, p, id: `pin:${ref}.${n}`});
  const netOf = id => id.startsWith('pin:') ? net[id.slice(4)] : id.startsWith('w:') ? wires[+id.slice(2)].net : id.slice(4);
  // pins
  for (const pn of pinList) {
    const nn = net[pn.ref + '.' + pn.n];
    if (nn === undefined) { errs.push(`drawn pin ${pn.ref}.${pn.n} is not in the netlist`); continue; }
    const touch = [];
    segs.forEach(s => { if (onSeg(pn.p, s)) touch.push('w:' + s.wi); });
    powers.forEach((pw, i) => { if (pw.x === pn.p[0] && pw.y === pn.p[1]) touch.push('pwr:' + pw.net); });
    pinList.forEach(o => { if (o !== pn && o.p[0] === pn.p[0] && o.p[1] === pn.p[1]) touch.push(o.id); });
    const isNc = ncs.some(c => c.ref === pn.ref && c.pin === pn.n);
    if (nn.startsWith('unconnected')) {
      if (!isNc) errs.push(`${pn.ref}.${pn.n} is unconnected but has no X`);
      if (touch.length) errs.push(`${pn.ref}.${pn.n} is unconnected but touches ${touch}`);
      continue;
    }
    if (!touch.length) errs.push(`${pn.ref}.${pn.n} (${nn}) is not connected in the drawing`);
    for (const t of touch) {
      const tn = netOf(t);
      if (tn !== nn) errs.push(`${pn.ref}.${pn.n} (${nn}) touches ${t} (${tn})`);
      else unite(pn.id, t);
    }
  }
  // wires: every end must land on something of the same net
  wires.forEach((wr, wi) => {
    for (const end of [wr.pts[0], wr.pts[wr.pts.length - 1]]) {
      let hit = false;
      segs.forEach(s => { if (s.wi !== wi && onSeg(end, s)) { hit = true; if (s.net !== wr.net) errs.push(`wire ${wr.net} end ${end} lands on wire ${s.net}`); else unite('w:' + wi, 'w:' + s.wi); } });
      pinList.forEach(pn => { if (pn.p[0] === end[0] && pn.p[1] === end[1]) hit = true; });
      powers.forEach(pw => { if (pw.x === end[0] && pw.y === end[1]) { hit = true; if (pw.net !== wr.net) errs.push(`wire ${wr.net} ends on ${pw.net} symbol`); } });
      if (!hit) errs.push(`wire ${wr.net} end ${end} is open`);
    }
    // interior points that sit on a pin of another net
    pinList.forEach(pn => { segs.filter(s => s.wi === wi).forEach(s => { if (onSeg(pn.p, s) && net[pn.ref + '.' + pn.n] !== wr.net) errs.push(`wire ${wr.net} passes over ${pn.ref}.${pn.n}`); }); });
  });
  powers.forEach(pw => {
    segs.forEach(s => { if (onSeg([pw.x, pw.y], s)) { if (s.net !== pw.net) errs.push(`${pw.net} symbol on wire ${s.net}`); else unite('pwr:' + pw.net, 'w:' + s.wi); } });
  });
  // every netlist pin drawn, each net one piece
  const skip = r => r.startsWith('#') || /^H\d$/.test(r);
  for (const k of Object.keys(net)) {
    const ref = k.split('.')[0];
    if (skip(ref)) continue;
    if (!parts[ref] || !parts[ref].pins[k.split('.')[1]]) errs.push(`netlist pin ${k} (${net[k]}) is not drawn`);
  }
  const groups = {};
  for (const pn of pinList) {
    const nn = net[pn.ref + '.' + pn.n];
    if (!nn || nn.startsWith('unconnected')) continue;
    (groups[nn] = groups[nn] || new Set()).add(find(pn.id));
  }
  for (const [nn, set] of Object.entries(groups)) if (set.size > 1) errs.push(`net ${nn} is drawn in ${set.size} separate pieces`);
  return errs;
}

// junction dots: wire end on another wire's interior, or 3+ ends at a point
function junctions() {
  const cnt = {};
  const add = (p, n) => { const k = p[0] + ',' + p[1]; cnt[k] = (cnt[k] || 0) + n; };
  const segs = [];
  wires.forEach(wr => { for (let i = 1; i < wr.pts.length; i++) segs.push([wr.pts[i - 1], wr.pts[i]]); });
  const pts = new Set();
  wires.forEach(wr => { pts.add(wr.pts[0] + ''); pts.add(wr.pts[wr.pts.length - 1] + ''); });
  const out = [];
  for (const s of pts) {
    const p = s.split(',').map(Number);
    let deg = 0;
    for (const [a, b] of segs) {
      const on = (a[0] === b[0] && p[0] === a[0] && p[1] >= Math.min(a[1], b[1]) && p[1] <= Math.max(a[1], b[1])) ||
                 (a[1] === b[1] && p[1] === a[1] && p[0] >= Math.min(a[0], b[0]) && p[0] <= Math.max(a[0], b[0]));
      if (!on) continue;
      deg += (p[0] === a[0] && p[1] === a[1]) || (p[0] === b[0] && p[1] === b[1]) ? 1 : 2;
    }
    for (const pt of Object.values(parts)) for (const q of Object.values(pt.pins)) if (q[0] === p[0] && q[1] === p[1]) deg++;
    if (powers.some(pw => pw.x === p[0] && pw.y === p[1])) deg++;
    if (deg >= 3) out.push(p);
  }
  return out;
}

if (fs.existsSync(NETLIST)) {
  const errs = check(readNetlist(NETLIST));
  if (errs.length) {
    console.error(errs.join('\n'));
    console.error(`${errs.length} problems: drawing not written`);
    process.exit(1);
  }
  console.log('checked against', path.relative(ROOT, NETLIST));
} else {
  console.warn('no netlist at', NETLIST, '- drawing not checked');
}

const css = `
svg{background:#fff}
.w{fill:none;stroke:#1a7f37;stroke-width:1.4}
.s{fill:none;stroke:#8b1a1a;stroke-width:1.3}
.sf{fill:#8b1a1a;stroke:#8b1a1a;stroke-width:1.3;stroke-linejoin:round}
.p{fill:none;stroke:#8b1a1a;stroke-width:1.4}
.nc{stroke:#0550ae;stroke-width:1.4}
.box{fill:#fffbe6;stroke:#8b1a1a;stroke-width:1.4}
.pad{fill:#fff;stroke:#8b1a1a;stroke-width:1}
.shunt{fill:#555;fill-opacity:.55;stroke:none}
.frame{fill:none;stroke:#9aa4b2;stroke-width:1;stroke-dasharray:6 4}
.dot{fill:#1a7f37}
text{font-family:Consolas,'DejaVu Sans Mono',monospace;fill:#24292f}
.t{font-size:11px}
.ref{font-size:10.5px;fill:#0550ae}
.val{font-size:10px;fill:#57606a}
.pin{font-size:9.5px}
.num{font-size:8.5px;fill:#8b1a1a}
.pwr{font-size:10px;fill:#8b1a1a;font-weight:bold}
.title{font-size:13px;font-weight:bold;fill:#24292f}
.sub{font-size:11px;font-weight:bold;fill:#57606a}
.note{font-size:10px;fill:#57606a}
.head{font-size:18px;font-weight:bold}`;
const dots = junctions().map(p => `<circle class="dot" cx="${p[0]}" cy="${p[1]}" r="3"/>`);
const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="${W}" height="${H}" viewBox="0 0 ${W} ${H}">
<style>${css}</style>
<rect width="${W}" height="${H}" fill="#fff"/>
<text class="head" x="20" y="34">MashTouch I/O backpack: overall schematic</text>
<text class="note" x="20" y="54">MCU-less board on J8 of the Waveshare ESP32-S3-Touch-LCD-3.5. J8 pin numbers follow the Waveshare pinout image (pin 1 BAT at the USB-C end, odd pins on the outer row).</text>
${wires.map(wr => `<polyline class="w" points="${wr.pts.map(p => p.join(',')).join(' ')}"/>`).join('\n')}
${g.join('\n')}
${dots.join('\n')}
</svg>
`;
if (OUT) {
  fs.mkdirSync(path.dirname(path.resolve(OUT)), {recursive: true});
  fs.writeFileSync(OUT, svg);
  console.log('wrote', OUT, `(${Object.keys(parts).length} parts, ${wires.length} wires)`);
}
if (KICAD) {
  const r = require('./backpack_kicad').emit(D, {sch: path.join(ROOT, 'hardware/backpack/backpack.kicad_sch')});
  console.log('wrote hardware/backpack/backpack.kicad_sch', r);
}
