// Node test for web/jazzband.wasm: no imports, packs load and verify, a song with a lead line renders in time and in tune.
import fs from 'node:fs';
import path from 'node:path';
const here = path.dirname(new URL(import.meta.url).pathname);
const web = path.join(here, '..');
let fails = 0;
const check = (ok, msg) => { console.log((ok ? 'ok:   ' : 'FAIL: ') + msg); if (!ok) fails++; };

const bytes = fs.readFileSync(path.join(web, 'jazzband.wasm'));
const mod = await WebAssembly.compile(bytes);
check(WebAssembly.Module.imports(mod).length === 0, `no imports (${WebAssembly.Module.imports(mod).map(i => i.name)})`);
const { exports: X } = await WebAssembly.instantiate(mod, {});
const mem = () => new Uint8Array(X.memory.buffer);
X.jbw_init();
for (const p of ['jbdrums', 'jbbass', 'jbpiano', 'jbguitar', 'jblead', 'jbgtrlead']) {
  const f = path.join(web, 'packs', p + '.jbp');
  if (!fs.existsSync(f)) { console.log('skip  ' + p + ' (not built)'); continue; }
  const b = fs.readFileSync(f); const ptr = X.jbw_alloc(b.length);
  check(ptr !== 0, `${p}: ${b.length} bytes fit the arena`);
  mem().set(b, ptr); X.jbw_add_pack(ptr);
}
// Autumn Leaves, A section, per-beat chords (root, quality) Q: 0 maj7 1 m7 2 7 3 h7 4 o7 5 m6 6 6 7 7b9
const chart = [[0, 1], [5, 2], [10, 0], [3, 0], [9, 3], [2, 7], [7, 5], [7, 5]];
const bars = X.jbw_bars();
chart.forEach(([r, q], b) => { for (let k = 0; k < 4; k++) { mem()[bars + 9 * b + 2 * k] = r; mem()[bars + 9 * b + 2 * k + 1] = q; } mem()[bars + 9 * b + 8] = b === 0 ? 65 : 0; });
X.jbw_load(8, 4, 120, 0);
// lead: a whole note A4 (69) in every bar, so its pitch can be measured
const LN = X.jbw_lead_buf();
const dv = () => new DataView(X.memory.buffer);
for (let b = 0; b < 8; b++) { const o = LN + 8 * b; dv().setUint16(o, b, true); dv().setUint16(o + 2, 0, true); dv().setUint16(o + 4, 180, true); mem()[o + 6] = 69; mem()[o + 7] = 100; }
X.jbw_lead_set(8, 0);
X.jbw_lead_sound(0, 30000, 0);
// let the pack checksums run (they are checked 4 KB a block)
for (let i = 0; i < 2500; i++) X.jbw_render();
for (const [i, n] of [[0, 'drums'], [1, 'bass'], [2, 'piano'], [3, 'guitar'], [5, 'lead (sax)'], [6, 'lead (guitar)']]) check(X.jbw_pack_ok(i) === 1, `${n} pack verified`);
X.jbw_play(-1);
const sr = 44100, secs = 20, L = new Float32Array(128 * Math.floor(sr * secs / 128)), R = new Float32Array(L.length);
const bars_seen = []; let last = -1;
for (let i = 0; i < L.length / 128; i++) {
  X.jbw_render();
  L.set(new Float32Array(X.memory.buffer, X.jbw_out_l(), 128), i * 128);
  R.set(new Float32Array(X.memory.buffer, X.jbw_out_r(), 128), i * 128);
  const st = X.jbw_state(), b = X.jbw_bar();
  if ((st & 15) === 3 && b !== last) { bars_seen.push([b, i * 128]); last = b; }
}
let pk = 0, e = 0; for (let i = 0; i < L.length; i++) { pk = Math.max(pk, Math.abs(L[i]), Math.abs(R[i])); e += L[i] * L[i]; }
check(pk > 0.05 && pk <= 1, `audible, no overflow (peak ${pk.toFixed(3)}, rms ${Math.sqrt(e / L.length).toFixed(3)})`);
const lens = bars_seen.slice(1).map((x, i) => x[1] - bars_seen[i][1]);
const want = sr * 4 * 60 / 120;
check(lens.length >= 7 && lens.every(l => Math.abs(l - want) <= 128), `bars last 2 s at 120 BPM (${lens.slice(0, 6).join(', ')} samples)`);
check(bars_seen.length && bars_seen[0][0] === 0, 'starts at bar 1 after the count-in');
// the lead alone (band muted): pitch and level of each lead sound
for (let i = 0; i < 4; i++) X.jbw_level(i, 1);
const alone = (sound) => {
  X.jbw_stop(); X.jbw_lead_sound(sound, 30000, 0); for (let i = 0; i < 600; i++) X.jbw_render();
  X.jbw_play(-1);
  const buf = new Float32Array(128 * Math.floor(sr * 4 / 128));
  for (let i = 0; i < buf.length / 128; i++) { X.jbw_render(); buf.set(new Float32Array(X.memory.buffer, X.jbw_out_l(), 128), i * 128); }
  const seg = buf.subarray(sr * 2.5, sr * 3.0);   // inside bar 1 (count-in 2 s)
  let best = 0, bl = 0, e2 = 0;
  for (let lag = 40; lag < 400; lag++) { let s = 0; for (let i = 0; i < seg.length - lag; i++) s += seg[i] * seg[i + lag]; if (s > best) { best = s; bl = lag; } }
  for (const v of seg) e2 += v * v;
  return [sr / bl, Math.sqrt(e2 / seg.length)];
};
const [f0, r0] = alone(0), [f4, r4] = alone(6), [, r1] = alone(1);
check(Math.abs(f0 - 440) < 8, `synth lead plays A4 in tune (${f0.toFixed(1)} Hz)`);
check(Math.abs(f4 - 440) < 8, `sax lead plays A4 in tune (${f4.toFixed(1)} Hz)`);
check(r4 > r0 * 0.4 && r4 < r0 * 1.0, `sax lead sits a little under the synth lead (rms ${r4.toFixed(3)} vs ${r0.toFixed(3)}; piano, decaying, ${r1.toFixed(3)})`);
{ const [fg, rg] = alone(2);                       // the melody guitar: an octave under the page, about as loud as the sax
  check(Math.abs(fg - 220) < 5, `guitar lead plays the A4 melody note as A3, where a guitarist plays it (${fg.toFixed(1)} Hz)`);
  check(rg > r4 * 0.6 && rg < r4 * 1.6, `guitar lead sits with the sax (rms ${rg.toFixed(3)} vs ${r4.toFixed(3)})`); }
