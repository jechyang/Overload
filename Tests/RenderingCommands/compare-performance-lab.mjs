// Compare completed local PerformanceLab captures without accepting pixel drift.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';

if(process.argv.length<4)throw Error('Usage: node compare-performance-lab.mjs baseline-dir candidate-dir [report.json]');
const directories=process.argv.slice(2,4).map(p=>path.resolve(p));
const read=(dir,file)=>JSON.parse(fs.readFileSync(path.join(dir,file),'utf8'));
const captures=directories.map(dir=>{
  const status=read(dir,'run-status.json');
  assert.equal(status.status,'complete');assert.equal(status.exitCode,0);
  const summary=read(dir,'summary.json');assert.equal(summary.schema,2);
  assert(summary.results.length>0);
  const ids=new Set();
  for(const scene of summary.results){
    assert(!ids.has(scene.id),'Duplicate scene '+scene.id);ids.add(scene.id);
    assert.equal(scene.samples,216);assert.equal(scene.repeats,3);
    for(const metric of Object.values(scene.metrics)){
      assert(Number.isFinite(metric.p50)&&Number.isFinite(metric.p95)&&metric.p50>=0&&metric.p95>=metric.p50);
      assert.equal(metric.repeatP50.length,3);
    }
  }
  return summary;
});
const [baseline,candidate]=captures;
assert.equal(candidate.machine,baseline.machine,'Machine/measurement settings changed');
assert.equal(candidate.method,baseline.method,'Measurement method changed');
assert.deepEqual(candidate.provenance.assetHashes,baseline.provenance.assetHashes,'Scene assets changed');
assert.deepEqual(candidate.results.map(s=>s.id).sort(),baseline.results.map(s=>s.id).sort(),'Scene set changed');
const sha=buffer=>crypto.createHash('sha256').update(buffer).digest('hex');
const scenes=candidate.results.map(after=>{
  const before=baseline.results.find(scene=>scene.id===after.id);
  assert.deepEqual(after.counters,before.counters,'Draw/update counters changed: '+after.id);
  const pixels=directories.map(dir=>fs.readFileSync(path.join(dir,after.id+'.rgba')));
  for(const image of pixels)assert.equal(image.length,960*540*4);
  assert(pixels[0].equals(pixels[1]),'Pixels differ: '+after.id);
  const metrics=Object.fromEntries(Object.keys(before.metrics).map(name=>{
    const a=before.metrics[name],b=after.metrics[name];
    return [name,{before:a,after:b,p50ChangePercent:a.p50?100*(b.p50/a.p50-1):null}];
  }));
  return {id:after.id,samplesPerCapture:216,pixelsEqual:true,rgbaSha256:sha(pixels[1]),counters:after.counters,metrics};
});
const report={schema:1,baseline:directories[0],candidate:directories[1],machine:candidate.machine,
  method:candidate.method,assetCount:Object.keys(candidate.provenance.assetHashes).length,
  inputs:directories.map(dir=>({summarySha256:sha(fs.readFileSync(path.join(dir,'summary.json'))),
    head:read(dir,'provenance.json').head,build:read(dir,'provenance.json').build})),scenes};
const output=process.argv[4]?path.resolve(process.argv[4]):path.join(directories[1],'comparison.json');
fs.writeFileSync(output,JSON.stringify(report,null,2)+'\n');
console.table(scenes.map(s=>({scene:s.id,extractBeforeMs:s.metrics.extract_us.before.p50/1000,
  extractAfterMs:s.metrics.extract_us.after.p50/1000,frontendBeforeMs:s.metrics.frontend_us.before.p50/1000,
  frontendAfterMs:s.metrics.frontend_us.after.p50/1000,frontendChangePercent:s.metrics.frontend_us.p50ChangePercent})));
console.log('PASS: '+scenes.length+' scenes; identical assets, settings, counters and exact RGBA. Report: '+output);
