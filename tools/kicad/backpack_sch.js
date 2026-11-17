// MashTouch - generates the first version of the I/O backpack schematic
// (hardware/backpack/*.kicad_sch) from the part list below.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Bootstrap only: once the schematic is edited in KiCad, edit it there and do
// not run this again (it overwrites the sheets). Every pin gets a net label or
// a power symbol at its end, so ERC checks the connections.
//
//   node tools/kicad/backpack_sch.js [outdir]
'use strict';
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const S = require('./sexpr');

const OUT = process.argv[2] || path.join(__dirname, '../../hardware/backpack');
const PROJECT = 'backpack';
const POWER = new Set(['GND', '+3V3', '+5V', '+12V']);
const uuid = () => crypto.randomUUID();
const q = s => ({s: String(s)});
const g = v => Math.round(v / 1.27) * 1.27;  // snap to the 1.27 mm grid
const num = v => +v.toFixed(4);

// ---- part list ----------------------------------------------------------------
// part(lib, value, footprint, nets, extra): nets maps pin number or pin name
// to a net; 'NC' places a no-connect flag. extra: {mpn, note, ref}.
const sheets = [];
let cur = null;
function sheet(file, name, title, notes) {
  cur = {file, name, title, notes, rows: [], uuid: uuid()};
  sheets.push(cur);
}
function row(caption) { cur.rows.push({caption, parts: []}); }
function part(lib, value, fp, nets, extra = {}) {
  const p = Object.assign({lib, value, fp, nets}, extra);
  delete p.forward;
  cur.rows[cur.rows.length - 1].parts.push(p);
  return p;
}
const R = (v, a, b, fp = 'Resistor_SMD:R_0603_1608Metric', x = {}) => part('Device:R', v, fp, {1: a, 2: b}, x);
const C = (v, a, b, fp = 'Capacitor_SMD:C_0603_1608Metric', x = {}) => part('Device:C', v, fp, {1: a, 2: b}, x);
const LED = (v, a, k, x = {}) => part('Device:LED', v, 'LED_SMD:LED_0603_1608Metric', {A: a, K: k}, x);
const TP = (net, x = {}) => part('Connector:TestPoint', net.replace(/^\+/, ''), 'TestPoint:TestPoint_Pad_D1.5mm', {1: net}, x);
const FLAG = net => part('power:PWR_FLAG', 'PWR_FLAG', '', {1: net}, {virtual: true});
// Pluggable 3.5 mm terminal. nets are in reading order on the board (left to
// right on the top edge, top to bottom on the side edges); with the plug
// opening facing outward that is pin n first, so pins are numbered backwards
// (forward: true for the left edge, where the footprint lies the other way).
const terminal = (n, value, nets, x = {}) => part(`Connector:Screw_Terminal_01x${String(n).padStart(2, '0')}`, value,
  `Connector_Phoenix_MC:PhoenixContact_MC_1,5_${n}-G-3.5_1x${String(n).padStart(2, '0')}_P3.50mm_Horizontal`,
  Object.fromEntries(nets.map((net, i) => [x.forward ? i + 1 : n - i, net])),
  Object.assign({mpn: `Phoenix MC 1,5/${n}-G-3,5 + plug MC 1,5/${n}-ST-3,5 (or 3.5 mm 15EDG equivalent)`}, x));

// ---------------------------------------------------------------- J8 sheet
sheet('panel.kicad_sch', 'Panel connector', 'J8 to the Waveshare panel, test points, mounting',
  ['J8 pin numbers follow the Waveshare pinout image: pin 1 (BAT) at the USB-C end, odd pins on the outer row.',
   'J8 is a male 2x16 header on the panel side of the board (mating pins 4.5 mm or shorter, see docs/backpack-layout.md).']);
row('J8: male 2x16 2.54 mm into the panel\'s SMD socket');
part('Connector_Generic:Conn_02x16_Odd_Even', 'J8 panel', 'backpack:PinHeader_2x16_P2.54mm_Vertical_Bottom', {
  1: 'NC', 2: '+5V', 3: 'GND', 4: 'GND', 5: 'PWR2', 6: 'NC', 7: 'OUT1', 8: 'NC', 9: 'OUT2', 10: 'SPI_SCK',
  11: 'OUT3', 12: 'SPI_MOSI', 13: 'OUT4', 14: 'SPI_MISO', 15: 'PWR1', 16: 'ONEWIRE', 17: 'GPIO45', 18: 'LEVEL',
  19: 'GPIO46', 20: 'ESP_BOOT', 21: 'RTD_CS1', 22: 'ESP_RST', 23: 'RTD_CS2', 24: 'NC', 25: 'UART_RX',
  26: 'I2C_SCL', 27: 'UART_TX', 28: 'I2C_SDA', 29: 'GND', 30: 'GND', 31: '+3V3', 32: '+3V3'},
  {ref: 'J8', mpn: '2x16 2.54 mm male header, short mating pins (<= 4.5 mm), 2.5 mm body', note: 'fitted on the bottom (panel) side'});
