// MashTouch - KiCad helper: PCB order files for the backpack (PCBWay, JLCPCB).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// node tools/kicad/fab_export.js
//
// Runs kicad-cli on hardware/backpack and writes hardware/backpack/fab/:
//   backpack-gerbers.zip     Gerbers + Excellon drill files
//   backpack-cpl.csv         placement of the assembled parts (Designator, Mid X, Mid Y, Layer, Rotation)
//   backpack-bom-pcbway.csv  assembly BOM with manufacturer and MPN (PCBWay sources the parts)
//   backpack-bom-jlc.csv     assembly BOM with LCSC numbers (JLCPCB)
// and regenerates hardware/backpack/bom.csv (the readable BOM, all parts).
//
// Parts come from the schematic; the chosen part and whether it is assembled
// ("smt"), hand-soldered ("hand") or not a part ("none") come from
// hardware/backpack/parts.csv, matched by Value + Footprint (Value "*" matches
// any value). A schematic part with no row there is an error.
'use strict';
const fs = require('fs');
const path = require('path');
const os = require('os');
const {execFileSync} = require('child_process');

const root = path.resolve(__dirname, '../..');
const dir = path.join(root, 'hardware/backpack');
const fab = path.join(dir, 'fab');
const pcb = path.join(dir, 'backpack.kicad_pcb');
const sch = path.join(dir, 'backpack.kicad_sch');

function kicadCli() {
  if (process.env.KICAD_CLI) return process.env.KICAD_CLI;
  const win = 'C:/Program Files/KiCad/10.0/bin/kicad-cli.exe';
  return fs.existsSync(win) ? win : 'kicad-cli';
}
const cli = kicadCli();
const run = args => execFileSync(cli, args, {stdio: ['ignore', 'pipe', 'pipe']}).toString();

// Minimal CSV: quoted or bare fields, no newlines inside fields.
function readCsv(file) {
  const rows = fs.readFileSync(file, 'utf8').split(/\r?\n/).filter(l => l.trim()).map(line => {
    const out = []; let i = 0;
    while (i <= line.length) {
      if (line[i] === '"') {
        let s = ''; i++;
        for (;;) {
          if (line[i] === '"' && line[i + 1] === '"') { s += '"'; i += 2; }
          else if (line[i] === '"') { i++; break; }
          else s += line[i++];
        }
        out.push(s); i++;
      } else {
        const j = line.indexOf(',', i); const end = j < 0 ? line.length : j;
        out.push(line.slice(i, end)); i = end + 1;
      }
    }
    return out;
  });
  const head = rows.shift();
  return rows.map(r => Object.fromEntries(head.map((h, k) => [h, r[k] === undefined ? '' : r[k]])));
}
const q = v => '"' + String(v).replace(/"/g, '""') + '"';
const writeCsv = (file, head, rows) =>
  fs.writeFileSync(file, [head.map(q).join(',')].concat(rows.map(r => r.map(q).join(','))).join('\n') + '\n');

// Natural order for references: C2 before C10.
const refKey = r => { const m = /^([A-Z]+)(\d+)$/.exec(r); return m ? [m[1], +m[2]] : [r, 0]; };
const byRef = (a, b) => { const x = refKey(a), y = refKey(b); return x[0] < y[0] ? -1 : x[0] > y[0] ? 1 : x[1] - y[1]; };

const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'fab-'));
fs.rmSync(fab, {recursive: true, force: true});
fs.mkdirSync(fab, {recursive: true});

// --- parts: schematic, one row per reference
run(['sch', 'export', 'bom', '--fields', 'Reference,Value,Footprint', '--group-by', '',
  '--exclude-dnp', '-o', path.join(tmp, 'sch.csv'), sch]);
const parts = readCsv(path.join(tmp, 'sch.csv'));
const table = readCsv(path.join(dir, 'parts.csv'));
const lookup = p => table.find(t => t.Footprint === p.Footprint && (t.Value === p.Value || t.Value === '*'));

let errors = 0; const warn = [];
const groups = new Map();   // Value|Footprint|LCSC -> {row, refs}
for (const p of parts) {
  const t = lookup(p);
  if (!t) { console.error(`no row in parts.csv for ${p.Reference}: "${p.Value}" ${p.Footprint}`); errors++; continue; }
  p.t = t;
  const k = [p.Value, p.Footprint, t.LCSC].join('|');
  if (!groups.has(k)) groups.set(k, {p, t, refs: []});
  groups.get(k).refs.push(p.Reference);
}
if (errors) process.exit(1);

