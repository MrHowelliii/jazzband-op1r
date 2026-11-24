/* The browser band: the WebAssembly build of the plugin (jazzband-op1r/out/jazzband.wasm, `make web`) runs the scripted
 * session in jazzband-op1r/test/web_script.h through the page's own glue (web/jbw-glue.js) and must match the same
 * session compiled natively (out/web_test) sample for sample. Then glue-level checks: resampling, packs, settings. */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { execFileSync } = require('node:child_process');
const root = path.join(__dirname, '../jazzband-op1r');
const JBW = require(path.join(root, 'web/jbw-glue.js'));
const wasm = fs.readFileSync(path.join(root, 'out/jazzband.wasm'));
const bookPath = path.join(root, 'test/web_book.txt');
const book = fs.readFileSync(bookPath);
let checks = 0;
const ok = (name) => { checks++; console.log('ok: ' + name); };

/* 1. the scripted session, sample for sample against the native build */
const native = execFileSync(path.join(root, 'out/web_test'), [bookPath], { encoding: 'utf8' }).trim().split('\n');
const band = new JBW.Band(new WebAssembly.Module(wasm), 44100);
const out = [`songs ${band.setBook(book, 3)}`];
let hash = 2166136261;
const L = new Float32Array(64), R = new Float32Array(64);
const run = n => { for (let b = 0; b < n; b++) { band.render(L, R, 64); for (let i = 0; i < 64; i++) {
  const l = Math.round(L[i] * 32768) & 0xFFFF, r = Math.round(R[i] * 32768) & 0xFFFF;
  hash = Math.imul(hash ^ l, 16777619) >>> 0; hash = Math.imul(hash ^ r, 16777619) >>> 0; } } };
const cp = label => { const s = band.state();
  out.push(`${label} state=${s.state} bar=${s.bar} song=${s.song} bpm=${s.bpm} key=${s.actual} chorus=${s.chorus} pass=${s.pass} kind=${s.kind} hash=${hash.toString(16).padStart(8, '0')}`); };
