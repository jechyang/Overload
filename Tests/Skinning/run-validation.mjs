// Windows/VS2022 standalone engine test. Build Debug/Release libraries first.
import fs from 'node:fs';
import path from 'node:path';
import cp from 'node:child_process';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { validateFrameGraphReports } from '../RenderingCommands/framegraph-report-validation.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const config = process.argv[2] || 'Release';
if (!['Debug', 'Release'].includes(config)) throw Error('Expected Debug or Release');
for (const retired of ['--effects-oracle','--frontend-immediate','--draw-inputs','--draw-packets','--frame-inputs'])
  if(process.argv.includes(retired))throw Error(retired+' was retired by A6; use --command-list, --bindings and --auxiliary. Historical implementation is in Git 935423e.');
const pipeline = process.argv.includes('--pipeline');
const framegraphMemory = process.argv.includes('--framegraph-memory');
const framegraph = process.argv.includes('--framegraph');
const asan = process.argv.includes('--asan');
const parallel = process.argv.includes('--parallel');
const postprocess = process.argv.includes('--postprocess');
const auxiliary = process.argv.includes('--auxiliary') || postprocess;
if (auxiliary && process.argv.some(x => x.startsWith('--') && x !== '--auxiliary' && x !== '--postprocess' && x !== '--vs')) throw Error('--auxiliary runs separately');
const bindings = process.argv.includes('--bindings');
const stages = process.argv.includes('--stages') || bindings;
const frontend = process.argv.includes('--frontend') || stages;
if (frontend && process.argv.some(x => x.startsWith('--') && x !== '--frontend' && x !== '--stages' && x !== '--bindings' && x !== '--vs')) throw Error('--frontend runs separately');
const parallelRecording = process.argv.includes('--parallel-recording');
const gameBenchmark = process.argv.includes('--game-benchmark');
const recordingBenchmark = process.argv.includes('--recording-benchmark');
const editorBenchmark = process.argv.includes('--editor-benchmark') || recordingBenchmark;
const productBenchmark = gameBenchmark || editorBenchmark;
if (gameBenchmark && editorBenchmark) throw Error('Product benchmarks run separately');
const editorStress = process.argv.includes('--editor-stress');
const framegraphEditor = process.argv.includes('--framegraph-editor');
if (framegraphEditor && process.argv.some(x => x.startsWith('--') && !['--framegraph-editor','--framegraph-baseline','--vs'].includes(x)))
  throw Error('--framegraph-editor runs separately; optionally add --framegraph-baseline or --vs');
