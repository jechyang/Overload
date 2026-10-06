// Native visual acceptance of the checked-in scene and real Lua keyboard controls.
import fs from 'node:fs';
import path from 'node:path';
import cp from 'node:child_process';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
const config=process.argv[2]||'Release';
assert(['Debug','Release'].includes(config));
const base=path.join(root,'Build/FrameGraphLabValidation');fs.mkdirSync(base,{recursive:true});
const reportFile=path.join(base,config+'-result.json');
// Mark pending before running; a failed rerun cannot leave an old passing result.
fs.writeFileSync(reportFile,JSON.stringify({passed:false,config,status:'running'},null,2));
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const files=[];
function visit(dir){for(const name of fs.readdirSync(dir)){const file=path.join(dir,name);if(fs.statSync(file).isDirectory())visit(file);else files.push(file);}}
visit(path.join(root,'Examples/FrameGraphLab/Assets'));files.push(path.join(root,'Examples/FrameGraphLab/FrameGraphLab.ovproject'));
const assetsBefore=files.map(f=>[path.relative(root,f),hash(fs.readFileSync(f))]);
const authored=JSON.parse(fs.readFileSync(path.join(root,'Examples/FrameGraphLab/manifest.json'),'utf8'));
const captures=['cold','all-effects','without-shadows','without-reflections','without-bloom','without-exposure','without-fxaa','without-environment','without-glass','without-gallery','six-faces','one-face','motion','paused','paused-stable','restored'];
const owners=[];
for(const owner of ['main','render']){
  if(!process.argv.includes('--inspect-only')){
    const result=cp.spawnSync(process.execPath,['Examples/FrameGraphLab/run.mjs','--verify',...(config==='Debug'?['--debug']:[]),...(owner==='main'?['--main']:[])],{cwd:root,windowsHide:true,stdio:'inherit',timeout:300000});
    assert.equal(result.status,0,config+' '+owner+' native verification failed');
  }
  const dir=path.join(base,config+'-'+owner);
  const log=fs.readFileSync(path.join(dir,'run.log'),'utf8');
  assert(!log.includes('[ERROR]'),'Lua/GL/product error in '+dir);
  assert(log.includes('FrameGraphLab product editor verification completed'));
  assert(log.includes('FrameGraphLab probe selection verification completed'));
  assert(log.includes('FrameGraphLab HDR environment texture verification completed'));
  const environment=JSON.parse(fs.readFileSync(path.join(dir,'environment-info.json'),'utf8'));
  assert.equal(environment.width,1024);assert.equal(environment.height,512);
  assert.equal(environment.format,'RGBA32F');assert.equal(environment.mipmapped,false);
  assert.equal(environment.estimatedBytes,authored.environment.estimatedTextureBytes);
  assert(environment.hdrMaxValue>1,'environment capture silently used an LDR texture');
  const read=name=>JSON.parse(fs.readFileSync(path.join(dir,name+'.json'),'utf8'));
  const count=(g,name)=>g.snapshot.passes.filter(p=>p.name===name).length;
  const cold=read('cold'),all=read('all-effects'),six=read('six-faces'),one=read('one-face');
  assert.equal(count(cold,'ReflectionCapture'),12,'cold capture must initialize both complete cubes');
  assert.equal(count(cold,'Shadows'),1);
  assert.equal(count(cold,'Exposure.InitializeHistory'),1);
  assert.equal(count(six,'ReflectionCapture'),6);
  assert.equal(count(one,'ReflectionCapture'),1);
  assert.equal(count(read('recapture'),'ReflectionCapture'),12,'R did not fully recapture both probes');
  for(const name of ['MainView','Tonemapping','FXAA','SceneUI','ImGui','Window.Present'])assert.equal(count(all,name),1);
  assert.equal(count(all,'Bloom.Downsample'),5);
  assert.equal(count(all,'Bloom.UpsampleLoadBlend'),5);
  const progressive=Array.from({length:12},(_,i)=>read('progressive-'+i));
  assert(progressive.every(g=>count(g,'ReflectionCapture')===1),'Once probe continued capturing or progressive speed changed');
  assert(progressive.some(g=>count(g,'Reflection.Mipmaps')===1)&&progressive.some(g=>count(g,'Reflection.Mipmaps')===0),'cube mipmaps did not alternate with face completion');
  assert(all.snapshot.resources.some(r=>r.origin==='History'),'warm graph omitted imported histories');
  const toggles={shadows:p=>p==='Shadows',reflections:p=>p==='ReflectionCapture'||p==='Reflection.Mipmaps',bloom:p=>p.startsWith('Bloom.'),exposure:p=>p.startsWith('Exposure.'),fxaa:p=>p==='FXAA'};
  for(const [name,matches] of Object.entries(toggles))assert(read('without-'+name).snapshot.passes.every(p=>!matches(p.name)),name+' passes remained after keyboard toggle');
  // All exported plans must retain valid content-version and dependency references.
  for(const name of [...captures,...progressive.map((_,i)=>'progressive-'+i),'recapture','resized']){
    const data=read(name),g=data.snapshot;
    assert.equal(data.schema,'overload.framegraph');assert.equal(data.schemaVersion,1);
    assert(g&&g.cpu.captured&&g.cpu.total>0);
    g.passes.forEach((p,i)=>{assert.equal(p.index,i);for(const a of p.accesses){assert(a.resource>=0&&a.resource<g.resources.length);assert('readVersion'in a&&'writeVersion'in a);}});
    for(const d of g.dependencies)assert(d.before>=0&&d.before<d.after&&d.after<g.passes.length);
    assert.equal(data.pool.entries.reduce((n,e)=>n+e.estimatedBytes,0),data.pool.idleBytes+data.pool.leasedBytes);
  }
  const images=captures.map(name=>{
    const pixels=fs.readFileSync(path.join(dir,name+'.rgba'));
    assert.equal(pixels.length,960*540*4);
    let changedPixels=0;const reference=fs.readFileSync(path.join(dir,'all-effects.rgba'));
    for(let i=0;i<pixels.length;i+=4)if(pixels[i]!==reference[i]||pixels[i+1]!==reference[i+1]||pixels[i+2]!==reference[i+2])++changedPixels;
    if(name.startsWith('without-')||name==='motion')assert(changedPixels>0,name+' did not change the scene image');
    return {name,sha256:hash(pixels),changedPixels};
  });
  const imageHash=name=>images.find(i=>i.name===name).sha256;
  assert.equal(imageHash('paused'),imageHash('paused-stable'),'paused scene continued changing');
  assert.equal(imageHash('paused'),imageHash('restored'),'restored resolution changed the paused image');
  const resized=read('resized');
  assert(resized.snapshot.resources.some(r=>r.origin==='Transient'&&r.width===640&&r.height===360));
  assert(resized.pool.entries.some(e=>e.category==='idleUnused'&&e.width===960&&e.height===540),'old-size scratch was not classified as unused');
  owners.push({owner,coldPasses:cold.snapshot.passes.length,coldResources:cold.snapshot.resources.length,
    warmPasses:progressive.map(g=>g.snapshot.passes.length),warmResources:progressive.map(g=>g.snapshot.resources.length),sixFacePasses:six.snapshot.passes.length,
    keyboardControls:true,probeSelectionFallback:true,environment,history:true,versionReferences:true,poolReconciled:true,skeletalPlaybackAndPause:true,resizeAndRestore:true,images});
}
assert.deepEqual(files.map(f=>[path.relative(root,f),hash(fs.readFileSync(f))]),assetsBefore,'verification changed authored assets/project');
assert.deepEqual(owners[0].images.map(i=>[i.name,i.sha256]),owners[1].images.map(i=>[i.name,i.sha256]),'Main/Render image mismatch');
const result={passed:true,config,actors:authored.actors.length,materials:authored.materials.length,environmentTexture:authored.environmentTexture,
  environmentSource:authored.environmentSource,environmentTextureBytes:authored.environment.estimatedTextureBytes,
  gameResolution:[960,540],editorResolution:[1600,900],nativeProductEditor:true,assetsUnchanged:true,exactMainRenderImages:true,owners};
fs.writeFileSync(reportFile,JSON.stringify(result,null,2)+'\n');console.log('PASS '+reportFile);
