#!/usr/bin/env node
// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// PC mock of the controller for trying the web UI without hardware.
// Serves web/index.html and imitates the JSON API (docs/web-api.md) with a
// simulated kettle, heater control, the automatic brew sequence, manual
// mode and autotune. A simplified model of the firmware, not the firmware.
//
//   node tools/mock/server.js [--port 8080] [--speed 20]
//
// Recipes and settings are kept in tools/mock/data.json between runs.
// No npm packages needed.
'use strict';

const http = require('http');
const fs = require('fs');
const path = require('path');

const args = process.argv.slice(2);
const arg = (name, def) => {
  const i = args.indexOf('--' + name);
  return i >= 0 && args[i + 1] ? args[i + 1] : def;
};
const PORT = parseInt(arg('port', '8080'), 10);
const ROOT = path.resolve(__dirname, '..', '..');
const PAGE = path.join(ROOT, 'web', 'index.html');
const DATA = path.join(__dirname, 'data.json');

// ---------------------------------------------------------------------------
// Settings and recipes (same JSON shape as the firmware)
// ---------------------------------------------------------------------------

function defaultSettings() {
  const out = (role) => ({role, index: 1, inverted: false});
  return {
    version: 1,
    heater: {kp: 30, ki: 0.05, kd: 0, sampleMs: 2000, windowMs: 5000, fullPowerBandC: 3.5, nearBoilBandC: 1,
             boilPowerPct: 80, boilHystC: 1, mashPowerPct: 100, controlSensor: 'kettle'},
    autotune: {outputHighPct: 100, outputLowPct: 0, hysteresisC: 0.2, cycles: 4, timeoutMin: 240, pumpOn: true},
    pump: {cycleMin: 10, restMin: 2, restRestartDropC: 1, stopTempC: 95, stopHystC: 2, externalSensor: false,
           inBoil: false, duringCooling: false, autoPrime: true, primePulses: 5},
    process: {boilTempC: 99, reachedTolC: 0.2, skipRemoveMalt: false, heatDuringRemoveMalt: false, iodineTest: false,
              iodineTimeoutMin: 0, whirlpool: 'off', whirlpoolMin: 5, coolingTargetC: 25, boilWarnC: 3},
    fan: {runOnSec: 120, onTempC: 40, fullTempC: 55, alarmTempC: 70, minPct: 40},
    safety: {maxTempC: 110, level: 'ignore', runawayC: 5},
    sparge: {tempC: 76, hystC: 1, autoWithBrew: true},
    sound: {volume: 70},
    sim: {speed: parseFloat(arg('speed', '20')), liters: 20, heaterW: 2500, lossWPerK: 8, ambientC: 20, startC: 15,
          sensorTauS: 10, driveOutputs: false},
    net: {ssid: 'HomeWifi', password: 'x', hostname: 'mashtouch', apPassword: 'mashtouch', webPassword: '', timezone: 'EET-2EEST,M3.5.0/3,M10.5.0/4'},
    outputs: [out('heater'), out('sparge'), out('none'), out('none'), out('pump'), out('fan')],
    sensors: [],
    fahrenheit: false,
    brightness: 200,
    activeRecipe: 'single-infusion',
  };
}

function defaultRecipe() {
  const st = (name, tempC, minutes) => ({name, tempC, minutes, pump: 'on'});
  return {
    version: 3, name: 'Single infusion', mash: true,
    mashIn: st('Mash in', 68, 0), rests: [st('Saccharification', 66, 60)], mashOut: st('Mash out', 76, 10),
    boilMinutes: 60,
    additions: [
      {name: 'Bittering hops', atMinutes: 60, amount: 0, unit: 'g'},
      {name: 'Aroma hops', atMinutes: 15, amount: 0, unit: 'g'},
      {name: 'Flameout hops', atMinutes: 0, amount: 0, unit: 'g'},
    ],
  };
}

let db = {settings: defaultSettings(), recipes: {'single-infusion': defaultRecipe()}, logs: []};
try {
  const saved = JSON.parse(fs.readFileSync(DATA, 'utf8'));
  const def = defaultSettings();
  for (const [k, v] of Object.entries(saved.settings || {}))   // keep defaults for settings added later
    def[k] = v && typeof v === 'object' && !Array.isArray(v) ? Object.assign(def[k] || {}, v) : v;
  db.settings = def;
  db.recipes = saved.recipes || db.recipes;
  db.logs = saved.logs || [];
} catch (e) { /* first run */ }
const save = () => fs.writeFileSync(DATA, JSON.stringify(db, null, 2));
const S = () => db.settings;
const activeRecipe = () => db.recipes[S().activeRecipe] || Object.values(db.recipes)[0] || defaultRecipe();

function recipeId(name) {
  let id = String(name).toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, '').slice(0, 32).replace(/-+$/, '');
  return id || 'recipe';
}
function freeId(name) {
  const base = recipeId(name);
  if (!db.recipes[base]) return base;
  for (let n = 2; ; n++) if (!db.recipes[`${base}-${n}`]) return `${base}-${n}`;
}

// Same rule as settings::arrangeMash(): first step = mash in, the step
// marked as mash out (or a last step >= 75 C) = mash out, others = rests.
function arrangeMash(r, steps, outIndex) {
  r.rests = [];
  if (!steps.length) { r.mash = false; r.mashIn = {name: 'Mash in', tempC: 67, minutes: 0, pump: 'on'};
    r.mashOut = {name: 'Mash out', tempC: 76, minutes: 10, pump: 'on'}; return; }
  r.mash = true;
  r.mashIn = steps[0];
  let out = null;
  steps.slice(1).forEach((s, i) => { if (i + 1 === outIndex) out = s; else r.rests.push(s); });
  if (!out && outIndex < 0 && r.rests.length && r.rests[r.rests.length - 1].tempC >= 75) out = r.rests.pop();
  if (!out) {
    const last = r.rests.length ? r.rests[r.rests.length - 1] : r.mashIn;
    out = {name: 'Mash out', tempC: last.tempC, minutes: 0, pump: last.pump};
  }
  r.mashOut = out;
}

