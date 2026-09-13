# R6 Script bridge contract (ABI 1)

R6 generates native API wrappers. It does not host CoreCLR, replace Lua, schedule
C# Behaviours, or generate metadata for user managed classes. The acceptance host
runs the generated wrappers in a real CoreCLR process and loads a resident native
library; an engine host can pass the same entry point to its managed bootstrap.

## Host and lifetime

The application captures OvReflection::MainThread during Main initialization.
Construct Script::Context on that thread, using this authority. The authority is
copied by value; creating a new authority on a worker does not identify application
Main. Context is native-owned and non-copyable. Its numeric ID addresses a registry
of native contexts. Closing a Context removes its ID; subsequent ABI calls return
InvalidContext before accessing objects. The native DLL/runtime must remain resident
until all generated wrappers and native calls have been drained. Context registration
and function tables must share the same OvReflection runtime instance; the current
static-library build links them into one resident host image. Independently linking
separate copies of OvReflection into native plugin DLLs is not a shared registry.

Publish an object with its owner-provided weak lifetime epoch. IDs are monotonically
allocated 64-bit integers, never reused, including across contexts; zero is null.
Exhaustion fails rather than wrapping. Re-publishing the same live object and epoch
returns its existing ID. Expired entries are reclaimed on resolve/publish. Each
lookup first checks the captured Main authority, then the epoch and reflected C++
type/constness. Borrowed ObjectRef storage is never used before these checks.
Owners must invalidate their epoch on Main before deletion/removal and must not
concurrently destroy objects on workers. No object is owned or kept alive by a handle.

OvCore::Scripting::PublishComponent reuses AComponent's removal epoch (currently
named InspectorTicket). Actor already invalidates it before removal/destruction
notifications; scene replacement destroys the epochs. UI refresh does not revoke
component handles. Context shutdown and scene replacement are independent scopes.

Returned object pointers must designate a currently valid native object already
published in that context. The bridge translates them to handles; it never creates
ownership from an address. Returning an unpublished pointer fails. Native methods
remain responsible for maintaining valid native pointers: the bridge cannot infer
the historical identity of a dangling pointer returned by business code. Const
handles cannot be used for mutable arguments or setters.

These are interoperability/lifecycle checks for trusted in-process code, not a
sandbox against malicious native pointers or guessed numeric IDs.

## ABI and failures

Target: Windows x64, cdecl, pack 8, little endian. C headers use stdint types and
C-compatible POD declarations. bool is uint8 (only 0/1 accepted); integers retain
signedness/8/16/32/64-bit widths and never pass through double. Enums use their exact
underlying integer type and preserve unknown representable values. Float/double
preserve their native bit width. Vector3 uses three float members at 0/4/8, size 12;
Quaternion uses x/y/z/w at 0/4/8/12, size 16. Values are copied member by member;
engine C++ object layout, padding, and STL layout never cross the ABI.

A generated C function table contains release_string then typed property/method and
bulk-read entries in deterministic order. Each operation receives context ID and
object handle; inputs and outputs use call-scoped pointers to ABI values. Pointers
must designate readable/writable storage of the declared size. Calls are synchronous;
none retain caller buffers. Methods/setters execute once. Exceptions are caught at
the native boundary; no C++ exception unwinds into CLR. Output is defaulted on a
failed scalar call. Native business side effects are not transactionally rolled back.

Status codes: 0 Success, 1 InvalidContext, 2 InvalidHandle, 3 WrongType,
4 WrongThread, 5 ReadOnly, 6 InvalidArgument, 7 NativeException, 8 AbiMismatch.
Generated C# converts failures to BridgeException with Status. Bulk reads process a
prefix, stop on first failure, and return Completed; trailing outputs stay unchanged.
Bulk support in ABI 1 is fixed-width numeric and mathematical property reads. It is
not an implicit general container adapter or a transactional batch write facility.

