import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
const file=path.resolve(process.argv[2]||'');
assert(file.endsWith('.csv'),'Pass parallel-recording-metrics.csv');
const lines=fs.readFileSync(file,'utf8').trim().split(/\r?\n/);
assert.equal(lines.shift(),'repeat,mode,prepare_us,encode_us,total_us');
const rows=lines.map(line=>line.split(',').map(Number));
assert.equal(rows.length,36);
const stats=values=>{values=[...values].sort((a,b)=>a-b);return {p50:values[Math.floor(values.length*.5)],p95:values[Math.floor(values.length*.95)],min:values[0],max:values.at(-1),mean:values.reduce((a,b)=>a+b,0)/values.length};};
const groups=[0,1,2].map(mode=>{
 const samples=rows.filter(row=>row[1]===mode);
 assert.deepEqual(samples.map(row=>row[0]),Array.from({length:12},(_,i)=>i));
 for(const row of samples){assert(row.every(Number.isFinite));assert(row[2]>=0&&row[3]>=0);assert(Math.abs(row[2]+row[3]-row[4])<.15);}
 const warmed=samples.filter(row=>row[0]>=2);
 return {mode:['legacy','serial','parallel'][mode],samples:warmed.length,prepareUs:stats(warmed.map(r=>r[2])),encodeUs:stats(warmed.map(r=>r[3])),totalUs:stats(warmed.map(r=>r[4]))};
});
const result={source:file,connected:path.basename(file).includes('connected'),drawsPerFrame:768,passes:4,activeMaterialUniforms:27,warmupFrames:2,groups};
fs.writeFileSync(file.replace(/\.csv$/,'-analysis.json'),JSON.stringify(result,null,2)+'\n');
console.log(JSON.stringify(result,null,2));
