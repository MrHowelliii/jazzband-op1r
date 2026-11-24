const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const C = require('../companion-core.js');
const html = fs.readFileSync(path.join(__dirname, '../Jazz Band.html'), 'utf8');
const script = id => html.match(new RegExp(`<script id="${id}"[^>]*>([\\s\\S]*?)<\\/script>`))[1];
const scope = { TextEncoder, TextDecoder, console };
vm.createContext(scope);
vm.runInContext(script('engine-src'), scope);
vm.runInContext(script('opdata-src'), scope);
const { JB, OPD } = scope;

function plugin(size = 96) {
  const bytes = new Uint8Array(size), d = new DataView(bytes.buffer), tail = size - 16;
  d.setUint32(0, 0x5031504f, true); d.setUint32(4, 1, true); d.setUint32(8, 1, true); d.setUint32(12, 1, true);
  bytes.set(new TextEncoder().encode('jazzband'), 16);
  d.setUint32(32, tail - 76, true); // dummy code: fixture only, never distributed as a plugin
  d.setUint32(tail, 0x5431504f, true); d.setUint32(tail + 4, 0, true); d.setUint32(tail + 8, tail, true);
  d.setUint32(tail + 12, C.crc32(bytes.subarray(0, tail)), true);
  return bytes;
}
/* A simulated OP1r. Like the real OP-1 on macOS, it never sees a USB-MIDI transfer over 128 bytes
 * (macLimit), and it can lose one request or one reply, or hand a reply over in two pieces. */
function fakeDevice({ failData = false, cancelCommit = false, protocol = 3, drop = false, macLimit = false,
                      loseRequestAt = -1, loseReplyAt = -1, split = false, files = [] } = {}) {
  const commands = [], chunks = [], sizes = [], paths = [], deletes = []; let listener, written = 0, commits = 0, dataSeen = 0;
  const input = { addEventListener: (_, f) => listener = f, removeEventListener: () => listener = null };
  const deliver = m => queueMicrotask(() => {
    if (!split) return listener?.({data:m});
    const k = m.length >> 1; listener?.({data:m.slice(0, k)}); queueMicrotask(() => listener?.({data:Uint8Array.of(0xf8, ...m.slice(k))}));
  });
  const output = { send(m) {
    const usb = Math.ceil(m.length / 3) * 4; sizes.push(usb);
    if (macLimit && usb > 128) return;                       // lost on the way, like the real Mac -> OP-1 link
    const cmd = m[5], seq = m[6] | m[7] << 7, payload = C.unpack7(m.slice(8, -1));
    if (cmd === 3 && dataSeen++ === loseRequestAt) return;
    commands.push(cmd);
    let status = 0, data = new Uint8Array();
    if (cmd === 1) data = C.u32(protocol);
    if (cmd === 8) data = new TextEncoder().encode('op1r TEST');
    if (cmd === 2) written = 0;                              // each file starts at offset 0
    if (cmd === 2) { const p = new TextDecoder().decode(payload.slice(8)); assert.match(p, /^plugins\/(jazzband|jbsongs|jbpiano|jbbass|jbguitar|jbdrums)\.op1p\0$/); paths.push(p.slice(0, -1)); }
    if (cmd === 3) {
      const offset = new DataView(payload.buffer, payload.byteOffset).getUint32(0, true);
      if (failData) status = 6;
      else if (offset !== written) status = 5;               // E_OFFSET: tells the host where it is
      else { chunks.push(payload.slice(4)); written += payload.length - 4; }
      data = C.u32(written);
      if (dataSeen - 1 === loseReplyAt) return;               // processed, but the reply is lost
    }
    if (cmd === 9) deletes.push(new TextDecoder().decode(payload).replace(/\0$/, ''));
    if (cmd === 10) { const i = payload[0] | payload[1] << 8; if (i >= files.length) status = 10; else data = C.join(C.u32(files[i][1]), new TextEncoder().encode(files[i][0] + '.op1p\0')); }
    if (cmd === 5) status = cancelCommit ? 2 : ++commits === 1 ? 9 : 0;
    if (!drop) deliver(C.message(cmd | 64, seq, C.join(Uint8Array.of(status), data)));
  }};
  return { input, output, commands, chunks, sizes, paths, deletes };
}