row('Decoupling at J8');
C('10u 10V', '+3V3', 'GND', 'Capacitor_SMD:C_0805_2012Metric', {mpn: 'X5R 10 uF 10 V 0805'});
C('100n', '+3V3', 'GND');
C('22u 10V', '+5V', 'GND', 'Capacitor_SMD:C_1206_3216Metric', {mpn: 'X5R 22 uF 25 V 1206'});
FLAG('+3V3');
row('UART debug header (optional) and test points');
part('Connector_Generic:Conn_01x03', 'UART', 'Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical',
  {1: 'GND', 2: 'UART_TX', 3: 'UART_RX'}, {ref: 'J9', note: 'pin 2 = ESP TX (GPIO43), pin 3 = ESP RX (GPIO44); optional'});
for (const n of ['+12V', '+5V', '+3V3', 'GND', 'GPIO45', 'GPIO46', 'ESP_BOOT', 'ESP_RST']) TP(n);
row('Mounting holes (M2, on the panel\'s 4 mm SMT standoffs)');
for (let i = 0; i < 4; i++) part('Mechanical:MountingHole', 'M2', 'MountingHole:MountingHole_2.2mm_M2', {}, {mpn: '-'});

// ---------------------------------------------------------------- power sheet
sheet('power.kicad_sch', '12 V input and 5 V', '12 V input protection and the 12 -> 5 V buck for the panel (J8 VBUS)',
  ['Fuse -> reverse-polarity P-FET -> TVS -> +12V. The buck feeds J8 pin 2 (5V/VBUS); the panel makes its own 3.3 V.',
   'AP63205 values from the datasheet typical application: 10 uF in, 4.7 uH, 2 x 22 uF out, 100 nF boost, EN to VIN.']);
row('12 V input');
terminal(2, '12V IN', ['+12V_IN', 'GND'], {ref: 'J3'});
part('Device:Fuse', '7.5A mini blade', 'Fuse:Fuseholder_Blade_Mini_Keystone_3568', {1: '+12V_IN', 2: '+12V_F'},
  {mpn: 'Keystone 3568 holder + 7.5 A mini blade fuse', note: 'hand-soldered'});
part('Transistor_FET:Q_PMOS_GDS', 'AOD4185', 'Package_TO_SOT_SMD:TO-252-2', {G: 'REVP_G', D: '+12V_F', S: '+12V'},
  {mpn: 'AOD4185 (P-ch -40 V, DPAK)', note: 'reverse-polarity protection'});
part('Device:D_Zener', 'BZT52C15', 'Diode_SMD:D_SOD-123', {K: '+12V', A: 'REVP_G'}, {mpn: 'BZT52C15 (15 V)', note: 'gate clamp'});
R('10k', 'REVP_G', 'GND');
part('Device:D_TVS', 'SMAJ15CA', 'Diode_SMD:D_SMA', {A1: '+12V', A2: 'GND'}, {mpn: 'SMAJ15CA'});
part('Device:C_Polarized', '100u 25V', 'Capacitor_SMD:CP_Elec_6.3x7.7', {1: '+12V', 2: 'GND'}, {mpn: 'Al electrolytic 100 uF 25 V 6.3x7.7'});
FLAG('+12V');
FLAG('GND');
row('12 -> 5 V buck (AP63205, 2 A, 1.1 MHz)');
C('10u 25V', '+12V', 'GND', 'Capacitor_SMD:C_1206_3216Metric', {mpn: 'X7R 10 uF 25 V 1206'});
C('100n', '+12V', 'GND');
part('Regulator_Switching:AP63205WU', 'AP63205WU', 'Package_TO_SOT_SMD:TSOT-23-6',
  {IN: '+12V', EN: '+12V', GND: 'GND', SW: 'BUCK_SW', BST: 'BUCK_BST', FB: '+5V'}, {mpn: 'AP63205WU-7', lcsc: 'C2071056'});
C('100n', 'BUCK_BST', 'BUCK_SW');
part('Device:L', '4.7u', 'Inductor_SMD:L_Bourns_SRN6045TA', {1: 'BUCK_SW', 2: '+5V'}, {mpn: 'Bourns SRN6045TA-4R7M (or 6x6 mm 4.7 uH, Isat >= 3 A)'});
C('22u', '+5V', 'GND', 'Capacitor_SMD:C_1206_3216Metric', {mpn: 'X5R 22 uF 25 V 1206'});
C('22u', '+5V', 'GND', 'Capacitor_SMD:C_1206_3216Metric', {mpn: 'X5R 22 uF 25 V 1206'});
R('1k5', '+5V', 'LED_5V');
LED('green', 'LED_5V', 'GND', {note: '5 V on'});
FLAG('+5V');

