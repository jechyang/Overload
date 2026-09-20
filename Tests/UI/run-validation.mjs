// Build native Debug/Release libraries first. This runner uses the generated
// compiler include contract and writes fresh evidence for each invocation.
import fs from 'node:fs';
import path from 'node:path';
import cp from 'node:child_process';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
const config=process.argv[2]||'Debug';
if(!['Debug','Release'].includes(config))throw Error('Expected Debug or Release');
const parent=path.join(root,'Build/UIValidation'); fs.mkdirSync(parent,{recursive:true});
const output=fs.mkdtempSync(path.join(parent,config+'-'));
// Keep compiler temporaries and collectible CLR copies beside this run on the workspace drive.
const temporary=path.join(output,'temp'); fs.mkdirSync(temporary,{recursive:true});
process.env.TEMP=temporary; process.env.TMP=temporary;
process.env.OV_SCRIPTING_SHADOW_ROOT=path.join(output,'managed-shadow');
const checks=[];
const env={...process.env,VSLANG:'1033'};
const stages=Array.from({length:14},(_,i)=>({id:'U'+i,flag:'--u'+i,requested:process.argv.includes('--u'+i),status:'not-requested'}));
if(process.argv.includes('--matrix-only')) stages[0].requested=true;
const revision=cp.spawnSync('git',['-c',`safe.directory=${root.replaceAll('\\','/')}`,'rev-parse','HEAD'],{cwd:root,encoding:'utf8',windowsHide:true});
const evidence={config,commit:revision.status===0?revision.stdout.trim():null,command:[process.execPath,...process.argv.slice(1)],node:process.version,
    scope:process.argv.includes('--matrix-only')?'matrix-only':'runtime',renderModes:[],stages,checks};
