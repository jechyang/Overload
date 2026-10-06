import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';

export function validateFrameGraphReports(directory) {
  const files=[];
  for(const owner of ['main','render'])for(const variant of ['report','escaped','gpu-report']) {
    const file=path.join(directory,'framegraph-'+owner+'-'+variant+'.json');
    const data=JSON.parse(fs.readFileSync(file,'utf8'));const graph=data.snapshot,pool=data.pool;
    assert.equal(data.schema,'overload.framegraph');assert.equal(data.schemaVersion,1);assert.equal(data.poolIsLive,true);
    assert(graph.passes.length>20&&graph.resources.length>10,'export omitted nodes from the filtered UI');
    const cpu=graph.cpu;
    assert(cpu.captured&&cpu.encodingDeferred&&cpu.compile>0&&cpu.realize>0);
    assert.equal(cpu.total,['compile','realize','prepare','diagnostics','recordViews','recordOperations','seal'].reduce((sum,k)=>sum+cpu[k],0));
    graph.resources.forEach((resource,i)=>{
      assert.equal(resource.index,i);
      if(resource.firstPass!==null)assert(resource.firstPass<graph.passes.length&&resource.lastPass>=resource.firstPass);
    });
    graph.passes.forEach((pass,i)=>{
      assert.equal(pass.index,i);
      for(const access of pass.accesses) {
        assert(access.resource>=0&&access.resource<graph.resources.length);
        assert(access.mipCount>0&&access.layerCount>0);
        assert('readVersion' in access&&'writeVersion' in access);
      }
    });
    for(const dep of graph.dependencies)assert(dep.before>=0&&dep.before<dep.after&&dep.after<graph.passes.length);
    assert.equal(pool.entries.reduce((sum,e)=>sum+e.estimatedBytes,0),pool.idleBytes+pool.leasedBytes);
    assert.equal(pool.entries.length,pool.idleTargets+pool.leasedTargets);
    assert.equal(pool.categories.reduce((sum,c)=>sum+c.bytes,0),pool.idleBytes+pool.leasedBytes);
    for(const category of pool.categories) {
      const entries=pool.entries.filter(e=>e.category===category.category);
      assert.equal(entries.length,category.targets);assert.equal(entries.reduce((sum,e)=>sum+e.estimatedBytes,0),category.bytes);
    }
    if(variant==='escaped')assert.equal(graph.passes[0].name,'quote " slash \\ tab\t line\n control\x01 中文');
    if(variant==='gpu-report') {
      assert(data.gpu);assert.equal(data.gpu.scope,graph.scope);assert.equal(data.gpu.captureIndex,graph.captureIndex);
      assert.equal(data.gpu.passNanoseconds.length,graph.passes.length);
      assert.equal(data.gpu.totalNanoseconds,data.gpu.passNanoseconds.reduce((sum,ns)=>sum+ns,0));
    } else assert.equal(data.gpu,null,'unmatched GPU sample was exported as this plan');
    files.push(file);
  }
  return {schemaVersion:1,files,fullPlan:true,reconciledPool:true,exactGpuPlan:true,jsonEscaping:true};
}