// ---------------------------------------------------------------- outputs sheet
sheet('outputs.kicad_sch', 'Outputs', 'OUT1-4 (signal, SSR/relay) and PWR1-2 (12 V, 3 A, PWM)',
  ['Every gate has a pull-down: all outputs are off while the ESP32 is in reset or the panel is unplugged.',
   'OUT1-4 switch to GND; their + side is OUT_V, 12 V or 5 V chosen with JP1. Flyback diodes allow relay coils.',
   'PWR1-2: UCC27511A gate driver from 12 V (input low or floating = output low), DPAK N-FET, PTC and flyback diode.']);
row('OUT +V select: jumper 1-2 = 12 V, 2-3 = 5 V');
part('Connector_Generic:Conn_01x03', 'OUT V select', 'Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical',
  {1: '+12V', 2: 'OUT_V', 3: '+5V'}, {ref: 'JP1', note: 'with a 2.54 mm jumper shunt'});
terminal(8, 'OUT1-4', ['OUT_V', 'OUT1_L', 'OUT_V', 'OUT2_L', 'OUT_V', 'OUT3_L', 'OUT_V', 'OUT4_L'], {ref: 'J1'});
for (let n = 1; n <= 4; n++) {
  row(`OUT${n} (GPIO ${37 + n})`);
  R('100', `OUT${n}`, `OUT${n}_G`);
  R('10k', `OUT${n}_G`, 'GND');
  part('Transistor_FET:AO3400A', 'AO3400A', 'Package_TO_SOT_SMD:SOT-23', {G: `OUT${n}_G`, S: 'GND', D: `OUT${n}_L`}, {mpn: 'AO3400A'});
  part('Device:D_Schottky', 'B5819W', 'Diode_SMD:D_SOD-123', {K: 'OUT_V', A: `OUT${n}_L`}, {mpn: 'B5819W', note: 'flyback'});
  LED('yellow', `OUT${n}`, `OUT${n}_LK`);
  R('1k', `OUT${n}_LK`, 'GND');
}
row('PWR terminals');
terminal(4, 'PWR1-2', ['PWR1_P', 'PWR1_L', 'PWR2_P', 'PWR2_L'], {ref: 'J2'});
for (const [n, gpio] of [[1, 42], [2, 21]]) {
  row(`PWR${n} (GPIO ${gpio})`);
  R('100k', `PWR${n}`, 'GND');
  part('Driver_FET:UCC27511ADBV', 'UCC27511A', 'Package_TO_SOT_SMD:SOT-23-6',
    {'V_{DD}': '+12V', GND: 'GND', 'IN+': `PWR${n}`, 'IN-': 'GND', OUTH: `PWR${n}_OH`, OUTL: `PWR${n}_G`}, {mpn: 'UCC27511ADBVR', lcsc: 'C2676982'});
  C('1u 25V', '+12V', 'GND', 'Capacitor_SMD:C_0805_2012Metric');
  R('10', `PWR${n}_OH`, `PWR${n}_G`);
  R('10k', `PWR${n}_G`, 'GND');
  part('Transistor_FET:Q_NMOS_GDS', 'AOD4184A', 'Package_TO_SOT_SMD:TO-252-2', {G: `PWR${n}_G`, D: `PWR${n}_L`, S: 'GND'},
    {mpn: 'AOD4184A (N-ch 40 V, DPAK)'});
  part('Device:Polyfuse', '3A hold', 'Fuse:Fuse_2920_7451Metric', {1: '+12V', 2: `PWR${n}_P`}, {mpn: 'PTC 2920, hold 3 A, >= 16 V'});
  part('Device:D_Schottky', 'SS34', 'Diode_SMD:D_SMA', {K: `PWR${n}_P`, A: `PWR${n}_L`}, {mpn: 'SS34', note: 'flyback'});
  LED('yellow', `PWR${n}`, `PWR${n}_LK`);
  R('1k', `PWR${n}_LK`, 'GND');
}

// ---------------------------------------------------------------- RTD sheet
sheet('rtd.kicad_sch', 'PT100 inputs', '2x MAX31865 for PT100/PT1000 (SPI3 on J8: SCK 11, MOSI 10, MISO 9; CS 47/48)',
  ['Wiring per the MAX31865 datasheet typical application. RREF 430 R for PT100, 4k3 for PT1000; Cf 100 nF for PT100, 10 nF for PT1000.',
   'JPx sets FORCE2: 1-2 (default) = 3-wire (FORCE2 to FORCE+); 2-3 = 2- or 4-wire (FORCE2 to GND).',
   '2-wire probe: link F+ to RTD+ and RTD- to F- at the terminal. Set the firmware to match (rtd.N.wires).']);
