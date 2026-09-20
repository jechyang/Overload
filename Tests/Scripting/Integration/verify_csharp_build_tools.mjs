import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {resolvePython} from '../../../Scripts/Windows/CSharpBuildTools.mjs';

const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../..');
const args=process.argv.slice(2), python=args[args.indexOf('--python')+1];
assert(args.includes('--python')&&path.isAbsolute(python),'Supply an absolute Python 3.10+ path');
const parent=process.env.OV_SCRIPTING_EVIDENCE_ROOT||path.join(root,'Build/Tests/Scripting');
fs.mkdirSync(parent,{recursive:true});
const output=fs.mkdtempSync(path.join(parent,'build-tools-'));
const checks=[];
function check(name,body){body();checks.push(name);console.log('PASS: '+name);}
for(const volume of ['H:/','C:/']){const s=fs.statfsSync(volume);console.log(volume+' free GiB: '+(s.bavail*s.bsize/2**30).toFixed(2));}
check('explicit Python 3',()=>assert.equal(path.resolve(resolvePython(root,python)),path.resolve(python)));
check('missing explicit interpreter gives actionable error',()=>assert.throws(()=>resolvePython(root,path.join(output,'missing.exe')),/Set csharp_python/));
check('relative explicit interpreter rejected',()=>assert.throws(()=>resolvePython(root,'python.exe'),/absolute/));
if(fs.existsSync('C:/Python27/python.exe'))check('actual Python 2 rejected before reflection build',()=>assert.throws(()=>resolvePython(root,'C:/Python27/python.exe'),/Python 2\.7.*requires 3\.10/));
const project=path.join(output,'Tools/OvReflectGen/Projects/OvReflectionGen.vcxproj');
fs.mkdirSync(path.dirname(project),{recursive:true});
fs.writeFileSync(project,'<Project><NMakeBuildCommandLine>&quot;'+python.replaceAll('&','&amp;')+'&quot; &quot;X:/reflection.py&quot; ensure</NMakeBuildCommandLine></Project>');
const env={...process.env};for(const key of Object.keys(env))if(key.toLowerCase()==='path'||key==='OV_REFLECTION_PYTHON')delete env[key];
env.PATH='C:/Python27';
check('native build interpreter discovered despite Python 2 PATH',()=>assert.equal(path.resolve(resolvePython(output,undefined,env)),path.resolve(python)));
fs.writeFileSync(path.join(output,'verification.json'),JSON.stringify({success:true,checks},null,2));
console.log('Evidence: '+output);
