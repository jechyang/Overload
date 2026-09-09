// Windows/VS2022 standalone engine test. Build Debug/Release libraries first.
import fs from 'node:fs';
import path from 'node:path';
import cp from 'node:child_process';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const config = process.argv[2] || 'Release';
if (!['Debug', 'Release'].includes(config)) throw Error('Expected Debug or Release');
const asan = process.argv.includes('--asan');
const parallel = process.argv.includes('--parallel');
const frontendImmediate = process.argv.includes('--frontend-immediate');
const frontend = process.argv.includes('--frontend') || frontendImmediate;
if (frontend && process.argv.some(x => x.startsWith('--') && x !== '--frontend' && x !== '--frontend-immediate' && x !== '--vs')) throw Error('--frontend runs separately');
const parallelRecording = process.argv.includes('--parallel-recording');
const gameBenchmark = process.argv.includes('--game-benchmark');
const recordingBenchmark = process.argv.includes('--recording-benchmark');
const editorBenchmark = process.argv.includes('--editor-benchmark') || recordingBenchmark;
const productBenchmark = gameBenchmark || editorBenchmark;
if (gameBenchmark && editorBenchmark) throw Error('Product benchmarks run separately');
const editorStress = process.argv.includes('--editor-stress');
const editorThread = process.argv.includes('--editor-thread') || editorStress || editorBenchmark;
const editor = process.argv.includes('--editor') || editorThread;
const gameThread = process.argv.includes('--game-thread') || gameBenchmark;
const gameFrame = process.argv.includes('--game-frame') || gameThread;
const productLoop = process.argv.includes('--product-loop');
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
const commandList = process.argv.includes('--command-list') || editorThread || parallelRecording || frontend;
if (productLoop && editor && (!commandList || !editorUI)) throw Error('Editor --product-loop requires --command-list --editor-ui');
const targetState = process.argv.includes('--target-state') || commandList;
const frameInputs = process.argv.includes('--frame-inputs') || targetState;
const drawPackets = process.argv.includes('--draw-packets') || frameInputs;
const drawInputs = process.argv.includes('--draw-inputs');
const snapshots = process.argv.includes('--snapshots') || drawInputs;
const material = process.argv.includes('--material') || snapshots;
const geometry = process.argv.includes('--geometry') || material;
if (geometry && (editor || parallel || asan || process.argv.includes('--current'))) throw Error('--geometry requires a baseline/current comparison');
if (drawPackets && (geometry || parallel || (asan && !parallelRecording) || process.argv.includes('--current'))) throw Error('--draw-packets runs separately from other test modes except --editor');
if (editor && (parallel || asan)) throw Error('--editor runs separately from --parallel/--asan');
const mutexDeque = process.argv.includes('--mutex-deque');
if (uiCommands && (editor || parallel || geometry || drawPackets || asan)) throw Error("--ui-commands runs separately");
const currentOnly = gameFrame || uiCommands || asan || parallel || editor || drawPackets || process.argv.includes('--current');
const output = path.join(root, 'Build/JobsValidation/Skinning', config + (frontendImmediate ? '-frontend-immediate' : frontend ? '-frontend' : '') + (parallelRecording ? '-parallel-recording' : '') + (recordingBenchmark ? '-recording-benchmark' : editorBenchmark ? '-editor-benchmark' : editorStress ? '-editor-stress' : editorThread ? '-editor-thread' : '') + (gameBenchmark ? '-game-benchmark' : gameThread ? '-game-thread' : gameFrame ? '-game-frame' : '') + (resourceExecutor ? '-resource-executor' : gpuProfiling ? '-gpu-profiling' : imguiLifetime ? '-imgui-lifetime' : textureRegistry ? '-texture-registry' : resourceLifetime ? '-resource-lifetime' : logDispatch ? '-log-dispatch' : renderThread ? '-render-thread' : renderFrame ? '-render-frame' : uiCommands ? '-ui-commands' : '') + (commandList ? '-command-list' : targetState ? '-target-state' : frameInputs ? '-frame-inputs' : drawPackets ? '-draw-packets' : drawInputs ? '-draw-inputs' : snapshots ? '-snapshots' : material ? '-material' : geometry ? '-geometry' : '') + (editorUI ? '-editor-ui' : editor ? '-editor' : parallel ? '-parallel' : '') + (productLoop ? '-product-loop' : '') + (mutexDeque ? '-mutex' : '') + (asan ? '-asan' : ''));
fs.mkdirSync(output, { recursive: true });
const baseline = drawInputs ? 'b1128a77b55b9d96c554aad63e743056e481f2cf' : snapshots ? '8cc2f8c5a9d992809b95d317a70dfbcfc3928442' : material ? '50194bfe7ee94eac37d63bbf84779c7c68b390e4' : geometry ? '4f8da08b854a7d57e3a2b0ce08f3d8a05039d7d9' : '73ae710a57889bfde94184c3c8f073228e998144';
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
const engineLibs = ['assimp','baregl','bullet3','freetype','glfw','ImGui','lua','OvAudio','OvCore','OvDebug','OvMaths','OvPhysics','OvRendering','OvTools','OvUI','OvWindowing','soloud','tinyxml2','tracy'];
const depIncludes = ['baregl/include','ImGui/include','lua/include','sol/include','tinyxml2/include','tracy','glfw/include','bullet3/src','soloud/include','assimp/include'];
const manifest = { baseline, config, frontend, frontendImmediate, parallelRecording, recordingBenchmark, gameBenchmark, editorBenchmark, productLoop, gameFrame, gameThread, resourceExecutor, gpuProfiling, imguiLifetime, textureRegistry, resourceLifetime, logDispatch, renderThread, renderFrame, uiCommands, editorUI, commandList, targetState, frameInputs, drawPackets, drawInputs, snapshots, material, geometry, editor, parallel, mutexDeque, asan, compiler, sdkVersion, results: [] };
if (frontend) {
  manifest.architectureBaseline = '620ef10fb56c287581bbd55b0c8904ef9cca26b7';
  manifest.oracle = frontendImmediate ? 'SceneRenderer immediate' : 'SceneRenderer owned commands';
}
fs.rmSync(path.join(output, 'result.json'), { force: true }); // Never leave a stale success after a failed rerun.

