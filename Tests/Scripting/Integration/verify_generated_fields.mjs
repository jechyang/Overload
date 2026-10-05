import fs from 'node:fs';
import path from 'node:path';
import cp from 'node:child_process';
import {fileURLToPath} from 'node:url';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const output = path.join(root, 'Build/Tests/GeneratedFields'); fs.mkdirSync(output, {recursive:true});
const core = path.join(root, 'Sources/OvManaged/Overload.ScriptCore/Overload.ScriptCore.csproj');
const hostProject = path.join(root, 'Sources/OvManaged/Overload.ManagedHost/Overload.ManagedHost.csproj');
const escape = text => text.replaceAll('&', '&amp;').replaceAll('"', '&quot;');
const results = [];
function run(name, executable, args, success = true, env) {
  const fd = fs.openSync(path.join(output, name + '.log'), 'w');
  const result = cp.spawnSync(executable, args, {cwd:root, env, windowsHide:true, stdio:['ignore',fd,fd], timeout:180000}); fs.closeSync(fd);
  results.push({name, exit:result.status, error:String(result.error || '')});
  fs.writeFileSync(path.join(output, 'results.json'), JSON.stringify(results, null, 2));
  if (result.error || (result.status === 0) !== success) throw Error(name + ': see ' + path.join(output, name + '.log'));
  console.log('PASS: ' + name);
}
function project(name, source, exe = false, reference = '', define = '', legacy = false) {
  const directory = path.join(output,name); fs.mkdirSync(directory,{recursive:true});
  fs.copyFileSync(path.join(root,'Sources/OvManaged/Overload.ManagedHost/NuGet.Config'),path.join(directory,'NuGet.Config'));
  const file = path.join(directory,name + '.csproj');
  fs.writeFileSync(file, `<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net10.0</TargetFramework><PlatformTarget>x64</PlatformTarget><Nullable>enable</Nullable><EnableDefaultCompileItems>false</EnableDefaultCompileItems><DefineConstants>${define}</DefineConstants>${legacy ? '<OverloadGenerateFieldSchema>false</OverloadGenerateFieldSchema>' : ''}${exe ? '<OutputType>Exe</OutputType>' : ''}<EmitCompilerGeneratedFiles>true</EmitCompilerGeneratedFiles><CompilerGeneratedFilesOutputPath>generated</CompilerGeneratedFilesOutputPath></PropertyGroup><ItemGroup><Compile Include="${escape(source)}"/><ProjectReference Include="${escape(core)}"/>${exe ? `<ProjectReference Include="${escape(hostProject)}"/>` : ''}${reference ? `<ProjectReference Include="${escape(reference)}"/>` : ''}</ItemGroup></Project>`);
  return file;
}
const build = (name, file, success = true) => run(name, process.execPath, [path.join(root,'Scripts/Windows/BuildCSharp.mjs'),'--compat',file,
  '--configuration','Debug','--artifacts-path',path.join(output,'artifacts',name)], success);
