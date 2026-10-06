// Adapt the existing local PerformanceLab package to the mainline dependencies.
// The package's scene assets and measurement source are read without modification.
import fs from 'node:fs';
import path from 'node:path';
import cp from 'node:child_process';
import {fileURLToPath} from 'node:url';

const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
const example=path.join(root,'Examples/PerformanceLab');
const build=path.join(root,'Build/PerformanceInvestigation');
const source=path.join(example,'run.mjs');
if(!fs.existsSync(source)||!fs.existsSync(path.join(example,'measure.cpp')))
  throw Error('Requires the local Examples/PerformanceLab package (run.mjs, measure.cpp and Project/Assets).');
let script=fs.readFileSync(source,'utf8');
const location="const dir=path.dirname(fileURLToPath(import.meta.url)),root=path.resolve(dir,'../..');";
if(script.split(location).length!==2)throw Error('PerformanceLab location adapter needs updating.');
script=script.replace(location,'const dir='+JSON.stringify(example)+',root='+JSON.stringify(root)+';');
const dependencies="const libs=['assimp','baregl','bullet3','freetype','glfw','ImGui','lua','OvAudio','OvCore','OvDebug','OvMaths','OvPhysics','OvRendering','OvTools','OvUI','OvWindowing','soloud','tinyxml2','tracy'];";
if(!script.includes("'OvReflection'")) {
  if(script.split(dependencies).length!==2)throw Error('PerformanceLab dependency adapter needs updating.');
  script=script.replace(dependencies,dependencies.replace("'OvPhysics','OvRendering'","'OvPhysics','OvReflection','OvRendering'"));
}
// Both compiler scratch and generated adapter stay in the workspace.
fs.mkdirSync(build,{recursive:true});const temp=path.join(build,'Temp');fs.mkdirSync(temp,{recursive:true});
const adapter=path.join(build,'performance-lab-mainline.mjs');fs.writeFileSync(adapter,script);
const result=cp.spawnSync(process.execPath,[adapter,...process.argv.slice(2)],{
  cwd:root,windowsHide:true,stdio:'inherit',env:{...process.env,TEMP:temp,TMP:temp}
});
if(result.error)throw result.error;
process.exitCode=result.status??1;