const script = fs.readFileSync(path.join(root, 'test/web_script.h'), 'utf8').replace(/\/\*[\s\S]*?\*\//g, '')
  .replace(/jbw_(\w+)\(/g, (m, f) => `call('${f}', `).replace(/call\('(\w+)', \)/g, "call('$1')");
new Function('call', 'run', 'cp', script.replace(/call\('(\w+)',? ?([^;]*)\)/g, (m, f, a) => `call('${f}', [${a}])`))(
  (f, a) => band.call(f, a), run, cp);
assert.deepEqual(out, native, 'wasm and native sessions differ');
ok(`WebAssembly band matches the native build sample for sample (${native.length - 1} checkpoints)`);

/* 2. resampling to 48 kHz keeps the timing (one 140 BPM bar = 4 * 60 / 140 s) */
{
  const b = new JBW.Band(wasm, 48000); b.setBook(book, 3); b.call('play');
  const l = new Float32Array(128), r = new Float32Array(128); let frames = 0, t0 = -1, t1 = -1;
  while (t1 < 0 && frames < 48000 * 10) { b.render(l, r, 128); frames += 128; const s = b.state();
    if (s.state === 3 && s.bar === 1 && t0 < 0) t0 = frames; if (s.state === 3 && s.bar === 2 && t1 < 0) t1 = frames; }
  const want = 4 * 60 / 140 * 48000;
  assert.ok(Math.abs(t1 - t0 - want) <= 256, `bar at 48 kHz: ${t1 - t0} frames, want ${want}`);
  let peak = 0; for (let i = 0; i < 2000; i++) { b.render(l, r, 128); for (const v of l) peak = Math.max(peak, Math.abs(v)); }
  assert.ok(peak > 0.02 && peak < 1, `48 kHz band level ${peak}`);
  ok('48 kHz output: resampled, bars keep their length');
}

/* 3. chart, sections and the state the page reads */
{
  const b = new JBW.Band(wasm, 44100); assert.equal(b.setBook(book, 3), 3);
  const s = b.state(), bars = b.bars();
  assert.equal(s.nbars, 16); assert.equal(bars[0].section, 'A'); assert.equal(bars[8].section, 'B');
  assert.deepEqual(bars[0].beat[0], { root: 0, q: 1 });
  assert.deepEqual(b.sections().map(x => x.bar), [0, 8]);
  assert.equal(s.bpm, 140);
  b.call('song', [1, -1, 0]); const w = b.state();
  assert.equal(w.nb, 3); assert.equal(w.bpm, 180); assert.equal(w.origBpm, 160);
  b.call('song', [2, -1, 0]); assert.equal(b.state().feel, 1, 'songbook feel 1: ballad');
  ok('chart, sections, 3/4, practice and original tempo, songbook feel');
}

/* 4. per-song settings survive a new book (the page keeps them between visits) */
{
  const b = new JBW.Band(wasm, 44100); b.setBook(book, 3);
  b.call('set', [JBW.R.LOOP, 2]); b.call('set', [JBW.R.CHORUS, 4]); b.call('key', [3]); b.call('tempo', [150]);
  b.call('keep'); const a = b.songSettings(0); const t = b.tempoOf(0);
  assert.deepEqual([a[0], a[4], a[10], t], [2, 4, 3, 150]);
  b.setBook(book, 3); assert.equal(b.state().loop, 0, 'a new book clears the session');
  b.setSongSettings(0, a); b.call('set_tadj', [0, t]); b.call('reload');
  const s = b.state(); assert.deepEqual([s.loop, s.choruses, s.actual, s.bpm], [2, 4, 3, 150]);
  ok('per-song settings restore after a new book');
}

/* 5. key while playing is queued for the next bar line; a queued song waits for the bar line */
{
  const b = new JBW.Band(wasm, 44100); b.setBook(book, 3); b.call('play');
  const l = new Float32Array(64), r = new Float32Array(64);
  while (b.state().state !== 3) b.render(l, r, 64);
  b.call('key', [-2]); let s = b.state();
  assert.equal(s.keyPend, 1); assert.equal(s.actual, 0);
  const bar0 = s.bar; while (b.state().bar === bar0) b.render(l, r, 64);
  assert.equal(b.state().actual, -2);
  b.call('song', [2, -1, 0]); assert.equal(b.state().pend, 2); b.call('cancel_queue'); assert.equal(b.state().pend, -1);
  ok('key and song changes wait for the bar line; the queue can be cancelled');
}

/* 6. sound packs: found inside an .op1p-shaped file, checksum checked */
{
  const zones = 1, dataLen = 400, hdr = 16 + 8 + 24 + 96 + 160 * 20;
  const p = new Uint8Array(hdr + dataLen); const dv = new DataView(p.buffer);
  p.set([0xCA, 0xC2, 0xD0, 0xCB], 8); dv.setUint32(16, 0x4B50424A, true); dv.setUint32(20, 0x31444E53, true);
  dv.setUint32(24, 1, true); dv.setUint32(28, zones, true); dv.setUint32(32, hdr, true); dv.setUint32(36, dataLen, true);
  const z = 24 + 24 + 96; p[z] = 40; p[z + 1] = 0; p[z + 2] = 127; dv.setUint32(z + 4, 65536, true); dv.setUint32(z + 16, 800, true);
  for (let i = 0; i < dataLen; i++) p[hdr + i] = (i * 37) & 255;
  let h = 2166136261; for (const x of p.subarray(z, z + 20)) h = Math.imul(h ^ x, 16777619) >>> 0;
  for (const x of p.subarray(hdr)) h = Math.imul(h ^ x, 16777619) >>> 0;
  dv.setUint32(40, h, true);
  const file = new Uint8Array(1000 + p.length); file.set(p, 1000);
  const found = JBW.findPack(file); assert.ok(found && found.length === p.length, 'pack found in the file');
  const b = new JBW.Band(wasm, 44100); b.setBook(book, 3);
  assert.equal(b.addPack(found), 1, 'bass pack accepted'); assert.equal(b.state().pkOk, 2);
  const bad = found.slice(); bad[hdr + 3] ^= 1; const b2 = new JBW.Band(wasm, 44100);
  assert.equal(b2.addPack(bad), -1, 'a damaged pack is refused');
  ok('sound packs: located in the file, checked, refused when damaged');
}

/* 7. the processor host: state messages about 60 a second, chart on song change */
{
  const msgs = []; const h = new JBW.Host(wasm, 48000, m => msgs.push(m));
  h.message({ t: 'book', bytes: book, count: 3, song: 1 });
  const l = new Float32Array(128), r = new Float32Array(128);
  for (let i = 0; i < 375; i++) h.process(l, r, 128, i * 128 / 48000);   /* one second */
  const states = msgs.filter(m => m.t === 'state');
  assert.ok(states.length >= 45 && states.length <= 70, `${states.length} state messages a second`);
  assert.ok(states[0].bars && states[0].bars.length === 8, 'the chart comes with the first state');
  assert.equal(states[0].s.song, 1, 'the book message selects the song');
  assert.equal(msgs.find(m => m.t === 'book').n, 3);
  ok('processor host: book, state rate, chart');
}
/* 8. notes for MIDI out: the count-in clicks, then the band, one tick at a time */
{
  const msgs = []; const h = new JBW.Host(wasm, 44100, m => msgs.push(m));
  h.message({ t: 'book', bytes: book, count: 3 }); h.message({ t: 'midi', on: true }); h.message({ t: 'call', f: 'play' });
  const l = new Float32Array(128), r = new Float32Array(128);
  for (let i = 0; i < 345 * 3; i++) h.process(l, r, 128, i * 128 / 44100);   /* three seconds at 140: count-in and a bar */
  const m = msgs.filter(x => x.t === 'midi'), ev = m.flatMap(x => x.ev), ticks = m.reduce((a, x) => a + x.ticks, 0);
  assert.deepEqual(ev.slice(0, 4).map(e => e[0] + ':' + e[1]), ['0:5', '0:5', '0:5', '0:5'], 'four count-in clicks (drums, cross-stick)');
  assert.ok(ev.some(e => e[0] === 1) && ev.some(e => e[0] === 2), 'bass and piano notes follow');
  const want = 3 * 44100 / (44100 * 60 / 140 / 12);
  assert.ok(Math.abs(ticks - want) < 3, `${ticks} ticks in 3 s, want ${want.toFixed(1)}`);
  ok('note and tick events for MIDI out');
}
/* 9. a pack arriving while the band plays (memory grows under the audio): no NaN, no gap, at 44.1 and 48 kHz */
for (const rate of [44100, 48000]) {
  const b = new JBW.Band(wasm, rate); b.setBook(book, 3); b.call('play');
  const l = new Float32Array(128), r = new Float32Array(128); let bad = 0, peak = 0;
  for (let i = 0; i < 1200; i++) {
    if (i === 600) { const big = new Uint8Array(3 << 20); assert.equal(b.addPack(big.subarray(0, 64)), -1); b.x.jbw_alloc(3 << 20); b.addPack(new Uint8Array(64)); }
    b.render(l, r, 128); for (const v of l) { if (!Number.isFinite(v)) bad++; peak = Math.max(peak, Math.abs(v)); }
  }
  assert.equal(bad, 0, `finite samples at ${rate}`); assert.ok(peak > 0.02);
}
ok('memory growth while playing (a pack arriving) keeps the audio clean');
console.log(`${checks} web checks passed`);
