# C# gameplay test project

This directory contains a small project and scene for exercising the C# scripting path:

- `CSharpGameplay.ovproject` is the editor/game project.
- `Assets/Scenes/CSharpGameplay.ovscene` contains a camera, light, and a visible cube with `Gameplay.MoveAndResize` attached.
- `GameScripts.csproj` builds the `GameScripts` assembly.
- `MoveAndResize.cs` moves the cube on X and grows its `Physical Box` on every update.

Open `CSharpGameplay.ovproject` with the updated OvEditor, then open `Assets/Scenes/CSharpGameplay.ovscene` in the Asset Browser. Opening the project automatically queues a background C# build. Wait for `C#: applied Debug` (or `C#: up to date`) in the C# Scripts panel, then press Play. Saving a C# file automatically builds it after a short debounce. The first build creates the managed generation and writes the applied runtime paths into the project settings. Set `render_thread=true` before reopening the project to exercise the RenderThread path. Standalone OvGame additionally requires a package with Game.ini, assets and the managed/runtime dependencies (see `Tools/OvCSharp/PACKAGING.md`).

`Build automatically on open and save` is enabled by default. A project without C# remains untouched; saving its first `.cs` file inside Assets creates the managed project and builds it. Manual `Build / Reload C#` remains available for retrying a failure or selecting Debug/Release. Both the automatic-build setting and `Reload during Play (supported fields)` are saved per project. During Play, a successful build waits for Stop by default. Enable runtime reload to apply at the next Main safe point using the existing supported-field migration and reload lifecycle; it does not migrate arbitrary object graphs, statics, tasks or delegates. Compilation or preflight failure retains the last working generation. User callback effects after a successful switch are not rolled back.

For a command-line build from the repository root, use the locked compatibility entry:

```text
node Scripts/Windows/BuildCSharp.mjs --compat Examples/CSharpGameplay/GameScripts.csproj --rebuild
```

The project uses SDK 10.0.401, runtime 10.0.12, and `net10.0`. The initial project settings leave the C# host disabled until the editor applies a successful generation, so the scene record remains safe to open before the first build.

The editor build also runs the Python reflection generator. It discovers the engine from the editor location, Node from PATH (or `Build/Toolchains/node/node.exe`), and a verified Python 3.10+ in the background worker. Python candidates include `OV_REFLECTION_PYTHON`, the engine-local toolchain, the interpreter selected in the generated native reflection project, the Python launcher, and PATH. Python 2.7 is rejected before running reflection. Explicit `csharp_engine_root`, `csharp_node`, and `csharp_python` settings remain supported; update or remove machine-specific overrides when moving this example. Tool settings are reread on each attempt, so correcting a path and retrying Build requires no editor restart. The standalone `BuildCSharp.mjs` command above only compiles managed code; it does not validate or apply the editor build workflow.

The scene omits the XML declaration for compatibility with older editor binaries. The updated SceneManager uses the document element, so leading XML declarations and comments are also accepted. The camera uses perspective projection.

After building the native Debug configuration, run the focused scene regression:

```text
node Tests/Scripting/Integration/verify_example_scene.mjs --python <absolute-python-path>
```

This starts with the C# host disabled and verifies automatic initial build, source-save builds, failure retention, recovery, no output-triggered build loops, Stop-delayed application, opt-in runtime reload, preference persistence and tool-path retry without restart. It verifies the C# attachment and native Transform/Box updates during Play/Stop, and exercises rendering in default and RenderThread modes. Declaration and leading-comment variants are tested in isolated project copies. A separate first-script case starts without a managed project. Use process-level `OV_SCRIPTING_EVIDENCE_ROOT` to place evidence on another writable volume.

The previous manual Build/scene validation passed in `Build/Tests/Scripting/example-scene-2HN7BI/verification.json` (2026-09-16). Current automatic-build results are recorded in `CS_SCRIPTING_AUTOBUILD_VALIDATION.md` at the repository root.