function normalizeRecipe(j) {
  const step = (s, name) => ({name: (s && s.name) || name, tempC: +((s && s.tempC) || 0), minutes: Math.round(+((s && s.minutes) || 0)),
    pump: ['off', 'on', 'cycle'].includes(s && s.pump) ? s.pump : 'on'});
  const r = {version: 3, name: j.name || 'Recipe', mash: j.mash !== false, spargeTempC: +(j.spargeTempC || 0),
    mashIn: step(j.mashIn, 'Mash in'), rests: (j.rests || []).map((s, i) => step(s, 'Rest ' + (i + 1))),
    mashOut: step(j.mashOut, 'Mash out'), boilMinutes: Math.round(+(j.boilMinutes ?? 60)),
    additions: (j.additions || []).map(a => ({name: a.name || 'Addition', atMinutes: Math.round(+(a.atMinutes || 0)),
      amount: +(a.amount || 0), unit: a.unit || ''}))};
  if ((j.version || 1) < 3 && Array.isArray(j.steps)) {  // old firmware format
    let outIndex = -1;
    const steps = j.steps.map((s, i) => { if (s.mashOut) outIndex = i; return step(s, 'Step ' + (i + 1)); });
    if ((j.version || 1) < 2 && j.mashInTempC > 0) { steps.unshift(step({tempC: j.mashInTempC}, 'Mash in')); if (outIndex >= 0) outIndex++; }
    arrangeMash(r, steps, outIndex);
  }
  return r;
}

function validate(r) {
  if (r.mash) {
    const all = [r.mashIn, ...r.rests, r.mashOut];
    for (let i = 0; i < all.length; i++) {
      const w = i === 0 ? 'mash in' : i === all.length - 1 ? 'mash out' : 'rest ' + i;
      if (!(all[i].tempC >= 1 && all[i].tempC <= 100)) return w + ': temperature must be 1..100 C';
      if (all[i].minutes > 1440) return w + ': more than 24 h';
    }
  }
  if (r.boilMinutes > 600) return 'boil longer than 10 h';
  if (r.additions.length > 32) return 'more than 32 additions';
  return null;
}

// ---- BeerXML (same mapping as src/storage/beerxml.cpp) -----------------------

