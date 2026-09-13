# C# Host ABI 1 and project contract

This is the S0 contract for the future resident `Overload.ManagedHost`. It does not
start CoreCLR, load a user assembly, create a Behaviour, or change Lua dispatch.
The normative machine-readable files are `host-abi.json` and
`Sources/OvCore/include/OvCore/Scripting/CSharp/HostABI.h`. Host ABI 1 is versioned
separately from R6 Script ABI 1; equality in one handshake never implies equality
in the other.

## Target, bootstrap, and handshake

Host ABI 1 targets Windows x64, cdecl, pack 8, little endian, and 64-bit pointers.
The resident bootstrap assembly is `Overload.ManagedHost`. The fixed hostfxr lookup
target is type `Overload.ManagedHost.Bootstrap, Overload.ManagedHost`, method
`GetHostApi`, obtained as an `UnmanagedCallersOnly` entry. S1 must request the exact
ABI version, `sizeof(OvCsHostApi)`, and the 32 signature bytes. The bootstrap must
zero the output table before validation and publish no function pointer unless all
three values match. ABI 1 uses exact table size rather than a prefix-compatible
range; any layout change requires a new ABI version and signature.

The signature is SHA-256 over `host-abi.json` after removing only the
`signature_sha256` member, recursively sorting object keys by their Unicode key
text, preserving array order, and encoding compact JSON as UTF-8 without ASCII
escaping. The lowercase hex digest and its 32 decoded bytes in `HostABI.h` must
both match. Descriptive Markdown is not signed; every normative layout/code/order
change must first change the machine contract and therefore its digest.

`OvCsHostApi` is native-to-managed. R6 tables are managed-to-native and remain
module-specific. Native passes an R6 module entry through `bind_native_api`; managed
performs that module's existing version/size/signature handshake before accepting
it. Neither table contains the other table by value, and neither version number is
used to infer the other.

Every entry is synchronous, runs on the captured engine Main thread, catches all
managed exceptions, and returns an `OvCsStatus`. No exception crosses the ABI. On
failure, native immediately calls `take_last_error` on the same thread and logs the
script identity, Actor GUID, operation, and returned stack text. A failure before a
session exists uses the zero session handle. Taking the error consumes it; success
clears stale error state.

## Strings, buffers, and numeric handles

UTF-8 is strict and lengths are bytes in `uint64_t`; embedded NUL is data. A null
pointer is legal only for zero length. Input views are borrowed for the call and are
never retained. Output strings, field values, type info, and field info are owned by
managed code until their matching release entry is called exactly once. Release
zeroes the structure and also runs on Main. Copying an owned result and releasing
both copies is a `BufferOwnership` error.

Session IDs, generations, type/field tokens, and instance IDs are nonzero 64-bit
values, allocated monotonically and never reused in the process. Type and field
tokens are session-local and never serialized. An instance handle carries its
session generation; both pieces must match on every lookup. R6 native context and
object IDs keep the R6 weak-lifetime rules and are neither GCHandles nor addresses.

## ScriptIdentity

The persisted default identity is the tuple:

`backend / project assembly logical name / canonical type name`

For C#, the canonical type name is the namespace-qualified metadata name, including
the arity marker for a generic definition (open generic Behaviours are rejected).
The assembly logical name is the project contract name, not a file path, MVID,
version, culture, public-key token, or `AssemblyQualifiedName`. A source filename is
never part of the identity. An explicit stable ScriptId replaces the default tuple's
last two elements as the primary key but retains the discovered tuple as an alias.
All explicit IDs and aliases must be unique within a project session; ambiguity is a
hard `DuplicateIdentity` failure and the prior usable generation remains active.

Renames use explicit old-identity aliases. Aliases are read-only migration keys and
serialization always writes the current primary identity. Lua keeps its normalized
project-relative path identity readable. Backend is always stored, so an identical
Lua path and C# type text cannot collide. Missing type/assembly/backend records stay
opaque and round-trip unchanged; no token, instance ID, address, or generation is
persisted.

## Project sessions and ownership

A project session owns its collectible user `AssemblyLoadContext`, type and field
metadata, user object strong references, error state, and its monotonically assigned
generation. `ScriptCore`, generated NativeApi wrappers, and ManagedHost live in the
resident/default context and must not be loaded again from project dependencies.
Native owns only the session/instance numbers and Host ABI function pointers.

Session state is `Loading -> Ready -> Closing -> Closed`, with `Loading -> Faulted`
and `Ready -> Faulted` on a fatal load/contract error. Only `Ready` accepts queries,
instance creation, field operations, and lifecycle dispatch. Closing first rejects
new calls, drains the current synchronous call, destroys all instances once, clears
metadata/delegates, then releases the project ALC. `close_session` is idempotent only
for the same still-known handle; a stale/unknown generation returns
`SessionNotFound`. `hostfxr_close` releases a host handle and is not CLR unload.

