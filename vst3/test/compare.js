/* Compares the C++ core with the WebApp (the reference) on a scenario.
 *   node vst3/test/compare.js <tiracon_compare binary> <scenario.txt> [--dump]
 * Both sides run: ROM firmware/tiracon_mod.bin, factory RAM, 48 kHz, 1 ms steps (4000 T-states),
 * and print a checkpoint every N ms: CPU/RAM state (must be equal), control voltage frame and
 * audio energy (must agree within a small tolerance).
 */
'use strict';
var fs = require('fs');
var path = require('path');
var cp = require('child_process');
// the WebApp (not part of this repository); default: next to vst3/, or set TIRACON_WEBAPP=<emulator folder>
var JS = path.join(process.env.TIRACON_WEBAPP || path.join(__dirname, '..', '..', 'emulator'), 'js');
var ROM = path.join(__dirname, '..', 'firmware', 'tiracon_mod.bin');

function parseScenario(text) {
  var sc = { every: 50, serial: '1266', ideal: false, end: 1000, events: [] };
  text.split('\n').forEach(function (line) {
    line = line.replace(/#.*/, '').trim();
    if (!line) return;
    var w = line.split(/\s+/);
    if (w[0] === 'every') { sc.every = +w[1]; return; }
    if (w[0] === 'device') { sc.serial = w[1]; return; }
    if (w[0] === 'ideal') { sc.ideal = true; return; }
    var ms = +w[0], cmd = w[1];
    if (cmd === 'end') sc.end = ms;
    else if (cmd === 'midi') sc.events.push({ ms: ms, t: 'midi', bytes: w.slice(2).map(function (h) { return parseInt(h, 16); }) });
    else if (cmd === 'blp') sc.events.push({ ms: ms, t: 'blp', code: parseInt(w[2], 16), down: w[3] === '1' });
    else throw new Error('unbekannt: ' + line);
  });
  return sc;
}

function fnv(bytes) {
  var h = 2166136261;
  for (var i = 0; i < bytes.length; i++) { h ^= bytes[i]; h = Math.imul(h, 16777619); }
  return (h >>> 0).toString(16);
}
function g(x) { return Number(x).toPrecision(9); }

function runJs(sc) {
  Math.random = function () { return 0; };          // oscillator start phases, same as the C++ side
  var Tiracon = require(path.join(JS, 'tiracon.js'));
  var T6VSynth = require(path.join(JS, 'synth-worklet.js'));
  var Device = require(path.join(JS, 'tolerance.js'));
  var Factory = require(path.join(JS, 'factory.js'));
  var b = new Tiracon();
  b.loadRom(new Uint8Array(fs.readFileSync(ROM)));
  b.ram.set(Factory.ram());
  var synth = new T6VSynth(48000);
  var dev = new Device(sc.serial, sc.ideal);
  b.setDevice(dev.boardProfile());
  synth.setDevice(dev.synthProfile());
  b.reset();
  var frame = new Float32Array(33), buf = new Float32Array(48), out = [], sumsq = 0, ev = 0;
  for (var ms = 0; ms < sc.end; ms++) {
    while (ev < sc.events.length && sc.events[ev].ms <= ms) {
      var e = sc.events[ev++];
      if (e.t === 'midi') b.midiIn(e.bytes); else b.setBlpKey(e.code, e.down);
    }
    var target = (ms + 1) * 4000;
    if (target > b.clock.now) b.run(target - b.clock.now);
    synth.setFrame(b.audioFrame(frame));
    synth.process(buf, 48);
    for (var k = 0; k < 48; k++) sumsq += buf[k] * buf[k];
    if ((ms + 1) % sc.every === 0) {
      var c = b.cpu;
      out.push(['C', ms + 1, b.clock.now, c.pc, c.sp, c.a, c.f, fnv(b.ram), b.idleCycles, b.ctc.ch[3].src.count,
        b.ctc.ch[3].lost, b.dart.overruns].join(' '));
      out.push('F ' + Array.prototype.map.call(frame, g).join(' '));
      out.push('A ' + g(sumsq) + ' ' + g(buf[47]));
      sumsq = 0;
    }
  }
  return out;
}

function compare(js, cc) {
  var bad = 0, firstC = -1, maxF = 0, maxA = 0;
  for (var i = 0; i < Math.max(js.length, cc.length); i++) {
    var a = js[i], b = cc[i];
    if (a === undefined || b === undefined) { console.log('Länge unterschiedlich bei Zeile ' + i); bad++; break; }
    var wa = a.split(' '), wb = b.split(' ');
    if (wa[0] === 'C') {
      if (a !== b) { if (firstC < 0) { firstC = i; console.log('Zustand weicht ab:\n  JS  ' + a + '\n  C++ ' + b); } bad++; }
    } else {
      for (var k = 1; k < wa.length; k++) {
        var x = +wa[k], y = +wb[k], d = Math.abs(x - y) / Math.max(1e-6, Math.abs(x), Math.abs(y));
        if (wa[0] === 'F') maxF = Math.max(maxF, d); else maxA = Math.max(maxA, d);
        if (d > (wa[0] === 'F' ? 1e-6 : 1e-3)) {
          if (bad < 5) console.log('Wert weicht ab (Zeile ' + i + ', ' + wa[0] + '[' + (k - 1) + ']): JS ' + x + ' C++ ' + y);
          bad++;
        }
      }
    }
  }
  console.log('Prüfpunkte: ' + js.filter(function (l) { return l[0] === 'C'; }).length +
    ', größte Abweichung Steuerspannungen ' + maxF.toExponential(2) + ', Audio ' + maxA.toExponential(2));
  return bad;
}

var bin = process.argv[2], scPath = process.argv[3];
if (!bin || !scPath) { console.log('node compare.js <tiracon_compare> <scenario.txt> [--dump]'); process.exit(2); }
var sc = parseScenario(fs.readFileSync(scPath, 'utf8'));
var t0 = Date.now();
var js = runJs(sc);
var t1 = Date.now();
var res = cp.spawnSync(bin, [scPath, ROM], { encoding: 'utf8', maxBuffer: 1 << 28 });
if (res.status !== 0) { console.log('C++ fehlgeschlagen: ' + res.stderr); process.exit(1); }
var cc = res.stdout.trim().split(/\r?\n/);
console.log(path.basename(scPath) + ': JS ' + (t1 - t0) + ' ms, C++ ' + (Date.now() - t1) + ' ms für ' + (sc.end / 1000) + ' s');
if (process.argv.indexOf('--dump') > 0) { fs.writeFileSync('compare_js.txt', js.join('\n')); fs.writeFileSync('compare_cpp.txt', cc.join('\n')); }
var bad = compare(js, cc);
console.log(bad ? 'FEHLER: ' + bad + ' Abweichungen' : 'ok: C++ rechnet wie die WebApp');
process.exit(bad ? 1 : 0);