function run(file, args, cwd, log, timeout = 120000) {
  const fd = fs.openSync(log, 'w');
  const result = cp.spawnSync(file, args, { cwd, windowsHide: true, encoding: 'utf8', timeout, stdio: ['ignore', fd, fd] });
  fs.closeSync(fd);
  if (result.error) fs.appendFileSync(log, String(result.error));
  if (result.status !== 0) throw Error(`${path.basename(file)} failed (${result.status}); see ${log}`);
  return fs.readFileSync(log, 'utf8');
}

for (const variant of currentOnly ? ['current'] : ['baseline', 'current']) {
  const dir = path.join(output, variant);
  const overlay = path.join(dir, 'include');
  fs.mkdirSync(dir, { recursive: true });
  for (const [name, original] of [['OvCore/ECS/Components/CSkinnedMeshRenderer.h', header], ['OvRendering/Resources/Model.h', 'Sources/OvRendering/include/OvRendering/Resources/Model.h']]) {
    const text = variant === 'baseline' && !geometry && original === header
      ? cp.execFileSync('git', ['-c', `safe.directory=${root.replaceAll('\\','/')}`, 'show', `${baseline}:${original}`], { cwd: root, encoding: 'utf8', windowsHide: true })
      : fs.readFileSync(path.join(root, original), 'utf8');
    const dest = path.join(overlay, name);
    fs.mkdirSync(path.dirname(dest), { recursive: true });
    // Access-only overlay: no production test API or global preprocessor macro.
    fs.writeFileSync(dest, original === header ? text.replaceAll('private:', 'public:') :
      text.replace('friend class Loaders::ModelLoader;', 'friend class Loaders::ModelLoader;\n\t\tfriend struct SkinningModelFixture;')
        .replace('private:\n\t\tstd::vector<Mesh*>', 'public:\n\t\tstd::vector<Mesh*>')
        .replace('private:\r\n\t\tstd::vector<Mesh*>', 'public:\r\n\t\tstd::vector<Mesh*>'));
  }
  const cpp = path.join(dir, 'CSkinnedMeshRenderer.cpp');
  fs.writeFileSync(cpp, variant === 'baseline' && !geometry
    ? cp.execFileSync('git', ['-c', `safe.directory=${root.replaceAll('\\','/')}`, 'show', `${baseline}:${source}`], { cwd: root, windowsHide: true })
    : fs.readFileSync(path.join(root, source)));
  const exe = path.join(dir, 'skinning-validation.exe');
  const scene = path.join(dir, 'Scene.cpp');
  const sceneSource = 'Sources/OvCore/src/OvCore/SceneSystem/Scene.cpp';
  fs.writeFileSync(scene, variant === 'baseline' && !geometry
    ? cp.execFileSync('git', ['-c', `safe.directory=${root.replaceAll('\\','/')}`, 'show', `${baseline}:${sceneSource}`], { cwd: root, windowsHide: true })
    : fs.readFileSync(path.join(root, sceneSource)));
  // Keep legacy draw logic while matching current renderer instance layout.
  if (geometry && variant === 'baseline') {
    const original = 'Sources/OvRendering/include/OvRendering/Core/ABaseRenderer.h';
    const dest = path.join(overlay, 'OvRendering/Core/ABaseRenderer.h');
    fs.mkdirSync(path.dirname(dest), { recursive: true });
    let legacy = cp.execFileSync('git', ['-c', 'safe.directory=' + root.split(path.sep).join('/'), 'show', baseline + ':' + original], { cwd: root, windowsHide: true, encoding: 'utf8' });
    const current = fs.readFileSync(path.join(root, original), 'utf8');
    // Keep the old vtable and draw implementation, append current helper types/state.
    const blitStart = current.indexOf('\t\tvoid BlitWithRetainedTextures(');
    const blitEnd = current.indexOf(');', blitStart) + 2;
    const ownersStart = current.indexOf('\t\tstruct RetainedTexturesDescriptor');
    const ownersEnd = current.indexOf('};', ownersStart) + 2;
    legacy = '#include <span>\n#include <OvRendering/Data/RenderCommandList.h>\n#include <OvRendering/Utils/Profiling.h>\n' + legacy.replace('public:', 'public:\n        Utils::GpuScope ProfileGpuScope(const tracy::SourceLocationData* p_source);').replace('\tprotected:', current.slice(blitStart, blitEnd) + '\n' + current.slice(ownersStart, ownersEnd) + '\n\tprotected:');
    const stateStart = current.indexOf('		struct ViewportCommand');
    const stateEnd = current.indexOf('		struct RetainedTexturesDescriptor', stateStart);
    legacy = legacy.replace('	protected:', current.slice(stateStart, stateEnd) + '\n	protected:')
      .replace('	private:', '		PassStateCommand m_passState;\n\t\tstd::unique_ptr<Data::RenderCommandList> m_commands;\n        OvTools::Jobs::JobSystem* m_commandRecordingJobs = nullptr;\n        bool m_parallelCommandRecording = true;\n\n	private:');
    if (!legacy.includes('struct GeometryDrawCommand')) {
      const begin = current.indexOf('\t\tstruct GeometryDrawCommand');
      const end = current.indexOf('\t\tData::FrameDescriptor m_frameDescriptor', begin);
      if (begin < 0 || end < begin) throw Error('Missing geometry helper declarations');
      legacy = legacy.replace('\tprotected:', '\tprotected:\n' + current.slice(begin, end));
    }
    fs.writeFileSync(dest, legacy);
  }
  if (targetState && !editor) {
    const original = 'Sources/OvRendering/include/OvRendering/Context/Driver.h';
    const dest = path.join(overlay, 'OvRendering/Context/Driver.h');
    fs.mkdirSync(path.dirname(dest), { recursive: true });
    fs.writeFileSync(dest, fs.readFileSync(path.join(root, original), 'utf8').replaceAll('private:', 'public:'));
  }
  if (drawPackets) {
    const original = 'Sources/OvCore/include/OvCore/ECS/Components/CReflectionProbe.h';
    const dest = path.join(overlay, 'OvCore/ECS/Components/CReflectionProbe.h');
    fs.mkdirSync(path.dirname(dest), { recursive: true });
    fs.writeFileSync(dest, fs.readFileSync(path.join(root, original), 'utf8').replaceAll('private:', 'public:'));
  }
  const extraSources = [];
  if (parallelRecording && asan) for (const source of [
    'Core/ABaseRenderer.cpp', 'Core/CompositeRenderer.cpp', 'Data/Material.cpp',
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
  if (geometry) {
    // Directly linking ABaseRenderer.cpp moves its legacy static Camera before
    // library initializers. Initialize the math constants it uses first, in
    // both variants, without altering the baseline renderer source.
    for (const name of ['FVector3', 'FQuaternion', 'FMatrix3', 'FMatrix4']) {
      extraSources.push(path.join(root, 'Sources/OvMaths/src/OvMaths', name + '.cpp'));
    }
    const rendererSource = 'Sources/OvRendering/src/OvRendering/Core/ABaseRenderer.cpp';
    const rendererCopy = path.join(dir, 'ABaseRenderer.cpp');
    let rendererText = variant === 'baseline'
      ? cp.execFileSync('git', ['-c', `safe.directory=${root.replaceAll('\\','/')}`, 'show', `${baseline}:${rendererSource}`], { cwd: root, windowsHide: true, encoding: 'utf8' })
      : fs.readFileSync(path.join(root, rendererSource), 'utf8');
    if (variant === 'baseline') {
      const current = fs.readFileSync(path.join(root, rendererSource), 'utf8');
      const begin = current.indexOf('void OvRendering::Core::ABaseRenderer::BlitWithRetainedTextures(');
      const end = current.indexOf('bool OvRendering::Core::ABaseRenderer::IsDrawable', begin);
      if (begin < 0 || end < begin) throw Error('Missing retained blit helper boundary');
      rendererText += '\n' + current.slice(begin, end);
      const stateBegin = current.indexOf('void OvRendering::Core::ABaseRenderer::BindTarget(');
      if (stateBegin < 0) throw Error('Missing pass state helpers');
      rendererText += '\n' + current.slice(stateBegin);
      if (!rendererText.includes('ABaseRenderer::PrepareGeometryDraw(')) {
        const begin = current.indexOf('OvRendering::Core::ABaseRenderer::GeometryDrawCommand');
        const end = current.indexOf('void OvRendering::Core::ABaseRenderer::DrawEntity(', begin);
        if (begin < 0 || end < begin) throw Error('Missing geometry helper definitions');
        rendererText += '\n' + current.slice(begin, end);
      }
      rendererText = '#include <OvRendering/Utils/Profiling.h>\n' + rendererText;
    }
    fs.writeFileSync(rendererCopy, rendererText);
    extraSources.push(rendererCopy);
    if (material) {
      const materialSource = 'Sources/OvRendering/src/OvRendering/Data/Material.cpp';
      const materialCopy = path.join(dir, 'Material.cpp');
      let text = variant === 'baseline'
        ? cp.execFileSync('git', ['-c', `safe.directory=${root.replaceAll('\\','/')}`, 'show', `${baseline}:${materialSource}`], { cwd: root, windowsHide: true, encoding: 'utf8' })
        : fs.readFileSync(path.join(root, materialSource), 'utf8');
      if (variant === 'baseline' && !snapshots) {
        // Normalize the legacy uninitialized hash seed. Comparing undefined
        // stack contents is not a valid oracle for intended cache semantics.
        if (!text.includes('MaterialSignatureSet signature;')) throw Error('Legacy hash seed normalization no longer matches');
        text = text.replace('MaterialSignatureSet signature;', 'MaterialSignatureSet signature{};');
      }
      if (variant === 'baseline') {
        // Current CompositeRenderer links the opt-in snapshot helpers even when
        // disabled. Supply only missing helper definitions; keep old Bind/Upload.
        const current = fs.readFileSync(path.join(root, materialSource), 'utf8');
        const names = ['OvRendering::Data::Material::BindingCommand OvRendering::Data::Material::PrepareBinding(',
          'void OvRendering::Data::Material::ExecuteBinding(',
          'std::optional<OvRendering::Data::Material::UniformUploadCommand> OvRendering::Data::Material::PrepareUniformUpload(',
          'void OvRendering::Data::Material::ExecuteUniformUpload(',
          'OvRendering::Data::Material::Snapshot OvRendering::Data::Material::RecordSnapshot(',
          'OvRendering::Data::Material::Snapshot OvRendering::Data::Material::RecordCachedSnapshot(',
          'void OvRendering::Data::Material::Snapshot::Execute()'];
        for (let i = 0; i < names.length; ++i) if (!text.includes(names[i])) {
          const begin = current.indexOf(names[i]);
          const end = current.indexOf('\n}', begin) + 2;
          if (begin < 0 || end < begin) throw Error('Missing snapshot helper boundary');
          text += '\n' + current.slice(begin, end);
        }
        const prepared = current.indexOf('class OvRendering::Data::Material::SnapshotInput');
        if (prepared < 0) throw Error('Missing prepared snapshot helper boundary');
        text += '\n' + current.slice(prepared);
      }
      fs.writeFileSync(materialCopy, '#include <algorithm>\n#include <cstring>\n#include <mutex>\n#include <stdexcept>\n' + text);
      extraSources.push(materialCopy);
      if (snapshots) {
        // Both immediate-path oracles use the current resource representation.
        // Lifetime/reload checks exercise the new APIs separately on current.
        for (const name of ['Shader', 'Texture']) extraSources.push(path.join(root, 'Sources/OvRendering/src/OvRendering/Resources', name + '.cpp'));
      }
    }
  }
  if (drawInputs) {
    extraSources.push(path.join(root, 'Sources/OvRendering/src/OvRendering/Resources/Mesh.cpp'));
    for (const name of ['EngineBufferRenderFeature', 'SkinningRenderFeature']) {
      const original = 'Sources/OvCore/src/OvCore/Rendering/' + name + '.cpp';
      const dest = path.join(dir, name + '.cpp');
      fs.writeFileSync(dest, variant === 'baseline'
        ? cp.execFileSync('git', ['-c', 'safe.directory=' + root.split(path.sep).join('/'), 'show', baseline + ':' + original], { cwd: root, windowsHide: true })
        : fs.readFileSync(path.join(root, original)));
      extraSources.push(dest);
    }
  }
  if (editor) {
    // Access-only overlays for state inspection; the editor mode/view code is
    // linked from the actual solution objects. No test branches in production.
    for (const name of ['Core/Application.h', 'Core/Editor.h', 'Core/PanelsManager.h', 'Panels/SceneView.h', 'Core/GizmoBehaviour.h', 'Panels/TextureDebugger.h', 'Panels/Console.h', 'Panels/AView.h', 'Rendering/PickingRenderPass.h']) {
      const dest = path.join(overlay, 'OvEditor', name);
      fs.mkdirSync(path.dirname(dest), { recursive: true });
      fs.writeFileSync(dest, fs.readFileSync(path.join(root, 'Sources/OvEditor/include/OvEditor', name), 'utf8').replaceAll('private:', 'public:').replaceAll('protected:', 'public:'));
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
    editorObjects.push(...fs.readdirSync(path.join(root, 'Bin-Int', config, 'OvEditor'))
      .filter(x => x.endsWith('.obj') && !['Main.obj', 'Context.obj', ...(productBenchmark ? ['Editor.obj'] : [])].includes(x))
      .map(x => path.join(root, 'Bin-Int', config, 'OvEditor', x)));
  }
  if (gameFrame) {
    const dest = path.join(overlay, 'OvGame/Core/Game.h');
    fs.mkdirSync(path.dirname(dest), { recursive: true });
    fs.writeFileSync(dest, fs.readFileSync(path.join(root, 'Sources/OvGame/include/OvGame/Core/Game.h'), 'utf8').replaceAll('private:', 'public:'));
    const appHeader = path.join(overlay, 'OvGame/Core/Application.h');
    fs.writeFileSync(appHeader, fs.readFileSync(path.join(root, 'Sources/OvGame/include/OvGame/Core/Application.h'), 'utf8').replaceAll('private:', 'public:'));
    const original = fs.readFileSync(path.join(root, 'Sources/OvGame/src/OvGame/Core/Context.cpp'), 'utf8');
    const windowCreation = 'window = std::make_unique<OvWindowing::Window>';
    if (!original.includes(windowCreation)) throw Error('Hidden Game window override no longer matches');
    const gameProject = JSON.stringify(path.join(dir, 'GameProject').replaceAll('\\', '/'));
    const contextText = original.replace(windowCreation, 'windowSettings.visible = false; windowSettings.focused = false;\n\t' + windowCreation)
      .replace('std::filesystem::current_path() / "Data" / "User" / "Assets"', 'std::filesystem::path(' + gameProject + ') / "Assets"')
      .replace('std::filesystem::current_path() / "Data" / "User" / "Game.ini"', 'std::filesystem::path(' + gameProject + ') / "Game.ini"');
    const contextFile = path.join(dir, 'GameContext.cpp'); fs.writeFileSync(contextFile, contextText); extraSources.push(contextFile);
    editorObjects.push(...fs.readdirSync(path.join(root, 'Bin-Int', config, 'OvGame'))
      .filter(x => x.endsWith('.obj') && !['Main.obj', 'Context.obj', ...(productBenchmark ? ['Game.obj'] : [])].includes(x))
      .map(x => path.join(root, 'Bin-Int', config, 'OvGame', x)));
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
      const boundary = editorBenchmark ? '        RenderViews(p_deltaTime);' : '            RenderCurrentScene(m_sceneRenderer, m_context);';
      if (!source.includes(boundary)) throw Error('Immediate benchmark boundary changed');
      return 'extern void BenchmarkBeforeImmediateRender();\n' + source.replace(boundary,'        BenchmarkBeforeImmediateRender();\n'+boundary);
    });
  }
  if (frontend) {
    // Isolate test logging/configuration from the interactive Windows profile.
    // The sandbox account may have no registered Known Folder at all.
    const profile = path.join(dir, 'profile'); fs.mkdirSync(profile, { recursive: true });
    let system = fs.readFileSync(path.join(root, 'Sources/OvTools/src/OvTools/Utils/SystemCalls.cpp'), 'utf8');
    const begin = system.indexOf('std::string OvTools::Utils::SystemCalls::GetPathToAppdata()');
    const end = system.indexOf('bool OvTools::Utils::SystemCalls::ExecuteCommand', begin);
    if (begin < 0 || end < begin) throw Error('SystemCalls test profile boundary changed');
    system = system.slice(0, begin) + 'std::string OvTools::Utils::SystemCalls::GetPathToAppdata() { return R"(' + profile + ')"; }\n\n' + system.slice(end);
    const systemCopy = path.join(dir, 'SystemCalls.cpp'); fs.writeFileSync(systemCopy, system); extraSources.push(systemCopy);
    const source = fs.readFileSync(path.join(root, 'Sources/OvRendering/src/OvRendering/Core/ViewPreparer.cpp'), 'utf8');
    const marker = '    const auto checkCancel =';
    if (!source.includes(marker)) throw Error('ViewPreparer entry changed');
    const copy = path.join(dir, 'ViewPreparer.cpp');
    const drawMarker = 'result->m_draws.push_back({index, state, OvMaths::FMatrix4::Transpose(mesh.model), mesh.user});';
    if (!source.includes(drawMarker)) throw Error('Prepared draw publication boundary changed');
    fs.writeFileSync(copy, 'extern void FrontendPrepareEntry();\nextern void FrontendPrepareDraw();\n' + source
      .replace(marker, '    FrontendPrepareEntry();\n' + marker)
      .replace(drawMarker, drawMarker + '\n        FrontendPrepareDraw();'));
    extraSources.push(copy);
  }
  const args = ['/nologo','/std:c++20','/EHsc','/W3','/WX',config === 'Debug' ? '/MDd' : '/MD',config === 'Debug' ? '/Od' : '/O2','/bigobj','/Gy','/Zi',
    ...(frontendImmediate ? ['/DFRONTEND_IMMEDIATE_ORACLE'] : []),
    '/DTRACY_ENABLE','/DTRACY_ON_DEMAND','/DTRACY_MEMORY_ENABLE','/DLUA_SCRIPTING','/DSOL_NO_LUA_HPP',config === 'Debug' ? '/D_DEBUG' : '/DNDEBUG',
    ...((variant === 'current' || geometry) ? ['/DSKINNING_NEW_PATH'] : []), ...((geometry || drawPackets) && variant === 'current' ? ['/DGEOMETRY_COMMANDS_NEW_PATH'] : []), ...((material || drawPackets) && variant === 'current' ? ['/DMATERIAL_COMMANDS_NEW_PATH'] : []), ...((snapshots || drawPackets) && variant === 'current' ? ['/DSNAPSHOT_NEW_PATH'] : []), ...((drawInputs || drawPackets) && variant === 'current' ? ['/DDRAW_INPUTS_NEW_PATH'] : []), ...(drawPackets ? ['/DDRAW_PACKETS_TEST'] : []), ...(commandList && editor ? ['/DEDITOR_COMMANDS_TEST'] : []), ...(editorUI ? ['/DEDITOR_UI_TEST'] : []), ...(productLoop && editor ? ['/DPRODUCT_EDITOR'] : []), ...(mutexDeque ? ['/DOVTOOLS_JOBS_USE_MUTEX_DEQUE'] : []), ...(asan ? ['/fsanitize=address', '/D_DISABLE_VECTOR_ANNOTATION', '/D_DISABLE_STRING_ANNOTATION'] : []),
    '/I' + overlay, '/I' + path.join(msvc,'include'), ...['ucrt','shared','um','winrt'].map(x => '/I' + path.join(sdk,'Include',sdkVersion,x)),
    ...fs.readdirSync(path.join(root,'Sources')).filter(x => x.startsWith('Ov')).map(x => '/I' + path.join(root,'Sources',x,'include')),
    ...depIncludes.map(x => '/I' + path.join(root,'Dependencies',x)),
    '/Fo' + dir + '/', '/Fd' + path.join(dir,'compile.pdb'), '/Fe' + exe, path.join(root, frontend ? 'Tests/RenderingCommands/frontend_validation.cpp' : parallelRecording ? 'Tests/RenderingCommands/parallel_recording_validation.cpp' : recordingBenchmark ? 'Tests/RenderingCommands/recording_benchmark.cpp' : editorBenchmark ? 'Tests/RenderingCommands/editor_benchmark.cpp' : gameBenchmark ? 'Tests/RenderingCommands/game_benchmark.cpp' : editorStress ? 'Tests/RenderingCommands/editor_stress_validation.cpp' : editorThread ? 'Tests/RenderingCommands/editor_thread_validation.cpp' : gameThread ? 'Tests/RenderingCommands/game_thread_validation.cpp' : productLoop ? 'Tests/RenderingCommands/application_loop_validation.cpp' : gameFrame ? 'Tests/RenderingCommands/game_frame_validation.cpp' : resourceExecutor ? 'Tests/RenderingCommands/resource_executor_validation.cpp' : gpuProfiling ? 'Tests/RenderingCommands/gpu_profiling_validation.cpp' : imguiLifetime ? 'Tests/RenderingCommands/imgui_lifetime_validation.cpp' : textureRegistry ? 'Tests/RenderingCommands/texture_registry_validation.cpp' : resourceLifetime ? 'Tests/RenderingCommands/resource_lifetime_validation.cpp' : logDispatch ? 'Tests/RenderingCommands/log_dispatch_validation.cpp' : renderThread ? 'Tests/RenderingCommands/render_thread_validation.cpp' : renderFrame ? 'Tests/RenderingCommands/render_frame_validation.cpp' : uiCommands ? 'Tests/RenderingCommands/ui_command_validation.cpp' : commandList && !editor ? 'Tests/RenderingCommands/command_list_validation.cpp' : targetState && !editor ? 'Tests/RenderingCommands/target_state_validation.cpp' : frameInputs && !editor ? 'Tests/RenderingCommands/frame_input_validation.cpp' : drawPackets && !editor ? 'Tests/RenderingCommands/packet_validation.cpp' : drawInputs ? 'Tests/RenderingCommands/draw_input_validation.cpp' : snapshots ? 'Tests/RenderingCommands/snapshot_validation.cpp' : material ? 'Tests/RenderingCommands/material_validation.cpp' : geometry ? 'Tests/RenderingCommands/geometry_validation.cpp' : editor ? 'Tests/Skinning/editor_validation.cpp' : parallel ? 'Tests/Skinning/parallel_validation.cpp' : 'Tests/Skinning/serial_validation.cpp'), cpp, scene, ...extraSources,
    path.join(root, 'Sources/OvTools/src/OvTools/Jobs/JobSystem.cpp'),
    '/link', '/LIBPATH:' + path.join(msvc,'lib/x64'), ...['ucrt','um'].map(x => '/LIBPATH:' + path.join(sdk,'Lib',sdkVersion,x,'x64')),
    ...editorObjects,
    ...engineLibs.map(x => path.join(root,'Bin',config,x,x + '.lib')),
    'gdi32.lib','advapi32.lib','ole32.lib','oleaut32.lib','shell32.lib','comdlg32.lib','uuid.lib','opengl32.lib','dbghelp.lib','ws2_32.lib','winmm.lib','imm32.lib','version.lib','dwmapi.lib','/SUBSYSTEM:CONSOLE','/DEBUG','/OPT:REF'];
  fs.writeFileSync(path.join(dir, 'compile-args.json'), JSON.stringify(args, null, 2));
  console.log('Compiling', config, variant, asan ? 'ASan' : '');
  run(compiler, args, root, path.join(dir,'compile.log'));
  if (asan) fs.copyFileSync(path.join(msvc,'bin/Hostx64/x64/clang_rt.asan_dynamic-x86_64.dll'), path.join(dir,'clang_rt.asan_dynamic-x86_64.dll'));
  const stdout = run(exe, [dir], path.join(root,'Build',config), path.join(dir,'run.log'), productBenchmark ? 600000 : parallelRecording ? 300000 : 120000);
  console.log(stdout.split(/\r?\n/).filter(line => /^(PASS:|Single-instance|BENCH:)/.test(line)).join('\n'));
  manifest.results.push({ variant, stdout });
}
if (!currentOnly) {
  for (const file of drawInputs ? ['draw-input-trace.bin', 'draw-input-frames.bin', 'material-state.bin', 'material-trace.bin', 'geometry-frames.bin', 'draws.bin', 'frames.bin'] : material ? ['material-state.bin', 'material-trace.bin', 'geometry-frames.bin', 'draws.bin', 'frames.bin'] : geometry ? ['geometry-frames.bin', 'draws.bin', 'frames.bin'] : ['states.bin', 'frames.bin']) {
    const a = fs.readFileSync(path.join(output,'baseline',file));
    const b = fs.readFileSync(path.join(output,'current',file));
    if (!a.equals(b)) {
      let first = 0; while (first < Math.min(a.length, b.length) && a[first] === b[first]) ++first;
      throw Error(`${file} differs at byte ${first}; lengths ${a.length}/${b.length}`);
    }
    console.log(`EXACT MATCH: ${file}, ${a.length} bytes`);
  }
}
fs.writeFileSync(path.join(output,'result.json'), JSON.stringify(manifest, null, 2));