row('Left-edge terminal: RTD1, RTD2, 1-Wire');
terminal(11, 'RTD1-2 1-Wire', ['RTD1_FP', 'RTD1_SP', 'RTD1_SM', 'RTD1_FM', 'RTD2_FP', 'RTD2_SP', 'RTD2_SM', 'RTD2_FM',
  'OW_3V3', 'OW_DQ', 'GND'], {ref: 'J4', forward: true, note: 'top to bottom: RTD1 F+ RTD+ RTD- F-, RTD2 F+ RTD+ RTD- F-, 1-Wire 3V3 DQ GND'});
for (const ch of [1, 2]) {
  row(`Channel ${ch} (CS GPIO ${ch === 1 ? 47 : 48})`);
  part('Sensor_Temperature:MAX31865xTP', 'MAX31865ATP+', 'Package_DFN_QFN:TQFN-20-1EP_5x5mm_P0.65mm_EP3.25x3.25mm', {
    BIAS: `RTD${ch}_BIAS`, 'REFIN+': `RTD${ch}_BIAS`, 'REFIN-': `RTD${ch}_REF`, ISENSOR: `RTD${ch}_REF`,
    'FORCE+': `RTD${ch}_FP`, FORCE2: `RTD${ch}_F2`, 'RTDIN+': `RTD${ch}_SP`, 'RTDIN-': `RTD${ch}_SM`, 'FORCE-': `RTD${ch}_FM`,
    GND: 'GND', DGND: 'GND', SDI: 'SPI_MOSI', SCLK: 'SPI_SCK', '~{CS}': `RTD_CS${ch}`, SDO: 'SPI_MISO',
    '~{DRDY}': 'NC', NC: 'NC', DVDD: '+3V3', VDD: '+3V3'}, {mpn: 'MAX31865ATP+', lcsc: 'C404011'});
  R('430 0.1%', `RTD${ch}_BIAS`, `RTD${ch}_REF`, 'Resistor_SMD:R_0805_2012Metric', {mpn: '430 R 0.1 % 25 ppm 0805', note: 'RREF; 4k3 for PT1000'});
  part('Jumper:Jumper_3_Bridged12', '3-wire', 'Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical',
    {A: `RTD${ch}_FP`, C: `RTD${ch}_F2`, B: 'GND'}, {mpn: '1x03 2.54 mm pin header + jumper shunt', note: '2.54 mm shunt: 1-2 = 3-wire, 2-3 = 2/4-wire'});
  C('100n', `RTD${ch}_SP`, `RTD${ch}_SM`, 'Capacitor_SMD:C_0805_2012Metric', {note: 'Cf; 10 nF for PT1000'});
  C('100n', '+3V3', 'GND');
  C('100n', '+3V3', 'GND');
  R('10k', `RTD_CS${ch}`, '+3V3', undefined, {note: 'CS pull-up: deselected during boot'});
}

// ---------------------------------------------------------------- sensors/front sheet
sheet('io.kicad_sch', '1-Wire, level, front panel', 'DS18B20 bus, level switch, TCA9535 for the two buttons and LED rings',
  ['TCA9535 at 0x22: A0 = 0, A1 = 1, A2 = 0. P00/P01 buttons to GND (10k pull-ups), P02/P03 drive the LED rings low-side.',
   'LED rings: anode on JST pin 5 (+5V); RLx is 0 R for rings with a built-in resistor, otherwise fit a series resistor.']);
row('1-Wire (DS18B20, any number in parallel)');
part('Device:Polyfuse', '100mA', 'Fuse:Fuse_0805_2012Metric', {1: '+3V3', 2: 'OW_3V3'}, {mpn: 'PTC 0805 hold 0.1 A'});
R('100', 'OW_DQ', 'ONEWIRE');
R('4k7', 'ONEWIRE', '+3V3');
part('Diode:PESD5V0S1UL', 'PESD5V0S1UL', 'Diode_SMD:D_SOD-882', {K: 'OW_DQ', A: 'GND'}, {mpn: 'PESD5V0S1UL'});
row('Level switch (to GND)');
terminal(2, 'Level', ['LEVEL_IN', 'GND'], {ref: 'J5'});
R('10k', 'LEVEL_IN', '+3V3');
R('1k', 'LEVEL_IN', 'LEVEL');
C('100n', 'LEVEL', 'GND');
part('Diode:PESD5V0S1UL', 'PESD5V0S1UL', 'Diode_SMD:D_SOD-882', {K: 'LEVEL_IN', A: 'GND'}, {mpn: 'PESD5V0S1UL'});
row('Front panel expander');
part('Interface_Expansion:TCA9535PWR', 'TCA9535PWR', 'Package_SO:TSSOP-24_4.4x7.8mm_P0.65mm', {
  VCC: '+3V3', GND: 'GND', SCL: 'I2C_SCL', SDA: 'I2C_SDA', A0: 'GND', A1: '+3V3', A2: 'GND', '~{INT}': 'NC',
  P00: 'BTN_START', P01: 'BTN_PUMP', P02: 'LED_START', P03: 'LED_PUMP',
  P04: 'NC', P05: 'NC', P06: 'NC', P07: 'NC', P10: 'NC', P11: 'NC', P12: 'NC', P13: 'NC', P14: 'NC', P15: 'NC', P16: 'NC', P17: 'NC'},
  {mpn: 'TCA9535PWR', lcsc: 'C130204'});
