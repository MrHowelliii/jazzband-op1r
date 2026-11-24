/* jbw-glue.js: runs the jazzband plugin (compiled to WebAssembly from plugins/jazzband/jazzband.c, see web/jb_wasm.c)
 * and turns it into audio for the browser. Shared by the companion's AudioWorklet, its ScriptProcessor fallback (older
 * Safari) and the Node tests. No DOM, no TextEncoder: it must run inside an AudioWorkletGlobalScope. */
var JBW = (function () {
  'use strict';
  /* SETUP rows (jazzband.c R_*) */
  const R = { LOOP: 1, FROM: 2, TO: 3, KSTEP: 4, CHORUS: 5, FEEL: 6, INTRO: 7, TURN: 8, END: 9,
    DPLAY: 10, BPLAY: 11, PPLAY: 12, GPLAY: 13, RKEY: 14, RTEMPO: 15 };
  /* the state words (jb_wasm.c W_*) */
  const W = ['state', 'bar', 'tick', 'beat', 'chorus', 'pass', 'ending', 'kind', 'shift', 'keyOff', 'keyPend', 'keyTgt', 'bpm',
    'origBpm', 'song', 'pend', 'loop', 'lfrom', 'lto', 'kstep', 'choruses', 'intro', 'turn', 'endtype', 'feel',
    'sty0', 'sty1', 'sty2', 'sty3', 'nb', 'nbars', 'jump', 'sbar', 'introI', 'nsec', 'tagI', 'nxBar', 'nxKind',
    'c0r', 'c0q', 'c1r', 'c1q', 'c2r', 'c2q', 'c3r', 'c3q', 'nr', 'nq', 'pkOk', 'pkFound', 'songBpm', 'kmsg', 'tpbar',
    'actual', 'nsongs', 'vsec', 'nxsec', 'spt', 'loopLo', 'loopHi'];
  const ST = { STOP: 0, ARMED: 1, COUNT: 2, PLAY: 3 };
  const KIND = { CHART: 0, TAG: 1, FINAL: 2, HOLD: 3, END: 4, INTRO: 5 };
  /* the calls the page may make (each is a knob action on the OP-1) */
  const CALLS = { play: 0, finish: 0, stop: 0, transport: 0, tempo: 1, key: 1, volume: 1, mute: 1, level: 2, set: 2, act: 1,
    song: 3, cancel_queue: 0, bar: 2, set_tadj: 2, reload: 0, keep: 0 };
  const RATE = 44100, BLOCK = 64;

  function sineTable(mem, ptr) {
    const t = new Int16Array(mem.buffer, ptr, 8192);
    for (let i = 0; i < 8192; i++) { const v = 32767 * Math.sin(2 * Math.PI * i / 8192); t[i] = v < 0 ? -Math.round(-v) : Math.round(v); }
  }

  class Band {
    /* module: a WebAssembly.Module (or the wasm bytes); outRate: the AudioContext's sample rate */
    constructor(module, outRate) {
      if (!(module instanceof WebAssembly.Module)) module = new WebAssembly.Module(module);
      this.inst = new WebAssembly.Instance(module, {});
      this.x = this.inst.exports; this.mem = this.x.memory;
      sineTable(this.mem, this.x.jbw_sine());
      this.x.jbw_init();
      this.rate = outRate || RATE;
      this.step = RATE / this.rate;               /* engine frames per output frame */
      this.buf = new Float32Array(0); this.bufR = this.buf; this.n = 0; this.pos = 0;   /* engine output not played yet */
      this.prevL = 0; this.prevR = 0;
      this.bookGen = 0; this.curL = this.curR = 0; this.ri = 0;
    }
    setRate(r) { this.rate = r; this.step = RATE / r; }
    view(T, ptr, n) { return new T(this.mem.buffer, ptr, n); }
    /* engine frames into this.buf (refill when the read position passes the end) */
    pull(blocks) {
      const n = this.x.jbw_render(blocks);
      this.buf = this.view(Float32Array, this.x.jbw_out_l(), n); this.bufR = this.view(Float32Array, this.x.jbw_out_r(), n);
      this.n = n;
    }
    render(L, R, frames) {
      if (this.step === 1) {                      /* 44.1 kHz: straight copy, 64 frames a block */
        let o = 0;
        while (o < frames) {
          if (this.pos >= this.n) { this.pull(Math.min(32, Math.ceil((frames - o) / BLOCK))); this.pos = 0; }
          const k = Math.min(frames - o, this.n - this.pos);
          L.set(this.buf.subarray(this.pos, this.pos + k), o); R.set(this.bufR.subarray(this.pos, this.pos + k), o);
          this.pos += k; o += k;
        }
        return;
      }
      /* other rates (48 kHz on most iPhones if Safari ignores the requested rate): linear interpolation */
      for (let o = 0; o < frames; o++) {
        while (this.pos >= 1) {                    /* advance one engine frame */
          this.pos -= 1;
          this.prevL = this.curL; this.prevR = this.curR;
          if (this.ri >= this.n) { this.pull(2); this.ri = 0; }
          this.curL = this.buf[this.ri]; this.curR = this.bufR[this.ri]; this.ri++;
        }
        const f = this.pos, a = this.prevL, b = this.curL, c = this.prevR, d = this.curR;
        L[o] = a + (b - a) * f; R[o] = c + (d - c) * f;
        this.pos += this.step;
      }
    }
    call(f, args) {
      if (!(f in CALLS)) throw Error('unknown call ' + f);
      return this.x['jbw_' + f](...(args || []).slice(0, CALLS[f]).map(v => v | 0));
    }
    /* the songbook text (UTF-8 bytes of OPD.songbookText) and its song count; returns the songs read (< 0: rejected) */
    setBook(bytes, count) {
      const max = this.x.jbw_book_max();
      if (bytes.length > max) return -1;
      this.view(Uint8Array, this.x.jbw_book_text(), max).set(bytes);
      this.bookGen++;
      return this.x.jbw_book_set(bytes.length, count);
    }
    /* a sound pack: the bytes from its jbpack_t on (see findPack); returns its instrument 0..4, or -1 */
    addPack(bytes) {
      const p = this.x.jbw_alloc(bytes.length);
      this.n = 0; this.ri = 0;                    /* the memory may have grown: views of the old buffer are gone, render afresh */
      if (!p) return -1;
      this.view(Uint8Array, p, bytes.length).set(bytes);
      return this.x.jbw_pack_add(p, bytes.length);
    }
    /* per-song settings of this session: [loop, lfrom, lto, kstep, choruses, intro, turn, endtype, ballad, bossa, key] */
    songSettings(i) {
      const tmp = this.scratch || (this.scratch = this.x.jbw_alloc(16));
      if (!this.x.jbw_ss_get(i, tmp)) return null;
      return Array.from(this.view(Uint8Array, tmp, 11), (v, k) => k === 10 ? (v << 24 >> 24) : v);
    }
    setSongSettings(i, a) {
      const tmp = this.scratch || (this.scratch = this.x.jbw_alloc(16));
      this.view(Uint8Array, tmp, 11).set(a.slice(0, 11).map(v => v & 255));
      this.x.jbw_ss_set(i, tmp);
    }
    tempoOf(i) { return this.x.jbw_tadj(i); }
    state() {
      const a = this.view(Int32Array, this.x.jbw_state(), this.x.jbw_state_len()), o = {};
      for (let i = 0; i < W.length; i++) o[W[i]] = a[i];
      return o;
    }
    /* the chart as written: [{beat: [{root, q} x4], section}] */
    bars() {
      const s = this.state(), a = this.view(Int8Array, this.x.jbw_bars(), s.nbars * 9), out = [];
      for (let b = 0; b < s.nbars; b++) {
        const beat = []; for (let k = 0; k < 4; k++) beat.push({ root: a[b * 9 + 2 * k], q: a[b * 9 + 2 * k + 1] });
        out.push({ beat, section: a[b * 9 + 8] ? String.fromCharCode(a[b * 9 + 8]) : '' });
      }
      return out;
    }
    sections() { const out = []; for (let i = 0; ; i++) { const v = this.x.jbw_section(i); if (v < 0) return out; out.push({ bar: v & 255, ch: String.fromCharCode(v >> 8) }); } }
  }

  /* where a jbpack_t sits in a sound pack .op1p: its op1_fx_t's press bytes (CA C2 D0 CB), then the JBPK SND1 magic.
   * Returns the bytes from there to the end (the audio follows the image), or null. */
  function findPack(file) {
    const u = file instanceof Uint8Array ? file : new Uint8Array(file);
    for (let i = 0; i + 64 < u.length; i += 4) {
      if (u[i + 8] !== 0xCA || u[i + 9] !== 0xC2 || u[i + 10] !== 0xD0 || u[i + 11] !== 0xCB) continue;
      if (u[i + 16] === 0x4A && u[i + 17] === 0x42 && u[i + 18] === 0x50 && u[i + 19] === 0x4B &&
          u[i + 20] === 0x53 && u[i + 21] === 0x4E && u[i + 22] === 0x44 && u[i + 23] === 0x31) return u.slice(i);
    }
    return null;
  }

  /* the processor body, shared by the AudioWorklet and the fallback: messages in, audio and state out */
  class Host {
    constructor(module, outRate, send) {
      this.band = new Band(module, outRate); this.send = send; this.frames = 0; this.rate = outRate;
      this.every = Math.max(128, Math.round(outRate / 60));   /* state about 60 times a second */
      this.since = this.every; this.lastSig = '';
    }
    message(d) {
      const b = this.band;
      try {
        if (d.t === 'call') b.call(d.f, d.a);
        else if (d.t === 'book') {
          const n = b.setBook(d.bytes, d.count);
          if (n > 0 && d.ss) for (const [i, a] of d.ss) b.setSongSettings(i, a);
          if (n > 0 && d.tadj) for (const [i, v] of d.tadj) b.call('set_tadj', [i, v]);
          if (n > 0) { b.call('reload'); if (d.song > 0) b.call('song', [d.song, -1, 0]); }
          this.send({ t: 'book', n });
        } else if (d.t === 'midi') this.midi = !!d.on;
        else if (d.t === 'pack') this.send({ t: 'pack', inst: b.addPack(d.bytes), name: d.name });
        else if (d.t === 'keep') {                 /* the session's per-song settings, to remember */
          b.call('keep'); const s = b.state(), ss = [], tadj = [];
          for (let i = 0; i < s.nsongs; i++) { const a = b.songSettings(i); if (a) ss.push([i, a]); const t = b.tempoOf(i); if (t) tadj.push([i, t]); }
          this.send({ t: 'keep', ss, tadj, id: d.id });
        }
      } catch (err) { this.send({ t: 'error', message: String(err && err.message || err) }); }
      this.since = this.every;                     /* answer with the new state at once */
    }
    process(L, R, frames, time) {
      this.band.render(L, R, frames);
      const x = this.band.x, ne = x.jbw_event_count(), nt = x.jbw_tick_count();
      if (this.midi && (ne || nt)) {
        const q = this.band.view(Uint8Array, x.jbw_events(), ne * 6), ev = [];
        for (let i = 0; i < ne; i++) ev.push([q[i * 6], q[i * 6 + 1], q[i * 6 + 2], q[i * 6 + 3]]);
        this.send({ t: 'midi', ev, ticks: nt, spt: this.band.state().spt, time });
      }
      if (ne || nt) x.jbw_events_clear();
      this.since += frames;
      if (this.since >= this.every) {
        this.since = 0;
        const s = this.band.state(), msg = { t: 'state', s, time };
        const sig = this.band.bookGen + ':' + s.song + ':' + s.nbars;
        if (sig !== this.lastSig) { this.lastSig = sig; msg.bars = this.band.bars(); msg.sections = this.band.sections(); }
        this.send(msg);
      }
    }
  }
  return { Band, Host, findPack, R, W, ST, KIND, RATE };
})();
if (typeof module !== 'undefined') module.exports = JBW;