function fold(s) {
  return s.replace(/ß/g, 'ss').replace(/Æ/g, 'AE').replace(/æ/g, 'ae').replace(/Œ/g, 'OE').replace(/œ/g, 'oe')
    .replace(/[‘’‚]/g, "'").replace(/[“”„]/g, '"').replace(/[–—]/g, '-').replace(/…/g, '...').replace(/€/g, 'EUR')
    .replace(/[ØøŁłĐđ]/g, c => ({'Ø': 'O', 'ø': 'o', 'Ł': 'L', 'ł': 'l', 'Đ': 'D', 'đ': 'd'}[c]))
    .normalize('NFD').replace(/[̀-ͯ]/g, '').replace(/[^\x20-\x7e°]/g, '?');
}
const entities = s => s.replace(/&(#x[0-9a-f]+|#\d+|amp|lt|gt|quot|apos);/gi, (m, e) =>
  e[0] === '#' ? String.fromCodePoint(e[1] === 'x' || e[1] === 'X' ? parseInt(e.slice(2), 16) : parseInt(e.slice(1), 10))
               : {amp: '&', lt: '<', gt: '>', quot: '"', apos: "'"}[e.toLowerCase()]);

function parseBeerXml(buf) {
  const head = buf.slice(0, 200).toString('latin1');
  const latin1 = /encoding\s*=\s*["'](iso-8859-1|windows-1252|latin-?1)/i.test(head);
  const xml = buf.toString(latin1 ? 'latin1' : 'utf8');
  const res = {recipes: [], notes: []};
  const stack = [];
  let text = '', inRecipe = false, rec, boil, steps, pending, skipped, step, item, kind;
  const num = v => parseFloat(String(v).replace(',', '.')) || 0;
  const clean = t => fold(entities(t)).trim();
  const re = /<!--[\s\S]*?-->|<\?[\s\S]*?\?>|<!\[CDATA\[([\s\S]*?)\]\]>|<![^>]*>|<(\/?)([^\s/>]+)[^>]*?(\/?)>|([^<]+)/g;
  let m;
  const start = (tag, parent) => {
    if (tag === 'RECIPE' && (!parent || parent === 'RECIPES')) {
      inRecipe = true; rec = {name: ''}; boil = -1; steps = []; pending = []; skipped = [];
    } else if (!inRecipe) return;
    else if (tag === 'MASH_STEP') step = {name: '', tempC: null, minutes: 0, pump: 'on'};
    else if (tag === 'HOP' && parent === 'HOPS') { item = {use: '', amount: 0, time: 0, display: '', isWeight: true, name: ''}; kind = 'hop'; }
    else if (tag === 'MISC' && parent === 'MISCS') { item = {use: '', amount: 0, time: 0, display: '', isWeight: true, name: ''}; kind = 'misc'; }
  };
  const end = (tag, parent, raw) => {
    if (!inRecipe) return;
    const t = clean(raw);
    if (parent === 'RECIPE') { if (tag === 'NAME') rec.name = t; else if (tag === 'BOIL_TIME') boil = Math.round(num(t)); }
    else if (parent === 'MASH_STEP') {
      if (tag === 'NAME') step.name = t; else if (tag === 'STEP_TEMP') step.tempC = num(t);
      else if (tag === 'STEP_TIME') step.minutes = Math.max(0, Math.round(num(t)));
    } else if ((parent === 'HOP' && kind === 'hop') || (parent === 'MISC' && kind === 'misc')) {
      if (tag === 'NAME') item.name = t; else if (tag === 'AMOUNT') item.amount = num(t);
      else if (tag === 'USE') item.use = t.toLowerCase(); else if (tag === 'TIME') item.time = num(t);
      else if (tag === 'DISPLAY_AMOUNT') item.display = t; else if (tag === 'AMOUNT_IS_WEIGHT') item.isWeight = t.toLowerCase() === 'true';
    }
    if (tag === 'MASH_STEP') { if (step.tempC != null) steps.push(step); }
    else if (tag === 'HOP' && kind === 'hop') {
      const a = {name: item.name || 'Hops', amount: Math.round(item.amount * 10000) / 10, unit: 'g', atMinutes: Math.round(item.time)};
      if (item.use === 'boil') pending.push([a, 0]);
      else if (item.use.includes('first wort')) { a.name += ' (first wort)'; pending.push([a, 1]); }
      else if (item.use === 'aroma') { a.name += ' (aroma)'; pending.push([a, 2]); }
      else skipped.push(`${item.name} (${item.use || 'no use'})`);
      kind = null;
    } else if (tag === 'MISC' && kind === 'misc') {
      if (item.use !== 'boil') skipped.push(`${item.name} (${item.use || 'no use'})`);
      else {
        const a = {name: item.name || 'Addition', atMinutes: Math.round(item.time), amount: 0, unit: ''};
        const dm = /^\s*([\d.,]+)\s*(.*)$/.exec(item.display);
        if (dm) { a.amount = num(dm[1]); a.unit = dm[2].trim() || 'pcs'; }
        else { a.amount = item.amount * 1000; a.unit = item.isWeight ? 'g' : 'ml'; }
        pending.push([a, 0]);
      }
      kind = null;
    } else if (tag === 'RECIPE') {
      const r = {version: 3, name: rec.name || 'Imported recipe', boilMinutes: boil >= 0 ? Math.min(boil, 600) : 60, additions: []};
      let outIndex = -1;
      steps.forEach((s, i) => { if (!s.name) s.name = 'Step ' + (i + 1); if (i > 0 && /out/i.test(s.name)) outIndex = i; });
      arrangeMash(r, steps, outIndex);
      for (const [a, where] of pending) {
        if (where === 1) a.atMinutes = r.boilMinutes; else if (where === 2) a.atMinutes = 0;
        a.atMinutes = Math.min(a.atMinutes, r.boilMinutes);
        r.additions.push(a);
      }
      r.additions.sort((x, y) => y.atMinutes - x.atMinutes);
      let note = r.mash ? `${r.name}: mash in ${r.mashIn.tempC.toFixed(1)} C, ${r.rests.length} rest(s), mash out ${r.mashOut.tempC.toFixed(1)} C, boil ${r.boilMinutes} min, ${r.additions.length} additions`
                        : `${r.name}: no mash, boil ${r.boilMinutes} min, ${r.additions.length} additions`;
      if (skipped.length) note += '; not for brew day: ' + skipped.join(', ');
      res.notes.push(note);
      res.recipes.push(r);
      inRecipe = false;
    }
  };
  while ((m = re.exec(xml))) {
    if (m[5] !== undefined) { text += m[5]; continue; }
    if (m[1] !== undefined) { text += m[1]; continue; }
    if (m[3] === undefined) continue;  // comment, prolog, doctype
    const name = m[3].toUpperCase();
    if (m[2] === '/') {
      if (stack[stack.length - 1] === name) stack.pop();
      end(name, stack[stack.length - 1] || '', text);
    } else {
      const parent = stack[stack.length - 1] || '';
      start(name, parent);
      if (m[4] === '/') end(name, parent, ''); else stack.push(name);
    }
    text = '';
  }
  if (!res.recipes.length) throw new Error('no <RECIPE> found (is this a BeerXML file?)');
  return res;
}

function importRecipes(buf) {
  const t = buf.toString('utf8').replace(/^﻿/, '').trimStart();
  let list = [], notes = [];
  if (t[0] === '<') { const r = parseBeerXml(buf); list = r.recipes; notes = r.notes; }
  else if (t[0] === '{' || t[0] === '[') {
    const j = JSON.parse(t);
    for (const o of Array.isArray(j) ? j : [j]) {
      const r = normalizeRecipe(o);
      r.name = fold(r.name);
      list.push(r);
      notes.push(`${r.name}: ${r.mash ? r.rests.length + ' rest(s), ' : 'no mash, '}${r.additions.length} additions`);
    }
  } else throw new Error('neither BeerXML nor MashTouch JSON');
  const ids = [];
  for (const r of list) {
    const err = validate(r);
    if (err) { notes.push(`not imported: ${r.name}: ${err}`); continue; }
    const id = freeId(r.name);
    db.recipes[id] = r;
    ids.push(id);
  }
  save();
  if (!ids.length) throw Object.assign(new Error('no recipe imported'), {notes});
  return {ids, notes};
}

// ---------------------------------------------------------------------------
// Simulated controller
// ---------------------------------------------------------------------------

const K = {
  mode: 'idle', paused: false, phase: 'idle', prompt: '', step: 0, seq: [], recipe: null, delayMin: 0,
  timerMs: 0, durationMs: 0, reached: false, maltAdded: false, iodineDone: false, fired: new Set(), lastAddition: -1,
  setpoint: 0, boilLatched: false, integral: 0, duty: 0, pumpOn: false, override: null, primeUntil: 0,
  manualHeater: false, manualPump: false, countdown: false, tune: {cycles: 0, of: 4, high: true, startT: 0},
  water: 15, sensor: 15, fan: 0, lastHeat: -1e9, simSpeed: parseFloat(arg('speed', '20')), vnow: 0,
  boilPower: 80, events: [], eventSeq: 0, error: '',
};
K.water = K.sensor = S().sim.startC;

const PHASE_NAME = {idle: 'Idle', waterCheck: 'Water check', prime: 'Pump prime', delayedStart: 'Delayed start', mash: 'Mash',
  iodineTest: 'Iodine test', removeMalt: 'Remove malt', boil: 'Boil', hotWhirlpool: 'Hot whirlpool', cooling: 'Cooling',
  coldWhirlpool: 'Whirlpool', finished: 'Finished'};
const PROMPT_TEXT = {waterAdded: 'Water added?', addMalt: 'Add malt, then confirm', iodineTest: 'Iodine test: conversion complete?',
  removeMalt: 'Remove malt, then confirm', startWhirlpool: 'Start whirlpool?', startCooling: 'Start cooling?', finished: 'Brew finished'};

const LOG_TEXT = {'temp-reached': 'Temperature reached', 'step-done': 'Step done', 'boil-soon': 'Close to the boil',
  'boil-stalled': 'Temperature stopped rising', 'brew-finished': 'Brew finished'};
function event(type, text) {
  K.events.push({seq: ++K.eventSeq, type, text: text || ''});
  if (K.events.length > 12) K.events.shift();
  if (K.log) {
    const t = type === 'addition' ? 'Add: ' + text : type === 'prompt' || type === 'step-started' ? text : LOG_TEXT[type];
    if (t) logLine({t: logT(), e: t});
  }
}

// ---- Brew log (same line format as /logs/NNNN.jsonl in the firmware) --------
const logT = () => Math.round((K.vnow - K.logStart) / 1000);
function logLine(o) { if (K.log) K.log.lines.push(o); }
function logBegin(recipe) {
  const id = db.logs.reduce((m, l) => Math.max(m, l.id), 0) + 1;
  K.log = {id, recipe, start: Math.round(Date.now() / 1000), sim: true, lines: []};
  K.logStart = K.lastLog = K.vnow;
  db.logs.push(K.log);
  while (db.logs.length > 30) db.logs.shift();
  logLine({t: 0, e: 'Brew started'});
}
function logEnd(result) {
  if (!K.log) return;
  logLine({t: logT(), end: result});
  K.log = null;
  save();
}

function enter(phase) {
  const P = S().process;
  K.phase = phase; K.reached = false; K.timerMs = 0; K.durationMs = 0; K.prompt = ''; K.override = null;
  switch (phase) {
    case 'waterCheck': K.prompt = 'waterAdded'; break;
    case 'prime': K.primeUntil = K.vnow + 1000 * S().pump.primePulses; break;
    case 'delayedStart': K.durationMs = K.delayMin * 60000; break;
    case 'mash': K.maltAdded = false; K.setpoint = K.seq[K.step].tempC; K.durationMs = K.seq[K.step].minutes * 60000; break;
    case 'iodineTest': K.setpoint = K.seq[Math.max(0, K.step - 1)].tempC; K.durationMs = P.iodineTimeoutMin * 60000; K.prompt = 'iodineTest'; break;
    case 'removeMalt': K.prompt = 'removeMalt'; break;
    case 'boil': K.setpoint = P.boilTempC; K.durationMs = K.recipe.boilMinutes * 60000;
      K.boilWarned = K.stallWarned = false; K.stallMs = 0; K.stallRef = 0; break;
    case 'hotWhirlpool': case 'coldWhirlpool': K.durationMs = P.whirlpoolMin * 60000; K.prompt = 'startWhirlpool'; break;
    case 'cooling': K.setpoint = P.coolingTargetC; K.prompt = 'startCooling'; break;
    case 'finished': K.prompt = 'finished'; event('brew-finished'); break;
  }
  if (K.prompt) event('prompt', PROMPT_TEXT[K.prompt]);
  event('step-started', PHASE_NAME[phase]);
}
function startStep(i) {
  if (i >= K.seq.length) return afterMash();
  K.step = i;
  if (i === K.seq.length - 1 && i > 0 && S().process.iodineTest && !K.iodineDone) { K.iodineDone = true; return enter('iodineTest'); }
  enter('mash');
}
const startMash = () => startStep(0);
function afterMash() { if (!S().process.skipRemoveMalt && K.seq.length) enter('removeMalt'); else afterRemoveMalt(); }
function afterRemoveMalt() { if (K.recipe.boilMinutes > 0) enter('boil'); else afterBoil(); }
function afterBoil() { if (S().process.whirlpool === 'hot') enter('hotWhirlpool'); else enter('cooling'); }
function afterCooling() { if (S().process.whirlpool === 'cold') enter('coldWhirlpool'); else enter('finished'); }
function advance() {
  switch (K.phase) {
    case 'waterCheck': if (S().pump.autoPrime) enter('prime'); else if (K.delayMin) enter('delayedStart'); else startMash(); break;
    case 'prime': if (K.delayMin) enter('delayedStart'); else startMash(); break;
    case 'delayedStart': startMash(); break;
    case 'mash': startStep(K.step + 1); break;
    case 'iodineTest': startStep(K.step); break;
    case 'removeMalt': afterRemoveMalt(); break;
    case 'boil':
      K.recipe.additions.forEach((a, i) => { if (!K.fired.has(i)) fireAddition(i); });
      afterBoil(); break;
    case 'hotWhirlpool': enter('cooling'); break;
    case 'cooling': afterCooling(); break;
    case 'coldWhirlpool': enter('finished'); break;
    case 'finished': toIdle(); break;
  }
}
function fireAddition(i) {
  K.fired.add(i); K.lastAddition = i;
  const a = K.recipe.additions[i];
  event('addition', a.name + (a.amount > 0 ? `: ${a.amount} ${a.unit}` : ''));
}
function toIdle() {
  if (K.mode === 'auto') logEnd(K.phase === 'finished' ? 'finished' : 'stopped');
  if (K.spargeAuto) K.spargeOn = K.spargeAuto = false;
  K.mode = 'idle'; K.phase = 'idle'; K.prompt = ''; K.paused = false; K.duty = 0; K.pumpOn = false; K.override = null;
  K.manualHeater = K.manualPump = K.countdown = false; K.timerMs = K.durationMs = 0;
}

function canStart() {
  if (K.mode !== 'idle') { K.error = 'already running'; return false; }
  return true;
}
const ACTIONS = {
  startAuto(b) {
    if (!canStart()) return false;
    const r = activeRecipe();
    K.recipe = JSON.parse(JSON.stringify(r));
    K.seq = r.mash ? [r.mashIn, ...r.rests, r.mashOut] : [];
    K.delayMin = b.delayMin || 0; K.fired = new Set(); K.lastAddition = -1; K.iodineDone = false;
    K.mode = 'auto'; K.paused = false; K.boilPower = S().heater.boilPowerPct;
    logBegin(r.name);
    if (r.spargeTempC > 0 && S().sparge.autoWithBrew && spargeAvailable()) { K.spargeOn = K.spargeAuto = true; K.spargeSet = r.spargeTempC; }
    enter('waterCheck'); return true;
  },
  startManual() {
    if (!canStart()) return false;
    K.mode = 'manual'; K.setpoint = 35; K.manualHeater = false; K.manualPump = false; K.countdown = false;
    K.timerMs = 0; K.durationMs = 0; K.reached = false; K.boilPower = S().heater.boilPowerPct; return true;
  },
  startAutotune(b) {
    if (!canStart()) return false;
    K.mode = 'autotune'; K.setpoint = b.tempC || 65; K.tune = {cycles: -1, of: S().autotune.cycles, high: true, startT: K.vnow, lastRise: 0};
    return true;
  },
  resume() { K.error = 'nothing to resume'; return false; },
  discardResume() { return true; },
  stop() { toIdle(); return true; },
  pauseResume() {
    if (K.mode === 'auto') K.paused = !K.paused; else if (K.mode === 'manual') K.manualHeater = !K.manualHeater;
    return true;
  },
  skip() { if (K.mode === 'auto' && K.phase !== 'finished') advance(); return true; },
  startTimer() {
    if (K.mode !== 'auto' || K.reached || (K.phase !== 'mash' && K.phase !== 'boil')) return true;
    if (K.phase === 'boil' && K.sensor < 85) { K.error = 'not near the boil yet'; return false; }
    if (K.phase === 'boil' && K.sensor < K.setpoint) K.setpoint = Math.round(K.sensor * 100) / 100;  // this kettle boils here
    K.reached = true; event('temp-reached');
    return true;
  },
  answer(b) {
    if (K.mode !== 'auto' || !K.prompt) return true;
    const yes = !!b.yes, p = K.prompt;
    if (p === 'waterAdded' || p === 'removeMalt') { if (yes) advance(); else toIdle(); }
    else if (p === 'addMalt') { if (!yes) toIdle(); else { K.maltAdded = true; K.prompt = ''; K.override = null; } }
    else if (p === 'iodineTest') { if (yes) advance(); }
    else if (p === 'startWhirlpool' || p === 'startCooling') { if (yes) K.prompt = ''; else advance(); }
    else if (p === 'finished') toIdle();
    return true;
  },
  pumpToggle() {
    if (K.mode === 'manual') K.manualPump = !K.manualPump;
    else if (K.mode !== 'idle') K.override = !K.pumpOn;
    return true;
  },
  pumpPrime() { K.primeUntil = K.vnow + 1000 * S().pump.primePulses; return true; },
  heaterToggle() { if (K.mode === 'manual') K.manualHeater = !K.manualHeater; return true; },
  setpoint(b) {
    if (K.mode === 'manual') K.setpoint = b.tempC;
    else if (K.mode === 'auto' && ['mash', 'boil', 'cooling'].includes(K.phase)) {
      K.setpoint = b.tempC;
      if (K.phase === 'mash') K.seq[K.step].tempC = b.tempC;
    }
    return true;
  },
  boilPower(b) { K.boilPower = Math.max(0, Math.min(100, b.pct | 0)); return true; },
  timer(b) {
    if (K.mode === 'manual') { K.countdown = b.min > 0; K.durationMs = b.min * 60000; K.timerMs = 0; }
    else if (K.mode === 'auto') K.durationMs = K.timerMs + b.min * 60000;
    return true;
  },
  addMinutes(b) {
    const left = Math.ceil(Math.max(0, K.durationMs - K.timerMs) / 60000);
    return ACTIONS.timer({min: left + (b.min || 5)});
  },
  sim(b) { if (b.speed) K.simSpeed = Math.max(1, Math.min(100, b.speed)); return true; },
  simTemp(b) { K.water = K.sensor = b.tempC; return true; },
  sparge(b) {
    if (typeof b.tempC === 'number') K.spargeSet = Math.max(20, Math.min(99, b.tempC));
    if (b.on === true) {
      if (!spargeAvailable()) { K.error = 'no output has the sparge heater role'; return false; }
      if (!K.spargeOn) K.spargeSet = typeof b.tempC === 'number' ? K.spargeSet : S().sparge.tempC;
      K.spargeOn = true; K.spargeAuto = false;
    } else if (b.on === false) K.spargeOn = K.spargeAuto = false;
    return true;
  },
};

// Heater: full power far below the setpoint, PI near it, boil power at the boil.
function heaterDuty(sp, t, dt, boilMode) {
  const H = S().heater, boil = S().process.boilTempC;
  if (boilMode || sp >= boil) {
    if (t >= sp) K.boilLatched = true; else if (t < sp - H.boilHystC) K.boilLatched = false;
    return K.boilLatched ? K.boilPower : 100;
  }
  const band = t >= boil - H.fullPowerBandC ? H.nearBoilBandC : H.fullPowerBandC;
  const err = sp - t;
  if (err > band) { K.integral = 0; return 100; }
  K.integral = Math.max(0, Math.min(100, K.integral + H.ki * err * dt / 1000));
  return Math.max(0, Math.min(100, H.kp * err + K.integral));
}

// Sparge water: its own little kettle (15 l, 2 kW) and a thermostat.
const spargeAvailable = () => S().outputs.some(o => o.role === 'sparge');
Object.assign(K, {spargeOn: false, spargeAuto: false, spargeHeating: false, spargeSet: 76, spargeWater: 14, log: null, logStart: 0, lastLog: 0});
// Rolling history for the chart: every 10 s of simulated time, 6 h deep.
const HIST = [];
let histNext = 0;

let last = Date.now();
function tick() {
  const now = Date.now();
  const dt = (now - last) * K.simSpeed;
  last = now;
  K.vnow += dt;
  const P = S().process, sim = S().sim;
  const t = K.sensor;
  let sp = null, pumpMode = 'off', stopApplies = true, chill = false;

  if (K.mode === 'auto' && !K.paused) {
    const reach = target => { if (!K.reached && t >= target - P.reachedTolC) { K.reached = true; event('temp-reached'); } };
    switch (K.phase) {
      case 'prime': if (K.vnow >= K.primeUntil) advance(); break;
      case 'delayedStart': K.timerMs += dt; if (K.timerMs >= K.durationMs) advance(); break;
      case 'mash': {
        const st = K.seq[K.step];
        sp = K.setpoint; reach(sp);
        const waitMalt = K.step === 0 && !K.maltAdded && K.reached;
        if (waitMalt && K.prompt !== 'addMalt') { K.prompt = 'addMalt'; event('prompt', PROMPT_TEXT.addMalt); }
        pumpMode = waitMalt ? 'off' : st.pump;
        if (K.reached && !waitMalt) {
          K.timerMs += dt;
          if (K.timerMs >= K.durationMs) { event('step-done'); advance(); }
        }
        break;
      }
      case 'iodineTest':
        sp = K.setpoint; pumpMode = K.seq[Math.max(0, K.step - 1)].pump;
        if (P.iodineTimeoutMin) { K.timerMs += dt; if (K.timerMs >= K.durationMs) advance(); }
        break;
      case 'removeMalt': if (P.heatDuringRemoveMalt && K.seq.length) sp = K.seq[K.seq.length - 1].tempC; break;
      case 'boil':
        sp = K.setpoint; reach(sp); pumpMode = S().pump.inBoil ? 'on' : 'off';
        if (!K.reached) {
          if (!K.boilWarned && P.boilWarnC > 0 && t >= sp - P.boilWarnC) { K.boilWarned = true; event('boil-soon'); }
          if (t > 90) {
            if (Math.abs(t - K.stallRef) > 0.3) { K.stallRef = t; K.stallMs = 0; }
            else { K.stallMs += dt; if (K.stallMs >= 240000 && !K.stallWarned) { K.stallWarned = true; event('boil-stalled'); } }
          }
        }
        if (K.reached) {
          K.timerMs += dt;
          const left = K.durationMs - K.timerMs;
          K.recipe.additions.forEach((a, i) => {
            if (!K.fired.has(i) && (a.atMinutes >= K.recipe.boilMinutes || left <= a.atMinutes * 60000)) fireAddition(i);
          });
          if (K.timerMs >= K.durationMs) { event('step-done'); advance(); }
        }
        break;
      case 'hotWhirlpool': case 'coldWhirlpool':
        if (!K.prompt) {
          pumpMode = 'on'; stopApplies = false; K.timerMs += dt;
          if (K.timerMs >= K.durationMs) { event('step-done'); advance(); }
        }
        break;
      case 'cooling':
        if (!K.prompt) {
          chill = true; pumpMode = S().pump.duringCooling ? 'on' : 'off'; stopApplies = false; K.timerMs += dt;
          if (t <= K.setpoint) { event('temp-reached'); advance(); }
        }
        break;
    }
  } else if (K.mode === 'manual') {
    if (!K.reached && t >= K.setpoint) { K.reached = true; K.timerMs = 0; event('temp-reached'); }
    else if (K.reached && t + 2 < K.setpoint) K.reached = false;
    if (K.reached) {
      if (K.countdown) {
        K.timerMs += dt;
        if (K.timerMs >= K.durationMs) { K.countdown = false; event('timer-done'); }
      } else K.timerMs += dt;
    }
    if (K.manualHeater) sp = K.setpoint;
    pumpMode = K.manualPump ? 'on' : 'off';
  } else if (K.mode === 'autotune') {
    const T = K.tune, h = S().autotune.hysteresisC;
    if (T.high && t > K.setpoint + h) T.high = false;
    else if (!T.high && t < K.setpoint - h) {
      T.high = true; T.cycles++;
      if (T.cycles >= T.of) {
        const H = S().heater;
        H.kp = +(20 + Math.random() * 15).toFixed(2); H.ki = +(0.02 + Math.random() * 0.04).toFixed(4); H.kd = +(Math.random() * 300).toFixed(1);
        save(); event('autotune-done', `kp ${H.kp} ki ${H.ki} kd ${H.kd} (saved)`); toIdle();
      }
    }
    K.duty = T.high ? S().autotune.outputHighPct : S().autotune.outputLowPct;
    pumpMode = S().autotune.pumpOn ? 'on' : 'off';
  }

  if (K.mode !== 'autotune') K.duty = sp == null ? 0 : heaterDuty(sp, t, dt, K.mode === 'auto' && K.phase === 'boil');
  if (K.mode === 'auto' && K.paused) K.duty = 0;

  // Pump: prime, manual override, step mode, stop temperature.
  let pump = pumpMode !== 'off';
  if (pump && stopApplies && t >= S().pump.stopTempC) pump = false;
  if (K.override !== null && K.mode !== 'idle') pump = K.override;
  if (K.vnow < K.primeUntil) pump = Math.floor(K.vnow / 800) % 2 === 0;
  if (K.mode === 'auto' && K.paused) pump = false;
  K.pumpOn = pump;
  if (K.duty > 0) K.lastHeat = K.vnow;
  K.fan = K.duty > 0 || K.vnow - K.lastHeat < S().fan.runOnSec * 1000 ? 100 : 0;

  // Sparge heater thermostat and its water.
  if (!spargeAvailable()) K.spargeOn = K.spargeAuto = false;
  if (K.spargeAuto && K.mode === 'auto' && K.phase === 'boil' && K.reached) K.spargeOn = K.spargeAuto = false;
  if (K.spargeOn) {
    if (K.spargeWater <= K.spargeSet - S().sparge.hystC) K.spargeHeating = true;
    else if (K.spargeWater >= K.spargeSet) K.spargeHeating = false;
  } else K.spargeHeating = false;
  K.spargeWater += ((K.spargeHeating ? 2000 : 0) - 4 * (K.spargeWater - sim.ambientC)) * (dt / 1000) / (15 * 4186);

  // History and brew log.
  if (K.vnow >= histNext) {
    histNext = K.vnow + 10000;
    HIST.push([Math.round(K.vnow / 1000), Math.round(K.sensor * 10), K.mode === 'idle' ? null : Math.round(K.setpoint * 10), Math.round(K.duty)]);
    if (HIST.length > 2160) HIST.shift();
  }
  if (K.log && K.vnow - K.lastLog >= 30000) {
    K.lastLog = K.vnow;
    logLine({t: logT(), T: Math.round(K.sensor * 10) / 10, sp: Math.round(K.setpoint * 10) / 10, h: Math.round(K.duty), p: K.pumpOn ? 1 : 0});
  }

  // Kettle: heater power in, losses out (a chiller while cooling), sensor lag.
  const dts = dt / 1000, cap = sim.liters * 4186;
  let watts = sim.heaterW * K.duty / 100 - sim.lossWPerK * (K.water - sim.ambientC);
  if (chill) watts -= 250 * (K.water - 12);
  K.water = Math.min(100, K.water + watts * dts / cap);
  K.sensor += (K.water - K.sensor) * Math.min(1, dts / Math.max(0.1, sim.sensorTauS));
}
setInterval(tick, 100);

function status() {
  const r = K.recipe;
  const stepName = K.phase === 'mash' ? (K.seq[K.step] || {}).name : K.phase === 'iodineTest' ? 'Iodine test' : PHASE_NAME[K.phase];
  let zone = 'off';
  const boil = S().process.boilTempC;
  if (K.mode === 'autotune') zone = 'tune';
  else if (K.duty > 0) zone = (K.setpoint >= boil || (K.mode === 'auto' && K.phase === 'boil')) && K.boilLatched ? 'boil' : K.duty >= 100 ? 'full' : 'pid';
  return {
    mode: K.mode, paused: K.paused, faults: 0, faultText: [], confirm: '',
    temp: Math.round(K.sensor * 100) / 100, setpoint: Math.round(K.setpoint * 100) / 100, reached: K.reached,
    heater: {zone, duty: Math.round(K.duty), on: K.duty > 0, boilPower: K.boilPower,
             count: Math.max(1, ...S().outputs.filter(o => o.role === 'heater').map(o => o.index)),
             stages: [K.duty > 0]},
    pump: {on: K.pumpOn, resting: false, override: K.override !== null, priming: K.vnow < K.primeUntil},
    fan: K.fan,
    phase: K.phase, phaseName: PHASE_NAME[K.phase], step: K.step, stepCount: K.seq.length, stepName,
    timer: {remainingMs: Math.max(0, Math.round(K.durationMs - K.timerMs)), elapsedMs: Math.round(K.timerMs),
            counting: K.reached && !K.prompt},
    prompt: K.prompt, promptText: PROMPT_TEXT[K.prompt] || '', lastAddition: K.lastAddition,
    manual: {heater: K.manualHeater, pump: K.manualPump, countdown: K.countdown},
    tune: {cycles: Math.max(0, K.tune.cycles), of: K.tune.of},
    resume: {offered: false, phase: 'Idle', step: 0},
    sim: {on: true, speed: K.simSpeed},
    fahrenheit: S().fahrenheit, recipe: activeRecipe().name, activeRecipe: S().activeRecipe,
    sensors: [{name: (S().sensors[0] || {}).name || 'DS18B20 (mock)', role: (S().sensors[0] || {}).role || 'kettle', present: true, ok: true,
               temp: Math.round(K.sensor * 100) / 100, offsetC: (S().sensors[0] || {}).offsetC || 0, errors: 0, rom: '28FF000000000001'},
              {name: 'Sparge water (mock)', role: 'sparge', present: true, ok: true, temp: Math.round(K.spargeWater * 100) / 100,
               offsetC: 0, errors: 0, rom: '28FF000000000002'}],
    sparge: {available: spargeAvailable(), on: K.spargeOn, heating: K.spargeHeating, temp: Math.round(K.spargeWater * 100) / 100,
             setpoint: K.spargeSet},
    clock: Math.round(K.vnow / 1000), time: Math.round(Date.now() / 1000),
    eventSeq: K.eventSeq, events: K.events, uptime: Math.round(process.uptime()),
    wifi: {connected: true, rssi: -58, apOn: false},
    _brewRecipe: r,
  };
}

// ---------------------------------------------------------------------------
// HTTP
// ---------------------------------------------------------------------------

function getPath(obj, p) {
  return p.split('.').reduce((o, k) => (o == null ? undefined : Array.isArray(o) ? o[parseInt(k, 10)] : o[k]), obj);
}
const ENUMS = {'heater.controlSensor': ['kettle', 'rims'], 'process.whirlpool': ['off', 'hot', 'cold'],
  'safety.level': ['ignore', 'closed', 'open']};
function setSetting(p, value) {
  if (p === 'version' || p === 'activeRecipe') return 'read-only here';
  const keys = p.split('.'), lastKey = keys.pop();
  const parent = keys.length ? getPath(S(), keys.join('.')) : S();
  if (parent == null || !(lastKey in parent)) return `no key '${p}'`;
  const old = parent[lastKey];
  if (typeof old !== typeof value) return 'wrong type for this setting';
  const en = ENUMS[p] || (lastKey === 'role' ? ['none', 'heater', 'pump', 'fan', 'sparge', 'alarm'] : null);
  if (en && !en.includes(value)) return 'value not accepted (out of range or unknown name)';
  if (typeof value === 'number' && /Pct$/.test(lastKey) && (value < 0 || value > 100)) return 'value not accepted (out of range or unknown name)';
  parent[lastKey] = value;
  if (p === 'sim.speed') K.simSpeed = value;
  save();
  return null;
}

function send(res, code, obj, type) {
  const body = type ? obj : JSON.stringify(obj);
  res.writeHead(code, {'Content-Type': type || 'application/json', 'Cache-Control': 'no-cache'});
  res.end(body);
}
const ok = res => send(res, 200, {ok: true});
const fail = (res, code, msg) => send(res, code, {error: msg});

function authorized(req, res) {
  const pw = S().net.webPassword;
  if (!pw) return true;
  if (req.headers.authorization === 'Basic ' + Buffer.from('admin:' + pw).toString('base64')) return true;
  res.writeHead(401, {'WWW-Authenticate': 'Basic realm="MashTouch"', 'Content-Type': 'application/json'});
  res.end('{"error":"login required"}');
  return false;
}

function route(req, res, body) {
  const url = new URL(req.url, 'http://x');
  const p = url.pathname, m = req.method;
  const json = () => { const j = JSON.parse(body.toString('utf8') || '{}'); if (typeof j !== 'object' || Array.isArray(j)) throw new Error('expected a JSON object'); return j; };
  const write = m !== 'GET';
  if (write && !authorized(req, res)) return;
  let mm;

  if (p === '/api/status' && m === 'GET') { const s = status(); delete s._brewRecipe; return send(res, 200, s); }
  if (p === '/api/info' && m === 'GET') return send(res, 200, {version: 'mock', build: 'PC mock (tools/mock/server.js)',
    heapFree: 180000, psramFree: 8000000, fsUsed: Buffer.byteLength(JSON.stringify(db)), fsTotal: 3538944,
    uptime: Math.round(process.uptime()), sketchFree: 6500000});
  if (p === '/api/brew/recipe' && m === 'GET') return send(res, 200, K.recipe || activeRecipe());
  if (p === '/api/action' && m === 'POST') {
    const b = json(), fn = ACTIONS[b.action];
    if (!fn) return fail(res, 400, `unknown action '${b.action}'`);
    K.error = '';
    return fn(b) ? ok(res) : fail(res, 409, K.error);
  }
  if (p === '/api/settings' && m === 'GET') {
    if (!authorized(req, res)) return;
    const s = JSON.parse(JSON.stringify(S()));
    const n = S().net;
    Object.assign(s.net, {password: '', apPassword: '', webPassword: '', passwordSet: !!n.password,
      apPasswordSet: !!n.apPassword, webPasswordSet: !!n.webPassword});
    return send(res, 200, s);
  }
  if (p === '/api/settings' && m === 'POST') {
    const b = json(), list = Array.isArray(b.changes) ? b.changes : [b], errors = [];
    for (const c of list) { const e = c.path ? setSetting(c.path, c.value) : 'missing path'; if (e) errors.push({path: c.path, error: e}); }
    return send(res, errors.length ? 400 : 200, {ok: !errors.length, errors});
  }
  if (p === '/api/settings/reset' && m === 'POST') {
    const net = S().net, active = S().activeRecipe;
    db.settings = defaultSettings(); db.settings.net = net; db.settings.activeRecipe = active; save();
    return ok(res);
  }
  if (p === '/api/recipes' && m === 'GET')
    return send(res, 200, Object.entries(db.recipes).map(([id, r]) => ({id, name: r.name, active: id === S().activeRecipe})));
  if (p === '/api/import' && m === 'POST') {
    try { return send(res, 200, Object.assign({ok: true}, importRecipes(body))); }
    catch (e) { return send(res, 400, {ok: false, error: e.message, ids: [], notes: e.notes || []}); }
  }
  if ((mm = /^\/api\/recipes\/([^/]+)\/use$/.exec(p)) && m === 'POST') {
    if (!db.recipes[mm[1]]) return fail(res, 404, 'no such recipe');
    S().activeRecipe = mm[1]; save(); return ok(res);
  }
  if ((mm = /^\/api\/recipes\/([^/]+)$/.exec(p))) {
    const id = mm[1];
    if (m === 'GET') return db.recipes[id] ? send(res, 200, db.recipes[id]) : fail(res, 404, 'no such recipe');
    if (m === 'POST') {
      if (recipeId(id) !== id) return fail(res, 400, 'invalid recipe id');
      const r = normalizeRecipe(json()), err = validate(r);
      if (err) return fail(res, 400, err);
      db.recipes[id] = r; save(); return ok(res);
    }
    if (m === 'DELETE') {
      if (id === S().activeRecipe) return fail(res, 400, 'cannot delete the active recipe');
      if (!db.recipes[id]) return fail(res, 400, `no recipe '${id}'`);
      delete db.recipes[id]; save(); return ok(res);
    }
  }
  if (p === '/api/history' && m === 'GET')
    return send(res, 200, {clock: Math.round(K.vnow / 1000), step: 10, t: HIST.map(h => h[0]), temp: HIST.map(h => h[1]),
      sp: HIST.map(h => h[2]), duty: HIST.map(h => h[3])});
  if (p === '/api/logs' && m === 'GET')
    return send(res, 200, db.logs.slice().reverse().map(l => {
      const lastL = l.lines[l.lines.length - 1] || {};
      const text = JSON.stringify(l.lines);
      return {id: l.id, recipe: l.recipe, start: l.start, sim: l.sim, durationS: lastL.t || 0, result: lastL.end || '',
              size: text.length, active: l === K.log};
    }));
  if ((mm = /^\/api\/logs\/(\d+)$/.exec(p))) {
    const l = db.logs.find(x => x.id === +mm[1]);
    if (m === 'GET') {
      if (!l) return fail(res, 404, 'no such log');
      const head = {v: 1, id: l.id, recipe: l.recipe, start: l.start, sim: l.sim};
      return send(res, 200, [head, ...l.lines].map(x => JSON.stringify(x)).join('\n') + '\n', 'application/x-ndjson');
    }
    if (m === 'DELETE') {
      if (!l || l === K.log) return fail(res, 400, 'no such log, or it is still being written');
      db.logs = db.logs.filter(x => x !== l); save(); return ok(res);
    }
  }
  if (p === '/api/time' && m === 'POST') return ok(res);
  if (p === '/api/sensors/rescan' && m === 'POST') return ok(res);
  if (/^\/api\/sensors\/\d+\/forget$/.test(p) && m === 'POST') { S().sensors = []; save(); return ok(res); }
  if ((mm = /^\/api\/sensors\/(\d+)$/.exec(p)) && m === 'POST') {
    const b = json();
    if (b.offsetC !== undefined && (b.offsetC < -10 || b.offsetC > 10)) return fail(res, 400, 'offset must be -10..10 C');
    S().sensors = [{rom: '28FF000000000001', role: b.role || 'kettle', offsetC: b.offsetC || 0, name: b.name || 'Kettle'}];
    save(); return ok(res);
  }
  if (p === '/api/wifi' && m === 'GET') return send(res, 200, {connected: true, ssid: S().net.ssid, ip: '127.0.0.1', rssi: -58,
    hostname: S().net.hostname, apOn: false, apSsid: 'MashTouch-MOCK', apIp: '', apClients: 0, scanning: false,
    networks: [{ssid: S().net.ssid, rssi: -58, open: false}, {ssid: 'Neighbour', rssi: -81, open: false}, {ssid: 'Cafe', rssi: -88, open: true}]});
  if (p === '/api/wifi' && m === 'POST') { const b = json(); S().net.ssid = b.ssid || S().net.ssid; save(); return ok(res); }
  if ((p === '/api/wifi/scan' || p === '/api/wifi/forget' || p === '/api/reboot') && m === 'POST') return ok(res);
  if (p === '/api/ota' && m === 'POST') {
    if (K.mode !== 'idle') return fail(res, 400, 'stop the running mode first');
    return ok(res);  // the mock only accepts the upload
  }
  if (p.startsWith('/api/')) return fail(res, 404, 'no such API endpoint');
  // Everything else: the real web UI, read fresh so edits show on reload.
  // Test hook: /?run=strikeCalc calls that page function after loading (for
  // screenshots of dialogs with a headless browser).
  try {
    let page = fs.readFileSync(PAGE, 'utf8');
    const run = url.searchParams.get('run');
    if (run && /^[A-Za-z]+$/.test(run)) page += '<script>setTimeout(() => window.' + run + '(), 1500)</script>';
    return send(res, 200, page, 'text/html; charset=utf-8');
  }
  catch (e) { return fail(res, 500, 'web/index.html not found'); }
}

http.createServer((req, res) => {
  const chunks = [];
  req.on('data', c => chunks.push(c));
  req.on('end', () => {
    try { route(req, res, Buffer.concat(chunks)); }
    catch (e) { fail(res, 400, e.message); }
  });
}).listen(PORT, () => {
  console.log(`MashTouch mock: http://localhost:${PORT}  (kettle simulated at ${K.simSpeed}x)`);
  console.log(`Data: ${DATA}   Ctrl+C to stop`);
});