C('100n', '+3V3', 'GND');
part('Connector_Generic:Conn_01x06', 'Buttons', 'Connector_JST:JST_PH_B6B-PH-K_1x06_P2.00mm_Vertical',
  {1: 'BTN_START_X', 2: 'BTN_PUMP_X', 3: 'LED_START_K', 4: 'LED_PUMP_K', 5: '+5V', 6: 'GND'},
  {ref: 'J7', mpn: 'JST B6B-PH-K', note: '1 START, 2 PUMP, 3 LED S-, 4 LED P-, 5 LED+ (5 V), 6 GND'});
for (const [k, name] of [['START', 'Start/Pause'], ['PUMP', 'Pump']]) {
  row(`${name} button and LED ring`);
  R('10k', `BTN_${k}`, '+3V3');
  R('100', `BTN_${k}_X`, `BTN_${k}`);
  part('Diode:PESD5V0S1UL', 'PESD5V0S1UL', 'Diode_SMD:D_SOD-882', {K: `BTN_${k}_X`, A: 'GND'}, {mpn: 'PESD5V0S1UL'});
  R('100k', `LED_${k}`, 'GND', undefined, {note: 'LED off while the expander pins are inputs'});
  part('Transistor_FET:2N7002', '2N7002', 'Package_TO_SOT_SMD:SOT-23', {G: `LED_${k}`, S: 'GND', D: `LED_${k}_D`}, {mpn: '2N7002'});
  R('0', `LED_${k}_K`, `LED_${k}_D`, 'Resistor_SMD:R_0805_2012Metric', {note: 'series resistor for rings without one'});
}

// ---- library symbols ----------------------------------------------------------
const symCache = new Map();
function sym(lib) {
  if (!symCache.has(lib)) {
    const def = S.symbol(lib);
    symCache.set(lib, {def, pins: S.pins(def), power: !!S.find(def, 'power')});
  }
  return symCache.get(lib);
}

// rotate (x, y) in library coordinates (y up) by r degrees CCW
function rot(x, y, r) {
  const a = r * Math.PI / 180, c = Math.round(Math.cos(a)), s = Math.round(Math.sin(a));
  return [x * c - y * s, x * s + y * c];
}
const dirOf = deg => ((deg % 360) + 360) % 360;

// ---- resolve pins and nets ----------------------------------------------------
const allNets = new Map();  // net -> Set(sheet file)
for (const sh of sheets) for (const r of sh.rows) for (const p of r.parts) {
  const {pins} = sym(p.lib);
  p.pinNets = new Map();
  for (const [key, net] of Object.entries(p.nets)) {
    let hit = pins.filter(x => x.num === key);
    if (!hit.length) hit = pins.filter(x => x.name === key);
    if (!hit.length) throw new Error(`${p.lib} ${p.value}: no pin '${key}'`);
    for (const h of hit) p.pinNets.set(h.num, net);
  }
  for (const pin of pins) if (!p.pinNets.has(pin.num)) throw new Error(`${p.lib} ${p.value}: pin ${pin.num} ${pin.name} unassigned`);
  for (const net of p.pinNets.values()) {
    if (net === 'NC') continue;
    if (!allNets.has(net)) allNets.set(net, new Set());
    allNets.get(net).add(sh.file);
  }
}
const isGlobal = net => !POWER.has(net) && allNets.get(net).size > 1;

// orientation: 2-pin vertical parts get their GND pin at the bottom; plain
// 2-pin passives between two signals lie horizontally.
for (const sh of sheets) for (const r of sh.rows) for (const p of r.parts) {
  const {pins} = sym(p.lib);
  p.rot = 0;
  if (pins.length === 2 && pins.every(x => x.x === 0)) {
    const nets = pins.map(x => p.pinNets.get(x.num));
    const top = pins[0].y > pins[1].y ? 0 : 1;
    if (nets.some(n => POWER.has(n))) {
      if (nets[top] === 'GND' || (POWER.has(nets[1 - top]) && nets[1 - top] !== 'GND' && !POWER.has(nets[top]))) p.rot = 180;
    } else p.rot = 90;
  }
}

