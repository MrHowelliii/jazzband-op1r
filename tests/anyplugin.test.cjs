/* Any op1r plugin through the companion's Updater (Device tab). Simulated device only; never touches hardware. */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const C = require('../companion-core.js');
function fix(b) { new DataView(b.buffer).setUint32(b.length - 4, C.crc32(b.subarray(0, b.length - 16)), true); return b; }
function plugin(kind = 0, name = 'test_synth') {
  const b = new Uint8Array(224), d = new DataView(b.buffer), tail = b.length - 16;
  for (const [at, value] of [[0,0x5031504f],[4,1],[8,1],[12,kind],[32,132],[44,0xffffffff],[tail,0x5431504f],[tail+8,tail]]) d.setUint32(at, value, true);
  b.set(new TextEncoder().encode(name), 16); return fix(b);
}
function fake({ cancel = false } = {}) {
  let listener, written = 0; const commands = [], paths = [], chunks = [];
  const input = { addEventListener(_, f) { listener = f; }, removeEventListener() { listener = null; } };
  const output = { send(m) {
    assert.ok(Math.ceil(m.length / 3) * 4 <= 128, 'Mac USB message limit');
    const cmd = m[5], seq = m[6] | m[7] << 7, p = C.unpack7(m.slice(8, -1)); commands.push(cmd);
    let status = 0, data = new Uint8Array();
    if (cmd === 1) data = C.u32(3);
    if (cmd === 8) data = new TextEncoder().encode('op1r TEST');
    if (cmd === 2) { written = 0; paths.push(new TextDecoder().decode(p.slice(8))); }
    if (cmd === 3) { chunks.push(p.slice(4)); written += p.length - 4; data = C.u32(written); }
    if (cmd === 5) status = cancel ? 2 : 0;
    const r = C.message(cmd | 64, seq, C.join(Uint8Array.of(status), data));
    queueMicrotask(() => listener?.({ data: r }));
  } };
  return { input, output, commands, paths, chunks };
}
module.exports = async function run() {
  let checks = 0;
  for (let kind = 0; kind < 6; kind++) assert.equal(C.validateAnyPlugin(plugin(kind)).kind, C.PLUGIN_KINDS[kind]);
  checks++;
  assert.throws(() => C.validatePlugin(plugin(1)), /Only the jazzband/, 'Songs tab path stays restricted');
  const bad = plugin(); bad[80] ^= 1; assert.throws(() => C.validateAnyPlugin(bad), /checksum/);
  assert.throws(() => C.validateAnyPlugin(plugin(9)), /type/);
  assert.throws(() => C.validateAnyPlugin(plugin(0, '../escape')), /name/);
  assert.throws(() => C.validateAnyPlugin(new Uint8Array(5)), /compiled/);
  checks++;
  for (const rel of ['../jazzband.op1p', '../../guitaramp/plugins/guitaramp.op1p']) {
    const f = path.join(__dirname, rel);
    if (fs.existsSync(f)) { C.validateAnyPlugin(new Uint8Array(fs.readFileSync(f))); checks++; }
  }
  {
    const d = fake(), u = new C.Updater(d.input, d.output, () => {}, 20), a = plugin(), b = plugin(1, 'other_fx');
    await u.installMany([a, b], { any: true }); u.close();
    assert.deepEqual(d.paths, ['plugins/test_synth.op1p\0', 'plugins/other_fx.op1p\0']);
    assert.deepEqual(C.join(...d.chunks), C.join(a, b)); assert.equal(d.commands.filter(c => c === 5).length, 1, 'one T1 for the batch');
    checks++;
  }
  {
    const d = fake(), u = new C.Updater(d.input, d.output, () => {}, 20);
    await assert.rejects(u.installMany([plugin(1)]), /Only the jazzband/, 'without any:true the old rule holds'); u.close();
    assert.equal(d.commands.length, 0); checks++;
  }
  {
    const d = fake({ cancel: true }), u = new C.Updater(d.input, d.output, () => {}, 20);
    await assert.rejects(u.installMany([plugin()], { any: true }), /cancelled/); u.close();
    assert.equal(d.commands.at(-1), 6, 'staging cleared after T2'); checks++;
  }
  console.log(`${checks} any-plugin checks passed`);
};
if (require.main === module) module.exports().catch(e => { console.error(e); process.exitCode = 1; });