Strings are strict UTF-8 with uint64 byte lengths (maximum INT_MAX); embedded NUL
is data. Inputs are pinned only for the call and copied to native std::string. A
null input pointer with zero length denotes empty; C# null string is rejected.
Malformed UTF-8 and unpaired UTF-16 surrogates fail. A returned UTF-8 buffer is
allocated by native code, decoded by C#, and released through that same table in
a finally block, including decode failure. release_string resets the supplied POD;
callers must not copy it and release the same allocation twice. No native allocator
or managed GC ownership is inferred from a C++ resource pointer.

## Annotations

Only properties/fields/methods explicitly containing Script receive ABI entries.
OV_TYPE alone and existing Serialize/Inspect capabilities do not export members.
Script calls always require Main; Thread = Any is rejected. No generated factory
constructs arbitrary business objects.

- Reference-returning Script getters/methods require Return = Copy. No borrowed
  native reference escapes to managed code. Mutable reference parameters and &&
  methods are rejected; value and const-reference parameters are accepted.
- Each annotated overload requires Overload = "<canonical signature>". The analyzer
  compares it with Clang's signature and reports the required value on mismatch.
  Distinct export names are required; the C++ emitter selects the exact member
  pointer type, including const, ref qualifier and noexcept.
- Object pointer positions require Ownership = Borrowed and Nullable = Allow or
  Deny. This member policy covers all object positions in that signature. Only
  pointers to complete reflected types are supported. Return = Copy is additionally
  required if the return is a reference to a pointer. No arbitrary void*, resources,
  containers, callbacks, transfer-of-ownership, or mutable reference inference.
- Script export names must be identifiers. C# uses qualified owner names and
  get/set/call suffixes; naming collisions including bulk entries fail generation.

## Matching and incremental publication

Each module emits Script.generated.h/.inc/.cs and script-abi.json alongside its
existing Reflection files. The .inc is compiled by the existing generated .cpp,
so bootstrap never depends on a newly discovered source glob. All new files are
owned by the same manifest and included in generation-state SHA256 receipts,
transactional staging, invalidation and content-stable publication.

OvScript_Get<Module>Api checks ABI version, exact table size and a SHA256 contract
signature before publishing any function pointers. C# performs this handshake in
Api's constructor. The contract includes operation identity/order, argument/result
canonical types and widths, enum values, reference/nullability/ownership/overload
policies, target, layouts and string/handle protocol. Source locations, local paths
and Debug/Release optimization do not alter ABI identity. A matching signature
proves interface compatibility, not identical business implementation.

The Context registry contains native State/ObjectRef/weak epochs only. Generated
C# Api instances hold native function pointers and numeric IDs; no delegates,
GCHandles, Assembly, Type or MethodInfo are registered in native process state.
Collectible AssemblyLoadContext tests load, execute and unload generated wrappers
repeatedly while the native library stays resident.

## Use and verification

Build the native configuration first; pass its resident
OvScript_GetOvCoreApi entry address and Script::Context::Id() to
Overload.Generated.OvCore.Api. Publish each component on Main and pass its numeric
handle to generated calls. Examples/ReflectionScript/Overload.NativeApi.csproj is
an SDK-style net8.0 project compiling both generated modules plus the shared bridge.
Use ReflectionConfiguration to select Debug, Release or Publish.

python Tests/Reflection/Integration/verify_r6.py --engine

The verifier builds all three native configurations with normal MSBuild tracking,
checks unchanged object/output hashes and mtimes, compiles C/C++ with MSVC and
Clang, and compiles managed acceptance programs using the installed VS Roslyn
compiler against an installed CoreCLR runtime. It supports --runtime and --dotnet;
this permits local validation without downloading an SDK or changing global tools.
--engine additionally drives real GL/Bullet components from generated C# wrappers
in default and RenderThread modes. CI omits the GPU-dependent engine option.

References: [C# unmanaged function pointers](https://learn.microsoft.com/en-us/dotnet/csharp/language-reference/unsafe-code),
[AssemblyLoadContext unloadability](https://learn.microsoft.com/en-us/dotnet/standard/assembly/unloadability).
