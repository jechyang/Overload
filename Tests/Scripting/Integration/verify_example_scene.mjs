// Focused regression using production Editor/SceneManager and the shipped C# example.
// Requires existing Debug native libraries and OvEditor objects from a normal native build.
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const args = process.argv.slice(2), python = args[args.indexOf('--python') + 1];
if (!args.includes('--python') || !path.isAbsolute(python)) throw Error('Supply --python <absolute interpreter>');
const parent = process.env.OV_SCRIPTING_EVIDENCE_ROOT || path.join(root, 'Build/Tests/Scripting');
fs.mkdirSync(parent, {recursive:true});
const output = fs.mkdtempSync(path.join(parent, 'example-scene-'));
const env = {...process.env, PYTHONDONTWRITEBYTECODE:'1', VSLANG:'1033'};
const checks = [];
function run(name, exe, argv, cwd = root, timeout = 300000) {
    const r = spawnSync(exe, argv, {cwd, env, windowsHide:true, encoding:'utf8', timeout, maxBuffer:64*1024*1024});
    fs.writeFileSync(path.join(output, name+'.log'), (r.stdout || '')+(r.stderr || '')+(r.error || ''));
    checks.push({name, argv:[exe,...argv], cwd, exit_code:r.status});
    if (r.error || r.status !== 0) throw Error(name+' failed; see '+path.join(output,name+'.log'));
    console.log('PASS: '+name);
}
console.log('Evidence: '+output);
try {
    run('export', python, [path.join(root,'Tools/OvReflectGen/reflection.py'),'export','--output',path.join(output,'Export')]);
    const tc = JSON.parse(fs.readFileSync(path.join(output,'Export/toolchain.json'),'utf8'));
    const compiler = path.join(tc.msvc_root,'bin/Hostx64/x64');
    env.INCLUDE = tc.system_includes.join(';');
    env.LIB = [path.join(tc.msvc_root,'lib/x64'),...['ucrt','um'].map(n=>path.join(tc.sdk_root,'Lib',tc.sdk_version,n,'x64'))].join(';');
    const pk = Object.keys(env).find(k=>k.toLowerCase()==='path'), prior = env[pk];
    delete env[pk]; env.PATH = compiler+';'+prior;
    fs.mkdirSync(path.join(output,'Data'));
    for (const name of ['Editor','Engine']) fs.symlinkSync(path.join(root,'Resources',name),path.join(output,'Data',name),'junction');
    // Isolate only OS profile lookup, as in verify_s3_apps.mjs.
    const original = fs.readFileSync(path.join(root,'Sources/OvTools/src/OvTools/Utils/SystemCalls.cpp'),'utf8');
    const start = original.indexOf('std::string OvTools::Utils::SystemCalls::GetPathToAppdata()');
    const body = original.indexOf('{',start); let end = body+1, depth = 1;
    for (;depth && end<original.length;++end) { if(original[end]==='{')++depth; else if(original[end]==='}')--depth; }
    if (start<0 || depth) throw Error('AppData isolation boundary changed');
    const profile = path.join(output,'Profile').replaceAll('\\','/');
    fs.mkdirSync(path.join(profile,'OverloadTech/OvEditor'),{recursive:true});
    const calls = path.join(output,'SystemCalls.cpp');
    fs.writeFileSync(calls,original.slice(0,body)+'{ return "'+profile+'"; }'+original.slice(end));
    const entry = JSON.parse(fs.readFileSync(path.join(output,'Export/x86_64-pc-windows-msvc/Debug/OvCore/compile_commands.json'),'utf8'))[0];
    const includes = ['/I'+path.join(root,'Build/Generated/Reflection/x86_64-pc-windows-msvc/Debug'),
        ...['Dependencies/glfw/include','Dependencies/bullet3/src','Sources/OvEditor/include'].map(n=>'/I'+path.join(root,n)),
        ...entry.arguments.filter(a=>/^\/[IDU]/.test(a) && a!=='/DOV_REFLECTION_ANALYSIS=1')];
    const objects = [];
    for (const [name,source] of [['test',path.join(root,'Tests/Scripting/Integration/native_example_scene_validation.cpp')],
        ['SceneManager',path.join(root,'Sources/OvCore/src/OvCore/SceneSystem/SceneManager.cpp')],['SystemCalls',calls],
        ['CSharpProject',path.join(root,'Sources/OvEditor/src/OvEditor/Core/CSharpProject.cpp')],
        ['CSharpScripts',path.join(root,'Sources/OvEditor/src/OvEditor/Panels/CSharpScripts.cpp')]]) {
        const obj = path.join(output,name+'.obj'); objects.push(obj);
        run(name+'-compile',path.join(compiler,'cl.exe'),['/nologo','/std:c++20','/EHsc','/MDd','/c',...includes,'/UUNICODE','/U_UNICODE',source,'/Fo'+obj]);
    }
    const names = ['OvReflection','OvAudio','OvCore','OvDebug','OvMaths','OvPhysics','OvRendering','OvTools','OvUI','OvWindowing','assimp','baregl','bullet3','freetype','glfw','ImGui','lua','soloud','tinyxml2','tracy'];
    const appDir = path.join(root,'Bin-Int/Debug/OvEditor');
    const exe = path.join(output,'example-scene.exe');
    run('link',path.join(compiler,'link.exe'),['/nologo','/OPT:REF','/OPT:ICF','/OUT:'+exe,...objects,
        ...fs.readdirSync(appDir).filter(n=>n.endsWith('.obj')&&!['Main.obj','CSharpProject.obj','CSharpScripts.obj'].includes(n)).map(n=>path.join(appDir,n)),
        ...names.map(n=>path.join(root,'Bin/Debug',n,n+'.lib')),
        ...['opengl32','dbghelp','ws2_32','winmm','imm32','version','user32','gdi32','shell32','ole32','oleaut32','advapi32','comdlg32','uuid'].map(n=>n+'.lib')]);
    const example = path.join(root,'Examples/CSharpGameplay');
    const xml = fs.readFileSync(path.join(example,'Assets/Scenes/CSharpGameplay.ovscene'),'utf8');
    if(args.includes('--s5-workflow-only')||args.includes('--with-s5')) {
        // Reuse the original S5 cases to catch regressions in configuration, cache,
        // concurrent workers, Inspector overrides, failed validation and Lua.
        const {verifyWorkflow}=await import('./verify_s5_cases.mjs');
        fs.copyFileSync(path.join(root,'Tests/Scripting/Fixtures/S3/legacy.lua'),path.join(output,'s3.lua'));
        await verifyWorkflow({root,output,env,compiler,python,project:output,
            gen:path.join(root,'Build/Generated/Reflection/x86_64-pc-windows-msvc/Debug'),
            nativeOptions:entry.arguments.filter(a=>/^\/[IDU]/.test(a)&&a!=='/DOV_REFLECTION_ANALYSIS=1'),
            libraries:names.map(n=>path.join(root,'Bin/Debug',n,n+'.lib')),
            run:(name,exe,argv,expected=0,timeout=300000,cwd=root)=>{
                if(expected!==0)throw Error('Unexpected nonzero expected exit');run(name,exe,argv,cwd,timeout);
            }});
    }
    const modes=args.includes('--s5-workflow-only')?[]:args.includes('--first-script-only')?['first-script']:['default','render-thread','first-script'];
    for (const mode of modes) {
        const project = path.join(output,mode); fs.mkdirSync(path.join(project,'Assets/Scenes'),{recursive:true});
        for (const [file,prefix] of [['CSharpGameplay',''],['WithDeclaration','<?xml version="1.0" encoding="utf-8"?>\n'],['WithComment','<!-- leading scene comment -->\n']])
            fs.writeFileSync(path.join(project,'Assets/Scenes',file+'.ovscene'),prefix+xml);
        if(mode!=='first-script')for (const file of ['MoveAndResize.cs','NuGet.Config','packages.lock.json']) fs.copyFileSync(path.join(example,file),path.join(project,file));
        const reference = path.join(root,'Sources/OvManaged/Overload.ScriptCore/Overload.ScriptCore.csproj').replaceAll('\\','/').replaceAll('&','&amp;').replaceAll('"','&quot;');
        if(mode!=='first-script')fs.writeFileSync(path.join(project,'GameScripts.csproj'),fs.readFileSync(path.join(example,'GameScripts.csproj'),'utf8').replace('../../Sources/OvManaged/Overload.ScriptCore/Overload.ScriptCore.csproj',reference));
        const settings = new Map(fs.readFileSync(path.join(example,'CSharpGameplay.ovproject'),'utf8').trim().split(/\r?\n/).map(line=>{
            const split=line.indexOf('='); return [line.slice(0,split),line.slice(split+1)];
        }));
        settings.set('csharp_enabled','false'); settings.set('render_thread',String(mode==='render-thread'));
        settings.set('csharp_engine_root',root); settings.set('csharp_node',process.execPath); settings.delete('csharp_python');
        settings.set('csharp_auto_build','true'); settings.set('csharp_hot_reload','false'); settings.set('csharp_configuration','Debug');
        // Keep generation paths below Win32 MAX_PATH even under long evidence roots.
        settings.set('csharp_build_cache',path.join(output,mode[0]));
        fs.writeFileSync(path.join(project,'CSharpGameplay.ovproject'),[...settings].map(([k,v])=>k+'='+v).join('\n')+'\n');
        run(mode,exe,mode==='first-script'?[project,'first-script']:[project],output);
        if(mode!=='first-script')run(mode+'-reopen',exe,[project,'reopen'],output);
    }
    fs.writeFileSync(path.join(output,'verification.json'),JSON.stringify({success:true,checks},null,2));
    console.log('Scene load, C# Play/Stop, resource and rendering regression passed: '+output);
} catch (error) {
    fs.writeFileSync(path.join(output,'failure.json'),JSON.stringify({error:String(error),checks},null,2));
    console.error(String(error)); process.exitCode=1;
}
