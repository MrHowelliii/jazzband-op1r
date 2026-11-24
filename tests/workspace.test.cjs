/* Exercise the real workspace handlers with a minimal DOM, without device access. */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
module.exports = function run() {
  const html = fs.readFileSync(path.join(__dirname, '../Jazz Band.html'), 'utf8');
  const elements = new Map();
  const element = id => {
    if (!elements.has(id)) elements.set(id, {value:'', style:{}, dataset:{}, classList:{add(){},remove(){}}, contains(){return false;}});
    return elements.get(id);
  };
  const context = vm.createContext({TextEncoder, TextDecoder, console, document:{querySelector:element,querySelectorAll(){return [];}}, $:element,
    buildUI(){}, saveSongs(){}, fillSongSelect(){}, selectSong(){}, showView(){}, songId(s){ if(!s.id) s.id='id'+Math.random().toString(36).slice(2); return s.id; }, escapeHTML:s=>String(s).replace(/</g,'&lt;')});
  for (const id of ['engine-src','opdata-src']) vm.runInContext(html.match(new RegExp(`<script id="${id}"[^>]*>([\\s\\S]*?)<\\/script>`))[1],context);
  vm.runInContext(html.slice(html.indexOf('function songKey('),html.indexOf('function escapeHTML(')),context);
  const start = html.indexOf('function selectedBook('), end = html.indexOf('function importText(source)', start);
  vm.runInContext(`var songs = [
    {title:'One',tempo:120,chart:'C^7 | F7',op1:true},
    {title:'Two',tempo:130,chart:'D-7 | G7',op1:false},
    {title:'Three',tempo:140,chart:'F^7 | Bb7',op1:true}
  ]; var sets = []; var st = {idx:0}; ${html.slice(start,end)}`,context);
  const evaluate = code => vm.runInContext(code,context);
  let checks = 0;
  function check(name, fn) { fn(); checks++; }
  check('remove keeps library chart and disables export when empty',()=>{
    evaluate('setSongIncluded(0,false);setSongIncluded(2,false)');
    assert.equal(evaluate('songs.length'),3); assert.equal(element('sbSave').disabled,true);
    assert.match(element('songbookList').innerHTML,/Your OP-1 songbook starts here/);
  });
  check('button add restores valid songbook and clears warning acceptance',()=>{
    element('acceptWarnings').checked=true; evaluate('setSongIncluded(1,true)');
    assert.equal(element('sbSave').disabled,false); assert.equal(element('acceptWarnings').checked,false);
    assert.equal(evaluate('selectedBook().count'),1);
  });
  function drop(area,index,targetIndex) {
    evaluate(`draggedSong={index:${index},origin:'library'}`);
    element(area).ondrop({preventDefault(){}, dataTransfer:{types:['application/x-jazzband-song']},target:{closest(){return targetIndex==null ? null : {dataset:{songIndex:String(targetIndex)}};}}});
  }
  check('drag adds before a song and keeps preview attached to the same song',()=>{
    drop('songbookDrop',0,1);
    assert.equal(evaluate('songs.filter(s=>s.op1!==false).map(s=>s.title).join(",")'),'One,Two');
    assert.equal(evaluate('songs[st.idx].title'),'One');
  });
  check('drag back removes from set without deleting song',()=>{
    drop('libraryDrop',0); assert.equal(evaluate('songs[0].op1'),false); assert.equal(evaluate('songs.length'),3);
  });
  check('reordering preserves chart data and selected song',()=>{
    evaluate('setSongIncluded(0,true);moveSong(1,0)');
    assert.equal(evaluate('songs.filter(s=>s.op1!==false).map(s=>s.title).join(",")'),'Two,One');
    assert.equal(evaluate('songs[st.idx].title'),'One');
    assert.equal(evaluate('songs[0].chart'),'D-7 | G7');
  });
  check('search filters library without hiding songbook',()=>{
    element('librarySearch').value='missing'; evaluate('renderLib()');
    assert.match(element('lib').innerHTML,/No songs found/); assert.match(element('songbookList').innerHTML,/Two/);
  });
  check('127-song limit blocks button and drag additions',()=>{
    evaluate("songs=Array.from({length:128},(_,i)=>({title:'Song '+i,tempo:120,chart:'C^7',op1:i<127}));setSongIncluded(127,true)");
    assert.equal(evaluate('songs[127].op1'),false); drop('songbookDrop',127);
    assert.equal(evaluate('songs.filter(s=>s.op1!==false).length'),127);
  });
  check('warnings still require acknowledgement for export',()=>{
    evaluate("songs=[{title:'Warning',tempo:120,chart:'C^7',warnings:['Converted meter']}];renderLib()");
    element('acceptWarnings').checked=false;
    assert.throws(()=>evaluate('selectedBook()'),/Review the import warnings/);
    element('acceptWarnings').checked=true; assert.equal(evaluate('selectedBook().count'),1);
  });
  check('set lists: add puts a song on the OP-1 too, order kept, lines in the songbook',()=>{
    evaluate("songs=[{title:'One',tempo:120,chart:'C^7 | F7',op1:true},{title:'Two',tempo:130,chart:'D-7 | G7',op1:false},{title:'Three',tempo:140,chart:'F^7 | Bb7',op1:true}];sets=[];renderLib()");
    evaluate("newSet('Friday gig');addToSet(2);addToSet(1);addToSet(0, songs[2].id)");
    assert.equal(evaluate('songs[1].op1'),true,'adding to a set adds to the songbook');
    assert.equal(evaluate('setSongs(sets[0]).map(s=>s.title).join(",")'),'One,Three,Two');
    const b = evaluate('selectedBook()'); assert.equal(b.sets,1);
    const text = new TextDecoder().decode(b.text);
    assert.match(text, /\n@FRIDAY GIG\|0 2 1\n$/);
    assert.equal(text.trim().split('\n').filter(l=>l.split(';').length===6).length,3,'songs keep 6 fields');
  });
  check('set lists: leaving the songbook leaves the set; 16 sets at most; names cleaned for the OP-1',()=>{
    evaluate('setSongIncluded(2,false)');
    assert.equal(evaluate('setSongs(sets[0]).map(s=>s.title).join(",")'),'One,Two');
    for (let i = 0; i < 20; i++) evaluate("newSet('Set')");
    assert.equal(evaluate('sets.length'),16);
    assert.equal(evaluate("OPD.cleanSetName('Sunday | brunch; (late)', 0)"),'SUNDAY BRUNCH LATE');
    assert.equal(evaluate("OPD.cleanSetName('', 3)"),'SET 4');
  });
  check('set lists from a backup or an iReal playlist map onto library songs',()=>{
    evaluate("songs=[{title:'One',tempo:120,chart:'C^7 | F7',op1:false}];sets=[]");
    const made = evaluate("importSets([{name:'Gig',songs:[1,0]}],[{title:'One',chart:'C^7  | F7'},{title:'Missing',chart:'C^7'}])");
    assert.deepEqual(Array.from(made),['Gig']); assert.equal(evaluate('songs[0].op1'),true);
    assert.equal(evaluate('setSongs(sets[0]).length'),1);
  });
  return checks;
};
if(require.main===module) console.log(`${module.exports()} workspace checks passed`);
