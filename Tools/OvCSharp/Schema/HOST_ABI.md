# C# Host ABI 1 and project contract

This contract was frozen in S0. S1 implements the resident host; S2 implements
project type discovery and backend-independent attachments. S3 implements runtime
instances, Owner and lifecycle dispatch. S4 implements field metadata, typed reads/writes,
Inspector overrides and scene/Prefab persistence. S5–S7 remain out of scope.
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

### S2 implemented subset

`create_session` accepts an absolute existing project assembly path (an empty path
still creates the S1 bootstrap session). Discovery considers only public, concrete,
closed `Overload.Behaviour` subclasses with a public parameterless constructor.
Invalid candidates are diagnosed and skipped. No user constructor or property
getter executes. A load or identity conflict publishes no session. Native builds a
candidate catalogue before replacing the prior catalogue, and rejects aliases
that would merge two already attached records on an Actor.

`Overload.ScriptCore.dll` is a resident shared contract. `[ScriptId("id")]` chooses
an explicit primary ID. `[ScriptAlias("old-id")]` and
`[ScriptAlias("Old.LogicalAssembly", "Old.Namespace.Type")]` define migration keys.
Attribute strings must be nonempty, valid UTF-8/XML text; comparisons are ordinal.
The discovered tuple is an automatic alias of an explicit ID. Duplicate aliases,
including self-duplicates and collisions with another primary key, fail the load.

Host ABI layouts/signature remain unchanged. `get_type_info` returns size 72,
flags 0, a nonzero token, stable_id (empty for a default tuple), the logical name,
metadata type name, alias count and field_count (zero in S2, supported field count in S4). Alias UTF-8 payloads encode
`I<byte-length>:<explicit-id>` or
`T<byte-length>:<logical-name><byte-length>:<metadata-type-name>`; lengths are UTF-8
bytes, not UTF-16 characters. These payloads are backend-local, length-delimited
keys, not source paths or assembly-qualified names. Type results are released with
`release_type_info`; alias results use `release_owned_utf8`.

S2 native attachments retain only persistent identities after closing the discovery
session; no expired tokens or managed Type objects are cached in native state.
The shared Script interface adapts Lua/Null and a CSharp type attachment.
`TypeReady` explicitly differs from `InstanceReady`: CSharp objects are not yet
constructed or scheduled. Owner injection, Prepared instances and runtime session
ownership are introduced together in S3. S2 releases its collectible discovery ALC
but makes no claim of proven collection, shadow-copy or hot reload (S6).

Scene XML keeps the legacy Lua `<type>relative/path.lua</type>` and adds
`<script_identity backend="csharp" assembly="GameScripts" type="Game.Player" id="..."/>`
for other backends. The complete Behaviour node and its data subtree are retained
for missing backends/types, including unknown attributes, comments and fields.
Malformed/duplicate records remain opaque. Resolved aliases serialize the current
primary; session/type/instance numbers are never written. XML tree content is
preserved; whitespace/quote style is not a byte-for-byte XML file guarantee.

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

### S3 implementation and S4 preparation

The native facade retains the successful project session and its type/field tokens until
project clear/close. Loading and attachment run no user constructor. S4 prepares the
attached object when first inspecting it or before Play: `create_instance` checks
Context/Owner GUID, runs the constructor with native access forbidden and injects
Owner. The same instance supplies field defaults and later receives Awake; no
temporary default-probing object is created. A Prepared edit object has no gameplay
callbacks (including Destroy if it never reached Awake). Scene Play prepares all
attachments before any Awake. Runtime Prefab batches restore GUID mappings and
prepare every attachment before the batch's Awake/Enable/Start phases.
No property getter is queried. Async lifecycle methods are rejected during discovery.

Host ABI 1 and R6 signatures/layouts are unchanged. The S3 owner object ID is an
R6-published mandatory `CTransform` lifetime anchor, not an Actor address or GUID.
The resident `ConfigureGameplay` transport is documented in `BOOTSTRAP_SERVICES.md`.
It resolves that exact type under R6 `Guard` before obtaining the native owner.
Transform and Box properties use the existing generated R6 wrapper. Managed
facades additionally check Main, construction restrictions and session closure on
every access, including cached component facades. Collision/trigger payloads use
the other Actor's validated anchor and expose only its read-only GUID.

Native `TypeReady` remains the metadata state; Behaviour reports `InstanceReady`
after successful construction, or `Faulted` after constructor/callback failure.
Every instance lookup validates ID plus session generation. Awake/Start are once
only, Enable/Disable are transitions, gameplay requires Started+Enabled, and
Destroy is attempted once for awakened instances, including faulted instances.
Destroy exceptions are logged and reference cleanup continues. Reload migration,
shadow copies and proven ALC collection remain S6 work.

Actor dispatch uses snapshots of component references and Behaviour keys plus
weak epochs. Behaviour order is now attachment/display order. No container iterator
survives a callback. Structural operations are deferred by the common Main callback
barrier; pending removal skips remaining callbacks. Actor storage is collected at
scene phase/manager safe points. Native creation may allocate the returned Actor
immediately (the existing C++ API returns a reference), but gameplay initialization
is queued and snapshots keep it out of the in-progress phase. C# AddBehaviour is
queued; new scripts Awake/Enable/Start before their first subsequent update.
Physics holds the barrier across the whole Bullet update, so queued actor/behaviour
and active-state changes cannot reenter the simulation. A requested scene load
stops the old scene immediately and replaces storage at `SceneManager::Update`.
Memory loads copy their document before deferral. Failed file loads resume the old
scene. Destruction/scene-stop allows validated reads, but rejects structural
mutations through the dying Owner. Reentrant raw Host ABI calls return Busy;
native project clear queues at the callback safe point.

