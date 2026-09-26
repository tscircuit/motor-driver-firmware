const assert=require('node:assert/strict'),fs=require('node:fs'),codec=require('../dist/song.js');
const demo=codec.decode(fs.readFileSync(__dirname+'/../dist/example-song.bin'));
assert.equal(demo.notes,7);assert.equal(demo.duration,1060);
for(const bytes of [[],[1],[0,0,0,0],[99,0,100,0],new Array(196).fill(0)])assert.throws(()=>codec.decode(Uint8Array.from(bytes)));
assert.equal(codec.decode(Uint8Array.from([0,0,100,0])).duration,100);
console.log('PASS tune byte validation and bundled demo');

for(const name of ['two-tone-song.bin','little-melody.bin']){const song=codec.decode(fs.readFileSync(__dirname+'/../dist/'+name));assert.ok(song.notes>0);assert.ok(song.duration<60000);}
