// Diagnostic only: live readback may perturb pixels on affected GPU/driver combinations.
// Run Debug/Release --frontend-immediate first; this never changes the formal results.
import fs from 'node:fs';
import path from 'node:path';
import cp from 'node:child_process';
import { fileURLToPath } from 'node:url';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const output = path.join(root, 'Build/JobsValidation/A1FirstFrame');
fs.mkdirSync(output, {recursive:true});
const reports = [];
for (const config of ['Debug', 'Release']) {
  const formal = path.join(root, 'Build/JobsValidation/Skinning', config + '-frontend-immediate-command-list/current');
  for (let mode = 0; mode < 3; ++mode) {
    const dir = path.join(output, config + '-live-' + mode); fs.mkdirSync(dir, {recursive:true});
    const log = path.join(dir,'run.log');const fd = fs.openSync(log,'w');
    const result = cp.spawnSync(path.join(formal,'skinning-validation.exe'),[dir,String(mode),'live'],{
      cwd:path.join(root,'Build',config),windowsHide:true,stdio:['ignore',fd,fd],timeout:120000});
    fs.closeSync(fd);if(result.status !== 0)throw Error('Diagnostic capture failed: '+log);
    const live = fs.readFileSync(path.join(dir,'frontend-'+mode+'.rgba'));
    const none = fs.readFileSync(path.join(formal,'cold-'+mode+'-none','frontend-'+mode+'.rgba'));
    const staged = fs.readFileSync(path.join(formal,'cold-'+mode+'-staged','frontend-'+mode+'.rgba'));
    const frameDiff = [];
    for(let frame=0;frame<8;++frame){let different=0;
      for(let i=frame*24576;i<(frame+1)*24576;i+=4)if(!live.subarray(i,i+4).equals(none.subarray(i,i+4)))++different;
      frameDiff.push(different);
    }
    const trace = fs.readFileSync(path.join(dir,'frontend-'+mode+'.draws'));
    const stableTrace = fs.readFileSync(path.join(formal,'cold-'+mode+'-staged','frontend-'+mode+'.draws'));
    const report={config,mode,frameDiff,noneEqualsStaged:none.equals(staged),liveTraceEqualsStaged:trace.equals(stableTrace),log};
    reports.push(report);console.log(JSON.stringify(report));
  }
}
fs.writeFileSync(path.join(output,'diagnostic.json'),JSON.stringify(reports,null,2));