const sorted = [...groups.values()].map(g => (g.refs.sort(byRef), g)).sort((a, b) => byRef(a.refs[0], b.refs[0]));
for (const g of sorted) {
  if (g.t.Fit === 'smt' && !g.t.MPN) warn.push(`${g.refs.join(',')} (${g.p.Value}): no MPN`);
  if (g.t.Fit === 'smt' && !g.t.LCSC) warn.push(`${g.refs.join(',')} (${g.p.Value}): no LCSC number (JLCPCB only)`);
}
const pkg = g => g.p.Footprint.replace(/^.*:/, '');

// readable BOM
writeCsv(path.join(dir, 'bom.csv'), ['Refs', 'Value', 'Footprint', 'Manufacturer', 'MPN', 'Description', 'LCSC', 'Fit', 'Note', 'Qty'],
  sorted.map(g => [g.refs.join(','), g.p.Value, g.p.Footprint, g.t.Manufacturer, g.t.MPN, g.t.Description, g.t.LCSC, g.t.Fit, g.t.Note, g.refs.length]));

// assembly BOMs: assembled parts only
const smt = sorted.filter(g => g.t.Fit === 'smt');
writeCsv(path.join(fab, 'backpack-bom-pcbway.csv'),
  ['Item #', 'Designator', 'Qty', 'Manufacturer', 'Mfg Part #', 'Description / Value', 'Package/Footprint', 'Type', 'Your Instructions / Notes'],
  smt.map((g, i) => [i + 1, g.refs.join(','), g.refs.length, g.t.Manufacturer, g.t.MPN, g.t.Description || g.p.Value, pkg(g), 'SMD', g.t.Note]));
writeCsv(path.join(fab, 'backpack-bom-jlc.csv'), ['Comment', 'Designator', 'Footprint', 'LCSC Part #'],
  smt.map(g => [g.p.Value, g.refs.join(','), pkg(g), g.t.LCSC]));

// --- placement
run(['pcb', 'export', 'pos', '--format', 'csv', '--units', 'mm', '--side', 'both', '-o', path.join(tmp, 'pos.csv'), pcb]);
const smtRefs = new Set(smt.flatMap(g => g.refs));
const pos = readCsv(path.join(tmp, 'pos.csv')).filter(r => smtRefs.has(r.Ref)).sort((a, b) => byRef(a.Ref, b.Ref));
for (const r of smtRefs) if (!pos.find(p => p.Ref === r)) { console.error(`${r} is not on the PCB`); errors++; }
if (errors) process.exit(1);
const num = v => (+v).toFixed(4).replace(/\.?0+$/, '');
writeCsv(path.join(fab, 'backpack-cpl.csv'), ['Designator', 'Mid X', 'Mid Y', 'Layer', 'Rotation'],
  pos.map(r => [r.Ref, num(r.PosX), num(r.PosY), r.Side === 'bottom' ? 'Bottom' : 'Top', num(((+r.Rot % 360) + 360) % 360)]));

// --- Gerbers and drill files
const g = path.join(tmp, 'gerbers');
fs.mkdirSync(g);
run(['pcb', 'export', 'gerbers', '--layers',
  'F.Cu,B.Cu,F.Paste,B.Paste,F.Silkscreen,B.Silkscreen,F.Mask,B.Mask,Edge.Cuts',
  '--subtract-soldermask', '-o', g + '/', pcb]);
run(['pcb', 'export', 'drill', '--format', 'excellon', '--excellon-units', 'mm',
  '--excellon-zeros-format', 'decimal', '--excellon-oval-format', 'alternate', '--excellon-separate-th',
  '-o', g + '/', pcb]);
const files = fs.readdirSync(g);
const zip = path.join(fab, 'backpack-gerbers.zip');
if (process.platform === 'win32')
  execFileSync('powershell', ['-NoProfile', '-Command',
    `Compress-Archive -Path '${g.replace(/'/g, "''")}\\*' -DestinationPath '${zip.replace(/'/g, "''")}' -Force`]);
else
  execFileSync('zip', ['-j', '-q', zip].concat(files.map(f => path.join(g, f))));
fs.rmSync(tmp, {recursive: true, force: true});

const hand = sorted.filter(x => x.t.Fit === 'hand');
console.log(`gerbers: ${files.length} files -> ${path.relative(root, zip)}`);
console.log(`assembled: ${smt.length} lines, ${smtRefs.size} parts; hand-soldered: ${hand.flatMap(x => x.refs).join(' ')}`);
for (const w of warn) console.log('warning: ' + w);