// ---- layout ---------------------------------------------------------------------
const labelLen = net => (POWER.has(net) ? 5 : net.length * 1.0 + (isGlobal(net) ? 5 : 2.5));
function extent(p) {
  const {pins} = sym(p.lib);
  let x0 = -2.54, x1 = 2.54, y0 = -2.54, y1 = 2.54;  // y down (schematic)
  for (const pin of pins) {
    const [rx, ry] = rot(pin.x, pin.y, p.rot);
    const sx = rx, sy = -ry;
    const o = dirOf(pin.angle + 180 + p.rot);
    const net = p.pinNets.get(pin.num);
    const L = net === 'NC' ? 1 : labelLen(net);
    const ex = sx + (o === 0 ? L : o === 180 ? -L : 0), ey = sy + (o === 270 ? L : o === 90 ? -L : 0);
    x0 = Math.min(x0, sx, ex); x1 = Math.max(x1, sx, ex); y0 = Math.min(y0, sy, ey); y1 = Math.max(y1, sy, ey);
  }
  if (pins.every(pin => dirOf(pin.angle + 180 + p.rot) === 180)) x1 += 8;  // connector bodies
  if (pins.length <= 1) { y0 -= 3; y1 += 3; }
  return {x0, x1, y0: y0 - 3, y1: y1 + 3};
}
// Where a part's reference and value go: a side without pins.
function textSide(p) {
  const {pins} = sym(p.lib);
  const sides = new Set(pins.map(pin => dirOf(pin.angle + 180 + p.rot)));
  let x0 = 0, x1 = 0, y0 = 0, y1 = 0;
  for (const pin of pins) {
    const [rx, ry] = rot(pin.x, pin.y, p.rot);
    x0 = Math.min(x0, rx); x1 = Math.max(x1, rx); y0 = Math.min(y0, -ry); y1 = Math.max(y1, -ry);
  }
  const vertical2 = pins.length === 2 && pins.every(x => x.x === 0) && (p.rot === 0 || p.rot === 180);
  if (vertical2) return {side: 'right', ref: [2.54, -1.27], val: [2.54, 1.27], just: p.rot === 180 ? 'right' : 'left'};
  if (pins.length === 2 && pins.every(x => x.x === 0)) return {side: 'top', ref: [0, -3.2], val: [0, 3.6], just: null};
  if (!sides.has(0)) return {side: 'right', ref: [x1 + 3, -1.27], val: [x1 + 3, 1.27], just: 'left'};
  if (!sides.has(180)) return {side: 'left', ref: [x0 - 3, -1.27], val: [x0 - 3, 1.27], just: 'right'};
  if (!sides.has(90)) return {side: 'top', ref: [0, y0 - 5], val: [0, y0 - 2.5], just: null};
  if (!sides.has(270)) return {side: 'bottom', ref: [0, y1 + 2.5], val: [0, y1 + 5], just: null};
  return {side: 'corner', ref: [x1 + 1, y0 - 2.5], val: [x1 + 1, y0], just: 'left'};
}
const textW = p => Math.max(p.value.length, (p.ref || 'XX99').length) * 1.05 + 2;

const PAGE = {left: 20, top: 40, bottom: 250, colW: 190, gap: 8};
for (const sh of sheets) {
  let colX = PAGE.left, colRight = PAGE.left, y = PAGE.top;
  for (const r of sh.rows) {
    // lay the row out at (0, 0), then move it to the column
    let x = 0, rowTop = 6, rowH = 0, w = 0;
    for (const p of r.parts) {
      const e = extent(p);
      const ts = textSide(p);
      if (ts.side === 'right') e.x1 = Math.max(e.x1, ts.ref[0] + textW(p));
      if (ts.side === 'left') e.x0 = Math.min(e.x0, ts.ref[0] - textW(p));
      if (ts.side === 'top') e.y0 = Math.min(e.y0, ts.ref[1] - 2);
      if (ts.side === 'bottom') e.y1 = Math.max(e.y1, ts.val[1] + 2);
      const pw = e.x1 - e.x0 + 4;
      if (x + pw > PAGE.colW && x > 0) { x = 0; rowTop += rowH + 4; rowH = 0; }
      p.rx = x - e.x0;
      p.ry = rowTop - e.y0;
      x += pw;
      w = Math.max(w, x);
      rowH = Math.max(rowH, e.y1 - e.y0);
    }
    const h = rowTop + rowH + PAGE.gap;
    if (y + h > PAGE.bottom && y > PAGE.top) { colX = colRight + 12; y = PAGE.top; }
    r.x = colX;
    r.y = y;
    for (const p of r.parts) { p.x = g(colX + p.rx); p.y = g(y + p.ry); }
    y += h;
    colRight = Math.max(colRight, colX + w);
  }
}

// ---- references -------------------------------------------------------------
const prefixOf = p => p.lib.startsWith('power:') ? (p.lib === 'power:PWR_FLAG' ? '#FLG' : '#PWR')
  : S.property(sym(p.lib).def, 'Reference').replace(/\?$/, '');
const counters = {};
const used = new Set();
for (const sh of sheets) for (const r of sh.rows) for (const p of r.parts) if (p.ref) used.add(p.ref);
for (const sh of sheets) for (const r of sh.rows) for (const p of r.parts) {
  if (p.ref) continue;
  const pre = prefixOf(p);
  do counters[pre] = (counters[pre] || 0) + 1; while (used.has(pre + counters[pre]));
  p.ref = pre + counters[pre];
  used.add(p.ref);
}
let pwrCount = 0;

