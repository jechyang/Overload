# Private runtime packages (S7)

The supported Windows x64 toolchain is SDK 10.0.401 / runtime 10.0.12 / net10.0. Host ABI 1 and generated R6 signatures remain checked at startup. NativeApi is compiled into Overload.ManagedHost; it is not a separately missing DLL.

Build native Overload.sln for Debug, Release or Publish. Build and publish both ManagedHost and the game project through the explicit complete MSBuild compatibility entry:

```text
node Scripts/Windows/BuildCSharp.mjs --compat <project.csproj> --configuration Publish --artifacts-path <intermediates> --output <build-output> --publish <publish-output> --rebuild
```

This performs locked restore, full build, then MSBuild publish using the built outputs. Omit --rebuild to verify unchanged builds. The ordinary dotnet build CET failure on this workstation is recorded separately; this entry does not patch the SDK, change CET or invoke a standalone compiler in place of build/publish.

PackageCSharpGame.mjs accepts --configuration, --destination (must not exist), --runtime (locked SDK root, only runtime files copied), --host, --game (complete published directories), --assets, --settings (Game.ini), --crt (configuration-matching app-local CRT), --redist-notice and --probe (the validation SDK-denial probe). The validation driver supplies the actual sample scene and probe. Game.ini uses executable-relative Runtime/dotnet and Managed, plus project-relative Managed/GameScripts.dll. Runtime paths contain no developer-machine absolute references.

The package contains all runtime files, nethost/hostfxr, managed deps/runtimeconfig files, native CRT, game/engine assets, project/dependency copyright notices and pinned upstream licenses. s7-package.json records every shipped file's size/SHA-256; Licenses/dependencies.json maps dependencies and embedded notices to source paths. Windows API sets, graphics/audio system libraries and the installed GPU driver remain OS prerequisites. DebugCRT packages are internal validation artifacts and explicitly not eligible for redistribution. Release/Publish use the installed VC redistributable inventory.

## Verification scopes

```text
node Tests/Scripting/Integration/verify_s7.mjs --python <absolute-python-path>
node Tests/Scripting/Integration/verify_s7.mjs --headless --python <absolute-python-path>
```

The first command verifies all three configurations and runs real OvGame under a temporary capability-free AppContainer. It grants RX only to a newly created package and writable low-integrity access only to Evidence. The probe verifies its token is AppContainer, installed SDK entrypoints return ACCESS_DENIED, the private CLR is readable and SearchPath cannot find dotnet.exe. PATH and dotnet environment variables are process-local. The game must load package-local hostfxr/CoreCLR, run actual script state/Transform/Box/lifecycle checks in default and RenderThread, and exit normally. The profile is removed and packaged file hashes are checked afterward. No installed SDK ACLs, OS settings or CET mitigations change. The environment is a local OS capability boundary, not an independent OS installation or VM.

The headless command never creates a window or graphics driver. It uses real Scene.Update, Behaviour and R6 Transform calls. Each workload warms for at least 500 frames and one second, then records 500 raw samples for 1/100/1000 instances. The timer surrounds Scene.Update only. Two allocation-free metric probes outside the timer are excluded from ABI counts; counters include lifecycle dispatch and managed native-access calls. Transform read/write entails two component queries plus read and write, four managed-to-native calls per update. GC bytes cover the managed Main thread and collection counts are process-wide. Rendering and physics stepping are outside this microbenchmark; numbers are not whole rendered-frame latency or a Lua comparison.

The CI workflow runs the complete three-configuration headless command. Local headless, local GPU, local isolation, independent machine/VM and remote CI results must always be reported separately. Full S1–S6 and R0–R6 regression receipts accompany final stage acceptance; the headless receipt alone is not S7 acceptance.