Editor/Game use these same production Scene/Actor paths. Editor pause/step/stop are
the existing EditorActions modes; there is no second managed loop. See
`CS_SCRIPTING_S3_VALIDATION.md` for exact runtime coverage and limitations.

Creating an instance validates the session/type, allocates the managed object,
injects its session-scoped Owner facade, and returns `Prepared`. User constructors
must not access Owner/native APIs. Native restores supported fields before the first
`Awake`. No editing-only metadata/default-value query constructs an extra instance
or invokes property getters.

The runtime states are:

`Prepared -> Awakened -> Started/Enabled/Disabled -> Enabled/Disabled -> Destroying -> Destroyed`

`Awake` occurs once. Prepared objects released without Awake do not receive user
Destroy; their references are still released. `Enable`/`Disable` follow Actor active transitions and may occur
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

### S4 metadata and value rules

Only load-time discovery enumerates reflection metadata. Each project session owns
the cached `FieldInfo` objects and tokens. Field access uses those cached fields;
it never enumerates or invokes properties. Base-class private fields are included
only with `[SerializeField]`. Static, literal, readonly and `[NonSerialized]` fields
are excluded. Unsupported selected types produce `UnsupportedField` diagnostics.
`[FieldId("id")]` overrides the default managed field name; the managed name becomes
an automatic alias. `[FormerlySerializedAs("old")]` adds migration aliases. All
primary IDs and aliases must be unique per Behaviour hierarchy; conflicts fail the
candidate project load without replacing the previous project.

`get_field_info` returns size 80, flags/reserved 0, token, tag, auxiliary, ID, name,
display name and alias count. `get_field_alias` returns the alias string verbatim.
Enum auxiliary is the underlying integer **tag** (2–9), preserving width/sign;
enum writes check the exact underlying range. AssetRef value auxiliary is AssetKind
(0 any, 1 model, 2 texture, 3 shader, 4 material, 5 sound, 6 scene, 7 script,
8 font, 9 Prefab); its field metadata auxiliary is 0. Other auxiliary values are 0.
Values use the unchanged 48-byte tagged scalar+UTF-8 structure, flags 0. All integer
tags use their actual fixed-width union members and never pass through double.
Strings are strict UTF-8, including embedded NUL. Null strings return UnsupportedField.
ActorRef stores only a uint64 scene GUID (0 none); AssetRef stores kind and normalized
project-relative path. Missing references retain their identity. Paths reject absolute
paths, drive prefixes and parent traversal. Asset resolution checks kind and file
existence and does not instantiate/load arbitrary resource objects.

Every read result, even a scalar or empty string, owns a unique allocation receipt
in its `text` member (length may be zero). It must be released through
`release_field_value`, not `release_owned_utf8`. Field metadata must be released
through `release_field_info`. Composite release validates all members before freeing,
zeroes the structure, and rejects forged/modified/copied repeated releases. Calls
check Main, session generation, instance identity and field membership in its type.

### S4 XML and Inspector

The existing complete Behaviour/raw-data preservation remains authoritative. C# adds
`<managed_fields><field id="speed" tag="10" auxiliary="0">7</field></managed_fields>`
under `<data>`. IDs are attribute values, not XML element names. Integers and enums
are decimal text, floats use roundtrip-width formatting, Vector3/Quaternion use
comma-separated float components. String/AssetRef text is hex-encoded UTF-8 with
`encoding="utf8-hex"`, allowing XML roundtrip of NUL and XML control characters.
Only explicit edit overrides are written. Constructor defaults are captured once
from the attached Prepared instance and kept separately. Reset removes the override
and restores that captured default. Invalid/incompatible/unknown and duplicate
ambiguous records are retained. Successful alias restoration writes the current ID
and preserves extension attributes/children; no guessed numeric/type conversions.

Inspector controls edit through the same typed Host field writes as runtime tests.
Text inputs parse exact integer widths, math components and enum underlying values;
ActorRef uses its GUID and shows resolved/missing identity; AssetRef exposes path/kind.
Invalid input is diagnosed without changing the live field or saved override. UI
callbacks check both an attachment/Inspector token and a widget-owned token. They
expire on refresh, widget disposal, deserialization, backend removal and Behaviour destruction.

Scene/Prefab objects and their GUID map exist before field preparation. Prefab
cloning rewrites only internal tagged ActorRef GUIDs, including opaque records while
the assembly is missing; external GUIDs are retained. `ActorRef.Resolve(owner)`
performs guarded scene lookup on use, so Awake can resolve a later-created sibling
and deleted targets resolve missing without caching stale native pointers. Play
uses the prepared values and never copies runtime field mutations into edit overrides.
Editor Stop rebuilds the existing scene snapshot; next inspection/Play prepares a
fresh attached instance and restores the saved edit overrides. This is Play/Stop
persistence, not S6 hot reload or arbitrary object graph migration.

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