// ---- emit ---------------------------------------------------------------------
const ROOT_UUID = uuid();
const font = (size = 1.27) => ['font', ['size', size, size]];
const eff = (just, hide, size) => ['effects', font(size), ...(just ? [['justify', ...just.split(' ')]] : []), ...(hide ? [['hide', 'yes']] : [])];
const prop = (k, v, x, y, just, hide, angle = 0) => ['property', q(k), q(v), ['at', num(x), num(y), angle], eff(just, hide)];

function placeSymbol(lib, ref, value, fp, x, y, r, pinNums, sheetUuid, extra = {}) {
  const props = [
    prop('Reference', ref, extra.refAt ? extra.refAt[0] : x, extra.refAt ? extra.refAt[1] : y - 5, extra.refJust, !!extra.hideRef, extra.fieldAngle || 0),
    prop('Value', value, extra.valAt ? extra.valAt[0] : x, extra.valAt ? extra.valAt[1] : y + 5, extra.valJust, false, extra.fieldAngle || 0),
    prop('Footprint', fp || '', x, y, null, true),
    prop('Datasheet', '~', x, y, null, true),
  ];
  if (extra.mpn) props.push(prop('MPN', extra.mpn, x, y, null, true));
  if (extra.note) props.push(prop('Note', extra.note, x, y, null, true));
  if (extra.lcsc) props.push(prop('LCSC', extra.lcsc, x, y, null, true));
  return ['symbol', ['lib_id', q(lib)], ['at', num(x), num(y), r], ['unit', 1],
    ['exclude_from_sim', 'no'], ['in_bom', extra.virtual ? 'no' : 'yes'], ['on_board', extra.virtual ? 'no' : 'yes'], ['dnp', 'no'],
    ['uuid', q(uuid())], ...props,
    ...pinNums.map(n => ['pin', q(n), ['uuid', q(uuid())]]),
    ['instances', ['project', q(PROJECT), ['path', q(`/${ROOT_UUID}/${sheetUuid}`), ['reference', q(ref)], ['unit', 1]]]]];
}

function powerSymbol(net, x, y, outward, sheetUuid) {
  const lib = 'power:' + net;
  const graphicUp = net !== 'GND';
  const r = dirOf(outward - (graphicUp ? 90 : 270));
  pwrCount++;
  const ref = '#PWR' + String(pwrCount).padStart(3, '0');
  // value text beyond the graphic
  // horizontal pins: value text level with the pin, beyond the graphic
  let valAt, valJust = null, fieldAngle = 0;
  if (outward === 0 || outward === 180) {
    const off = 3.6 + net.length * 0.55;  // centred text, clear of the graphic
    valAt = [x + (outward === 0 ? off : -off), y + 0.5];
    fieldAngle = 90;
  } else {
    const [vx, vy] = rot(0, graphicUp ? 3.81 : -3.81, r);
    valAt = [x + vx, y - vy + (outward === 270 ? 1 : 0)];
  }
  return placeSymbol(lib, ref, net, '', x, y, r, ['1'], sheetUuid,
    {hideRef: true, refAt: [x, y], valAt, valJust, fieldAngle, virtual: true});
}

function labelFor(net, x, y, outward) {
  const angle = outward;  // 0 right, 90 up, 180 left, 270 down
  const just = angle === 180 || angle === 270 ? 'right' : 'left';
  if (isGlobal(net)) {
    return ['global_label', q(net), ['shape', 'bidirectional'], ['at', num(x), num(y), angle], ['fields_autoplaced', 'yes'],
      eff(just), ['uuid', q(uuid())],
      ['property', q('Intersheetrefs'), q('${INTERSHEET_REFS}'), ['at', num(x), num(y), angle], eff(just, true)]];
  }
  return ['label', q(net), ['at', num(x), num(y), angle], ['fields_autoplaced', 'yes'], eff(just + ' bottom'), ['uuid', q(uuid())]];
}

function text(t, x, y, size = 1.6) {
  return ['text', q(t), ['exclude_from_sim', 'no'], ['at', num(x), num(y), 0], eff('left bottom', false, size), ['uuid', q(uuid())]];
}

function libSymbols(libs) {
  return ['lib_symbols', ...[...libs].sort().map(l => sym(l).def)];
}

function titleBlock(title, comment) {
  return ['title_block', ['title', q(title)], ['date', q(new Date().toISOString().slice(0, 10))], ['rev', q('0.1')],
    ['company', q('MashTouch (GPL-3.0-or-later)')], ['comment', 1, q(comment)]];
}