function run(name,exe,argv) {
    const log=path.join(output,name+'.log'),fd=fs.openSync(log,'w');
    const r=cp.spawnSync(exe,argv,{cwd:root,env,windowsHide:true,stdio:['ignore',fd,fd],timeout:120000});
    fs.closeSync(fd); checks.push({name,exe,argv,exit:r.status,error:String(r.error||'')});
    if(r.status!==0)throw Error(name+' failed: '+log);
    console.log('PASS '+name);
}
try {
    const allowed=new Set(['--managed','--scripting-regression','--matrix-only','--foundations',...stages.map(s=>s.flag)]);
    for(const option of process.argv.slice(3)) if(!allowed.has(option)&&!option.startsWith('--core-library=')) throw Error('Unknown UI validation option: '+option);
    const coreOptions=process.argv.filter(option=>option.startsWith('--core-library='));
    if(coreOptions.length>1) throw Error('--core-library may only be specified once');
    const coreLibrary=coreOptions[0]?.slice('--core-library='.length);
    if(coreLibrary!==undefined&&(!path.isAbsolute(coreLibrary)||path.extname(coreLibrary).toLowerCase()!=='.lib'||!fs.existsSync(coreLibrary)||!fs.statSync(coreLibrary).isFile())) throw Error('--core-library requires an existing absolute .lib path');
    if(coreLibrary) evidence.libraryOverrides={OvCore:coreLibrary};
    if(process.argv.includes('--scripting-regression')&&!process.argv.includes('--managed')) throw Error('--scripting-regression requires --managed');
    if(process.argv.includes('--foundations')&&!process.argv.includes('--managed')) throw Error('--foundations requires --managed');
    if(process.argv.includes('--matrix-only')&&process.argv.some(option=>['--managed','--scripting-regression','--foundations'].includes(option)||option.startsWith('--core-library=')))
        throw Error('--matrix-only cannot be combined with runtime validation options');
    for(const stage of stages.filter(s=>s.requested)) {
        stage.status='running';
        if(stage.id!=='U0') { stage.status='unavailable'; throw Error(`${stage.flag} is reserved; ${stage.id} acceptance is not implemented yet`); }
        run('U0-reference',process.execPath,[path.join(root,'Tools/UI/reference-ugui.mjs'),'--check']);
        run('U0-matrix',process.execPath,[path.join(root,'Tests/UI/runtime-matrix.test.mjs')]);
        stage.status='passed';
    }
    if(process.argv.includes('--matrix-only')) {
        fs.writeFileSync(path.join(output,'result.json'),JSON.stringify({...evidence,success:true},null,2));
        console.log('Evidence: '+output); process.exit(0);
    }
    const generated=path.join(root,'Build/Generated/Reflection/x86_64-pc-windows-msvc',config);
    const args=JSON.parse(fs.readFileSync(path.join(generated,'OvCore/compile_commands.json'),'utf8'))[0].arguments;
    const system=args.filter(x=>x.startsWith('/clang:C:/')).map(x=>x.slice(7));
    const msvc=path.dirname(system.find(x=>x.includes('/MSVC/')&&x.endsWith('/include')));
    const sdkInclude=system.find(x=>x.endsWith('/ucrt'));
    const sdkVersion=path.basename(path.dirname(sdkInclude));
    const sdk=path.resolve(sdkInclude,'../../..');
    const bin=path.join(msvc,'bin/Hostx64/x64');
    env.LIB=[path.join(msvc,'lib/x64'),...['ucrt','um'].map(x=>path.join(sdk,'Lib',sdkVersion,x,'x64'))].join(';');
    evidence.compiler=path.join(bin,'cl.exe');
    evidence.renderModes=['default','render-thread'];
    const includes=[...args.filter(x=>(x.startsWith('/I')||x.startsWith('/D'))&&!x.includes('OV_REFLECTION_ANALYSIS')),
        ...system.map(x=>'/I'+x),'/I'+root,'/I'+generated,
        ...['glfw/include','bullet3/src'].map(x=>'/I'+path.join(root,'Dependencies',x))];
    const object=path.join(output,'test.obj'),exe=path.join(output,'native-ui.exe');
    run('compile',path.join(bin,'cl.exe'),['/nologo','/std:c++20','/EHsc',config==='Debug'?'/MDd':'/MD','/c',...includes,
        path.join(root,'Tests/UI/native_ui_validation.cpp'),'/Fo'+object]);
    const libraries=['OvReflection','OvCore','OvRendering','OvUI','OvTools','OvMaths','OvAudio','OvDebug','OvPhysics','OvWindowing',
        'assimp','baregl','bullet3','freetype','glfw','ImGui','lua','soloud','tinyxml2','tracy'];
    const libraryPaths=libraries.map(x=>x==='OvCore'&&coreLibrary?coreLibrary:path.join(root,'Bin',config,x,x+'.lib'));
    run('link',path.join(bin,'link.exe'),['/nologo','/OPT:REF','/OPT:ICF','/OUT:'+exe,object,
        ...libraryPaths,
        ...['opengl32','dbghelp','ws2_32','winmm','imm32','version','user32','gdi32','shell32','ole32','oleaut32','advapi32','comdlg32','uuid'].map(x=>x+'.lib')]);
    run('native-ui',exe,[output]);
    if (process.argv.includes('--managed')) {
        const lock=JSON.parse(fs.readFileSync(path.join(root,'Tools/OvCSharp/toolchain.lock.json'),'utf8'));
        const runtime=path.join(root,'Build/Toolchains/dotnet-'+lock.sdk_version);
        evidence.runtime={sdk:lock.sdk_version,version:lock.runtime_version,path:runtime};
        const managed=path.join(output,'managed'), project=path.join(output,'project');
        fs.mkdirSync(project,{recursive:true});
        run('managed-build',process.execPath,[path.join(root,'Scripts/Windows/BuildCSharp.mjs'),'--compat',
            path.join(root,'Sources/OvManaged/Overload.ManagedHost/Overload.ManagedHost.csproj'),'--rebuild',
            '--artifacts-path',path.join(output,'artifacts'),'--output',managed]);
        const references=path.join(runtime,'packs/Microsoft.NETCore.App.Ref',lock.runtime_version,'ref',lock.target_framework);
        const fixture=path.join(project,'ManagedUI.dll'), response=path.join(output,'fixture.rsp');
        fs.writeFileSync(response,[
            '/nologo','/nostdlib+','/langversion:14.0','/nullable:enable','/warnaserror+','/deterministic+','/platform:x64','/target:library',
            '/out:'+JSON.stringify(fixture),'/reference:'+JSON.stringify(path.join(managed,'Overload.ScriptCore.dll')),
            ...fs.readdirSync(references).filter(p=>p.endsWith('.dll')).map(p=>'/reference:'+JSON.stringify(path.join(references,p))),
            JSON.stringify(path.join(root,'Tests/UI/ManagedContracts.cs')),
            ...(process.argv.includes('--foundations')?['FoundationLifetime.cs','FoundationFields.cs'].map(file=>JSON.stringify(path.join(root,'Tests/UI/fixtures',file))):[]),
            JSON.stringify(path.join(root,'Examples/ManagedUI/Assets/Scripts/CustomHorizontalLayout.cs'))
        ].join('\n'));
        run('managed-fixture',path.join(runtime,'dotnet.exe'),[path.join(runtime,'sdk',lock.sdk_version,'Roslyn/bincore/csc.dll'),'@'+response]);
        if(process.argv.includes('--foundations')) {
            const foundation=path.join(managed,'UIFoundationTests.dll'),foundationResponse=path.join(output,'foundations.rsp');
            const common=fs.readFileSync(response,'utf8').split('\n').filter(line=>line.startsWith('/')&&!line.startsWith('/out:')&&!line.startsWith('/target:'));
            fs.writeFileSync(foundationResponse,[...common,'/target:exe','/out:'+JSON.stringify(foundation),
                JSON.stringify(path.join(root,'Tests/UI/fixtures/FoundationCollections.cs'))].join('\n'));
            fs.copyFileSync(path.join(managed,'Overload.ManagedHost.runtimeconfig.json'),path.join(managed,'UIFoundationTests.runtimeconfig.json'));
            run('U1-foundations-compile',path.join(runtime,'dotnet.exe'),[path.join(runtime,'sdk',lock.sdk_version,'Roslyn/bincore/csc.dll'),'@'+foundationResponse]);
            run('U1-foundations',path.join(runtime,'dotnet.exe'),[foundation]);
        }
        const uiObject=path.join(output,'managed-ui.obj'),uiExe=path.join(output,'managed-ui.exe');
        run('managed-native-compile',path.join(bin,'cl.exe'),['/nologo','/std:c++20','/EHsc',config==='Debug'?'/MDd':'/MD','/c',...includes,
            path.join(root,'Tests/UI/managed_ui_validation.cpp'),'/Fo'+uiObject]);
        run('managed-native-link',path.join(bin,'link.exe'),['/nologo','/OPT:REF','/OPT:ICF','/OUT:'+uiExe,uiObject,
            ...libraryPaths,
            ...['opengl32','dbghelp','ws2_32','winmm','imm32','version','user32','gdi32','shell32','ole32','oleaut32','advapi32','comdlg32','uuid'].map(x=>x+'.lib')]);
        run('managed-ui',uiExe,[runtime,managed,fixture]);
        if(fs.readFileSync(path.join(output,'managed-ui.log'),'utf8').includes('[ERROR]')) throw Error('Managed UI reported an engine/lifecycle error; see managed-ui.log');
        if(process.argv.includes('--foundations')) {
            run('U1-event-lifetime',uiExe,[runtime,managed,fixture,'foundations']);
            if(fs.readFileSync(path.join(output,'U1-event-lifetime.log'),'utf8').includes('[ERROR]')) throw Error('UI event lifetime reported an engine error');
            // This mode deliberately submits invalid ABI values and asserts rejected writes.
            run('U1-value-fields',uiExe,[runtime,managed,fixture,'fields']);
        }
        const demo=path.join(output,'demo'); fs.mkdirSync(demo);
        const demoAssembly=path.join(demo,'GameScripts.dll'), demoResponse=path.join(output,'demo.rsp');
        const demoOptions=fs.readFileSync(response,'utf8').split('\n').filter(line=>!line.startsWith('/out:') && !line.includes('ManagedContracts.cs') && !line.includes('FoundationLifetime.cs') && !line.includes('FoundationFields.cs'));
        fs.writeFileSync(demoResponse,[...demoOptions,'/out:'+JSON.stringify(demoAssembly),
            JSON.stringify(path.join(root,'Examples/ManagedUI/Assets/Scripts/DemoComponents.cs')),
            JSON.stringify(path.join(root,'Tests/UI/FirstFrameContracts.cs'))].join('\n'));
        run('managed-demo-compile',path.join(runtime,'dotnet.exe'),[path.join(runtime,'sdk',lock.sdk_version,'Roslyn/bincore/csc.dll'),'@'+demoResponse]);
        run('managed-demo',uiExe,[runtime,managed,demoAssembly,'demo']);
        if(fs.readFileSync(path.join(output,'managed-demo.log'),'utf8').includes('[ERROR]')) throw Error('Managed UI demo reported an engine/lifecycle error; see managed-demo.log');
        if (process.argv.includes('--scripting-regression')) {
            const legacy=path.join(output,'s3'); fs.mkdirSync(legacy);
            const legacyResponse=path.join(output,'s3.rsp');
            const common=fs.readFileSync(response,'utf8').split('\n').filter(line=>line.startsWith('/')&&!line.startsWith('/out:'));
            fs.writeFileSync(legacyResponse,[...common,'/out:'+JSON.stringify(path.join(legacy,'S3.dll')),
                JSON.stringify(path.join(root,'Tests/Scripting/Fixtures/S3/Types.cs')),
                JSON.stringify(path.join(root,'Examples/CSharpGameplay/MoveAndResize.cs'))].join('\n'));
            run('s3-fixture',path.join(runtime,'dotnet.exe'),[path.join(runtime,'sdk',lock.sdk_version,'Roslyn/bincore/csc.dll'),'@'+legacyResponse]);
            fs.copyFileSync(path.join(root,'Tests/Scripting/Fixtures/S3/legacy.lua'),path.join(legacy,'s3.lua'));
            const legacyObject=path.join(output,'s3.obj'), legacyExe=path.join(output,'s3.exe');
            run('s3-native-compile',path.join(bin,'cl.exe'),['/nologo','/std:c++20','/EHsc',config==='Debug'?'/MDd':'/MD','/c',...includes,
                path.join(root,'Tests/Scripting/Integration/native_s3_validation.cpp'),'/Fo'+legacyObject]);
            run('s3-native-link',path.join(bin,'link.exe'),['/nologo','/OPT:REF','/OPT:ICF','/OUT:'+legacyExe,legacyObject,
                ...libraryPaths,
                ...['opengl32','dbghelp','ws2_32','winmm','imm32','version','user32','gdi32','shell32','ole32','oleaut32','advapi32','comdlg32','uuid'].map(x=>x+'.lib')]);
            for (const mode of ['default','render-thread']) {
                env.OV_S3_TRACE=path.join(output,'s3-'+mode+'-events.txt'); env.OV_S3_SCENE=path.join(output,'s3-next.ovscene');
                run('s3-'+mode,legacyExe,[runtime,managed,legacy,mode]);
            }
        }
    }
    fs.writeFileSync(path.join(output,'result.json'),JSON.stringify({...evidence,success:true},null,2));
    console.log('Evidence: '+output);
} catch(error) {
    for(const stage of stages) if(stage.status==='running') stage.status='failed';
    fs.writeFileSync(path.join(output,'result.json'),JSON.stringify({...evidence,success:false,error:String(error)},null,2));
    console.error(String(error)); process.exitCode=1;
}