## Instance lifecycle and dispatch

Creating an instance validates the session/type, allocates the managed object,
injects its session-scoped Owner facade, and returns `Prepared`. User constructors
must not access Owner/native APIs. Native restores supported fields before the first
`Awake`. No editing-only metadata/default-value query constructs an extra instance
or invokes property getters.

The runtime states are:

`Prepared -> Awakened -> Started/Enabled/Disabled -> Enabled/Disabled -> Destroying -> Destroyed`

`Awake` occurs once. `Enable`/`Disable` follow Actor active transitions and may occur
around `Start`; `Start` occurs once before the first update. Update, fixed update,
late update, collision, and trigger events require Started+Enabled. `Destroy` is
attempted at most once from every non-destroyed state. A user callback exception
makes only that instance `Faulted`; further gameplay callbacks are skipped, but the
single destroy cleanup attempt and host reference release still occur. `ReloadRestore`
is reserved for S6 and is illegal until a new instance has passed S6 compatibility
preflight.

The existing Scene/Actor loop remains authoritative. Calls made during a callback
that add/remove Behaviour or Actor, change active state, switch scene, close a
session, or apply a compiled generation are queued. The Main-thread safe points are
after the current callback and before the next lifecycle item, and after each phase
(FixedUpdate, Update, LateUpdate) before beginning the next phase. Once queued for
removal, an instance receives no later gameplay callback in that phase.

Destroy order is fixed: mark instance Destroying and remove it from dispatch;
invoke managed `Destroy` once while its validated Owner/component handles are still
live; catch/log errors; clear managed references; invalidate native component/Actor
epochs on Main; remove native objects; then collect the project ALC when the whole
session closes. Scene replacement first stops dispatch for the old scene and drains
callbacks, then performs this order for its instances before destroying native scene
storage. This does not extend an epoch beyond native object destruction.

## Field matrix for the first implementation

| Managed field | ABI tag | Representation | S0 decision |
|---|---|---|---|
| `bool` | Bool | uint8, only 0/1 | Supported |
| `sbyte`/`byte` | Int8/UInt8 | exact 8-bit | Supported |
| `short`/`ushort` | Int16/UInt16 | exact 16-bit | Supported |
| `int`/`uint` | Int32/UInt32 | exact 32-bit | Supported |
| `long`/`ulong` | Int64/UInt64 | exact 64-bit, never double | Supported |
| `float`/`double` | Float32/Float64 | exact IEEE width | Supported |
| `string` | String | strict UTF-8 | Supported; null rejected in ABI 1 |
| enum | EnumInt64/EnumUInt64 | exact underlying width in `auxiliary` | Supported |
| Vector3 | Vector3 | x/y/z float members | Supported |
| Quaternion | Quaternion | x/y/z/w float members | Supported |
| ActorRef | ActorRef | scene GUID, zero is none | Supported |
| AssetRef | AssetRef | asset type in `auxiliary`, normalized project path in text | Supported |
| arrays, nullable, object graphs, properties, static/readonly fields | none | none | Rejected with `UnsupportedField` diagnostic |

Metadata includes public instance fields and explicitly annotated private instance
fields; static, readonly, `[NonSerialized]`, and properties are excluded. Field IDs
and aliases are stable strings, separate from managed names and session-local tokens.
Unknown or unsupported serialized fields remain opaque for later round-trip rather
than being discarded. Editor defaults and explicit overrides stay distinct; runtime
field changes are not written back automatically.

## Toolchain and execution boundary

`Tools/OvCSharp/toolchain.lock.json` fixes SDK 10.0.401, runtime 10.0.12 and the
official Windows x64 archive SHA-512. `global.json` disables SDK roll-forward.
`Overload.ManagedHost.runtimeconfig.json` disables runtime roll-forward. Restore for
the S0 smoke uses the checked-in empty dependency lock and a NuGet configuration
with all remote sources cleared; it therefore proves the SDK's included net10.0
reference/runtime packs rather than downloading packages.

Install explicitly with:

`node Scripts/Windows/InstallCSharpToolchain.mjs`

Validate with:

`node Tests/Scripting/Integration/verify_s0.mjs`

The verifier prefers `Build/Toolchains/dotnet-10.0.401/dotnet.exe`, isolates CLI and
NuGet state under ignored `Build/Tests/Scripting`, validates x64/version/runtime and
both ABI signatures, performs a locked restore and runs the compiled managed DLL.
It first attempts the normal `dotnet build`. If an outdated Windows installation
rejects the SDK's CET-marked `csc.exe`, that command remains a recorded failed check;
the verifier invokes the same SDK's managed `csc.dll` through the already-running
`dotnet` host only to prove compiler/runtime execution. It does not call the failed
project build a pass or disable CET. A Windows update and a clean rerun are required
to clear that environment blocker. Normal Lua/native builds do not invoke the
installer or verifier and therefore do not download this SDK.
