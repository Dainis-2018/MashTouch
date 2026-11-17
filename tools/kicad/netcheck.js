// MashTouch - what changed in the backpack netlist?
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Exports the netlist of hardware/backpack/backpack.kicad_sch with kicad-cli
// and compares it, pin by pin, with hardware/backpack/netlist-reference.txt
// (the last reviewed state). It lists every pin that moved to another net,
// pins that appeared or went away, and nets that were only renamed. Review
// the list after a schematic edit; when every change is intended, accept it:
//
//   node tools/kicad/netcheck.js            compare (exit code 1 if anything changed)
//   node tools/kicad/netcheck.js --update   save the current netlist as the new reference
//
// kicad-cli is taken from KICAD_CLI, else the default KiCad 10 install path.
'use strict';
const fs = require('fs');
const os = require('os');
const path = require('path');
const {execFileSync} = require('child_process');

const DIR = path.join(__dirname, '..', '..', 'hardware', 'backpack');
const SCH = path.join(DIR, 'backpack.kicad_sch');
const REF = path.join(DIR, 'netlist-reference.txt');
const CLI = process.env.KICAD_CLI || (process.platform === 'win32'
  ? 'C:/Program Files/KiCad/10.0/bin/kicad-cli.exe' : 'kicad-cli');

function exportNetlist() {
  const out = path.join(fs.mkdtempSync(path.join(os.tmpdir(), 'netcheck-')), 'backpack.net');
  execFileSync(CLI, ['sch', 'export', 'netlist', '-o', out, SCH], {stdio: ['ignore', 'ignore', 'inherit']});
  const s = fs.readFileSync(out, 'utf8');
  const pins = {};
  for (const blk of s.split(/\n\t\t\(net\b/).slice(1)) {
    const name = blk.match(/\(name "([^"]*)"\)/)[1];
    for (const m of blk.matchAll(/\(ref "([^"]+)"\)\s*\(pin "([^"]+)"\)/g)) {
      if (!m[1].startsWith('#')) pins[m[1] + '.' + m[2]] = name;
    }
  }
  return pins;
}
const byPin = (a, b) => a.localeCompare(b, undefined, {numeric: true});
function save(pins) {
  const lines = Object.keys(pins).sort(byPin).map(k => `${k}\t${pins[k]}`);
  fs.writeFileSync(REF, '# backpack netlist reference (tools/kicad/netcheck.js --update): pin<TAB>net\n' + lines.join('\n') + '\n');
}
function load() {
  const pins = {};
  for (const line of fs.readFileSync(REF, 'utf8').split('\n')) {
    if (!line || line.startsWith('#')) continue;
    const [k, n] = line.split('\t');
    pins[k] = n;
  }
  return pins;
}

const now = exportNetlist();
if (process.argv.includes('--update') || !fs.existsSync(REF)) {
  save(now);
  console.log(`saved ${Object.keys(now).length} pins as ${path.relative(process.cwd(), REF)}`);
  process.exit(0);
}
const was = load();

// nets that kept exactly the same pins but changed name are renames
const group = pins => {
  const g = {};
  for (const [k, n] of Object.entries(pins)) (g[n] = g[n] || []).push(k);
  for (const n in g) g[n].sort(byPin);
  return g;
};
const gWas = group(was), gNow = group(now);
const nowBySet = new Map(Object.entries(gNow).map(([n, ks]) => [ks.join(' '), n]));
const renamed = {};
for (const [n, ks] of Object.entries(gWas)) {
  const m = nowBySet.get(ks.join(' '));
  if (m && m !== n) renamed[n] = m;
}
const out = [];
for (const [a, b] of Object.entries(renamed)) out.push(`renamed  ${a} -> ${b}`);
for (const k of Object.keys({...was, ...now}).sort(byPin)) {
  const a = was[k], b = now[k];
  if (a === b || (a !== undefined && renamed[a] === b)) continue;
  if (a === undefined) out.push(`added    ${k} on ${b}`);
  else if (b === undefined) out.push(`removed  ${k} (was on ${a})`);
  else out.push(`moved    ${k}: ${a} -> ${b}`);
}
if (!out.length) {
  console.log(`no changes: ${Object.keys(now).length} pins as in the reference`);
} else {
  console.log(out.join('\n'));
  console.log(`\n${out.length} change(s). If they are all intended: node tools/kicad/netcheck.js --update`);
  process.exit(1);
}
