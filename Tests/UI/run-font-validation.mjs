import fs from 'node:fs';
import path from 'node:path';
import cp from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {makeTestFont} from './fixtures/make-test-font.mjs';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
const config=process.argv[2]||'Debug';
if(!['Debug','Release'].includes(config))throw Error('Expected Debug or Release');
for(const option of process.argv.slice(3))if(option!=='--integration')throw Error('Unknown font validation option: '+option);
const parent=path.join(root,'Build/UIValidation');fs.mkdirSync(parent,{recursive:true});
const output=fs.mkdtempSync(path.join(parent,config+'-font-'));
const checks=[];
const env={...process.env,VSLANG:'1033'};
const revision=cp.spawnSync('git',['-c',`safe.directory=${root.replaceAll('\\','/')}`,'rev-parse','HEAD'],{cwd:root,encoding:'utf8',windowsHide:true});
const evidence={commit:revision.stdout?.trim(),config,command:process.argv,node:process.version,scope:'U6 native SDF + managed layout; no production GPU',renderModes:[],checks};
function run(name,exe,args){
    const log=path.join(output,name+'.log'), fd=fs.openSync(log,'w');
    const result=cp.spawnSync(exe,args,{cwd:root,env,windowsHide:true,stdio:['ignore',fd,fd],timeout:180000});
    fs.closeSync(fd);checks.push({name,exe,args,exit:result.status,error:String(result.error||'')});
    if(result.status!==0)throw Error(name+' failed: '+log);
    console.log('PASS '+name);
}
try{
    const generated=path.join(root,'Build/Generated/Reflection/x86_64-pc-windows-msvc',config);
    const args=JSON.parse(fs.readFileSync(path.join(generated,'OvCore/compile_commands.json'),'utf8'))[0].arguments;
    const system=args.filter(x=>x.startsWith('/clang:C:/')).map(x=>x.slice(7));
    const msvc=path.dirname(system.find(x=>x.includes('/MSVC/')&&x.endsWith('/include')));
    const sdkInclude=system.find(x=>x.endsWith('/ucrt'));
    const sdkVersion=path.basename(path.dirname(sdkInclude)),sdk=path.resolve(sdkInclude,'../../..');
    const bin=path.join(msvc,'bin/Hostx64/x64');
    env.LIB=[path.join(msvc,'lib/x64'),...['ucrt','um'].map(x=>path.join(sdk,'Lib',sdkVersion,x,'x64'))].join(';');
    evidence.compiler=path.join(bin,'cl.exe');
    const includes=[...system.map(x=>'/I'+x),'/I'+path.join(root,'Sources/OvCore/include'),'/I'+path.join(root,'Dependencies/freetype/include')];
    const sources=['Sources/OvCore/src/OvCore/UI/Fonts/Font.cpp','Sources/OvCore/src/OvCore/ResourceManagement/FontManager.cpp','Tests/UI/font_backend_validation.cpp'];
    const objects=[];
    for(const source of sources){
        const object=path.join(output,path.basename(source)+'.obj');objects.push(object);
        run('compile-'+path.basename(source),path.join(bin,'cl.exe'),['/nologo','/std:c++20','/EHsc','/W4','/WX',config==='Debug'?'/MDd':'/MD','/c',...includes,path.join(root,source),'/Fo'+object]);
    }
    const engineIncludes=[...args.filter(x=>(x.startsWith('/I')||x.startsWith('/D'))&&!x.includes('OV_REFLECTION_ANALYSIS')),
        ...system.map(x=>'/I'+x),'/I'+root,'/I'+generated,'/I'+path.join(root,'Dependencies/freetype/include'),
        ...['glfw/include','bullet3/src'].map(x=>'/I'+path.join(root,'Dependencies',x))];
    run('compile-FontBridge.cpp',path.join(bin,'cl.exe'),['/nologo','/std:c++20','/EHsc',config==='Debug'?'/MDd':'/MD','/c',...engineIncludes,
        path.join(root,'Sources/OvCore/src/OvCore/Scripting/CSharp/FontBridge.cpp'),'/Fo'+path.join(output,'FontBridge.obj')]);
    const exe=path.join(output,'font-backend.exe');
    run('link',path.join(bin,'link.exe'),['/nologo','/OUT:'+exe,...objects,path.join(root,'Bin',config,'freetype/freetype.lib')]);
    makeTestFont(path.join(output,'outline-fixture.ttf'));
    run('native-font',exe,[path.join(root,'Resources/Editor/Fonts/Roboto-Regular.ttf'),output]);
    const lock=JSON.parse(fs.readFileSync(path.join(root,'Tools/OvCSharp/toolchain.lock.json'),'utf8'));
    const runtime=path.join(root,'Build/Toolchains/dotnet-'+lock.sdk_version),managed=path.join(output,'managed');
    evidence.runtime={sdk:lock.sdk_version,version:lock.runtime_version,path:runtime};
    fs.mkdirSync(managed);
    run('scriptcore-build',process.execPath,[path.join(root,'Scripts/Windows/BuildCSharp.mjs'),'--compat',
        path.join(root,'Sources/OvManaged/Overload.ManagedHost/Overload.ManagedHost.csproj'),'--rebuild',
        '--configuration',config,'--artifacts-path',path.join(output,'artifacts'),'--output',managed]);
    const references=path.join(runtime,'packs/Microsoft.NETCore.App.Ref',lock.runtime_version,'ref',lock.target_framework);
    const fixture=path.join(managed,'TextLayoutContracts.dll'),response=path.join(output,'text.rsp');
    fs.writeFileSync(response,['/nologo','/nostdlib+','/langversion:14.0','/nullable:enable','/warnaserror+','/deterministic+','/platform:x64','/target:exe',
        '/out:'+JSON.stringify(fixture),'/reference:'+JSON.stringify(path.join(managed,'Overload.ScriptCore.dll')),
        ...fs.readdirSync(references).filter(p=>p.endsWith('.dll')).map(p=>'/reference:'+JSON.stringify(path.join(references,p))),
        JSON.stringify(path.join(root,'Tests/UI/fixtures/TextLayoutContracts.cs'))].join('\n'));
    fs.writeFileSync(path.join(managed,'TextLayoutContracts.runtimeconfig.json'),JSON.stringify({runtimeOptions:{tfm:lock.target_framework,framework:{name:'Microsoft.NETCore.App',version:lock.runtime_version}}}));
    run('text-compile',path.join(runtime,'dotnet.exe'),[path.join(runtime,'sdk',lock.sdk_version,'Roslyn/bincore/csc.dll'),'@'+response]);
    run('text-layout',path.join(runtime,'dotnet.exe'),[fixture]);
    if(process.argv.includes('--integration')){
        evidence.scope='U6 production integration';
        evidence.renderModes=['default','render-thread'];
        const project=path.join(output,'project');fs.mkdirSync(project);
        fs.copyFileSync(path.join(output,'outline-fixture.ttf'),path.join(project,'outline-fixture.ttf'));
        fs.copyFileSync(path.join(root,'Resources/Editor/Fonts/Roboto-Regular.ttf'),path.join(project,'Roboto-Regular.ttf'));
        fs.mkdirSync(path.join(project,'Fonts'));
        fs.copyFileSync(path.join(root,'Resources/Editor/Fonts/Roboto-Regular.ttf'),path.join(project,'Fonts/Roboto-Regular.ttf'));
        const assembly=path.join(project,'FontFixture.dll'),projectResponse=path.join(output,'pipeline.rsp');
        fs.writeFileSync(projectResponse,fs.readFileSync(response,'utf8').split('\n').filter(l=>!l.startsWith('/out:')&&!l.startsWith('/target:')&&!l.includes('TextLayoutContracts.cs')).concat([
            '/target:library','/out:'+JSON.stringify(assembly),JSON.stringify(path.join(root,'Tests/UI/fixtures/TextPipelineContracts.cs'))]).join('\n'));
        run('pipeline-fixture',path.join(runtime,'dotnet.exe'),[path.join(runtime,'sdk',lock.sdk_version,'Roslyn/bincore/csc.dll'),'@'+projectResponse]);
        const object=path.join(output,'text-validation.obj'),executable=path.join(output,'text-validation.exe');
        run('pipeline-compile',path.join(bin,'cl.exe'),['/nologo','/std:c++20','/EHsc',config==='Debug'?'/MDd':'/MD','/c',...engineIncludes,
            path.join(root,'Tests/UI/text_validation.cpp'),'/Fo'+object]);
        const libraries=['OvReflection','OvCore','OvRendering','OvUI','OvTools','OvMaths','OvAudio','OvDebug','OvPhysics','OvWindowing',
            'assimp','baregl','bullet3','freetype','glfw','ImGui','lua','soloud','tinyxml2','tracy'];
        run('pipeline-link',path.join(bin,'link.exe'),['/nologo','/OPT:REF','/OPT:ICF','/OUT:'+executable,object,
            ...libraries.map(x=>path.join(root,'Bin',config,x,x+'.lib')),
            ...['opengl32','dbghelp','ws2_32','winmm','imm32','version','user32','gdi32','shell32','ole32','oleaut32','advapi32','comdlg32','uuid'].map(x=>x+'.lib')]);
        for(const mode of ['default','render-thread']) run('text-'+mode,executable,[runtime,managed,assembly,mode,output]);
        if(!fs.readFileSync(path.join(output,'text-default.rgba')).equals(fs.readFileSync(path.join(output,'text-thread.rgba'))))throw Error('Default and RenderThread text pixels differ');
        for(const mode of ['default','render-thread'])if(fs.readFileSync(path.join(output,'text-'+mode+'.log'),'utf8').includes('[ERROR]'))throw Error('Production Text emitted engine error; inspect '+mode+' log');
    }
    fs.writeFileSync(path.join(output,'result.json'),JSON.stringify({...evidence,success:true},null,2));
    console.log('Evidence: '+output);
}catch(error){
    fs.writeFileSync(path.join(output,'result.json'),JSON.stringify({...evidence,success:false,error:String(error)},null,2));
    console.error(String(error));process.exitCode=1;
}