module.exports = async function run() {
  let checks = 0;
  function check(name, callback) { callback(); checks++; console.log('ok:', name); }
  check('CRC32 reference vector', () => assert.equal(C.crc32(new TextEncoder().encode('123456789')), 0xcbf43926));
  check('7-bit roundtrip and maximum MIDI packet', () => {
    const data = Uint8Array.from({length:196}, (_, i) => (i * 19) & 255);
    assert.equal(Buffer.from(C.unpack7(C.pack7(data))).toString('hex'), Buffer.from(data).toString('hex'));
    const m = C.message(3, 16383, data); assert.ok(m.length <= 256); assert.ok(m.slice(1, -1).every(x => x < 128));
    assert.equal(C.reply(C.message(67, 42, C.join(Uint8Array.of(0), data))).seq, 42);
    assert.equal(C.reply(Uint8Array.of(0x90, 60, 127)), null);
  });
  check('plugin identity and checksum validation', () => {
    assert.equal(C.validatePlugin(plugin()).name, 'jazzband');
    const bad = plugin(); bad[77] ^= 1; assert.throws(() => C.validatePlugin(bad), /checksum/);
    const other = plugin(); other[16] = 120; new DataView(other.buffer).setUint32(92,C.crc32(other.subarray(0,80)),true);
    assert.throws(() => C.validatePlugin(other), /Only the jazzband/);
    assert.throws(() => C.validatePlugin(new Uint8Array(12)), /compiled/);
  });
  const song = {title:'Practice', chart:'D-7 | G7 | C^7 | C^7 |', tempo:140, key:0};
  check('JSON backup roundtrip, validation and duplicate merge', () => {
    const backup = JSON.stringify({format:'jazzband-library', version:1, songs:[song]});
    const r = C.readLibrary(backup, OPD, JB.parseChart);
    assert.equal(r.songs[0].title, song.title);
    assert.equal(C.mergeSongs([song], r.songs).added, 0);
    assert.equal(C.mergeSongs([], r.songs).added, 1);
    assert.throws(() => C.readLibrary('{"format":"jazzband-library","version":2,"songs":[]}', OPD, JB.parseChart), /version/);
    assert.throws(() => C.validateSongs([{...song, tempo:NaN}], JB.parseChart), /tempo/);
    assert.throws(() => C.validateSongs([{...song, chart:''}], JB.parseChart), /chart/);
  });
  check('iReal Pro modern link import', () => {
    const music = OPD.scramble('*AT44D-7 | G7 | C^7 | C^7 Z');
    const link = 'irealb://' + encodeURIComponent(`Practice=Composer=Medium Swing=C==1r34LbKcu7${music}=140=0`);
    const r = C.readLibrary(link, OPD, JB.parseChart);
    assert.equal(r.songs.length, 1); assert.equal(r.songs[0].title, 'Practice');
    assert.ok(JB.parseChart(r.songs[0].chart,0).length > 0);
    assert.throws(() => C.readLibrary('irealbook://old', OPD, JB.parseChart), /old format/);
    fs.writeFileSync(path.join(__dirname,'ireal-demo.html'), `<html><a href="${link}">Practice chart</a></html>`);
    fs.writeFileSync(path.join(__dirname,'library-demo.json'), JSON.stringify({format:'jazzband-library',version:1,songs:[song]},null,2));
  });
  check('songbook AIFF preserves encoded PCM and metadata', () => {
    const b = OPD.buildSongbook([{...song, minor:false, ballad:false}]);
    const bytes = OPD.writeOP1Aiff(b.samples, 'jb songbook', 440), decoded = OPD.readAiff(bytes.buffer);
    assert.equal(decoded.rate, 44100); assert.equal(decoded.channels.length, 1);
    for (let i=0; i<b.samples.length; i++) assert.equal(Math.round(decoded.channels[0][i]*32768), b.samples[i]);
    fs.writeFileSync(path.join(__dirname,'songbook-test.raw'), Buffer.from(b.samples.buffer));
    const edited = OPD.buildSongbook([{...song, chart:song.chart.replace('D-7','E-7'), minor:false, ballad:false}]);
    fs.writeFileSync(path.join(__dirname,'songbook-edited-test.raw'), Buffer.from(edited.samples.buffer));
    /* a 12-song book with long titles, for the OP-1 song browser test and screen preview */
    const titles = ['All The Things You Are','Autumn Leaves','Blue Bossa','Bye Bye Blackbird','Footprints','Have You Met Miss Jones',
      'In A Sentimental Mood','Just Friends','Out Of Nowhere','Someday My Prince Will Come','Stella By Starlight','There Will Never Be Another You'];
    const charts = ['F-7 | Bb-7 | Eb7 | Ab^7 |', 'C-7 | F7 | Bb^7 | Eb^7 |', 'C-7 | C-7 | F-7 | F-7 |', 'T34 F^7 | F^7 | G-7 C7 | F^7 |'];
    const list = OPD.buildSongbook(titles.map((title, i) => ({title, chart:charts[i % 4], key:i % 12, minor:i % 3 === 0, ballad:i === 6})));
    fs.writeFileSync(path.join(__dirname,'songbook-list-test.raw'), Buffer.from(list.samples.buffer));
    /* the same 12 songs as a USB songbook (jbsongs.op1p), for the C test that loads it with op1r's own loader */
    const usbText = new TextEncoder().encode(OPD.songbookText(titles.map((title, i) => ({title, chart:charts[(i + 1) % 4], key:(i + 5) % 12, minor:i % 2 === 0, ballad:false})), [{name:'Friday gig', items:[3, 0]}, {name:'Empty', items:[]}]));
    fs.writeFileSync(path.join(__dirname,'jbsongs-test.op1p'), C.buildSongsPlugin(usbText, 12));
  });
  check('3/4: iReal import keeps it, chart token, warnings', () => {
    const link = (title, style, music) => 'irealb://' + encodeURIComponent(`${title}=Composer==${style}=F=n=1r34LbKcu7${OPD.scramble(music)}=150=0`);   // Title=Composer==Style=Key=n=music
    const w = C.readLibrary(link('Waltz', 'Jazz Waltz', '*AT34F^7 | G-7 C7 | F^7 | F^7 Z'), OPD, JB.parseChart).songs[0];
    assert.match(w.chart, /^T34 /); assert.equal(JSON.stringify(w.warnings), '[]');
    const bars = JB.parseChart(w.chart, 0);
    assert.equal(bars.length, 4); assert.ok(bars.every(b => b.beats === 3));
    assert.equal(JSON.stringify(bars[1].beat.slice(0, 3).map(JB.chordName)), '["Gm7","Gm7","C7"]', '2 chords in 3/4: 2 + 1 beats');
    const f = C.readLibrary(link('Four', 'Medium Swing', '*AT44F^7 | G-7 C7 | F^7 | F^7 Z'), OPD, JB.parseChart).songs[0];
    assert.doesNotMatch(f.chart, /T34/); assert.equal(JB.parseChart(f.chart, 0)[0].beats, 4);
    const five = C.readLibrary(link('Five', 'Medium Swing', '*AT54F^7 | G-7 C7 Z'), OPD, JB.parseChart).songs[0];
    assert.ok(five.warnings.some(x => /5\/4: played as 4\/4/.test(x))); assert.doesNotMatch(five.chart, /T34/);
    const mixed = C.readLibrary(link('Mixed', 'Jazz Waltz', '*AT34F^7 | G-7 C7 |T44 F^7 | F^7 Z'), OPD, JB.parseChart).songs[0];
    assert.match(mixed.chart, /^T34 /); assert.ok(mixed.warnings.some(x => /changes to 4\/4: played in 3\/4/.test(x)));
    const untagged = C.readLibrary(link('Untagged', 'Jazz Waltz', '*AF^7 | G-7 C7 Z'), OPD, JB.parseChart).songs[0];
    assert.match(untagged.chart, /^T34 /, 'a Jazz Waltz without a time signature plays in 3');
  });
  check('3/4: the Mac preview counts in 3 and plays 3-beat bars', () => {
    const e = new JB.Engine(44100, 7), beats = [];
    e.load({title:'W', tempo:180, style:0, bars:JB.parseChart('T34 F^7 | G-7 C7 | F^7 | F^7 |', 0)});
    e.onBeat = (phase, bar, beat) => beats.push([phase, bar, beat]);
    const L = new Float32Array(128), R = new Float32Array(128); let peak = 0;
    e.play(1);
    for (let i = 0; i < 44100 * 7 / 128; i++) { e.render(L, R, 128); for (let k = 0; k < 128; k++) peak = Math.max(peak, Math.abs(L[k])); }
    const count = beats.filter(b => b[0] === JB.PH_COUNT), song = beats.filter(b => b[0] === JB.PH_SONG);
    assert.equal(count.length, 3, 'count-in: 3 clicks');
    assert.equal(Math.max(...song.map(b => b[2])), 2, 'beats 0..2 only');
    assert.equal(song.length, 12, '4 bars x 3 beats');
    assert.ok(peak > 0.01 && peak < 1, `audible, no clipping (peak ${peak.toFixed(3)})`);
    const four = new JB.Engine(44100, 7), b4 = [];
    four.load({title:'F', tempo:180, style:1, bars:JB.parseChart('F^7 | G-7 C7 |', 0)}); four.onBeat = (p, bar, beat) => b4.push(beat); four.play(1);
    for (let i = 0; i < 44100 * 4 / 128; i++) four.render(L, R, 128);
    assert.equal(Math.max(...b4), 3, '4/4 songs keep 4 beats');
  });
  {   /* the real sound packs from jazzband-op1r/packs/out, in one session with one T1 */
    const dir = path.join(__dirname, '../jazzband-op1r/packs/out');
    const packs = C.SOUND_PACKS.map(n => new Uint8Array(fs.readFileSync(path.join(dir, n + '.op1p'))));
    packs.forEach((b, i) => assert.equal(C.validatePlugin(b).name, C.SOUND_PACKS[i]));
    const device = fakeDevice({macLimit:true}), statuses = [], client = new C.Updater(device.input, device.output, s => statuses.push(s), 100);
    await client.installMany(packs); client.close();
    assert.deepEqual(device.paths, C.SOUND_PACKS.map(n => `plugins/${n}.op1p`));
    assert.equal(device.commands.filter(c => c === 5).length, 2, 'one commit (T1) for all four');
    assert.equal(device.commands.filter(c => c === 4).length, 4, 'four files finished');
    assert.ok(Math.max(...device.sizes) <= 128, 'every message fits the Mac USB-MIDI limit');
    assert.equal(Buffer.from(C.join(...device.chunks)).toString('hex'), Buffer.from(C.join(...packs)).toString('hex'));
    assert.ok(statuses.some(s => /Uploading sound packs \(jbguitar, 3 of 4\)/.test(s)) && statuses.some(s => /Sound packs installed/.test(s)));
    await assert.rejects(new C.Updater(device.input, device.output, () => {}, 50).installMany([packs[0], packs[0]]), /twice/);
    checks++; console.log(`ok: four sound packs (${(packs.reduce((n, b) => n + b.length, 0) / 1048576).toFixed(2)} MB) in one session, one T1`);
  }
  {   /* memory plan on an OP-1 with every op1r 73 plugin, Jazz Band, the songbook and the four packs */
    const mem = JSON.parse(fs.readFileSync(path.join(__dirname, 'op1r_mem.json'), 'utf8'));
    const packDir = path.join(__dirname, '../jazzband-op1r/packs/out');
    const files = [...Object.entries(mem).map(([n, v]) => [n, v[1]]), ['jazzband', 65792], ['jbsongs', 67153],
                   ...C.SOUND_PACKS.map(n => [n, fs.statSync(path.join(packDir, n + '.op1p')).size])];
    const device = fakeDevice({macLimit:true, files}), client = new C.Updater(device.input, device.output, () => {}, 100);
    const list = await client.listPlugins();
    assert.equal(list.length, files.length);
    const plan = C.planMemory(list);
    assert.ok(plan.used > C.MEM_CAP, `today over the cap: ${(plan.used / 1048576).toFixed(2)} MB`);
    assert.ok(plan.fits && plan.after + 64 * 1024 <= C.MEM_CAP);
    assert.ok(!plan.remove.some(n => ['jazzband', 'jbsongs', ...C.SOUND_PACKS, 'samplerplus', 'drumplus', 'perc'].includes(n)));
    assert.deepEqual(plan.remove.slice(0, 2), ['steve', 'vapora']);
    await client.removePlugins(plan.remove); client.close();
    assert.deepEqual(device.deletes, plan.remove.map(n => `plugins/${n}.op1p`));
    assert.equal(device.commands.filter(c => c === 5).length, 2, 'one T1');
    checks++; console.log(`ok: memory plan: ${(plan.used / 1048576).toFixed(2)} MB used of 4 MB; removing ${plan.remove.length} (${plan.remove.join(', ')}) leaves ${((C.MEM_CAP - plan.after) / 1024).toFixed(0)} KB free; one T1`);
  }
  check('backups carry set lists as song indices', () => {
    const r = C.readLibrary(JSON.stringify({format:'jazzband-library', version:1, songs:[song, {...song, title:'B'}], sets:[{name:'Gig', songs:[1, 0, 7]}, {name:'', songs:[0]}, {name:'X', songs:'bad'}]}), OPD, JB.parseChart);
    assert.equal(JSON.stringify(r.sets), JSON.stringify([{name:'Gig', songs:[1, 0]}]));
    assert.equal(JSON.stringify(C.readLibrary(JSON.stringify([song]), OPD, JB.parseChart).sets), '[]');
  });
  check('songbook lines carry the song tempo last (older plugins ignore it)', () => {
    const t = OPD.songbookText([{...song, minor:false, ballad:false}, {...song, title:'Fast', tempo:300.4, minor:false, ballad:false}, {...song, title:'None', tempo:undefined, minor:false, ballad:false}]);
    const lines = t.trim().split('\n').map(l => l.split(';'));
    assert.deepEqual(lines.map(l => l.length), [6, 6, 6]);
    assert.deepEqual(lines.map(l => l[5]), ['140', '300', '140']);
    assert.equal(lines[0][4], 'D-7 | G7 | C^7 | C^7 |');
  });
  check('USB songbook plugin: template filled, checksums, limits', () => {
    const text = new TextEncoder().encode(OPD.songbookText([{...song, minor:false, ballad:false}]));
    const b = C.buildSongsPlugin(text, 1), d = new DataView(b.buffer);
    assert.deepEqual(C.validatePlugin(b), {name:'jbsongs', bytes:b.length});
    let at = -1; for (let i = 0; i + 8 <= b.length; i += 4) if (d.getUint32(i, true) === 0x534A424A && d.getUint32(i + 4, true) === 0x31474E4F) { at = i; break; }
    assert.ok(at > 0, 'entry magic present');
    assert.equal(d.getUint32(at + 8, true), text.length); assert.equal(d.getUint32(at + 12, true), C.fnv1a(text)); assert.equal(d.getUint32(at + 16, true), 1);
    assert.equal(Buffer.from(b.subarray(at + 20, at + 20 + text.length)).toString(), Buffer.from(text).toString());
    assert.ok(b.subarray(at + 20 + text.length, at + 20 + 65536).every(x => x === 0), 'rest of the songbook area is empty');
    assert.equal(C.fnv1a(new TextEncoder().encode('a')), 0xe40c292c);
    assert.throws(() => C.buildSongsPlugin(new Uint8Array(), 1), /1–65536/);
    assert.throws(() => C.buildSongsPlugin(new Uint8Array(65537), 1), /1–65536/);
    assert.throws(() => C.buildSongsPlugin(text, 128), /1–127/);
  });
  {
    const device = fakeDevice({macLimit:true}), statuses = [], client = new C.Updater(device.input,device.output,s=>statuses.push(s),100);
    const songsFile = C.buildSongsPlugin(new TextEncoder().encode(OPD.songbookText([{...song, minor:false, ballad:false}])), 1);
    await client.install(songsFile); client.close();
    assert.deepEqual(device.paths, ['plugins/jbsongs.op1p']);
    assert.equal(Buffer.from(C.join(...device.chunks)).toString('hex'), Buffer.from(songsFile).toString('hex'));
    assert.ok(statuses.some(s => /Songbook sent.*T1/.test(s)) && statuses.some(s => /Songbook installed/.test(s)));
    checks++; console.log('ok: songbook sent over USB as plugins/jbsongs.op1p (64-byte pieces), T1 confirmation');
  }
  {
    const device = fakeDevice(), statuses = [], client = new C.Updater(device.input,device.output,s=>statuses.push(s),100);
    await client.install(plugin(1024)); client.close();
    assert.equal(JSON.stringify(device.commands), '[1,8,6,2,' + '3,'.repeat(16) + '4,5,5]');   // 1024 bytes in 64-byte pieces
    assert.equal(Buffer.from(C.join(...device.chunks)).toString('hex'),Buffer.from(plugin(1024)).toString('hex')); assert.ok(statuses.some(s=>s.includes('T1')));
    checks++; console.log('ok: staged plugin upload and physical-confirmation polling');
  }
  {
    const device = fakeDevice({macLimit:true}), client = new C.Updater(device.input,device.output,()=>{},100);
    await client.install(plugin(5000)); client.close();
    assert.equal(C.DATA_CHUNK, 64); assert.ok(Math.max(...device.sizes) <= 128, 'every USB-MIDI transfer fits the Mac limit');
    assert.equal(Buffer.from(C.join(...device.chunks)).toString('hex'),Buffer.from(plugin(5000)).toString('hex'));
    const old = fakeDevice({macLimit:true}), stalls = new C.Updater(old.input,old.output,()=>{},20,{chunk:192});
    await assert.rejects(stalls.install(plugin(5000)), /No reply/); stalls.close();   // the previous 192-byte setting stalls
    checks++; console.log('ok: every message fits the ~128-byte macOS USB-MIDI limit (192-byte pieces stall)');
  }
  for (const [name, opts] of [['lost DATA request is resent', {loseRequestAt:3}], ['lost DATA reply: resync from the OP-1 offset', {loseReplyAt:5}],
                              ['reply split across MIDI events', {split:true}]]) {
    const device = fakeDevice({macLimit:true, ...opts}), client = new C.Updater(device.input,device.output,()=>{},30);
    await client.install(plugin(1024)); client.close();
    assert.equal(Buffer.from(C.join(...device.chunks)).toString('hex'),Buffer.from(plugin(1024)).toString('hex'));
    checks++; console.log('ok:', name);
  }
  for (const mode of ['data-error','device-cancel','user-cancel','old-protocol','timeout']) {
    const device = fakeDevice({failData:mode==='data-error',cancelCommit:mode==='device-cancel',protocol:mode==='old-protocol'?2:3,drop:mode==='timeout'});
    const client = new C.Updater(device.input,device.output,()=>{if(mode==='user-cancel')client.cancel();},20);
    await assert.rejects(client.install(plugin()));
    if (['data-error','device-cancel','user-cancel'].includes(mode)) assert.equal(device.commands.at(-1),6);
    if (mode==='old-protocol') assert.equal(JSON.stringify(device.commands),'[1]');
    client.close(); checks++; console.log('ok:',mode);
  }
  console.log(`${checks} companion checks passed`);
  return checks;
};
if (require.main === module) module.exports().catch(e => {console.error(e);process.exitCode=1;});