// a held note (two tied bars and more) keeps sounding: the sax zone loops inside its steady tone
{ X.jbw_stop(); X.jbw_lead_sound(6, 30000, 0);
  dv().setUint16(LN + 4, 1500, true);                     // bar 1: 1500 / 48 quarter notes = 15.6 s at 120 BPM
  X.jbw_lead_set(1, 0); for (let i = 0; i < 600; i++) X.jbw_render();
  X.jbw_play(-1);
  const at = []; let e2 = 0, n = 0;
  for (let i = 0; i < Math.floor(sr * 12 / 128); i++) {
    X.jbw_render(); const b = new Float32Array(X.memory.buffer, X.jbw_out_l(), 128);
    for (const v of b) e2 += v * v; n += 128;
    if (n >= sr / 2) { at.push(Math.sqrt(e2 / n)); e2 = 0; n = 0; }
  }
  const early = at[6], late = at[20], steps = at.slice(8, 22).map((v, i, a) => i ? Math.abs(v - a[i - 1]) / v : 0);
  check(late > early * 0.6 && late < early * 1.6, `a 10 s sax note still sounds (rms ${early.toFixed(3)} at 3.5 s, ${late.toFixed(3)} at 10.5 s)`);
  check(Math.max(...steps) < 0.35, `its level stays even through the loop (largest half-second change ${(Math.max(...steps) * 100).toFixed(0)}%)`);
}
// a player's touch moves notes by milliseconds, never the bar lines; off = the old exact playback
// MIDI out: the bass part and the melody as note events, MIDI clock at 24 a beat
{ X.jbw_stop(); X.jbw_feel(0); X.jbw_trade(0); X.jbw_lead_mask(0xFFFFFFFF); X.jbw_lead_sound(0, 30000, 0);
  for (let i = 0; i < 600; i++) X.jbw_render();
  X.jbw_midi_route((1 << 1) | (1 << 4), 1); X.jbw_play(-1);
  const evs = []; let frame = 0;
  for (let i = 0; i < Math.floor(sr * 6 / 128); i++) {
    X.jbw_render(); const n = X.jbw_midi_n(), p = X.jbw_midi_buf(), d = new DataView(X.memory.buffer);
    for (let k = 0; k < n; k++) { const o = p + 12 * k; evs.push({ inst: d.getUint8(o), note: d.getUint8(o + 1), vel: d.getUint8(o + 2), kind: d.getUint8(o + 3), at: frame + d.getInt32(o + 4, true), len: d.getInt32(o + 8, true) }); }
    frame += 128;
  }
  X.jbw_midi_route(0, 0);
  const bass = evs.filter(e => !e.kind && e.inst === 1), mel = evs.filter(e => !e.kind && e.inst === 4), clk = evs.filter(e => e.kind === 1);
  const inBar1 = (e) => e.at >= sr * 2 - 256 && e.at < sr * 4 - 256;    // 120 BPM: count-in 0-2 s, bar 1 2-4 s
  check(bass.filter(inBar1).length >= 2 && bass.every(e => e.note >= 24 && e.note <= 60), `bass part out over MIDI (${bass.filter(inBar1).length} notes in bar 1, ${bass[0] && bass[0].note})`);
  check(mel.filter(inBar1).length === 1 && mel.filter(inBar1)[0].note === 69, 'melody out over MIDI (the held A4 the test before left in bar 1)');
  const perBeat = clk.filter(e => e.at >= sr * 2 && e.at < sr * 4).length / 4;
  check(perBeat === 24, `MIDI clock: ${perBeat} pulses a beat`);
  check(evs.every(e => e.at >= 0), 'events carry their place in the render call');
}
// more feels (web only) and trading fours: each feel plays in time; the drums alone in the drum solo bars
X.jbw_lead_sound(0, 1, 0);
const band = (feel, onlyInst, nbars) => {         // render nbars after the count-in; rms per bar with one instrument on
  X.jbw_stop(); for (let i = 0; i < 600; i++) X.jbw_render();
  for (let i = 0; i < 4; i++) X.jbw_level(i, onlyInst < 0 || i === onlyInst ? 26000 : 1);
  X.jbw_feel(feel); X.jbw_play(-1);
  const per = [], spb = sr * 2; let acc = 0, n = 0, pk = 0;
  for (let i = 0; i < (nbars + 1) * spb / 128; i++) {
    X.jbw_render(); const b = new Float32Array(X.memory.buffer, X.jbw_out_l(), 128);
    for (let k = 0; k < 128; k++) { acc += b[k] * b[k]; pk = Math.max(pk, Math.abs(b[k])); if (Number.isNaN(b[k])) pk = 9; }
    n += 128; if (n >= spb) { per.push(Math.sqrt(acc / n)); acc = 0; n = 0; }
  }
  return { per: per.slice(1), pk };                // drop the count-in bar
};
for (const [f, name] of [[3, 'two-feel'], [4, 'two-feel, then four'], [5, 'shuffle'], [6, 'samba'], [7, 'even eighths']]) {
  const { per, pk } = band(f, -1, 8);
  check(pk > 0.03 && pk <= 1 && per.every(r => r > 0.005), `${name}: the band plays every bar (peak ${pk.toFixed(2)})`);
}
// trading fours on the solo choruses: melody in chorus 1 only, 8-bar chart, so in chorus 2 bars 5-8 are the drums'
X.jbw_lead_mask(1); X.jbw_trade(4); X.jbw_form(3, 0, 0, 0);
{ const bass = band(0, 1, 16).per, drums = band(0, 0, 16).per;
  const q = (a, i, j) => a.slice(i, j).reduce((s, x) => s + x, 0) / (j - i);
  check(q(bass, 8, 12) > 0.004 && q(bass, 12, 16) < q(bass, 8, 12) * 0.1,
    `trading: the bass comps your 4 and lays out for the drums' 4 (${q(bass, 8, 12).toFixed(4)} / ${q(bass, 12, 16).toFixed(4)})`);
  check(q(bass, 4, 8) > 0.004, 'no trading while the melody plays (chorus 1)');
  check(q(drums, 12, 16) > q(drums, 8, 12) * 0.6, `the drums solo in their 4 (${q(drums, 12, 16).toFixed(3)} vs ${q(drums, 8, 12).toFixed(3)})`);
}
X.jbw_trade(0); X.jbw_lead_mask(0xFFFFFFFF); X.jbw_feel(0); for (let i = 0; i < 4; i++) X.jbw_level(i, 26000);
X.jbw_stop(); X.jbw_human(0);
// write a demo
const wav = (Lc, Rc, file) => {
  const n = Lc.length, b = Buffer.alloc(44 + n * 4);
  b.write('RIFF', 0); b.writeUInt32LE(36 + n * 4, 4); b.write('WAVEfmt ', 8); b.writeUInt32LE(16, 16); b.writeUInt16LE(1, 20);
  b.writeUInt16LE(2, 22); b.writeUInt32LE(sr, 24); b.writeUInt32LE(sr * 4, 28); b.writeUInt16LE(4, 32); b.writeUInt16LE(16, 34);
  b.write('data', 36); b.writeUInt32LE(n * 4, 40);
  for (let i = 0; i < n; i++) { b.writeInt16LE(Math.max(-32768, Math.min(32767, Math.round(Lc[i] * 32767))), 44 + 4 * i); b.writeInt16LE(Math.max(-32768, Math.min(32767, Math.round(Rc[i] * 32767))), 46 + 4 * i); }
  fs.writeFileSync(file, b);
};
fs.mkdirSync(path.join(web, 'out'), { recursive: true });
wav(L, R, path.join(web, 'out', 'wasm_demo.wav'));
console.log(fails ? `${fails} FAILED` : 'all passed');
process.exit(fails ? 1 : 0);