const editorThread = process.argv.includes('--editor-thread') || framegraphEditor || editorStress || editorBenchmark;
const editorCamera = process.argv.includes('--editor-camera');
const editor = process.argv.includes('--editor') || editorThread || editorCamera;
const gameThread = process.argv.includes('--game-thread') || gameBenchmark;
const gameFrame = process.argv.includes('--game-frame') || gameThread;
const productLoop = process.argv.includes('--product-loop');
const framegraphBaseline = process.argv.includes('--framegraph-baseline');
if(framegraphBaseline&&!editor)throw Error('--framegraph-baseline requires an Editor test');
if (editorThread && productLoop) throw Error('--editor-thread runs separately from --product-loop');
if (productLoop && !editor && !gameFrame) throw Error('--product-loop requires --editor or --game-frame');
if (gameFrame && process.argv.some(x => ['--editor','--editor-thread','--editor-stress','--editor-benchmark','--command-list','--ui-commands','--render-frame','--render-thread','--log-dispatch','--resource-lifetime','--texture-registry','--imgui-lifetime','--gpu-profiling','--resource-executor','--parallel','--asan','--geometry','--material','--snapshots','--draw-inputs','--draw-packets','--frame-inputs','--target-state'].includes(x))) throw Error('--game-frame runs separately');
const resourceExecutor = process.argv.includes('--resource-executor');
const gpuProfiling = process.argv.includes('--gpu-profiling');
const imguiLifetime = process.argv.includes('--imgui-lifetime');
const textureRegistry = process.argv.includes('--texture-registry');
const resourceLifetime = process.argv.includes('--resource-lifetime');
const logDispatch = process.argv.includes('--log-dispatch');
const renderThread = process.argv.includes('--render-thread');
const renderFrame = process.argv.includes('--render-frame');
const uiCommands = process.argv.includes('--ui-commands') || renderFrame || renderThread || logDispatch || resourceLifetime || textureRegistry || imguiLifetime || gpuProfiling || resourceExecutor;
const editorUI = process.argv.includes('--editor-ui') || editorThread;
if (editorUI && !editor) throw Error('--editor-ui requires --editor');
const commandList = framegraph || framegraphMemory || pipeline || process.argv.includes('--command-list') || editorThread || parallelRecording || frontend || auxiliary;
if (productLoop && editor && (!commandList || !editorUI)) throw Error('Editor --product-loop requires --command-list --editor-ui');
const targetState = process.argv.includes('--target-state') || commandList;
const snapshots = process.argv.includes('--snapshots');
const material = process.argv.includes('--material') || snapshots;
const geometry = process.argv.includes('--geometry') || material;
if (geometry && (editor || parallel || asan)) throw Error('--geometry runs separately');
if (editor && (parallel || asan)) throw Error('--editor runs separately from --parallel/--asan');
const mutexDeque = process.argv.includes('--mutex-deque');
if (uiCommands && (editor || parallel || geometry || asan)) throw Error("--ui-commands runs separately");
const output = path.join(root, 'Build/JobsValidation/Skinning', config + (framegraph ? '-framegraph' : '') + (framegraphMemory ? '-framegraph-memory' : '') + (editorCamera ? '-editor-camera' : '') + (pipeline ? '-pipeline' : '') + (postprocess ? '-postprocess' : auxiliary ? '-auxiliary' : '') + (bindings ? '-bindings' : stages ? '-stages' : '') + (frontend ? '-frontend' : '') + (parallelRecording ? '-parallel-recording' : '') + (framegraphEditor ? '-framegraph-editor' : recordingBenchmark ? '-recording-benchmark' : editorBenchmark ? '-editor-benchmark' : editorStress ? '-editor-stress' : editorThread ? '-editor-thread' : '') + (gameBenchmark ? '-game-benchmark' : gameThread ? '-game-thread' : gameFrame ? '-game-frame' : '') + (resourceExecutor ? '-resource-executor' : gpuProfiling ? '-gpu-profiling' : imguiLifetime ? '-imgui-lifetime' : textureRegistry ? '-texture-registry' : resourceLifetime ? '-resource-lifetime' : logDispatch ? '-log-dispatch' : renderThread ? '-render-thread' : renderFrame ? '-render-frame' : uiCommands ? '-ui-commands' : '') + (commandList ? '-command-list' : targetState ? '-target-state' : snapshots ? '-snapshots' : material ? '-material' : geometry ? '-geometry' : '') + (editorUI ? '-editor-ui' : editor ? '-editor' : parallel ? '-parallel' : '') + (productLoop ? '-product-loop' : '') + (mutexDeque ? '-mutex' : '') + (asan ? '-asan' : ''));
fs.mkdirSync(output, { recursive: true });
// Standalone project builds can skip the solution's resource publishing step.
// Always test the checked-in assets, rather than stale Debug/Release shaders.
for(const area of ['Engine','Editor']) {
  const source=path.join(root,'Resources',area),destination=path.join(root,'Build',config,'Data',area);
  fs.cpSync(source,destination,{recursive:true,force:true,filter:(from,to)=>
    fs.statSync(from).isDirectory()||!fs.existsSync(to)||!fs.readFileSync(from).equals(fs.readFileSync(to))});
}
const header = 'Sources/OvCore/include/OvCore/ECS/Components/CSkinnedMeshRenderer.h';
const source = 'Sources/OvCore/src/OvCore/ECS/Components/CSkinnedMeshRenderer.cpp';
const vswhere = 'C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe';
const vsArg = process.argv.indexOf("--vs");
const vs = (vsArg >= 0 ? process.argv[vsArg + 1] : process.env.VSINSTALLDIR) || cp.execFileSync(vswhere, ['-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], { windowsHide: true, encoding: 'utf8' }).trim();
if (!vs) throw Error('VS2022 was not found; set VSINSTALLDIR to its installation directory');
const version = fs.readFileSync(path.join(vs, 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt'), 'utf8').trim();
const msvc = path.join(vs, 'VC/Tools/MSVC', version);
const sdk = process.env.WindowsSdkDir || 'C:/Program Files (x86)/Windows Kits/10';
const sdkVersion = fs.readdirSync(path.join(sdk, 'Include')).filter(x => fs.existsSync(path.join(sdk, 'Lib', x, 'um/x64/kernel32.lib'))).sort((a,b) => a.localeCompare(b, undefined, { numeric: true })).at(-1);
const compiler = path.join(msvc, 'bin/Hostx64/x64/cl.exe');
const engineLibs = ['assimp','baregl','bullet3','freetype','glfw','ImGui','lua','OvAudio','OvCore','OvDebug','OvMaths','OvPhysics','OvReflection','OvRendering','OvTools','OvUI','OvWindowing','soloud','tinyxml2','tracy'];
function productObjects(project,exclude) {
  const projectText=fs.readFileSync(path.join(root,'Sources',project,project+'.vcxproj'),'utf8');
  const active=new Set([...projectText.matchAll(/<ClCompile Include="([^"]+\.cpp)"/g)].map(m=>path.win32.basename(m[1],'.cpp')+'.obj'));
  return fs.readdirSync(path.join(root,'Bin-Int',config,project)).filter(x=>active.has(x)&&!exclude.includes(x)).map(x=>path.join(root,'Bin-Int',config,project,x));
}
const depIncludes = ['baregl/include','ImGui/include','lua/include','sol/include','tinyxml2/include','tracy','glfw/include','bullet3/src','soloud/include','assimp/include'];
const manifest = { config, framegraph, framegraphMemory, framegraphEditor, editorCamera, pipeline, postprocess, auxiliary, bindings, stages, frontend, parallelRecording, recordingBenchmark, gameBenchmark, editorBenchmark, productLoop, gameFrame, gameThread, resourceExecutor, gpuProfiling, imguiLifetime, textureRegistry, resourceLifetime, logDispatch, renderThread, renderFrame, uiCommands, editorUI, commandList, targetState, snapshots, material, geometry, editor, parallel, mutexDeque, asan, compiler, sdkVersion, results: [] };
fs.rmSync(path.join(output, 'result.json'), { force: true }); // Never leave a stale success after a failed rerun.

function run(file, args, cwd, log, timeout = 120000) {
  const fd = fs.openSync(log, 'w');
  const result = cp.spawnSync(file, args, { cwd, windowsHide: true, encoding: 'utf8', timeout, stdio: ['ignore', fd, fd] });
  fs.closeSync(fd);
  if (result.error) fs.appendFileSync(log, String(result.error));
  if (result.status !== 0) throw Error(`${path.basename(file)} failed (${result.status}); see ${log}`);
  return fs.readFileSync(log, 'utf8');
}

{
  const variant = 'current';
  const dir = path.join(output, variant);
  const overlay = path.join(dir, 'include');
  fs.mkdirSync(dir, { recursive: true });
  for (const [name, original] of [['OvCore/ECS/Components/CSkinnedMeshRenderer.h', header], ['OvRendering/Resources/Model.h', 'Sources/OvRendering/include/OvRendering/Resources/Model.h']]) {
    const text = fs.readFileSync(path.join(root, original), 'utf8');
    const dest = path.join(overlay, name);
    fs.mkdirSync(path.dirname(dest), { recursive: true });
    // Access-only overlay: no production test API or global preprocessor macro.
    // Generated reflection calls the private serialization accessors from a
    // production object. MSVC includes access in their symbols: retain that ABI
    // while exposing only the pose internals/fields used by these fixtures.
    fs.writeFileSync(dest, original === header ? text.replaceAll('private:', 'public:')
      .replace('friend struct OvReflection::Access<CSkinnedMeshRenderer>;', 'private:\n\t\tfriend struct OvReflection::Access<CSkinnedMeshRenderer>;')
      .replace('void SetSerializedAnimation(const std::string& name);', 'void SetSerializedAnimation(const std::string& name);\n\tpublic:') :
      text.replace('friend class Loaders::ModelLoader;', 'friend class Loaders::ModelLoader;\n\t\tfriend struct SkinningModelFixture;')
        .replace('private:\n\t\tstd::vector<Mesh*>', 'public:\n\t\tstd::vector<Mesh*>')
        .replace('private:\r\n\t\tstd::vector<Mesh*>', 'public:\r\n\t\tstd::vector<Mesh*>'));
  }
  const cpp = path.join(dir, 'CSkinnedMeshRenderer.cpp');
  fs.writeFileSync(cpp, fs.readFileSync(path.join(root, source)));
  const exe = path.join(dir, 'skinning-validation.exe');
  const scene = path.join(dir, 'Scene.cpp');
  const sceneSource = 'Sources/OvCore/src/OvCore/SceneSystem/Scene.cpp';
  fs.writeFileSync(scene, fs.readFileSync(path.join(root, sceneSource)));
  if (targetState && !editor) {
    const original = 'Sources/OvRendering/include/OvRendering/Context/Driver.h';
    const dest = path.join(overlay, 'OvRendering/Context/Driver.h');
    fs.mkdirSync(path.dirname(dest), { recursive: true });
    fs.writeFileSync(dest, fs.readFileSync(path.join(root, original), 'utf8').replaceAll('private:', 'public:'));
  }
  const extraSources = [];
  if (parallelRecording && asan) for (const source of [
    'Core/MeshCommandRecorder.cpp', 'Core/DrawPacketBuilder.cpp', 'Data/Material.cpp',
    'Data/RenderCommandList.cpp', 'Data/RenderFrame.cpp'])
    extraSources.push(path.join(root, 'Sources/OvRendering/src/OvRendering', source));
  if (targetState && !editor) extraSources.push(path.join(root, 'Sources/OvRendering/src/OvRendering/Context/Driver.cpp'));
  if (gpuProfiling) {
    // Keep the current Driver implementation and ABI; add only deterministic
    // hook boundaries around profiling initialization for lifetime fault tests.
    let driver = fs.readFileSync(path.join(root, 'Sources/OvRendering/src/OvRendering/Context/Driver.cpp'), 'utf8');
    if (!driver.includes('TracyGpuContext;') || !driver.includes('} profilingGuard;')) throw Error('Driver profiling hook boundaries changed');
    driver = 'extern void GpuProfilingBeforeInitialize();\nextern void GpuProfilingAfterInitialize();\n' + driver
      .replace('TracyGpuContext;', 'GpuProfilingBeforeInitialize();\n\tTracyGpuContext;')
      .replace('} profilingGuard;', '} profilingGuard;\n\tGpuProfilingAfterInitialize();');
    const driverCopy = path.join(dir, 'Driver.cpp'); fs.writeFileSync(driverCopy, driver); extraSources.push(driverCopy);
  }
  const editorObjects = [];
  if (editor) {
    // Access-only overlays for state inspection; the editor mode/view code is
    // linked from the actual solution objects. No test branches in production.
    for (const name of ['Core/Application.h', 'Core/Editor.h', 'Core/PanelsManager.h', 'Panels/SceneView.h', 'Core/GizmoBehaviour.h', 'Panels/TextureDebugger.h', 'Panels/FrameGraph.h', 'Panels/Console.h', 'Panels/AView.h', 'Rendering/PickingReadback.h']) {
      const dest = path.join(overlay, 'OvEditor', name);
      fs.mkdirSync(path.dirname(dest), { recursive: true });
      const text=fs.readFileSync(path.join(root, 'Sources/OvEditor/include/OvEditor', name), 'utf8');
      // The camera fixture derives from SceneView. MSVC encodes virtual member
      // access in symbol names, so retain its production access qualifiers.
      fs.writeFileSync(dest, editorCamera && name==='Panels/SceneView.h' ? text : text.replaceAll('private:', 'public:').replaceAll('protected:', 'public:'));
    }
    const panelHeader = path.join(overlay, 'OvUI/Panels/PanelWindow.h');
    fs.mkdirSync(path.dirname(panelHeader), { recursive: true });
    fs.writeFileSync(panelHeader, fs.readFileSync(path.join(root, 'Sources/OvUI/include/OvUI/Panels/PanelWindow.h'), 'utf8').replaceAll('private:', 'public:').replaceAll('protected:', 'public:'));
    const contextFile = path.join(dir, 'EditorContext.cpp');
    const contextText = fs.readFileSync(path.join(root, 'Sources/OvEditor/src/OvEditor/Core/Context.cpp'), 'utf8');
    const windowCreation = 'window = std::make_unique<OvWindowing::Window>';
    if (!contextText.includes(windowCreation)) throw Error('Hidden-window test override no longer matches Context');
    fs.writeFileSync(contextFile, contextText.replace(windowCreation, 'windowSettings.visible = false; windowSettings.focused = false;\n\t' + windowCreation));
    const callsFile = path.join(dir, 'SystemCalls.cpp');
    const calls = fs.readFileSync(path.join(root, 'Sources/OvTools/src/OvTools/Utils/SystemCalls.cpp'), 'utf8');
    const start = calls.indexOf('std::string OvTools::Utils::SystemCalls::GetPathToAppdata()');
    if (start < 0) throw Error('Appdata isolation function missing');
    const body = calls.indexOf('{', start); let end = body + 1, depth = 1;
    for (; depth && end < calls.length; ++end) { if (calls[end] === '{') ++depth; else if (calls[end] === '}') --depth; }
    if (depth) throw Error('Unclosed Appdata isolation function');
    fs.writeFileSync(callsFile, calls.slice(0, body) + '{ return ' + JSON.stringify(path.join(dir, 'AppData').replaceAll('\\', '/')) + '; }' + calls.slice(end));
    extraSources.push(contextFile, callsFile);
    editorObjects.push(...productObjects('OvEditor',['Main.obj','Context.obj',...(productBenchmark?['Editor.obj']:[])]));
  }
  if (gameFrame) {
    const dest = path.join(overlay, 'OvGame/Core/Game.h');
    fs.mkdirSync(path.dirname(dest), { recursive: true });
    fs.writeFileSync(dest, fs.readFileSync(path.join(root, 'Sources/OvGame/include/OvGame/Core/Game.h'), 'utf8').replaceAll('private:', 'public:'));
    const appHeader = path.join(overlay, 'OvGame/Core/Application.h');
    fs.writeFileSync(appHeader, fs.readFileSync(path.join(root, 'Sources/OvGame/include/OvGame/Core/Application.h'), 'utf8').replaceAll('private:', 'public:'));
    const fpsHeader=path.join(overlay,'OvGame/Utils/FPSCounter.h');
    fs.mkdirSync(path.dirname(fpsHeader),{recursive:true});
    fs.writeFileSync(fpsHeader,fs.readFileSync(path.join(root,'Sources/OvGame/include/OvGame/Utils/FPSCounter.h'),'utf8').replaceAll('private:','public:'));
    const original = fs.readFileSync(path.join(root, 'Sources/OvGame/src/OvGame/Core/Context.cpp'), 'utf8');
    const windowCreation = 'window = std::make_unique<OvWindowing::Window>';
    if (!original.includes(windowCreation)) throw Error('Hidden Game window override no longer matches');
    const gameProject = JSON.stringify(path.join(dir, 'GameProject').replaceAll('\\', '/'));
    const contextText = original.replace(windowCreation, 'windowSettings.visible = false; windowSettings.focused = false;\n\t' + windowCreation)
      .replace('std::filesystem::current_path() / "Data" / "User" / "Assets"', 'std::filesystem::path(' + gameProject + ') / "Assets"')
      .replace('std::filesystem::current_path() / "Data" / "User" / "Game.ini"', 'std::filesystem::path(' + gameProject + ') / "Game.ini"');
    const contextFile = path.join(dir, 'GameContext.cpp'); fs.writeFileSync(contextFile, contextText); extraSources.push(contextFile);
    editorObjects.push(...productObjects('OvGame',['Main.obj','Context.obj',...(productBenchmark?['Game.obj']:[])]));
  }
  if (gameThread || editorThread) {
    // Current production Window implementation, with a single readback boundary
    // before presentation. All platform and GL operations otherwise stay intact.
    let window = fs.readFileSync(path.join(root,'Sources/OvWindowing/src/OvWindowing/Window.cpp'),'utf8');
    if (!window.includes('glfwSwapBuffers(m_glfwWindow);')) throw Error('Window presentation boundary changed');
    window = 'extern void GameThreadBeforePresent();\n' + window.replace('glfwSwapBuffers(m_glfwWindow);','GameThreadBeforePresent();\n\tglfwSwapBuffers(m_glfwWindow);');
    if (productBenchmark) window = 'extern void BenchmarkAfterPresent();\n' + window.replace('glfwSwapBuffers(m_glfwWindow);', 'glfwSwapBuffers(m_glfwWindow); BenchmarkAfterPresent();');
    const copy = path.join(dir,'Window.cpp'); fs.writeFileSync(copy,window); extraSources.push(copy);
  }
  if (productBenchmark) {
    // Current production code, with timing hooks only. State, queue capacity,
    // submission/presentation/fence order and normal implementation stay intact.
    const copy = (file, edit) => {
      const source = fs.readFileSync(path.join(root,file),'utf8');
      const dest = path.join(dir,path.basename(file));
      fs.writeFileSync(dest,edit(source));extraSources.push(dest);
    };
    copy('Sources/OvRendering/src/OvRendering/Data/RenderFrame.cpp', source => {
      const submit = 'auto& storage = *m_storage;\n        try';
      const retired = 'storage.shared->retired.store(true, std::memory_order_release);';
      if (!source.includes(submit) || !source.includes(retired)) throw Error('Frame benchmark boundaries changed');
      return '#include <cstdint>\n#include <tracy/Tracy.hpp>\nextern void BenchmarkBeforeSubmit(uint64_t);\nextern void BenchmarkRetired(uint64_t);\n' + source
        .replace(submit,'BenchmarkBeforeSubmit(GetCompletion().GetFrameID());\n        ZoneScopedN("Benchmark Render Frame");\n        '+submit)
        .replace(retired,'BenchmarkRetired(storage.shared->id);\n        '+retired);
    });
    copy('Sources/OvRendering/src/OvRendering/Threading/RenderThread.cpp', source => {
      const wait = 's.wake->changed.wait(lock, [&] { return s.closed || s.frames < s.maxFrames; });';
      if (!source.includes(wait) || !source.includes('++s.frames;')) throw Error('Queue benchmark boundaries changed');
      return '#include <cstdint>\n#include <cstddef>\nextern void BenchmarkWaitStart(uint64_t);\nextern void BenchmarkAdmitted(uint64_t,size_t,size_t);\n' + source
        .replace(wait,'BenchmarkWaitStart(frame.GetCompletion().GetFrameID());\n        '+wait)
        .replace('++s.frames;','++s.frames;\n        size_t queuedFrames=0;for(const auto& entry:s.queue)if(entry->frame)++queuedFrames;\n        BenchmarkAdmitted(completion.GetFrameID(),queuedFrames,s.frames);');
    });
    const product = editorBenchmark ? 'Editor' : 'Game';
    copy('Sources/Ov'+product+'/src/Ov'+product+'/Core/'+product+'.cpp', source => {
      const boundary = editorBenchmark ? '        auto immediate=RecordRenderFrame(p_deltaTime);' : '        product=BuildSceneFrame();';
      if (!source.includes(boundary)) throw Error('Immediate benchmark boundary changed');
      return 'extern void BenchmarkBeforeImmediateRender();\n' + source.replace(boundary,(editorBenchmark?'        BenchmarkBeforeImmediateRender();\n':'        if(!frame)BenchmarkBeforeImmediateRender();\n')+boundary);
    });
  }
  if (frontend || (commandList && !editor && !gameFrame)) {
    // Isolate test logging/configuration from the interactive Windows profile.
    // The sandbox account may have no registered Known Folder at all.
    const profile = path.join(dir, 'profile'); fs.mkdirSync(profile, { recursive: true });
    let system = fs.readFileSync(path.join(root, 'Sources/OvTools/src/OvTools/Utils/SystemCalls.cpp'), 'utf8');
    const begin = system.indexOf('std::string OvTools::Utils::SystemCalls::GetPathToAppdata()');
    const end = system.indexOf('bool OvTools::Utils::SystemCalls::ExecuteCommand', begin);
    if (begin < 0 || end < begin) throw Error('SystemCalls test profile boundary changed');
    system = system.slice(0, begin) + 'std::string OvTools::Utils::SystemCalls::GetPathToAppdata() { return R"(' + profile + ')"; }\n\n' + system.slice(end);
    const systemCopy = path.join(dir, 'SystemCalls.cpp'); fs.writeFileSync(systemCopy, system); extraSources.push(systemCopy);
  }
  if (frontend) {
    let mesh = fs.readFileSync(path.join(root, 'Sources/OvRendering/src/OvRendering/Core/MeshFeatureProcessor.cpp'), 'utf8');
    const entry = '    MeshRangeResult result;', draw = '        result.visible.push_back({i, distance});';
    if (!mesh.includes(entry) || !mesh.includes(draw)) throw Error('Mesh range test boundary changed');
    mesh = 'extern void FrontendPrepareEntry();\nextern void FrontendPrepareDraw();\n' + mesh
      .replace(entry, '    FrontendPrepareEntry();\n' + entry)
      .replace(draw, draw + '\n        FrontendPrepareDraw();');
    const meshCopy = path.join(dir, 'MeshFeatureProcessor.cpp');
    if (stages) {
      mesh = '#include <cstddef>\nextern void StageRangeEntry(size_t);\nextern void StageRangeExit(size_t);\nextern void StageRangeDraw(size_t);\n' + mesh
        .replace(entry, '    struct Exit { size_t begin; ~Exit() { StageRangeExit(begin); } } exit{begin};\n    StageRangeEntry(begin);\n' + entry)
        .replace(draw, draw + '\n        StageRangeDraw(begin);');
      let coordinator = fs.readFileSync(path.join(root, 'Sources/OvRendering/src/OvRendering/Core/RenderTaskCoordinator.cpp'), 'utf8');
      const submit = '            handles.push_back(jobs->Submit(';
      if (!coordinator.includes(submit)) throw Error('Coordinator submission boundary changed');
      coordinator = '#include <cstddef>\nextern void StageBeforeSubmit(size_t);\n' + coordinator.replace(submit, '            StageBeforeSubmit(slot);\n' + submit);
      const taskCopy = path.join(dir, 'RenderTaskCoordinator.cpp'); fs.writeFileSync(taskCopy, coordinator); extraSources.push(taskCopy);
    }
    if (bindings) {
      for (const [name, phase, entry, draw] of [
        ['DrawPacketBuilder', 1, '    BindingRangeResult result;', '        result.materials.push_back(Data::Material::EncodeSnapshot(source.material));'],
        ['MeshCommandRecorder', 2, '    Data::RenderCommandList commands;', '        check();\n        commands.Seal();']]) {
        let source = fs.readFileSync(path.join(root, 'Sources/OvRendering/src/OvRendering/Core/' + name + '.cpp'), 'utf8');
        if (!source.includes(entry) || !source.includes(draw)) throw Error('A3 range test boundary changed');
        source = '#include <cstddef>\nextern void BindingEntry(int,size_t);\nextern void BindingExit(int,size_t);\nextern void BindingOutput(int,size_t);\n' + source
          .replace(entry, '    struct Exit { size_t begin; ~Exit() { BindingExit(' + phase + ',begin); } } exit{begin};\n    BindingEntry(' + phase + ',begin);\n' + entry)
          .replace(draw, '        BindingOutput(' + phase + ',begin);\n' + draw);
        const file = path.join(dir, name + '.cpp'); fs.writeFileSync(file, source); extraSources.push(file);
      }
      const file = path.join(dir, 'RenderTaskCoordinator.cpp');
      let source = fs.readFileSync(file, 'utf8');
      source = 'extern void BindingSubmit(int,size_t);\n' + source
        .replace('if (jobs) handles.push_back(jobs->Submit(std::move(task)));', 'if (jobs) { BindingSubmit(1,slot); handles.push_back(jobs->Submit(std::move(task))); }')
        .replace('if (jobs) handles.push_back(jobs->Submit(std::move(task)));', 'if (jobs) { BindingSubmit(2,slot); handles.push_back(jobs->Submit(std::move(task))); }');
      fs.writeFileSync(file, source);
    }
    fs.writeFileSync(meshCopy, mesh); extraSources.push(meshCopy);
  }
  if (auxiliary) {
    let source=fs.readFileSync(path.join(root,'Sources/OvRendering/src/OvRendering/Core/RenderTaskCoordinator.cpp'),'utf8');
    source='#include <cstddef>\nextern void AuxiliaryEntry(int,size_t);\nextern void AuxiliaryExit(int,size_t);\nextern void AuxiliaryBeforeSubmit(size_t);\n'+source;
    for(const [phase,entry] of [[1,'value = MeshFeatureProcessor::PrepareRange(*scenes[view], views[view], begin, end, cancel);'],[2,'value.emplace(recorder->RecordRange(frames[view], targets[view], begin, end, cancel));']]) {
      if(!source.includes(entry))throw Error('Auxiliary task boundary changed');
      source=source.replace(entry,'struct Exit {size_t slot;~Exit(){AuxiliaryExit('+phase+',slot);}} exit{slot}; AuxiliaryEntry('+phase+',slot); '+entry);
    }
    const submit='if (jobs) handles.push_back(jobs->Submit(std::move(task))); else task();';
    if(!source.includes(submit))throw Error('Auxiliary submit boundary changed');
    source=source.replace(submit,'if (jobs) {AuxiliaryBeforeSubmit(i);handles.push_back(jobs->Submit(std::move(task)));} else task();');
    const file=path.join(dir,'RenderTaskCoordinator.cpp');fs.writeFileSync(file,source);extraSources.push(file);

  }
  if ((editorThread || gameThread) && !productBenchmark) {
    let source=fs.readFileSync(path.join(root,'Sources/OvRendering/src/OvRendering/Core/RenderTaskCoordinator.cpp'),'utf8');
    source='#include <cstddef>\nextern void ProductTaskEntry(int,size_t,size_t);\n'+source;
    for(const [phase,entry,count] of [[1,'value = MeshFeatureProcessor::PrepareRange(*scenes[view], views[view], begin, end, cancel);','total'],[2,'value.emplace(recorder->RecordRange(frames[view], targets[view], begin, end, cancel));','total']]) {
      if(!source.includes(entry))throw Error('Product task boundary changed');
      source=source.replace(entry,'ProductTaskEntry('+phase+',slot,'+count+'); '+entry);
    }
    const copy=path.join(dir,'RenderTaskCoordinator.cpp');fs.writeFileSync(copy,source);
    extraSources.push(copy,path.join(root,'Tests/RenderingCommands/product_concurrency_probe.cpp'));
  }
  const args = ['/nologo','/std:c++20','/EHsc','/W3','/WX',config === 'Debug' ? '/MDd' : '/MD',config === 'Debug' ? '/Od' : '/O2','/bigobj','/Gy','/Zi',
    ...(stages ? ['/DFRONTEND_STAGE_VALIDATION'] : []),
    ...(bindings ? ['/DFRONTEND_BINDING_VALIDATION'] : []),
    ...(postprocess ? ['/DA5_POSTPROCESS'] : []),
    '/DTRACY_ENABLE','/DTRACY_ON_DEMAND','/DTRACY_MEMORY_ENABLE','/DLUA_SCRIPTING','/DSOL_NO_LUA_HPP',config === 'Debug' ? '/D_DEBUG' : '/DNDEBUG',
    '/DSKINNING_NEW_PATH', ...((geometry || commandList) ? ['/DGEOMETRY_COMMANDS_NEW_PATH'] : []), ...((material || commandList) ? ['/DMATERIAL_COMMANDS_NEW_PATH'] : []), ...((snapshots || commandList) ? ['/DSNAPSHOT_NEW_PATH'] : []), ...(commandList && editor ? ['/DEDITOR_COMMANDS_TEST'] : []), ...(editorUI ? ['/DEDITOR_UI_TEST'] : []), ...(productLoop && editor ? ['/DPRODUCT_EDITOR'] : []), ...(mutexDeque ? ['/DOVTOOLS_JOBS_USE_MUTEX_DEQUE'] : []), ...(asan ? ['/fsanitize=address', '/D_DISABLE_VECTOR_ANNOTATION', '/D_DISABLE_STRING_ANNOTATION'] : []),
    '/I' + overlay, '/I' + path.join(msvc,'include'), ...['ucrt','shared','um','winrt'].map(x => '/I' + path.join(sdk,'Include',sdkVersion,x)),
    ...fs.readdirSync(path.join(root,'Sources')).filter(x => x.startsWith('Ov')).map(x => '/I' + path.join(root,'Sources',x,'include')),
    ...depIncludes.map(x => '/I' + path.join(root,'Dependencies',x)),
    '/Fo' + dir + '/', '/Fd' + path.join(dir,'compile.pdb'), '/Fe' + exe, path.join(root, framegraphEditor ? 'Tests/RenderingCommands/framegraph_editor_validation.cpp' : framegraph ? 'Tests/RenderingCommands/framegraph_validation.cpp' : framegraphMemory ? 'Tests/RenderingCommands/framegraph_memory_validation.cpp' : editorCamera ? 'Tests/RenderingCommands/editor_camera_validation.cpp' : pipeline ? 'Tests/RenderingCommands/pipeline_validation.cpp' : auxiliary ? 'Tests/RenderingCommands/auxiliary_validation.cpp' : frontend ? 'Tests/RenderingCommands/frontend_validation.cpp' : parallelRecording ? 'Tests/RenderingCommands/parallel_recording_validation.cpp' : recordingBenchmark ? 'Tests/RenderingCommands/recording_benchmark.cpp' : editorBenchmark ? 'Tests/RenderingCommands/editor_benchmark.cpp' : gameBenchmark ? 'Tests/RenderingCommands/game_benchmark.cpp' : editorStress ? 'Tests/RenderingCommands/editor_stress_validation.cpp' : editorThread ? 'Tests/RenderingCommands/editor_thread_validation.cpp' : gameThread ? 'Tests/RenderingCommands/game_thread_validation.cpp' : productLoop ? 'Tests/RenderingCommands/application_loop_validation.cpp' : gameFrame ? 'Tests/RenderingCommands/game_frame_validation.cpp' : resourceExecutor ? 'Tests/RenderingCommands/resource_executor_validation.cpp' : gpuProfiling ? 'Tests/RenderingCommands/gpu_profiling_validation.cpp' : imguiLifetime ? 'Tests/RenderingCommands/imgui_lifetime_validation.cpp' : textureRegistry ? 'Tests/RenderingCommands/texture_registry_validation.cpp' : resourceLifetime ? 'Tests/RenderingCommands/resource_lifetime_validation.cpp' : logDispatch ? 'Tests/RenderingCommands/log_dispatch_validation.cpp' : renderThread ? 'Tests/RenderingCommands/render_thread_validation.cpp' : renderFrame ? 'Tests/RenderingCommands/render_frame_validation.cpp' : uiCommands ? 'Tests/RenderingCommands/ui_command_validation.cpp' : commandList && !editor ? 'Tests/RenderingCommands/command_list_validation.cpp' : targetState && !editor ? 'Tests/RenderingCommands/target_state_validation.cpp' : snapshots ? 'Tests/RenderingCommands/snapshot_validation.cpp' : material ? 'Tests/RenderingCommands/material_validation.cpp' : geometry ? 'Tests/RenderingCommands/geometry_validation.cpp' : editor ? 'Tests/Skinning/editor_validation.cpp' : parallel ? 'Tests/Skinning/parallel_validation.cpp' : 'Tests/Skinning/serial_validation.cpp'), cpp, scene, ...extraSources,
    path.join(root, 'Sources/OvTools/src/OvTools/Jobs/JobSystem.cpp'),
    '/link', '/LIBPATH:' + path.join(msvc,'lib/x64'), ...['ucrt','um'].map(x => '/LIBPATH:' + path.join(sdk,'Lib',sdkVersion,x,'x64')),
    ...editorObjects,
    ...engineLibs.map(x => path.join(root,'Bin',config,x,x + '.lib')),
    'gdi32.lib','advapi32.lib','ole32.lib','oleaut32.lib','shell32.lib','comdlg32.lib','uuid.lib','opengl32.lib','dbghelp.lib','ws2_32.lib','winmm.lib','imm32.lib','version.lib','dwmapi.lib','/SUBSYSTEM:CONSOLE','/DEBUG','/OPT:REF'];
  fs.writeFileSync(path.join(dir, 'compile-args.json'), JSON.stringify(args, null, 2));
  console.log('Compiling', config, variant, asan ? 'ASan' : '');
  run(compiler, args, root, path.join(dir,'compile.log'));
  if (asan) fs.copyFileSync(path.join(msvc,'bin/Hostx64/x64/clang_rt.asan_dynamic-x86_64.dll'), path.join(dir,'clang_rt.asan_dynamic-x86_64.dll'));
  if (auxiliary) {
    const captures=[];
    for (const mode of [1,2,3]) {
      const captureDir=path.join(dir,'mode-'+mode);fs.mkdirSync(captureDir,{recursive:true});
      const stdout=run(exe,[captureDir,String(mode)],path.join(root,'Build',config),path.join(captureDir,'run.log'),120000);
      captures.push(captureDir);manifest.results.push({mode,stdout});console.log('PASS: auxiliary mode',mode);
    }
    for(const name of ['frames.bin','shadows.bin','cubes.bin']) {
      const reference=fs.readFileSync(path.join(captures[0],name));
      if(!reference.length || captures.some(dir=>!fs.readFileSync(path.join(dir,name)).equals(reference)))
        throw Error('Auxiliary serial/two Worker/one Worker '+name+' differ');
    }
    manifest.auxiliaryComparison={exactMain:true,exactShadow:true,exactCubeMips:true,framesPerMode:14,modes:3};
  } else if (frontend) {
    // Each scheduling/trace mode gets a fresh process/context; compare all captures to serial.
    const captures = [];
    for (const bindings of ['clean', 'poison']) for (const trace of ['none', 'staged']) for (let mode = 1; mode < 3; ++mode) {
      const captureDir = path.join(dir, 'cold-' + mode + '-' + trace + (bindings === 'clean' ? '' : '-poison'));
      fs.mkdirSync(captureDir, { recursive: true });
      const stdout = run(exe, [captureDir, String(mode), trace, bindings], path.join(root, 'Build', config), path.join(captureDir, 'run.log'), 120000);
      captures.push({ mode, trace, bindings, directory: captureDir });
      manifest.results.push({ variant, mode, trace, bindings, stdout });
      console.log('PASS: cold mode', mode, trace, bindings, '8 frames');
    }
    const pixels = captures.map(c => fs.readFileSync(path.join(c.directory, 'frontend-' + c.mode + '.rgba')));
    if (pixels[0].length !== 8 * 96 * 64 * 4 || pixels.some(p => !p.equals(pixels[0])))
      throw Error('Cold serial/Worker or traced/untraced pixels differ');
    const traces = captures.filter(c => c.trace === 'staged').map(c => fs.readFileSync(path.join(c.directory, 'frontend-' + c.mode + '.draws')));
    if (!traces[0].length || traces.some(t => !t.equals(traces[0])))
      throw Error('Cold serial/Worker logical draw data differ');
    manifest.frontendComparison = { captures, pixelBytes: pixels[0].length, traceBytes: traces[0].length,
      exactPixels: true, exactDrawData: true, traceDoesNotChangePixels: true, constantReadback: 'all active UBOs staged after all draws',
      uniformBlocks: [{ name: 'EngineUBO', binding: 0, bytes: 272 }, { name: 'ReflectionUBO', binding: 1, bytes: 128 }],
      invalidBindingControls: stages ? [] : ['missing', '124-byte range', '16-byte range'],
      drawsPerFrame: stages ? 12 : 4,
      bindingAndRecording: bindings ? 'two Worker private binding ranges and command blocks; controlled overlap/reverse/fail/cancel/partial submit; single Worker and recovery' : null,
      storageBlocks: bindings ? [{name:'LightSSBO',binding:0,bytes:64},{name:'SkinningSSBO',binding:1,bytes:'64 identity / 192 pose'}] : [],
      preparation: stages ? 'two Worker ranges; controlled overlap, forward/reverse completion, failure/cancel/partial submit join, single Worker' : 'shared candidate preparation; serial/Worker failure and lifetime regression',
      pollution: 'new paths: absent/short/stale bindings before replay and between staged draws'  };
    console.log('PASS: 8 cold processes with clean/poisoned explicit bindings, exact serial/Worker pixels and staged draw data; tracing does not change any pixel');
  } else {
    const stdout = run(exe, [dir], path.join(root,'Build',config), path.join(dir,'run.log'), productBenchmark ? 600000 : (parallelRecording || editorThread || editorUI) ? 300000 : 120000);
    console.log(stdout.split(/\r?\n/).filter(line => /^(PASS:|Single-instance|BENCH:)/.test(line)).join('\n'));
    manifest.results.push({ variant, stdout });
  }
}
// A5 baseline is data only: never compile or patch historical renderer sources.
if(framegraphEditor) {
  manifest.framegraphReports=validateFrameGraphReports(path.join(output,'current'));
  console.log('PASS: 6 full Frame Graph JSON reports: parsed schema, escaped Unicode/control names, CPU stages, reconciled pool entries and exact GPU sample');
}
const golden=JSON.parse(fs.readFileSync(path.join(root,'Tests/RenderingCommands/a5-image-baseline.json'),'utf8'));
const relative=path.relative(root,output).replaceAll('\\','/')+'/current/';
const expected=golden.files.filter(f=>f.path.startsWith(relative)&&
  (/(?:^|\/)frames\.bin$|(?:shadows|cubes)\.bin$|editor-command-frames\.bin$|\.(rgba|draws)$/.test(f.path))&&
  (!auxiliary||/\/mode-[123]\//.test(f.path))&&(!frontend||/\/cold-[12]-/.test(f.path)));
manifest.historicalComparison={baseline:golden.baseline,files:[]};
const f0=framegraphBaseline?JSON.parse(fs.readFileSync(path.join(root,'Tests/RenderingCommands/framegraph-image-baseline.json'),'utf8')):null;
if(f0)manifest.framegraphComparison={baseline:f0.baseline,files:[],preexistingA5Differences:[]};
for(const item of expected) {
  const bytes=fs.readFileSync(path.join(root,item.path));
  const hash=crypto.createHash('sha256').update(bytes).digest('hex');
  if(bytes.length!==item.bytes||hash!==item.sha256) {
    const frozen=f0?.files.find(f=>f.name===path.basename(item.path));
    if(!frozen||bytes.length!==frozen.bytes||hash!==frozen.sha256)throw Error('A5/F0 exact baseline differs: '+item.path);
    manifest.framegraphComparison.preexistingA5Differences.push(item.path);
    console.log('PASS: frozen F0 pixels; preexisting A5 difference retained: '+item.path);
  } else manifest.historicalComparison.files.push(item.path);
}
if(f0)for(const item of f0.files) {
  const file=path.join(output,'current',item.name),bytes=fs.readFileSync(file);
  if(bytes.length!==item.bytes||crypto.createHash('sha256').update(bytes).digest('hex')!==item.sha256)throw Error('Frozen F0 baseline differs: '+file);
  manifest.framegraphComparison.files.push(file);
}
fs.writeFileSync(path.join(output,'result.json'), JSON.stringify(manifest, null, 2));
