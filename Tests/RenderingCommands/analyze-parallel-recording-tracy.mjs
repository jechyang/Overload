// tracy-csvexport -u -f Record capture.tracy > events.csv
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
const file=path.resolve(process.argv[2]||'');
assert(file.endsWith('.csv'),'Pass exported Tracy CPU events');
const names=new Set(['Stage4 Legacy Recording','Stage4 Serial Recording','Stage4 Parallel Recording','Record Pass Commands','Record Pass Command Block','Wait Pass Recording','Merge Pass Recording']);
const events=fs.readFileSync(file,'utf8').trim().split(/\r?\n/).slice(1).flatMap(line=>{
 const cells=line.split(',');if(!names.has(cells[0]))return [];
 assert.equal(cells.length,6);
 const start=Number(cells[3]),duration=Number(cells[4]);assert(Number.isSafeInteger(start)&&Number.isSafeInteger(duration));
 return [{name:cells[0],source:cells[1],start,end:start+duration,thread:cells[5]}];
});
const cases=events.filter(e=>e.name.startsWith('Stage4 ')).sort((a,b)=>a.start-b.start);
assert.equal(cases.length,36,'Incomplete recording capture');
const details=cases.map(test=>{
 assert(test.source.endsWith('parallel_recording_validation.cpp'),'Unexpected recording source');
 const inside=name=>events.filter(e=>e.name===name&&e.start>=test.start&&e.end<=test.end);
 const blocks=inside('Record Pass Command Block');
 const encoding=inside('Record Pass Commands');assert.equal(encoding.length,1);
 const parallel=test.name.includes('Parallel'),legacy=test.name.includes('Legacy');
 assert.equal(blocks.length,legacy?0:parallel?5:1);
 if(!parallel)assert(blocks.every(b=>b.thread===test.thread));
 else assert(blocks.filter(b=>b.thread===test.thread).length===1&&new Set(blocks.map(b=>b.thread)).size>=2);
 for(const block of blocks){assert(block.source.endsWith('RenderCommandList.cpp'));assert(block.end<=encoding[0].end,'Output escaped finalization boundary');}
 const points=blocks.filter(b=>b.end>b.start).flatMap(b=>[[b.start,1],[b.end,-1]]).sort((a,b)=>a[0]-b[0]||a[1]-b[1]);
 let active=0,peak=0;for(const [,change]of points){active+=change;peak=Math.max(peak,active);}assert.equal(active,0);
 const duration=items=>items.reduce((sum,e)=>sum+e.end-e.start,0)/1000;
 return {mode:legacy?'legacy':parallel?'parallel':'serial',totalUs:(test.end-test.start)/1000,encodeUs:duration(encoding),waitUs:duration(inside('Wait Pass Recording')),mergeUs:duration(inside('Merge Pass Recording')),blocks:blocks.length,threads:[...new Set(blocks.map(b=>b.thread))],peak};
});
const stats=values=>{values=[...values].sort((a,b)=>a-b);return {p50:values[Math.floor(values.length*.5)],min:values[0],max:values.at(-1)};};
const summary=['legacy','serial','parallel'].map(mode=>{const rows=details.filter(d=>d.mode===mode);assert.equal(rows.length,12);return {mode,cases:rows.length,totalUs:stats(rows.map(r=>r.totalUs)),encodeUs:stats(rows.map(r=>r.encodeUs)),waitUs:stats(rows.map(r=>r.waitUs)),mergeUs:stats(rows.map(r=>r.mergeUs)),overlappingCases:rows.filter(r=>r.peak>=2).length,maxConcurrent:Math.max(...rows.map(r=>r.peak))};});
assert(summary[2].overlappingCases>=10,'Insufficient actual command-encoding overlap');
const output=file.replace(/\.csv$/,'-analysis.json');fs.writeFileSync(output,JSON.stringify({source:file,summary,cases:details},null,2)+'\n');
console.log(JSON.stringify(summary,null,2));console.log('PASS: complete serial/parallel cases, worker overlap and encoding-before-publication; '+output);
