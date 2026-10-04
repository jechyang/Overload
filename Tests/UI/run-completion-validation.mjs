import fs from 'node:fs';
import path from 'node:path';
import cp from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {completionCommands,completionFingerprint} from './completion-evidence.mjs';

const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
const parent=path.join(root,'Build/UIValidation');fs.mkdirSync(parent,{recursive:true});
const output=fs.mkdtempSync(path.join(parent,'completion-'));
const receipt={success:false,started:new Date().toISOString(),fingerprint:completionFingerprint(root),checks:[]};
try {
  for(const [index,args] of completionCommands.entries()) {
    const log=path.join(output,String(index).padStart(2,'0')+'.log');
    const fd=fs.openSync(log,'w');
    console.log('RUN '+args.join(' '));
    const result=cp.spawnSync(process.execPath,args,{cwd:root,windowsHide:true,stdio:['ignore',fd,fd],timeout:1200000});
    fs.closeSync(fd);receipt.checks.push({args,exit:result.status,log});
    if(result.status!==0)throw Error('Failed: '+args.join(' ')+'; '+log);
    console.log('PASS '+args.join(' '));
  }
  if(receipt.fingerprint!==completionFingerprint(root))throw Error('Sources changed during acceptance; rerun against a stable checkout.');
  receipt.success=true;
} catch(error) {receipt.error=String(error);console.error(receipt.error);process.exitCode=1;}
receipt.finished=new Date().toISOString();
fs.writeFileSync(path.join(output,'result.json'),JSON.stringify(receipt,null,2));
fs.writeFileSync(path.join(parent,'completion.json'),JSON.stringify(receipt,null,2));
console.log('Evidence: '+output);
