/* Jazz Band companion: local library and OP1r plugin transfer.
 * Protocol reference: pcppcp/op1r-src tools/op1push.py and synth_sdk/make_plugin.py.
 * This module never sends firmware, bootloader, sample-slot, or database files.
 */
var JazzCompanion = (() => {
  'use strict';
  const text = new TextEncoder(), decoder = new TextDecoder();
  const HEADER = [0xf0, 0, 0x20, 0x76, 0x7d];
  function u32(n) { return Uint8Array.of(n & 255, n >>> 8 & 255, n >>> 16 & 255, n >>> 24 & 255); }
  function join(...parts) {
    const out = new Uint8Array(parts.reduce((n, p) => n + p.length, 0));
    let offset = 0; for (const p of parts) { out.set(p, offset); offset += p.length; } return out;
  }
  function crc32(data) {
    let crc = 0xffffffff;
    for (const b of data) { crc ^= b; for (let k = 0; k < 8; k++) crc = crc >>> 1 ^ (crc & 1 ? 0xedb88320 : 0); }
    return (crc ^ 0xffffffff) >>> 0;
  }
  function pack7(bytes) {
    const out = [];
    for (let i = 0; i < bytes.length; i += 7) {
      const g = bytes.subarray(i, i + 7); let high = 0;
      for (let k = 0; k < g.length; k++) high |= (g[k] >>> 7) << k;
      out.push(high, ...Array.from(g, b => b & 127));
    }
    return Uint8Array.from(out);
  }
  function unpack7(bytes) {
    const out = [];
    for (let i = 0; i < bytes.length;) {
      const high = bytes[i++];
      for (let k = 0; k < 7 && i < bytes.length; k++) out.push(bytes[i++] | ((high >>> k) & 1) << 7);
    }
    return Uint8Array.from(out);
  }
  function message(cmd, seq, payload = new Uint8Array()) {
    const out = join(Uint8Array.from([...HEADER, cmd, seq & 127, seq >>> 7 & 127]), pack7(payload), Uint8Array.of(247));
    if (out.length > 256) throw Error('MIDI message exceeds OP1r limit');
    return out;
  }
  function reply(bytes) {
    if (bytes.length < 10 || bytes[bytes.length - 1] !== 247 || !HEADER.every((b, i) => bytes[i] === b) || !(bytes[5] & 64)) return null;
    const payload = unpack7(bytes.slice(8, -1));
    return { cmd: bytes[5] & 63, seq: bytes[6] | bytes[7] << 7, status: payload[0], data: payload.slice(1) };
  }
  const SOUND_PACKS = ['jbpiano', 'jbbass', 'jbguitar', 'jbdrums', 'jbnylon'];
  /* ---- plugin memory: op1r loads every plugin file at boot into one budget (plugload.c TOTAL_CAP, 4 MB, image + bss);
   * a file that does not fit is refused. Loaded sizes of op1r 73's plugins (built from its sources): name -> [bytes, kind]. */
  const MEM_CAP = 4096 * 1024;
  const OP1R_MEM = {"aether":[9488,"synth"],"amp":[32464,"synth"],"arc":[19248,"synth"],"bloodmoon":[4700,"synth"],"blot":[8372,"synth"],"brassophone":[35956,"synth"],"bubbler":[131436,"fx"],"chord":[13076,"seq"],"clouds":[50152,"fx"],"dddd":[28184,"synth"],"delayed":[116016,"fx"],"dots":[102116,"seq"],"drumplus":[41260,"drum"],"elg":[16820,"synth"],"ente":[12260,"fx"],"euclid":[23204,"seq"],"eva":[21748,"synth"],"father":[16164,"fx"],"fmbell":[1724,"synth"],"granular":[53576,"synth"],"grids":[13188,"seq"],"hail":[26448,"fx"],"hollowsaint":[36600,"synth"],"in_a_room":[20792,""],"juni":[18064,"synth"],"juni_chorus":[20208,"fx"],"mbira":[23616,"synth"],"mixtur":[6544,"synth"],"moai":[55316,"synth"],"monolit":[14912,"synth"],"nostromo":[41616,"fx"],"oracle":[12472,"seq"],"palme":[11968,"fx"],"peal":[11672,"seq"],"perc":[89832,"drum"],"percsyn":[40924,"synth"],"pew":[18804,"synth"],"pitch_delay":[14596,""],"rackverb":[57808,"fx"],"randomiser":[16532,"tool"],"reactor":[16176,"synth"],"reedie":[56124,"synth"],"reverb":[3968,"fx"],"samplerplus":[40860,"sampler"],"sid":[23364,"synth"],"sister":[38816,"fx"],"snowglobe":[152744,"fx"],"steve":[719748,"fx"],"stutter":[4992,"fx"],"supersaw":[5400,"synth"],"taffy":[21636,"synth"],"tape":[7772,"fx"],"vapora":[477568,"synth"],"washer":[15864,"fx"],"wavetable":[28736,"synth"],"windwood":[49356,"synth"]};
  const JB_MEM = { jazzband: 229496, jbsongs: 66116 };
  const KEEP = ['jazzband', 'jbsongs', ...SOUND_PACKS, 'samplerplus', 'drumplus', 'perc'];   /* never suggested for removal */
  function pluginMem(name, fileBytes) {
    if (JB_MEM[name]) return JB_MEM[name];
    if (SOUND_PACKS.includes(name)) return fileBytes;                       /* no .bss: the image is the file, near enough */
    if (OP1R_MEM[name]) return OP1R_MEM[name][0];
    return Math.round(fileBytes * 1.5);                                      /* unknown plugin: its .bss is not known */
  }
  /* installed: [{name, size}] from the OP-1 -> memory now, and the smallest set of the largest synths / FX to remove so
   * everything fits with `headroom` to spare (sequencers, drum and sampler engines last, Jazz Band's own never) */
  function planMemory(installed, headroom = 64 * 1024) {
    const rows = installed.map(p => ({ ...p, mem: pluginMem(p.name, p.size), kind: (OP1R_MEM[p.name] || [])[1] || (p.name.startsWith('jb') ? 'jazz band' : ''),
                                       known: !!(JB_MEM[p.name] || OP1R_MEM[p.name] || SOUND_PACKS.includes(p.name)) }));
    const used = rows.reduce((n, r) => n + r.mem, 0), need = used + headroom - MEM_CAP;
    const order = rows.filter(r => !KEEP.includes(r.name))
      .sort((a, b) => ((['fx', 'synth'].includes(b.kind) ? 1 : 0) - (['fx', 'synth'].includes(a.kind) ? 1 : 0)) || b.mem - a.mem);
    const remove = []; let freed = 0;
    for (const r of order) { if (freed >= need) break; remove.push(r.name); freed += r.mem; }
    return { rows, used, cap: MEM_CAP, need: Math.max(0, need), remove, after: used - freed, fits: used - freed + headroom <= MEM_CAP };
  }
  function validatePluginName(name) { return /^[a-z0-9_ +\-]{1,15}$/.test(name); }
  const PLUGIN_NAMES = ['jazzband', 'jbsongs', ...SOUND_PACKS];
  function validatePlugin(bytes) {
    if (bytes.length < 92 || bytes.length > 2 * 1024 * 1024) throw Error('Choose a compiled Jazz Band .op1p file (maximum 2 MB)');
    const d = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength), tail = bytes.length - 16;
    const offset = d.getUint32(tail + 4, true), size = d.getUint32(tail + 8, true);
    if (d.getUint32(tail, true) !== 0x5431504f || size < 76 || offset + size !== tail) throw Error('Invalid OP1r plugin footer');
    if (crc32(bytes.subarray(offset, tail)) !== d.getUint32(tail + 12, true)) throw Error('Plugin checksum does not match');
    if (d.getUint32(offset, true) !== 0x5031504f || d.getUint32(offset + 4, true) !== 1 || d.getUint32(offset + 8, true) !== 1 || d.getUint32(offset + 12, true) !== 1) throw Error('Expected an API 1 OP1r FX plugin');
    const name = decoder.decode(bytes.subarray(offset + 16, offset + 32)).split('\0')[0];
    if (!PLUGIN_NAMES.includes(name)) throw Error('Only the jazzband, jbsongs and sound pack (jbpiano, jbbass, jbguitar, jbdrums, jbnylon) plugins can be installed here');
    return { name, bytes: bytes.length };
  }
  /* any op1r plugin (synth, FX, sequencer, drum, sampler, tool): the loader's own header, image, entry, relocation and
   * import-table checks (op1p.h / op1p_load.c), as in plugin-uploader/protocol.js */
  const PLUGIN_KINDS = ['Synth', 'Effect', 'Sequencer', 'Drum', 'Sampler', 'Tool'];
  function validateAnyPlugin(bytes) {
    if (!(bytes instanceof Uint8Array) || bytes.length < 92 || bytes.length > 8 * 1024 * 1024) throw Error('Choose a compiled .op1p file, up to 8 MB.');
    const d = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength), tail = bytes.length - 16;
    const off = d.getUint32(tail + 4, true), len = d.getUint32(tail + 8, true);
    if (d.getUint32(tail, true) !== 0x5431504f || off % 4 || len < 76 || off + len !== tail) throw Error('Not an OP1r plugin (bad footer). Unzip downloads first.');
    if (crc32(bytes.subarray(off, tail)) !== d.getUint32(tail + 12, true)) throw Error('Plugin checksum does not match: the file is damaged.');
    const word = n => d.getUint32(off + n, true);
    if (word(0) !== 0x5031504f || word(4) !== 1 || word(8) !== 1) throw Error('Needs an OP1r format 1 / API 1 plugin.');
    const kind = PLUGIN_KINDS[word(12)];
    if (!kind) throw Error('Unknown plugin type.');
    const raw = bytes.subarray(off + 16, off + 32), end = raw.indexOf(0);
    const name = decoder.decode(raw.subarray(0, end < 0 ? 16 : end));
    if (end < 1 || !validatePluginName(name)) throw Error('Invalid plugin name.');
    const image = word(32), bss = word(36), entry = word(40), screen = word(44), relocs = word(68), imports = word(72);
    if (image < 4 || image > 2097152 || image % 4 || bss > 2097152 || image > len - 76 || relocs > (len - 76 - image) / 16 || imports > 32)
      throw Error('Invalid plugin image or relocation sizes.');
    if (entry % 4 || entry > image - 4 || (screen !== 0xffffffff && (screen % 4 || screen > image - 4))) throw Error('Invalid plugin entry or screen offset.');
    let pos = off + 76 + image + relocs * 16;
    for (let i = 0; i < imports; i++) {
      const start = pos;
      while (pos < tail && bytes[pos]) pos++;
      if (pos >= tail || pos === start || pos - start > 31) throw Error('Invalid plugin import table.');
      pos++;
    }
    for (let i = 0; i < relocs; i++) {
      const p = off + 76 + image + i * 16, at = d.getUint32(p, true), type = d.getUint32(p + 4, true), target = d.getUint32(p + 8, true);
      const width = type === 1 ? 4 : 2;
      if (![1, 2, 3].includes(type) || at % width || at > image - width || (target & 0x80000000 ? (target & 0x7fffffff) >= imports : target > image + bss))
        throw Error('Invalid plugin relocation.');
    }
    return { name, kind, bytes: bytes.length, memory: image + bss };
  }
  /* ---- songbook over USB: a jbsongs.op1p made from the compiled template (jbsongs.h): fill len, sum, count and the
   * text, then fix the trailer CRC. The OP-1 installs it like a plugin (T1, restart); jazzband finds it at boot. */
  /* SONGS_TEMPLATE begin: generated by make_songs_template.py from a compiled jbsongs.op1p; do not edit */
  const SONGS_TEMPLATE = { prefix: 'PD94bWwgdmVyc2lvbj0iMS4wIiBlbmNvZGluZz0idXRmLTgiPz4KPHN2ZyB2ZXJzaW9uPSIxLjEiIHhtbG5zPSJodHRwOi8vd3d3LnczLm9yZy8yMDAwL3N2ZyIgd2lkdGg9IjMyMHB4IiBoZWlnaHQ9IjE2MHB4IiB2aWV3Qm94PSIwIDAgMzIwIDE2MCI+CjwhLS0gamF6emJhbmQgc2NyZWVuOiBldmVyeXRoaW5nIGlzIGRyYXduIGJ5IHRoZSBjb2RlIChzdHJva2UgdGV4dCk7IHRoaXMgZnJhbWUgb25seSBob2xkcyB0aGUgY2FudmFzIC0tPgo8bGluZSBmaWxsPSJub25lIiBzdHJva2U9IiMwMDAwMDAiIHN0cm9rZS13aWR0aD0iMSIgeDE9IjAiIHkxPSIxNTkiIHgyPSIxIiB5Mj0iMTU5Ii8+Cjwvc3ZnPgoAAAAAT1AxUAEAAAABAAAAAQAAAGpic29uZ3MAAAAAAAAAAABEAgEAAAAAAIABAABsAQAAAAAAAAAAAAAAAAAAAAAAAAIAAAAjAAAAAwAAABAAAAAQAAAAEAAAABAAAADtBWcBSuGjAWZuCuHItGIASuGjAQrhkLRiADgwQOEAAADhfAGwsSDhAARwsUDhAQAA4bQBMLFN4QAAIOFmBvCwIuEAYCHhAEAHMA3hgAEA44EAKKIADGEcdjAoouZkCDAKYAYwAONxACmiCQxC4QEAQeEBAALhpAEB4awBCgcA418AKKIADFUcQOEAAEXhAAAA4XQBBeF4AbCxIOEACHCxNrHwsILhAOAh4QBABzAA41EAQOEBACbhAAQA4dABGmC1sXaxMLH2sHpPIeEAQAcwAONAAEDhAQAA4fwBOmC1sXaxMLH2sHJPIeEAQAcwAOMxAEDhAQAA4SACCmC1sXaxMLH2sAcwik8h4QBAAOMiAKZtJwGtBRAAdjDmZEHhAQAB4cgBBjAA4wsAKKIADK0XReEAAAXheAEFMK8vCeEAAEnhAA5RAAnhAAFJ4QAOUQAJ4QACSeEADlEAAAAMAAAAEAAAAADtlf9vcXP//////wAAAAAEAAAAysLTxwgAAABKQkpTT05HMQAAAAAAAAAAAAAAAA==',
    gap: 65536, fields: 832, text: 844, binOff: 348,
    suffix: 'IFNPTkcAAAAgU09OR1MAAEpBWlogQkFORCBTT05HQk9PSwAARU1QVFkAAABTRU5UIEJZIFVTQiBGUk9NIFRIRSBKQVpaIEJBTkQgQ09NUEFOSU9OAAAAAFRISVMgRlggRE9FUyBOT1RISU5HIFRPIFRIRSBTT1VORAAAAFBMQVkgV0lUSCBKQVpaQkFORCBBUyBUSEUgTUFTVEVSIEZYAC4AAAACAAAAfAEAAAAAAAAyAAAAAwAAAHwBAAAAAAAAPgAAAAIAAAC0AQEAAAAAAEIAAAADAAAAtAEBAAAAAABIAAAAAgAAAIABAAAAAAAAXAAAAAMAAACAAQAAAAAAAH4AAAACAAAApAEBAAAAAACCAAAAAgAAAKwBAQAAAAAAhgAAAAMAAACkAQEAAAAAAIoAAAADAAAArAEBAAAAAACaAAAAAgAAAHQBAAAAAAAAngAAAAIAAAB4AQAAAAAAAKIAAAADAAAAdAEAAAAAAACmAAAAAwAAAHgBAAAAAAAAxAAAAAIAAADQAQEAAAAAAMwAAAADAAAA0AEBAAAAAADmAAAAAgAAAPwBAQAAAAAA6gAAAAMAAAD8AQEAAAAAAAQBAAACAAAAIAIBAAAAAAAIAQAAAwAAACACAQAAAAAALgEAAAIAAADIAQEAAAAAADIBAAADAAAAyAEBAAAAAABCAQAAAgAAAHgBAAAAAAAARgEAAAMAAAB4AQAAAAAAAE4BAAADAAAAAAAAgAAAAABSAQAAAgAAAAAAAIAAAAAAWAEAAAMAAAABAACAAAAAAFwBAAACAAAAAQAAgAAAAABiAQAAAwAAAAIAAIAAAAAAZgEAAAIAAAACAACAAAAAAGwBAAABAAAADAAAAAAAAABwAQAAAQAAABAAAAAAAAAAgAEAAAEAAAAAAAAAAAAAAIQBAAABAAAABAAAAAAAAACMAQAAAQAAAAgAAAAAAAAAc3FfY2F0AHNxX251bQBzcV90ZXh0AGF1dGhvcj1CcmFuZG9uCk9QMVRcAQAA5QQBALMEUoo=' };
  /* SONGS_TEMPLATE end */
  function fnv1a(bytes) { let h = 0x811c9dc5; for (const b of bytes) h = Math.imul(h ^ b, 16777619) >>> 0; return h >>> 0; }
  function b64(s) {
    if (typeof atob === 'function') return Uint8Array.from(atob(s), c => c.charCodeAt(0));
    return Uint8Array.from(Buffer.from(s, 'base64'));
  }
  function buildSongsPlugin(text, count) {
    const t = SONGS_TEMPLATE;
    if (!t) throw Error('This companion build has no songbook template');
    if (!(text instanceof Uint8Array) || !text.length || text.length > t.gap) throw Error(`Songbook must be 1–${t.gap} bytes (${text ? text.length : 0})`);
    if (!Number.isInteger(count) || count < 1 || count > 127) throw Error('Songbook must hold 1–127 songs');
    const pre = b64(t.prefix), suf = b64(t.suffix), out = new Uint8Array(pre.length + t.gap + suf.length);
    out.set(pre, 0); out.set(text, pre.length); out.set(suf, pre.length + t.gap);
    const d = new DataView(out.buffer);
    if (d.getUint32(t.fields - 8, true) !== 0x534A424A || d.getUint32(t.fields - 4, true) !== 0x31474E4F) throw Error('Songbook template damaged');
    d.setUint32(t.fields, text.length, true); d.setUint32(t.fields + 4, fnv1a(text), true); d.setUint32(t.fields + 8, count, true);
    const tail = out.length - 16, binOff = d.getUint32(tail + 4, true), binLen = d.getUint32(tail + 8, true);
    if (binOff !== t.binOff || binOff + binLen !== tail) throw Error('Songbook template damaged');
    d.setUint32(tail + 12, crc32(out.subarray(binOff, tail)), true);
    validatePlugin(out);
    return out;
  }
  function validateSongs(value, parseChart) {
    if (!Array.isArray(value) || !value.length || value.length > 2000) throw Error('Library must contain 1–2000 songs');
    return value.map((s, i) => {
      if (!s || typeof s.title !== 'string' || !s.title.trim() || typeof s.chart !== 'string' || s.chart.length > 65536 || !parseChart(s.chart, 0).length) throw Error(`Song ${i + 1}: missing title or valid chart`);
      const tempo = Number(s.tempo ?? 140);
      if (!Number.isFinite(tempo) || tempo < 40 || tempo > 320) throw Error(`Song ${i + 1}: tempo must be 40–320`);
      if (s.key !== undefined && (!Number.isInteger(s.key) || s.key < 0 || s.key > 11)) throw Error(`Song ${i + 1}: invalid key`);
      const practice = Number(s.practice);                 // practice tempo (optional): the OP-1 plays it, the original stays visible
      return { title: s.title.trim().slice(0, 200), chart: s.chart, composer: String(s.composer || '').slice(0, 200),
        tempo, ...(Number.isFinite(practice) && practice >= 40 && practice <= 320 && Math.round(practice) !== Math.round(tempo) ? { practice: Math.round(practice) } : {}), style: s.style === 2 ? 2 : s.style === 1 ? 1 : 0, ...(s.key === undefined ? {} : { key: s.key }), minor: !!s.minor,
        op1: s.op1 !== false, warnings: Array.isArray(s.warnings) ? s.warnings.map(String).slice(0, 100) : [] };
    });
  }
  /* set lists in a backup: [{name, songs: [index into the backup's songs]}] (16 at most, organisation only) */
  function validateSets(value, count) {
    if (value === undefined) return [];
    if (!Array.isArray(value)) throw Error('Set lists must be a list');
    return value.slice(0, 16).filter(x => x && typeof x.name === 'string' && x.name.trim() && Array.isArray(x.songs)).map(x => ({
      name: x.name.trim().slice(0, 60), songs: x.songs.filter(i => Number.isInteger(i) && i >= 0 && i < count).slice(0, 127) }));
  }
  function readLibrary(source, opd, parseChart) {
    if (source.trim().startsWith('{') || source.trim().startsWith('[')) {
      const value = JSON.parse(source);
      if (!Array.isArray(value) && (value.format !== 'jazzband-library' || value.version !== 1)) throw Error('Unsupported Jazz Band library version');
      const songs = validateSongs(Array.isArray(value) ? value : value.songs, parseChart);
      return { songs, errors: [], playlist: '', sets: validateSets(Array.isArray(value) ? [] : value.sets, songs.length) };
    }
    const r = opd.parseIReal(source);
    /* feel: ballad, else bossa nova for Brazilian and Latin styles (the plugin's straight-eighths feel), else swing */
    return { ...r, songs: validateSongs(r.songs.map(s => ({ ...s, style: s.ballad ? 1 : /bossa|samba|latin|brazil/i.test(s.style || '') ? 2 : 0 })), parseChart) };
  }
  function mergeSongs(current, incoming) {
    const key = s => s.title.trim().toLowerCase() + '\0' + s.chart.replace(/\s+/g, ' ').trim();
    const seen = new Set(current.map(key)), songs = current.slice(); let added = 0;
    for (const s of incoming) if (!seen.has(key(s))) { seen.add(key(s)); songs.push(s); added++; }
    if (songs.length > 2000) throw Error('Library exceeds 2000 songs');
    return { songs, added, duplicates: incoming.length - added };
  }
  /* DATA payload per message. The OP-1 drops any USB-MIDI transfer from macOS longer than about 128 bytes
   * (measured with op1mac.py probe: 64 data bytes get through, 96 do not), and two messages in flight get
   * merged into one transfer. So: 64 bytes (87 SysEx bytes, 116 USB bytes) and one message at a time. */
  const DATA_CHUNK = 64, TRIES = 3;
  class Updater {
    constructor(input, output, status = () => {}, timeout = 2500, options = {}) {
      this.input = input; this.output = output; this.status = status; this.timeout = timeout;
      this.chunk = Math.max(1, Math.min(192, options.chunk || DATA_CHUNK)); this.tries = options.tries || TRIES;
      this.seq = 0; this.pending = null; this.cancelled = false; this.busy = false; this.buf = [];
      this.receive = event => {
        /* macOS can hand a reply over in pieces: collect bytes and cut out whole F0 ... F7 messages */
        for (const b of event.data) if (b < 0xf8) this.buf.push(b);
        for (;;) {
          const i = this.buf.indexOf(0xf0); if (i < 0) { this.buf.length = 0; return; }
          const j = this.buf.indexOf(0xf7, i); if (j < 0) { if (i) this.buf.splice(0, i); return; }
          const m = Uint8Array.from(this.buf.splice(0, j + 1).slice(i));
          const r = reply(m), p = this.pending;
          if (r && p && r.cmd === p.cmd && r.seq === p.seq) { clearTimeout(p.timer); this.pending = null; p.resolve(r); }
        }
      };
      input.addEventListener('midimessage', this.receive);
    }
    close() { this.input.removeEventListener('midimessage', this.receive); if (this.pending) { clearTimeout(this.pending.timer); this.pending.reject(Error('MIDI connection closed')); this.pending = null; } }
    async ask(cmd, payload = new Uint8Array()) {
      if (this.pending) throw Error('Another MIDI request is still active');
      const seq = this.seq = (this.seq + 1) & 0x3fff;
      return new Promise((resolve, reject) => {
        const timer = setTimeout(() => { this.pending = null; const e = Error('No reply from OP1r. Check ports, USB connection, and update support.'); e.timeout = true; reject(e); }, this.timeout);
        this.pending = { cmd, seq, resolve, reject, timer };
        try { this.output.send(message(cmd, seq, payload)); } catch (e) { clearTimeout(timer); this.pending = null; reject(e); }
      });
    }
    /* for requests that are safe to repeat (HELLO, IDENT, ABORT, DATA): resend after a lost message or reply */
    async askRetry(cmd, payload) {
      for (let k = 1; ; k++) {
        try { return await this.ask(cmd, payload); }
        catch (e) { if (!e.timeout || k >= this.tries || this.cancelled) throw e; }
      }
    }
    check(r, command) { if (r.status !== 0) throw Error(`${command} refused by OP1r (status ${r.status})`); return r; }
    async identify() {
      const r = this.check(await this.askRetry(1), 'Connect');
      if (r.data.length < 4) throw Error('Incomplete OP1r handshake');
      const protocol = new DataView(r.data.buffer, r.data.byteOffset, r.data.byteLength).getUint32(0, true);
      if (protocol < 3) throw Error('This updater needs OP1r update protocol 3 or newer. Use the OP1r installer to update the base once.');
      const id = this.check(await this.askRetry(8), 'Identify');
      return { protocol, version: decoder.decode(id.data).replace(/\0/g, '') };
    }
    cancel() { this.cancelled = true; }
    async install(bytes) { return this.installMany([bytes]); }
    /* the plugin files on the OP-1: [{name, size}] (update protocol LIST, one index at a time) */
    async listPlugins() {
      if (this.busy) throw Error('An update is already running');
      this.busy = true;
      try {
        await this.identify();
        const out = [];
        for (let i = 0; i < 200; i++) {
          const r = await this.askRetry(10, Uint8Array.of(i & 255, i >> 8));
          if (r.status === 10) break;                                           /* no file at this index: the end */
          this.check(r, 'List plugins');
          const size = new DataView(r.data.buffer, r.data.byteOffset, r.data.byteLength).getUint32(0, true);
          const file = decoder.decode(r.data.subarray(4)).replace(/\0.*$/, '');
          if (/\.op1p$/.test(file)) out.push({ name: file.replace(/\.op1p$/, ''), size });
        }
        return out;
      } finally { this.busy = false; }
    }
    /* remove plugin files: staged deletes, then T1 on the OP-1 applies them and restarts it */
    async removePlugins(names) {
      if (!names.length) throw Error('Nothing to remove');
      for (const n of names) if (!validatePluginName(n) || n === 'jazzband') throw Error(`Cannot remove ${n}`);
      if (this.busy) throw Error('An update is already running');
      this.busy = true; this.cancelled = false; let staging = false, committing = false;
      const checkCancel = () => { if (this.cancelled) throw Error('Update cancelled'); };
      try {
        await this.identify(); checkCancel();
        this.check(await this.askRetry(6), 'Clear staging'); staging = true;
        for (const n of names) { checkCancel(); this.check(await this.ask(9, text.encode(`plugins/${n}.op1p\0`)), `Remove ${n}`); }
        this.status(`${names.length} ${names.length === 1 ? 'plugin' : 'plugins'} marked for removal. On the OP-1 press T1 to remove ${names.length === 1 ? 'it' : 'them'} and restart, or T2 to cancel.`);
        const deadline = Date.now() + 120000;
        while (Date.now() < deadline) {
          checkCancel(); committing = true;
          const r = await this.ask(5); committing = false;
          if (r.status === 0) { staging = false; this.status(`Removed ${names.join(', ')}. The OP-1 is restarting; the freed memory goes to the plugins that are left.`); return; }
          if (r.status === 2) throw Error('Removal cancelled on the OP-1');
          if (r.status !== 9) this.check(r, 'Apply removal');
          await new Promise(resolve => setTimeout(resolve, 300));
        }
        throw Error('Timed out waiting for confirmation on the OP-1');
      } catch (e) {
        if (staging && !committing) { try { await this.askRetry(6); } catch (_) {} }
        if (committing) throw Error('Connection lost while applying the removal. Its outcome is unknown; check the OP-1 before retrying.');
        throw e;
      } finally { this.busy = false; }
    }
    /* one update session: every file staged, then a single T1 on the OP-1 installs them all and restarts it */
    /* options.any: accept any valid op1r plugin, not only Jazz Band's own */
    async installMany(files, options = {}) {
      if (!files.length) throw Error('Nothing to send');
      const names = files.map(b => (options.any ? validateAnyPlugin : validatePlugin)(b).name);
      if (new Set(names).size !== names.length) throw Error('The same plugin was chosen twice');
      const what = names.length === 1 ? names[0] : 'packs';
      const label = what === 'jbsongs' ? 'songbook' : what === 'jazzband' ? 'Jazz Band plugin' : names.every(n => SOUND_PACKS.includes(n)) ? (names.length > 1 ? 'sound packs' : 'sound pack') : names.length > 1 ? 'plugins' : `plugin ${names[0]}`;
      if (this.busy) throw Error('An update is already running');
      this.busy = true; this.cancelled = false; let staging = false, committing = false;
      const checkCancel = () => { if (this.cancelled) throw Error('Update cancelled'); };
      const total = files.reduce((n, b) => n + b.length, 0);
      try {
        await this.identify(); checkCancel();
        this.check(await this.askRetry(6), 'Clear staging'); staging = true;
        const started = Date.now(); let done = 0;
        for (let f = 0; f < files.length; f++) {
          const bytes = files[f]; let resyncs = 0;
          this.check(await this.ask(2, join(u32(bytes.length), u32(crc32(bytes)), text.encode(`plugins/${names[f]}.op1p\0`))), 'Begin');
          for (let offset = 0; offset < bytes.length;) {
            checkCancel();
            const part = bytes.subarray(offset, offset + this.chunk);
            const r = await this.askRetry(3, join(u32(offset), part));
            const have = r.data.length >= 4 ? new DataView(r.data.buffer, r.data.byteOffset, r.data.byteLength).getUint32(0, true) : -1;
            if (r.status === 5 && have >= 0 && have <= bytes.length && ++resyncs <= 20) { offset = have; continue; }   /* a reply was lost: go on from where the OP-1 is */
            this.check(r, 'Send data');
            if (have !== offset + part.length) throw Error('OP1r reported an unexpected transfer offset');
            offset = have;
            const sent = done + offset, secs = (Date.now() - started) / 1000, rate = sent / Math.max(secs, 0.001);
            const size = n => n >= 1048576 ? `${(n / 1048576).toFixed(2)} MB` : `${(n / 1024).toFixed(1)} KB`;
            const left = secs > 3 ? Math.round((total - sent) / rate) : -1;
            const eta = left < 0 ? '' : left >= 90 ? `, about ${Math.round(left / 60)} min left` : `, about ${left} s left`;
            this.status(`Uploading ${label}${files.length > 1 ? ` (${names[f]}, ${f + 1} of ${files.length})` : ''}: ${Math.round(sent * 100 / total)}% (${size(sent)} of ${size(total)}${secs > 1 ? `, ${(rate / 1024).toFixed(1)} KB/s` : ''}${eta})`);
          }
          checkCancel(); this.check(await this.ask(4), 'Finish file (size and checksum)');
          done += bytes.length;
        }
        this.status(`${label[0].toUpperCase() + label.slice(1)} sent. On the OP-1 press T1 to install ${files.length > 1 ? 'them' : 'it'} and restart, or T2 to cancel.`);
        const deadline = Date.now() + 120000;
        while (Date.now() < deadline) {
          checkCancel(); committing = true;
          const r = await this.ask(5); committing = false;
          if (r.status === 0) {
            staging = false;
            this.status(what === 'jbsongs' ? 'Songbook installed. The OP-1 is restarting; your songs are in the Jazz Band song list (turn white).'
              : label.startsWith('sound pack') ? `${label[0].toUpperCase() + label.slice(1)} installed. The OP-1 is restarting; every Jazz Band player uses them (1.5: samples only).`
              : what === 'jazzband' ? 'OP1r accepted the update. Wait for the OP-1 to restart; check Jazz Band on the device.'
              : `Installed ${names.join(', ')}. The OP-1 is restarting; the new ${names.length > 1 ? 'plugins appear' : 'plugin appears'} in its synth or FX list.`);
            return;
          }
          if (r.status === 2) throw Error('Update cancelled on the OP-1');
          if (r.status !== 9) this.check(r, 'Apply update');
          await new Promise(resolve => setTimeout(resolve, 300));
        }
        throw Error('Timed out waiting for confirmation on the OP-1');
      } catch (e) {
        if (staging && !committing) { try { await this.askRetry(6); } catch (_) {} }
        if (committing) throw Error('Connection lost while applying the update. Its outcome is unknown; check the OP-1 before retrying.');
        throw e;
      } finally { this.busy = false; }
    }
  }
  return { DATA_CHUNK, SOUND_PACKS, MEM_CAP, OP1R_MEM, pluginMem, planMemory, buildSongsPlugin, validateAnyPlugin, PLUGIN_KINDS, fnv1a, u32, join, crc32, pack7, unpack7, message, reply, validatePlugin, validateSongs, readLibrary, mergeSongs, Updater };
})();
if (typeof module !== 'undefined') module.exports = JazzCompanion;
