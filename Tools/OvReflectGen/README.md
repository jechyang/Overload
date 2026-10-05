# OvReflectGen — R0/R1/R2/R3/R4

Standalone Clang LibTooling host tool. Engine libraries and shipped applications
have no LLVM dependency. The tool analyzes real definitions and extracts a
validated, explicitly annotated Schema. The separate Python emitter produces
C++20 descriptors and accessors for the lightweight OvReflection runtime.

## Requirements

- Windows x64, Visual Studio 2022 C++ tools and DIA SDK, Windows SDK.
- LLVM **18.1.8 full development archive**, including include/clang, static
  libraries and lib/cmake. The compiler-only VS installation or LLVM installer
  is insufficient to build the tool.
- CMake 3.24+, Ninja (the VS-bundled copy is discovered), Python 3.10+.
- Repository's Premake 5.0.0-beta7 executable.

Validated local baseline: MSVC 14.42.34433 (compiler 19.42.34433),
Windows SDK 10.0.22621.0. Discovery records the actual compiler banner, all
system include directories, Clang resource directory and paths. No fake
third-party types or compiler-version-check suppression is used.

Run from the repository root:

~~~powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Windows/InstallReflectionToolchain.ps1
python Tools/OvReflectGen/reflection.py probe
python Tools/OvReflectGen/reflection.py build
python Tools/OvReflectGen/reflection.py export
python Tools/OvReflectGen/reflection.py generate
python Tests/Reflection/Integration/verify_r0.py
python Tests/Reflection/Integration/verify_r1.py
python Tests/Reflection/Integration/verify_r2.py
~~~

The installer downloads the official archive with curl resume support, extracts
it into Build/Toolchains/llvm-18.1.8, and checks the version and required
development files. The printed SHA256 is a local download receipt, not a claimed
comparison with an upstream signed checksum. A manually extracted full archive
at the same path works too. Use -ShowPaths to inspect the installer paths without
downloading.

Specify --llvm-root, --vs-root, --msvc-root, --sdk-root, --sdk-version or --cmake
when auto-discovery is inappropriate. CI can set OV_REFLECTION_MSVC_ROOT and
OV_REFLECTION_SDK_VERSION. These select an analysis/host-tool environment and
are recorded in provenance; custom MSBuild property sheets are outside this
Premake export contract. To change the actual engine toolchain, use its normal
build configuration as well.

The tool is built in Release even when it analyzes Debug or Publish engine
headers. The official LLVM package was built with the release dynamic CRT.
The package's LLVMDebugInfoPDB export contains the release builder's VS2019 DIA
library path; our CMake setup redirects only that import to the selected VS
installation's real diaguids.lib. The downloaded package is not modified.

## Inputs and outputs

[scan-modules.json](scan-modules.json) is the explicit module/header list:
OvMaths scans FVector3; OvPhysics and OvRendering scan their reflected enums;
OvCore scans the registered component headers. Each module
gets one isolated translation unit per configuration, with completeness checks
for expected definitions. R2 annotates FVector3 and CPhysicalBox.Size.

The current annotation-driven serialization workflow, C# generator integration,
compatibility rules and validation commands are described in
[SERIALIZATION.zh-CN.md](SERIALIZATION.zh-CN.md).

The optional --reflection-config-out option wraps VS2022's onProject callback
and reads the already baked Premake configurations. This retains inherited
workspace/module defines and include ordering. It does not scrape source Lua
or create separate hand-maintained include/define lists. The integration check
compares the export against the VS writer's actual .vcxproj output.

The Python driver maps these configurations to Clang's CL-driver options,
including C++20, target triple, actual MSVC compatibility version, CRT,
exceptions, RTTI, Unicode defaults, explicit system includes and resource
headers. Only /FS and /bigobj are omitted as nonsemantic output options. CL,
_CL_, INCLUDE and common Clang include environment overrides are cleared during
analysis. System include discovery is disabled in favor of the recorded paths.