const fixture = project('Fixture', path.join(root,'Tests/Scripting/Integration/generated_fields_validation.cs'), false, '', 'GENERATOR_PROJECT');
build('positive', project('Positive', path.join(root,'Tests/Scripting/Integration/generated_fields_validation.cs'), true, fixture));
const generated = path.join(output, 'Fixture/generated/Overload.SerializationGenerator/Overload.SerializationGenerator.FieldSchemaGenerator/Overload.FieldSchema.g.cs');
if (!fs.existsSync(generated) || !fs.readFileSync(generated,'utf8').includes('UnsafeAccessor')) throw Error('Missing compiler-generated field accessors');
run('runtime', path.join(root,'Build/Toolchains/dotnet-10.0.401/dotnet.exe'), [path.join(output,'artifacts/positive/bin/Positive/debug/Positive.dll')]);
for (const [name, declaration] of [
  ['duplicate', 'public class Base : Overload.Behaviour { [Overload.FieldId("same")] public int A; } public class Derived : Base { [Overload.FormerlySerializedAs("same")] public int B; }'],
  ['invalid', 'public class Bad : Overload.Behaviour { [Overload.FieldId(" ")] public int A; }']]) {
  const source = path.join(output,name + '.cs'); fs.writeFileSync(source, declaration);
  build(name, project(name, source), false);
  if (!fs.readFileSync(path.join(output,name + '.log'),'utf8').includes('error OVSG001')) throw Error('Expected field identity diagnostic for ' + name);
}
if (process.argv.includes('--native')) {
  const buildScript = path.join(root,'Scripts/Windows/BuildCSharp.mjs');
  const host = path.join(output,'host'), scripts = path.join(output,'project'); fs.mkdirSync(scripts,{recursive:true});
  run('host-build',process.execPath,[buildScript,'--compat',path.join(root,'Sources/OvManaged/Overload.ManagedHost/Overload.ManagedHost.csproj'),
    '--artifacts-path',path.join(output,'artifacts/host'),'--output',host]);
  for (const [name,define,legacy] of [['S4','',false],['S4Renamed','RENAMED',false],['S4Duplicate','DUPLICATE_FIELD',true]]) {
    const file = project(name,path.join(root,'Tests/Scripting/Fixtures/S4/Types.cs'),false,'',define,legacy);
    run(name+'-build',process.execPath,[buildScript,'--compat',file,'--artifacts-path',path.join(output,'artifacts',name),'--output',scripts]);
    const schema = path.join(output,name,'generated/Overload.SerializationGenerator/Overload.SerializationGenerator.FieldSchemaGenerator/Overload.FieldSchema.g.cs');
    if (!legacy && !fs.existsSync(schema)) throw Error(name + ' compiled without field generator');
  }
  const generatedRoot = path.join(root,'Build/Generated/Reflection/x86_64-pc-windows-msvc/Debug');
  const args = JSON.parse(fs.readFileSync(path.join(generatedRoot,'OvCore/compile_commands.json'),'utf8'))[0].arguments;
  const system = args.filter(x => x.startsWith('/clang:C:/')).map(x => x.slice(7));
  const msvc = path.dirname(system.find(x => x.includes('/MSVC/') && x.endsWith('/include')));
  const ucrt = system.find(x => x.endsWith('/ucrt')), version = path.basename(path.dirname(ucrt)), sdk = path.resolve(ucrt,'../../..');
  const env = {};
  for (const [key,value] of Object.entries(process.env)) if (!Object.keys(env).some(k => k.toLowerCase() === key.toLowerCase())) env[key] = value;
  env.LIB = [path.join(msvc,'lib/x64'),...['ucrt','um'].map(x => path.join(sdk,'Lib',version,x,'x64'))].join(';');
  const options = [...args.filter(x => (x.startsWith('/I') || x.startsWith('/D')) && !x.includes('OV_REFLECTION_ANALYSIS')),
    ...system.map(x => '/I'+x), '/I'+root, '/I'+generatedRoot, '/I'+path.join(root,'Dependencies/glfw/include'), '/I'+path.join(root,'Dependencies/bullet3/src')];
  const bin = path.join(msvc,'bin/Hostx64/x64'), object = path.join(output,'fields.obj'), exe = path.join(output,'fields.exe');
  run('native-compile',path.join(bin,'cl.exe'),['/nologo','/std:c++20','/EHsc','/MDd','/W4','/WX','/c',...options,
    path.join(root,'Tests/Scripting/Integration/native_s4_validation.cpp'),'/Fo'+object],true,env);
  const libraries = ['OvReflection','OvCore','OvRendering','OvUI','OvTools','OvMaths','OvAudio','OvDebug','OvPhysics','OvWindowing',
    'assimp','baregl','bullet3','freetype','glfw','ImGui','lua','soloud','tinyxml2','tracy'];
  run('native-link',path.join(bin,'link.exe'),['/nologo','/OPT:REF','/OPT:ICF','/OUT:'+exe,object,
    ...libraries.map(x => path.join(root,'Bin/Debug',x,x+'.lib')),
    ...['opengl32','dbghelp','ws2_32','winmm','imm32','version','user32','gdi32','shell32','ole32','oleaut32','advapi32','comdlg32','uuid'].map(x => x+'.lib')],true,env);
  for (const mode of ['default','render-thread']) run('native-'+mode,exe,[path.join(root,'Build/Toolchains/dotnet-10.0.401'),host,scripts,mode],true,env);
}
console.log('PASS: generated fields validation; evidence ' + output);