const bom = [];
for (const sh of sheets) {
  const items = [];
  const libs = new Set();
  items.push(text(sh.title, PAGE.left, 22, 3));
  sh.notes.forEach((n, i) => items.push(text(n, PAGE.left, 28 + i * 3.2, 1.6)));
  for (const r of sh.rows) {
    items.push(text(r.caption, r.x, r.y + 3, 2));
    for (const p of r.parts) {
      const s = sym(p.lib);
      libs.add(p.lib);
      const ts = textSide(p);
      const ex = {refAt: [p.x + ts.ref[0], p.y + ts.ref[1]], valAt: [p.x + ts.val[0], p.y + ts.val[1]],
        refJust: ts.just, valJust: ts.just, fieldAngle: p.rot === 90 || p.rot === 270 ? 90 : 0};
      items.push(placeSymbol(p.lib, p.ref, p.value, p.fp, p.x, p.y, p.rot, s.pins.map(x => x.num), sh.uuid,
        Object.assign(ex, {mpn: p.mpn, note: p.note, lcsc: p.lcsc, virtual: p.virtual})));
      if (!p.virtual) bom.push({ref: p.ref, value: p.value, fp: p.fp, mpn: p.mpn || '', lcsc: p.lcsc || '', note: p.note || '', sheet: sh.name});
      for (const pin of s.pins) {
        const [rx, ry] = rot(pin.x, pin.y, p.rot);
        const px = num(p.x + rx), py = num(p.y - ry);
        const out = dirOf(pin.angle + 180 + p.rot);
        const net = p.pinNets.get(pin.num);
        if (net === 'NC') items.push(['no_connect', ['at', px, py], ['uuid', q(uuid())]]);
        else if (POWER.has(net)) { items.push(powerSymbol(net, px, py, out, sh.uuid)); libs.add('power:' + net); }
        else items.push(labelFor(net, px, py, out));
      }
    }
  }
  const doc = ['kicad_sch', ['version', 20250114], ['generator', q('eeschema')], ['generator_version', q('9.0')],
    ['uuid', q(sh.uuid)], ['paper', q('A3')], titleBlock(sh.name, sh.title), libSymbols(libs), ...items, ['embedded_fonts', 'no']];
  fs.writeFileSync(path.join(OUT, sh.file), S.write(doc) + '\n');
}

// root sheet with one sheet symbol per page
{
  const items = [];
  items.push(text('MashTouch I/O backpack: MCU-less board behind the Waveshare ESP32-S3-Touch-LCD-3.5 (header J8).', 20, 25, 3));
  const lines = ['Sheets are connected by global labels (signal names as in src/board/pins.h) and the power nets +12V, +5V, +3V3, GND.',
    'Placement plan: docs/backpack-layout.md. Pin numbers of J8 follow the Waveshare pinout image.',
    'Generated by tools/kicad/backpack_sch.js as a starting point; edit in KiCad from now on.'];
  lines.forEach((l, i) => items.push(text(l, 20, 32 + i * 4, 1.8)));
  sheets.forEach((sh, i) => {
    const x = 25 + (i % 3) * 125, y = 60 + Math.floor(i / 3) * 50, w = 100, h = 30;
    items.push(['sheet', ['at', x, y], ['size', w, h], ['exclude_from_sim', 'no'], ['in_bom', 'yes'], ['on_board', 'yes'], ['dnp', 'no'],
      ['fields_autoplaced', 'yes'], ['stroke', ['width', 0.1524], ['type', 'solid']], ['fill', ['color', 0, 0, 0, 0.0]],
      ['uuid', q(sh.uuid)],
      ['property', q('Sheetname'), q(sh.name), ['at', x, num(y - 0.7116), 0], eff('left bottom')],
      ['property', q('Sheetfile'), q(sh.file), ['at', x, num(y + h + 0.5884), 0], eff('left top')],
      ['instances', ['project', q(PROJECT), ['path', q('/' + ROOT_UUID), ['page', q(String(i + 2))]]]]]);
    items.push(text(sh.title, x + 2, y + 8, 1.6));
  });
  const doc = ['kicad_sch', ['version', 20250114], ['generator', q('eeschema')], ['generator_version', q('9.0')],
    ['uuid', q(ROOT_UUID)], ['paper', q('A3')], titleBlock('I/O backpack', 'MashTouch brew controller'), ['lib_symbols'],
    ...items, ['sheet_instances', ['path', q('/'), ['page', q('1')]]], ['embedded_fonts', 'no']];
  fs.writeFileSync(path.join(OUT, PROJECT + '.kicad_sch'), S.write(doc) + '\n');
}

// project file (KiCad fills in the rest on first save)
const proFile = path.join(OUT, PROJECT + '.kicad_pro');
if (!fs.existsSync(proFile)) {
  fs.writeFileSync(proFile, JSON.stringify({meta: {filename: PROJECT + '.kicad_pro', version: 3},
    sheets: [[ROOT_UUID, 'Root'], ...sheets.map(s => [s.uuid, s.name])]}, null, 2) + '\n');
}

console.log(`${sheets.length} sheets, ${bom.length} parts, ${allNets.size} nets -> ${OUT}`);