Build/Generated/Reflection contains toolchain.json and Premake/*.json, followed
by <target>/<config>/<module>/ containing:

- Reflection.scan.cpp: real header includes; no generated header dependency.
- compile_commands.json: exact argv, directory and translation unit.
- analysis-input.json: baked config, toolchain, scan list and option mapping.
- analysis-report.json: LibTooling evidence, semantic type names, AST categories,
  canonical base names and repository-relative source locations.
- schema.json: sorted R1 types, enum values, fields, logical properties and
  method signatures. Unannotated business types do not enter this output.
- dependencies.json: actual entered Clang input buffers and SHA256 hashes,
  including transitive and system headers.
- input-summary.json: dependency receipts and an aggregate local input digest.
  R3 hashes command/configuration, include inventory, generator and scan inputs
  into that digest; analysis-input.json retains exact commands and toolchain
  provenance. Standalone analyze also retains individual non-header input hashes.
- Reflection.generated.h/.cpp and manifest.json: R2 aggregate C++ outputs and
  content receipts, emitted by generate or the build-time ensure command.
- diagnostics.txt: native Clang diagnostics. Failure returns nonzero and
  invalidates analysis and generated success outputs (report, Schema,
  dependencies, summary, generated header/source and manifest).

For preflight work without LibTooling libraries:

~~~powershell
python Tools/OvReflectGen/reflection.py export
python Tools/OvReflectGen/reflection.py analyze --syntax-only
python Tests/Reflection/Integration/verify_r0.py --syntax-only
~~~

This mode emits syntax-report.json and syntax-diagnostics.txt. It is explicitly
not LibTooling acceptance and cannot produce schema or generated C++.

The default verification creates a fresh Build/Tests/Reflection/r0-* directory.
It covers all configurations, VS macro/include/dialect parity, deterministic
arguments and AST reports, real type completeness and kind, source locations,
syntax failures, missing dependencies/types and stale-success rejection.
Evidence is retained in that directory.

## R1 verification and scope

The [protocol](Schema/PROTOCOL.md) defines the implemented annotation grammar,
type support, access rules, identity/alias checks and JSON shape. The fixed
header lives in Sources/OvReflection/include/OvReflection/Annotations.h; it
declares only macros and an external Access<T> template. Reflection.h defines
R2 runtime APIs; it is not needed by business headers.
The driver adds the analysis-only define/include and ignored-attribute checks
explicitly to its exported arguments. The R1 baseline originally emitted empty
production Schemas. R2 adds FVector3 and CPhysicalBox; R0 completeness reports
continue to include CTransform.

R1 acceptance creates a fresh Build/Tests/Reflection/r1-* directory. It covers
the four declaration kinds, nested/inline namespaces, default/explicit IDs and
aliases, canonical types, lossless enum values, getters/setters, private friend
access, method signatures, inheritance conflicts and unsupported declarations.
It verifies deterministic bytes, actual transitive/system input hashes, indirect
header and command-line changes, failure diagnostics and stale-output rejection.
Positive fixtures and wrappers deriving from the **real** CPhysicalBox and
CTransform compile in normal Clang/MSVC mode in all three configurations. The
wrappers exercise real ancestry and FVector3 types, without fake engine headers
or replacing engine dependencies. The wrapper property uses fixtureSize to
avoid colliding with the now annotated production size property.

Direct LibTooling CLI: supply -p, --source-root, --module, --configuration,
--report and exactly one scan translation unit. --expect is optional (R0 smoke
checks); --schema and --dependencies optionally override the default sibling
paths. The compilation database must contain the analysis-only options above.
Native invocation emits Schema and dependency hashes; the Python driver also
produces input-summary.json. Direct native invocations are diagnostic interfaces and must use isolated output directories.
The managed driver serializes analyze/generate/ensure/clean by target/configuration.

R2 provides runtime descriptors, external queries and generated C++ access.
R3 integrates automatic generation into the engine build graph through the
independent OvReflectionGen project.

The dedicated Reflection R0-R4 CI workflow uses windows-2022, installs the
compatible MSVC 14.42 component if needed and downloads the fixed LLVM archive.
CI results must be reported separately from local verification.

## R2 generation and runtime

Run export once for the selected toolchain, then run generate. The generate
command checks each requested module/configuration and analyzes changed inputs
before emitting owned types. Annotated includes remain in Schema for validation; only types under
Sources/<module>/ are emitted by that module. An annotated dependency without a
scan-module owner fails generation. Module headers include the generated headers
of their annotated dependencies. The configuration-wide transaction validates this ownership graph before publishing.

The C++ emitter lives in Emitters/cpp.py and consumes Schema without LLVM APIs.
Its own source is included in input-summary.json. The generated header includes
business definitions through repository-relative paths; compile with the
repository root and normal module include directories on the include path.
Compile exactly one generated .cpp per module/configuration, then link the
OvReflection static library. Include the generated header in typed consumers.
The regular Premake build now compiles these generated sources into their owning
OvMaths/OvCore static libraries and links the fixed OvReflection runtime.

~~~cpp
#include "OvCore/Reflection.generated.h" // configuration's generated root in include path

OvReflection::Registry registry; // construct on Main before starting workers
// Check each returned Error before continuing initialization.
auto builtins = OvReflection::RegisterBuiltinTypes(registry);
auto maths = OvReflection::RegisterOvMathsTypes(registry);
auto core = OvReflection::RegisterOvCoreTypes(registry);
auto frozen = registry.Freeze();

const auto& type = OvReflection::TypeOf<OvCore::ECS::Components::CPhysicalBox>();
const auto* size = type.FindProperty("size");
OvMaths::FVector3 input{2, 3, 4};
auto result = size->Write(registry.Main(), OvReflection::ObjectRef::From(box),
                         OvReflection::ValueRef::From(input));
// High-frequency typed path, with the same Main check:
auto typed = OvReflection::Access<OvCore::ECS::Components::CPhysicalBox>::Write_GetSize(
    registry.Main(), box, input);
~~~

The registry checks module ownership, C++ type identity, and global type/property/
method IDs and aliases before committing a batch. Duplicate registration is an
error, including registering the same module twice. Freeze closes registration.
Generated descriptors have immutable static storage; publish the initialized,
frozen registry with ordinary C++ synchronization before concurrent reads.
Registration/Freeze execute only on the initialization thread.

Primitive TypeOf queries support bool, signed/unsigned integer C++ types,
float/double and std::string. Fixed cpp.* IDs identify C++ types; widths/sign
remain descriptor facts. Typedefs resolve to their underlying C++ identity.
Enums retain exact decimal strings, including UINT64_MAX. Method descriptors
retain signatures and metadata; there is no generic method invocation.

ObjectRef/ValueRef must be made from typed lvalues. No public raw pointer/type
pair constructor or raw cast callback is exposed. Reads copy values; writes
check exact value type, object/base compatibility and mutability before invoking
an accessor. Read-only fields/getters have no typed setter. Private access is
limited to the explicitly authorized Access<T> specialization. Public ancestor
conversions, including unannotated gaps, use C++ casts, never offsets.

The MainThread authority captures the initialization thread; use the same
registry.Main() for component accesses. Do not construct a new authority on a
worker to relabel that worker as Main. Any metadata does not make an object's
storage thread-safe. Borrowed references neither own nor validate object
lifetime: use only for the current call while the object is alive, without
retaining component pointers across frames/scenes. These are native C++ APIs,
not an untrusted scripting boundary. DLL/plugin unloading and cross-DLL token
identity are outside R2's static-library contract.

Run the Windows toolchain acceptance without a GPU:

~~~powershell
python Tests/Reflection/Integration/verify_r2.py
~~~

This runs MSVC and Clang object compilation, static-library linking and executable
tests in all three configurations, and compiles the actual production generated
objects in both compilers. It also tests deterministic C++ bytes and invalidation
after failed analysis. It does not run the physical engine.

On a Windows machine with an OpenGL 4.5 driver, run full R2 acceptance:

~~~powershell
python Tests/Reflection/Integration/verify_r2.py --engine
~~~

The full test builds real SDK/dependency projects using x64 MSVC and executes
CPhysicalBox in default Main GL ownership and with the real RenderThread.
It observes real Bullet ray hits before/after Size changes, not just metadata
strings or test replacements for engine types. Hidden windows provide GL
contexts. Test-local MSBuild options disable file tracking, force source builds
and disable unrelated machine-wide vcpkg integration; these tests do not establish
incremental-build behavior. Each evidence directory retains compiler/link/run
logs and command receipts. CI runs the non-GPU suite; report local GL evidence
separately from remote CI results.

## R3 build and incremental contract

Generate VS2022 projects normally with gen_proj, or use:

~~~powershell
Dependencies/premake5/bin/premake5.exe --reflection-python=C:/Path/To/python.exe vs2022
msbuild Overload.sln /m /p:Configuration=Debug /p:Platform=x64
~~~

Without the Python option the project uses py -3. Python-driven export records
the currently running Python executable. Install the pinned LLVM development
archive once. A first solution build bootstraps the independent host tool using
CMake/Ninja; it does not require export, analysis or generated C++ to exist.

OvReflectionGen is a VS Makefile project, with no linked binary output. Its build
command invokes reflection.py ensure on every build. Both solution dependencies
and explicit ProjectReference entries order OvMaths, OvCore, OvEditor and OvGame
after it, including when building an individual .vcxproj. Generated filenames are
listed explicitly for all three configurations, with the other two excluded from
each compile. Applications still register types explicitly; R4 owns production
registration migration.

Baked configuration sidecars are written to Tools/OvReflectGen/Projects/Premake
(ignored, alongside generated projects), not Build/Generated. Deleting the whole
generated-output tree therefore does not break the next build. The exporter
records Lua project inputs; changing those requires gen_proj, as for ordinary
compiler option changes. A stale baked configuration fails with a specific
diagnostic instead of analyzing against old macros. Editing headers, annotations
or the scan header list does not require regenerating projects. Adding a new
module/project requires updating the Premake graph and regenerating projects.

The check hashes actual entered files, including indirect and system headers,
plus the selected compiler options, toolchain/Schema/version, generator binary
and source list, and scan manifest. It also records include-directory file names
to detect newly added shadowing headers and __has_include inputs that did not
previously exist. Each physical dependency is hashed once per invocation.
Unchanged modules reuse their validated Schema; changed modules are analyzed.
All module ownership/dependency relationships are then checked together.

A process-owned OS lock admits one writer per target/configuration. The host-tool
build has a separate global lock, so different configurations can scan concurrently
without racing on CMake/Ninja. Process termination releases locks automatically.
The driver writes native/emitter results into a private transaction directory,
validates input hashes, and atomically replaces each changed file. Module manifests
are published last, followed by generation-state.json as the configuration commit
marker. This is a build-gated multi-file transaction, not an atomic directory swap:
consumers must wait for a successful generator target. External programs reading
files mid-build without that gate are outside the contract.

Unchanged file bytes retain timestamps. Failed generation removes the commit
marker and owned success outputs and returns nonzero, stopping consumer compilation.
Only flat files named by a module manifest are eligible for stale cleanup; paths
that escape the owning directory, directories and symlinks are rejected. Unowned
neighbor files survive. Temporary output cleanup is confined to the transaction
directory allocated by that invocation. The clean action uses the same lock and
ownership rules. Direct native CLI analysis remains available for isolated tests.

~~~powershell
python Tests/Reflection/Integration/verify_r3.py --fixtures-only
python Tests/Reflection/Integration/verify_r3.py
python Tests/Reflection/Integration/verify_r3.py --full-solution
~~~

The default suite runs the complete invalidation/concurrency matrix and real MSBuild
module builds. --full-solution additionally builds Editor/Game and all dependencies
in Debug/Release/Publish. It temporarily edits and restores the production Box
annotation to measure recompilation and failure blocking; run it without concurrent
source edits or other builds. Old generated outputs and the host executable are
moved into the evidence directory before the clean build. It retains normal
MSBuild tracking, uses no ForceRebuild, and records object hashes and timestamps.
For a full compiler-object clean as well, run msbuild Overload.sln /t:Clean for
the selected configuration, followed by /t:Build and a no-change /t:Build. The
R3 local acceptance record includes this additional Debug check against the
exact current solution ClCompile object set.
On restricted desktop environments it supplies missing standard Windows directory
variables and a writable temporary directory, and disables MSBuild node reuse.
It does not change system settings or compiler tracking behavior.

The fixture suite runs real LibTooling in an isolated miniature source tree; it
covers same-size/same-timestamp edits, direct/indirect headers, macros, new optional
includes, rename/delete, scan-list changes, tool/Schema version changes, missing or
corrupt outputs, bounded stale cleanup, analysis failure/recovery, concurrent
same/different configuration writers and killed-writer lock recovery. Production
MSBuild evidence separately verifies clean bootstrap, no-op object retention,
metadata edits, failure before consumer compilation and parallel builds.

## Upstream references

- [LLVM 18.1.8 release](https://github.com/llvm/llvm-project/releases/tag/llvmorg-18.1.8)
- [LibTooling 18.1.8](https://releases.llvm.org/18.1.8/tools/clang/docs/LibTooling.html)
- [Premake Makefile projects](https://premake.github.io/docs/Makefile-Projects/)
- [MSBuild FileTracker implementation](https://github.com/dotnet/msbuild/blob/main/src/Utilities/TrackedDependencies/FileTracker.cs)
- [Premake action callbacks](https://premake.github.io/docs/newaction/)
- [Microsoft VS component identifiers](https://learn.microsoft.com/en-us/visualstudio/install/workload-component-id-vs-enterprise?view=visualstudio)

## R4 native component registration

The native component annotations now generate OvCore's shared Actor/Inspector
registry. The registry is initialized by the first Actor on Main, explicitly
registers/finalizes the reflection modules, and does not construct components
while registering. Transform uses existing-instance lookup; abstract bases are
queryable but not addable. The Inspector preserves the original 16 item names,
order and physical-family restrictions. Existing ComponentTraits remain checked
compatibility aliases and the archive output format. Unknown component XML is
retained across Actor/Scene/Prefab saves.

Run all three complete solution builds, no-change rebuilds, native policy
diagnostics and Clang-generated-object checks:

~~~powershell
python Tests/Reflection/Integration/verify_r4.py
~~~

For real component/scene/Prefab/lifecycle and Bullet/RenderThread tests on a
machine with OpenGL 4.5:

~~~powershell
python Tests/Reflection/Integration/verify_r4.py --engine
~~~

--skip-build is a development shortcut using already built libraries; final
acceptance must omit it. Engine tests use the normal baregl ResourceExecutor
in threaded mode, matching Editor/Game initialization. CI runs the non-GPU
variant; no remote success is implied by local results. See
CLANG_REFLECTION_R4_VALIDATION.md for evidence and R5 handoff.

## Generated collections and probe settings

Native value arrays/lists now share generated XML and Inspector operations:

~~~cpp
struct OV_TYPE(Record) Waypoint
{
    OV_FIELD(Serialize, Inspect, Step = 0.05) OvMaths::FVector3 position;
    OV_FIELD(Serialize, Inspect, Step = 0.1) float wait = 1;
};
// In an annotated type, with exact Access<T> friendship for private members:
OV_FIELD(Serialize, Inspect, ItemName = "item") std::array<float, 3> weights;
OV_FIELD(Serialize, Inspect, ItemName = "waypoint", MaxItems = 64)
std::vector<Waypoint> waypoints;
~~~

Fixed arrays require exactly their declared length in XML. Lists require an
explicit runtime MaxItems limit (1..4096). Supported elements include scalars,
strings, reflected enums, math values and Records. Inspector provides bounded
resize, add/remove/duplicate/reorder, 32-item pages and whole-property defaults
and clipboard. Multi-edit preserves untouched record leaves/math axes; mixed
lengths can be unified with Size. Collections use the existing scene history.
Nested collections, custom allocators and collection Script signatures fail
during Clang analysis. See Schema/PROTOCOL.md for the complete contract.

Reflection Probe now uses a flattened generated settings Record with its eight
legacy XML keys. Its batch setter rebuilds cubemap resources when resolution or
buffering changes; invalid power-of-two resolutions/enums fail before changes.
Capture Now and conditional local-influence controls remain native UI behaviors.

~~~powershell
python Tests/Reflection/Integration/verify_collection_schema.py
python Tests/Reflection/Integration/verify_record_schema.py
python Tests/Reflection/Integration/verify_resource_schema.py
node Tests/Editor/run-panel-validation.mjs
~~~

The Editor runner composes a real Clang-generated fixture module with production
descriptors only in its isolated validation executable. Production generated
outputs and shipped component menus contain no fixture components. It verifies
real scene factories, XML, Inspector operations, history and callback lifetimes
in both default and RenderThread modes, and saves GPU screenshots.
