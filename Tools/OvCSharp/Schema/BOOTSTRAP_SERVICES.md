# Resident bootstrap services (S1, with S2 type discovery)

`BootstrapABI.h` describes the bootstrap transport, separate from the frozen Host
ABI 1 and R6 ABI 1. It does not change either signature. All three use Windows x64,
cdecl and pack 8. The 32-byte services structure contains version 1, exact size,
native-owned R6 context ID, a Main predicate and a log function. No object address
is passed as an engine handle. This transport is not a user/project API.

The native host resolves `Bootstrap.GetHostApi` through hostfxr, validates the full
184-byte Host table and signature, then calls `Bootstrap.Configure` with services.
GetHostApi captures the calling Main managed thread only after a valid handshake.
Native has already checked its captured `OvReflection::MainThread`. Configure also
calls the native Main predicate. Every subsequent call checks Main before touching
the session registry; reentry returns Busy. Logs borrow strict UTF-8 for the duration
of a synchronous noexcept native callback, forwarded to `OvDebug::Logger`.

`Bootstrap.Detach` clears all sessions and native bindings before native destroys
the R6 context. Outstanding owned results must first be released. Both resident
exports return Host status codes, and native retrieves and logs same-thread error
text immediately. The services pointer itself is never retained; the structure is
copied. Native callbacks remain valid until Detach succeeds. The process supports
one active native host and one CLR; detach/reattach on the same Main is supported.
Closing a hostfxr handle never unloads CLR or the resident managed assembly.

S1 bootstrap sessions have an existing absolute project root, a nonempty logical name, flags
zero, project_generation zero (host assigns a monotonic ID and generation), and an
empty assembly_path. They intentionally contain zero user types. A nonempty user
assembly path is rejected instead of pretending to load it. Frozen S2–S7 table
slots are callable but return explicit unavailable/type/field/instance statuses.
There is no Behaviour, user ALC, lifecycle, field persistence or reload in S1.
S2 additionally accepts a nonempty absolute project assembly path and implements
type/alias discovery; see `HOST_ABI.md` for the implemented subset and alias encoding.

`bind_native_api` supports the resident OvCore R6 wrapper in S1. It first compares
the supplied module/version/size/signature to that wrapper's generated constants,
then constructs the generated Api, which independently calls the supplied native
entry with its own contract. It publishes a binding only after both checks pass.
The wrapper and bridge compile into ManagedHost's default ALC in S1; no duplicate
project copy is loaded. The native entry and Context registry belong to the same
linked image and OvReflection instance.

## Paths and application configuration

Editor project settings and Game.ini accept these optional keys:

```ini
csharp_enabled=true
csharp_runtime_root=Runtime/dotnet
csharp_managed_root=Managed
csharp_project_assembly=GameScripts/bin/Debug/net10.0/GameScripts.dll
csharp_assembly_name=GameScripts
```

The default is disabled. The optional project assembly is resolved relative to the
project root (parent of Assets), and the logical name is explicit, never inferred
from the DLL or source filename. An absent assembly retains scene records and
reports a load error. These settings provide S2 programmatic/configured discovery;
they do not implement S5 build/type-picker workflows.

Runtime/managed relative paths resolve against the actual application
executable directory, never the working directory or PATH. Absolute paths are also
accepted. Managed root must contain `Overload.ManagedHost.dll` and its checked-in
runtimeconfig and the matching resident `Overload.ScriptCore.dll`. Runtime root must contain the locked 10.0.12 runtime, hostfxr and
`packs/Microsoft.NETCore.App.Host.win-x64/10.0.12/runtimes/win-x64/native/nethost.dll`.
This is the S1 private toolchain layout; standalone runtime packaging is S7.

nethost receives the explicit dotnet root. Its returned hostfxr path and the loaded
CoreCLR path must equal the locked paths. Dependencies are checked for existence
and x64 PE before use, and DLL loading uses absolute paths with DLL-directory and
System32 search flags. No SDK is downloaded or needed by a disabled native build.

The optional initialization runs after ServiceLocator registration. Failure logs a
diagnostic and leaves the existing Lua/Null service usable. Teardown explicitly
closes managed sessions, detaches callbacks and invalidates R6 context, then unloads
the native scene before resource managers and graphics/physics services. S1 has no
user instances; later instance Destroy must run during session close while native
handles remain valid, before the existing context/scene teardown.

## Building and acceptance

Build the matching native configuration first, then use the pinned private SDK:

```text
Build/Toolchains/dotnet-10.0.401/dotnet.exe restore Sources/OvManaged/Overload.ManagedHost/Overload.ManagedHost.csproj --locked-mode --configfile Sources/OvManaged/Overload.ManagedHost/NuGet.Config
Build/Toolchains/dotnet-10.0.401/dotnet.exe build Sources/OvManaged/Overload.ManagedHost/Overload.ManagedHost.csproj --no-restore -p:ReflectionConfiguration=Debug
node Tests/Scripting/Integration/verify_s1.mjs --python <absolute-Python-3-executable>
```

The acceptance variant adds only a compile-time `S1_ACCEPTANCE` export, `Smoke`, to
the same Bootstrap class. It calls the session's real generated R6 wrapper on
native-published component handles and converts exceptions through the same guard.
The production DLL is built separately without that export. Runtime tests launch a
native EXE directly; dotnet is only a build tool. All child tests have time limits.
The known CET `dotnet build` failure remains separately recorded even when direct
SDK `csc.dll` compilation and native runtime tests succeed.
