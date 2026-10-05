# Reflection protocol v1 (R1/R2/R3/R4/R5/R6 implementation)

This contract applies to implementation plan v1. R0's analysis-report.json
remains evidence, not public reflection metadata. R1 emits the separate
schema.json from explicitly annotated declarations only.

## Identity and names

- Generator schema version: 1. Generator version: 0.7.0. Business archive versions
  are independent of both.
- Only OV_TYPE(), OV_FIELD(), OV_PROPERTY() and OV_METHOD() opt declarations in.
  OV_TYPE is the sole class/struct/enum/enum-class entry point. The AST determines
  declaration kind; union is rejected in R1. R0's expected-type list is a scan
  smoke test, never implicit export authorization.
- A default type ID is its semantic, fully qualified C++ name, without a leading
  :: or class/struct/enum tag. Inline and nested namespaces remain part of the
  name. Typedef spelling is not a distinct underlying type identity. Anonymous
  or local types cannot acquire persistent identity from source coordinates.
- Id = "..." supplies a nonempty, case-sensitive, opaque UTF-8 identity.
  Renaming a type changes its default ID. Retain Id or add Alias = "old-id" to
  migrate. Alias may repeat for multiple historical IDs. An alias shares the
  lookup namespace with IDs and must be unique.
- A field's default name is its member name. A getter GetX/IsX yields X, retaining
  case; an empty suffix or another spelling requires Name. Name = "size" preserves
  an existing archive key. Default property ID is declaring-type-ID + "." +
  logical-property-name. This exact concatenation is checked for collisions;
  explicit Id resolves otherwise ambiguous names. Inherited declarations retain
  the declaring type's identity.
- Empty IDs/names, repeated scalar keys, duplicate IDs, alias conflicts and
  inherited property conflicts are errors with source locations.
- No std::hash, array index, Clang USR, RTTI name, line number, absolute path or
  member offset is a persistent identity.

## Access and generation

- No GENERATED_BODY and no injected members. Type lookup uses external
  Traits<T>/TypeOf<T>() declarations. A consumer needing typed lookup explicitly
  includes its module's Reflection.generated.h.
- Business headers include only fixed annotation/access authorization headers.
  Neither those headers nor Reflection.scan.cpp may include generated reflection
  output. Friend declarations remain present in both compilation modes.
- Private access requires explicit friendship to a fixed accessor specialization.
  Emitted C++ uses legal member expressions and conversions, never AST offsets.
- Components require explicit creation/lookup strategies; OV_TYPE does not imply
  default construction. Component getters/setters run on Main in the first version.
- Logical properties use existing getter/setter behavior. UI ranges do not
  silently become business validation rules. Annotation arguments never change
  members, bases, ownership or ABI between analysis and normal builds.
- R6 generates checked Script calls, C function tables and C# wrappers. The full
  ABI, object lifetime and CoreCLR boundary contract is in [SCRIPT_ABI.md](SCRIPT_ABI.md).

## Modules and files

The versioned scan-modules.json maps each module to its Premake project, explicit
headers and R0 expected complete types. One translation unit per module and
configuration avoids repeating the scan for every consumer.

All generated data lives under
Build/Generated/Reflection/<target>/<config>/<module>/.
Target is x86_64-pc-windows-msvc for the first implementation. Config is Debug,
Release or Publish. Stable outputs are schema.json,
Reflection.generated.cpp, Reflection.generated.h, Script.generated.h/.inc/.cs,
script-abi.json and manifest.json. R0 emits
Reflection.scan.cpp, compile_commands.json, analysis-input.json, diagnostics.txt
and analysis-report.json instead. Diagnostic bootstrap uses separate
syntax-diagnostics.txt and syntax-report.json.

Absolute paths are allowed in local compilation/environment provenance, never
in persistent IDs. AST source locations in reports are repository-relative.
Schema output sorts types/properties/methods by logical identity and aliases
lexically. Bases and enumerators retain declaration order. R1 collects actual
input buffer SHA256 hashes, including the scan TU, transitive headers and system
headers. dependencies.json contains local absolute paths for provenance, separate
from semantic Schema. input-summary.json additionally hashes the compilation
database, analysis-input.json, generator binary/sources, driver, toolchain lock
and scan manifest. Its aggregate digest is local to the selected environment;
it is not a persistent type ID or a relocatable cache key.

R3 managed generation is serialized per target/configuration and runs from the
Premake dependency graph. Native analysis writes only into a transaction directory.
Changed output files are atomically replaced; module manifests are written last,
then generation-state.json commits the complete configuration. Consumers may
compile only after the generation target succeeds. This does not provide an
atomic snapshot for arbitrary external readers during publication.

The cache checks actual transitive/system input hashes, search-directory file
inventories, selected baked options, scan list, generator binary/source inventory,
Schema/version lock and output receipts. Identical bytes preserve timestamps.
Failure invalidates the configuration marker and owned success files and fails
the build. Cleanup accepts only flat manifest-owned files within that module,
never parent paths, directories or symlinks. Baked options live beside generated
VS projects outside Build/Generated, allowing clean bootstrap without a cycle.
Project Lua changes require rebaking and stale exports explicitly fail.

The native CLI preserves its immediate invalidation behavior for isolated
diagnostic tests. Do not invoke it against a managed build's output directories.

## Implemented annotation grammar

Include `<OvReflection/Annotations.h>`. Define `OV_REFLECTION_ANALYSIS=1` only
in the Clang scan. The exported database records that define, the fixed header's
include directory, and warnings-as-errors for ignored/unknown attributes in
`analysis_only_options`, separate from the baked target configuration.
Normal MSVC/Clang compilation erases annotations. Do not put OV_TYPE before the
class/struct/enum keyword: the annotation belongs after the keyword (after
`enum class` for a scoped enumeration). No generated header is required.

~~~cpp
struct OV_TYPE(Id = "game.Position", Alias = "old.Position") Position
{
    OV_FIELD(Serialize, Inspect) float x;
    OV_PROPERTY(Setter = SetSize, Name = "size", Serialize, Inspect, Thread = Main)
    float GetSize() const;
    void SetSize(const float& value);
    OV_METHOD(Script, Thread = Main) int Count(int value) const & noexcept;
};

class OV_TYPE() Secret
{
    friend struct OvReflection::Access<Secret>;
    OV_FIELD() int value;
};
~~~

Arguments are comma-separated keys/flags, not C++ expressions. Empty argument
lists are valid. Unknown keys, repeated scalar keys, empty values and trailing
commas fail. Strings must be quoted; only `\"` and `\\` escapes are supported.
Identifiers and numbers are unquoted. No macro expansion of individual argument
tokens is promised: the annotation string preserves the written arguments.

| Target | Flags | Value keys besides Id/Alias/Name |
|---|---|---|
| Type | Component | DisplayName, Category, Order, Factory, Lookup, SerializeHook, DeserializeHook |
| Field | Serialize, Inspect, Script, ReadOnly | Thread, DisplayName, Category, Min, Max, Step, Drawer, Order, Missing, Default, ItemName, Return, Ownership, Nullable |
| Property getter | Serialize, Inspect, Script, ReadOnly | Field keys plus Setter |
| Method | Script | Thread, Return, Ownership, Nullable, Overload |

Id/Alias/Name/DisplayName/Category/Drawer/SerializeHook/DeserializeHook/Default/ItemName take strings. Alias may repeat, but its
values must be unique. Order/Min/Max/Step take finite numbers and are metadata
only. Setter/Factory/Lookup/Thread/Missing/Return/Ownership/Nullable take identifiers.
Overload takes a quoted canonical signature. R6 Script boundary policies require
Script and follow SCRIPT_ABI.md. Thread is Main or Any; Script always requires Main.
Type Name is rejected: the C++ name comes from AST, and DisplayName is the UI
label. An enum cannot use Component/Factory/Lookup. Factory/Lookup require
Component; Component requires real AComponent ancestry and exactly one of
`Factory = AddComponent` (concrete type) or `Lookup = Existing`. These record
explicit strategies; R1 does not emit or invoke factories. Component property
Thread defaults to Main and cannot be Any. R5 explicitly adds the property policies and type serialization hook IDs documented below.
Other engine policies and ABI adapters are not accepted implicitly.

Fields must be non-static, non-bitfield, non-reference data members. Const
fields imply read-only. A logical getter must be non-static, const, take zero
arguments, return a supported value/reference, and be callable on an lvalue.
It must specify either ReadOnly or Setter. Setter selects exactly one
non-template method **declared in the same type**, returning void, non-const,
callable on an lvalue, with one matching value or const lvalue-reference
parameter. Private/protected getters, setters and fields require friendship
to `OvReflection::Access<ExactDeclaringType>`; broad template friendship or a
different specialization is not sufficient.

Only public single non-virtual inheritance is accepted, including unannotated
ancestors. Marked local/anonymous/union/template types fail. Member annotations
require an OV_TYPE owner and must be written inside the defining class, not
introduced only on an out-of-class method definition. A forward annotation must resolve to a definition in
the scan; it does not register twice. Unsupported annotation targets fail.

Supported property/method value categories: bool, target integer types of
8/16/32/64 bits (including signed/unsigned char, excluding plain char/wchar),
float/double, std::string with standard traits/allocator, explicitly reflected
enums, and the named FVector3/FQuaternion adapters. R6 additionally accepts
Script-only reflected object pointers with explicit borrowed/nullability policies.
Other pointers, arrays, arbitrary records/templates/containers, volatile values and rvalue-reference value types
fail. Reference cv qualifiers are retained. Void is allowed only as a method
return. Static/deleted/variadic/template/operator methods, constructors,
destructors and implicit overload sets are rejected. R6 Script overloads require
an exact Overload signature and unique exported name. Method const/ref/noexcept qualifiers
and parameter types are modeled; R1 does not provide generic invocation.

## R2 C++ and runtime contract

- R2's Python emitter consumes the validated JSON and emits a module's fixed
  Reflection.generated.h/.cpp plus manifest.json. The manifest lists owned type
  IDs, generated-header dependencies and SHA256 content receipts. It is not an
  incremental cache or atomic publication guarantee.
- Schema retains annotated includes for semantic validation. Production
  generation owns declarations by Sources/<module>/; dependencies must have
  their own scan-module owner. There is no duplicate TypeOf specialization for
  the same C++ type merely because it is included in multiple scans.
- Traits<T>/TypeOf<T>() and generated Access<T> live outside business classes.
  The runtime has no LLVM, OvCore, XML or UI dependencies. Fixed primitive
  descriptors use Kind::Value and cpp.* IDs; their ValueType facts reside in
  TypeInfo::underlying. Generated enums preserve exact decimal enumerator values.
- Runtime TypeInfo::bases contains legal ancestor conversions, nearest first,
  including unannotated intermediate/ancestor types. These conversion entries
  do not register or expose members of unannotated types. Downcasts are rejected.
- MainThread captures the application initialization thread. Registry::Main()
  supplies that same authority to typed and erased operations. Component
  reads/writes reject other threads before calling business code. Any denotes
  absence of this affinity check, not automatic synchronization.
- ObjectRef/ValueRef are typed, borrowed lvalue views with private constructors.
  Exact C++ tokens, constness and generated legal base conversions guard erased
  access. Tokens are process-local static-library identities, never serialized.
  Reads copy into a typed mutable output; references returned by getters are
  not exposed as persistent handles. Native exceptions propagate normally.
- TypeDesc and PropertyDesc expose immutable information and checked operations.
  Generated storage has static lifetime. Registry batch registration validates
  global IDs/aliases, type identity and module ownership before mutation; explicit
  RegisterBuiltinTypes/RegisterOvMathsTypes/RegisterOvCoreTypes calls retain static
  library objects. Freeze closes initialization; callers synchronize publication
  before read-only sharing.
- R2 does not change Actor creation, ComponentTraits, Inspector, XML, Lua or
  component ownership. Factory/Lookup were subsequently implemented in R4. Method
  metadata stays descriptive in this generic descriptor API; R6 additionally emits
  strong C++ invocation helpers and checked Script ABI functions.

## Schema JSON shape

- Root: schema_version, generator_version, module, configuration, types.
- Every type: id, cpp_name, kind, aliases, source, metadata, bases, properties,
  methods. R2 adds cast_bases, the nearest-first public ancestor chain, including
  unannotated ancestors; bases remains the direct-base list. Kind is class/struct/enum/enum class. Enum types additionally contain
  underlying_type and declaration-ordered enumerators with decimal **string**
  values, preserving signed and unsigned 64-bit values without JSON rounding.
- Every property: id, cpp_name, name, aliases, source, metadata, type, strategy
  (field/property), access, read_only, getter, setter. Empty getter/setter
  strings mean that access path does not apply. Inherited properties remain on
  the declaring type; consumers follow bases.
- Every method: id (default `type-ID.export-name`), cpp_name, name, aliases,
  source, metadata, signature, return_type, parameters, const, ref_qualifier,
  noexcept, access. Parameters contain name and type.
- Type reference: display (written typedef spelling), canonical (qualified
  desugared signature), value_type (canonical, unqualified, non-reference),
  category, bits, signed. bits/signed describe builtin scalar types; enum
  storage is described on the enum type, and references do not store offsets.
- Source: path relative to source root, line, column. Metadata values are
  strings; flags use `"true"`. Aliases are a dedicated sorted string array.

Type, property and method IDs/aliases share one lookup namespace within the
module Schema. Property names must also be unique along an inheritance chain.
Duplicate diagnostics identify the second declaration and name the first
source location. Cross-module registry ownership and collision rejection at
registration belong to R2/R3; R1 does not create a global runtime registry.

## R4 native component adapter

- OvCore scans 19 explicitly annotated native component types: 17 concrete types
  and the abstract CLight/CPhysicalObject bases. The generated OvCore C++ unit
  provides GetOvCoreComponents(), an explicit static-library linkage root.
  OvCore's fixed ComponentRegistry registers builtin, OvMaths and OvCore types,
  freezes the reflection registry, and publishes immutable component descriptions.
  OvReflection itself still has no Actor, UI, XML, or LLVM dependency.
- OV_TYPE metadata is authoritative for discovery, identity/aliases, display name,
  numeric Order and Factory/Lookup. Existing constexpr ComponentTraits strings
  remain a compatibility API and the archive writer; generated static_asserts
  require each to match an annotated alias so they cannot silently diverge.
  The legacy strings are never scanned to discover components or build the menu.
- Factory = AddComponent calls Actor::AddComponent<T>(); its fixed typed adapter
  requires public Actor& construction. Lookup = Existing returns an existing
  concrete instance (Transform), never constructs it. Abstract lookup descriptions
  remain useful for inheritance reflection but return null for archive creation.
- Inspector uses the generated ordered descriptions. Addability preserves exact
  per-type uniqueness and the existing CPhysicalObject-family exclusion. Archive
  creation preserves the old AddComponent semantics, including different physical
  types in one archive. These are deliberately separate operations.
- Actor initializes the native registry on application Main before CreatedEvent.
  Factory/lookup/addability operations use that registry's Main authority, reject
  other threads before touching the Actor, and store no Actor/component pointers.
  Read-only identity/descriptor queries remain available to worker/render threads.
- Actor deserialization resolves both canonical IDs and aliases. Serialization
  continues to write the original ComponentTraits names and existing component
  payloads. Unknown types, abstract entries and entries missing type/data retain
  their complete component XML nodes independent of the input document lifetime.
  Saves append those opaque nodes after known components, retaining XML structure,
  attributes, comments and CDATA (not original byte-level whitespace/quote style).
  Repeated deserialization replaces the opaque set rather than duplicating it.
  Scene and Prefab operations inherit this preservation through Actor serialization.
- Transform remains first/unique and is excluded from the add menu. Erased and
  base-template removal paths now also refuse to remove it; the old erased path
  could otherwise leave Actor::transform dangling. Arbitrary direct mutation of
  GetComponents() is outside the adapter's contract.
- R4 does not migrate property drawers, property XML formats or Lua bindings.
  Existing component constructors, callbacks and serializers remain in charge.

## R5 property adapters

- OvReflection adds owned, exact-type value copies through PropertyDesc::ReadValue,
  WriteValue and ValidateValue using std::any. Enum copies contain exact decimal
  strings, with generated underlying-type range checks before writing. Existing
  typed and ValueRef accessors remain available. The runtime still depends only
  on the standard library; no XML, UI, Actor, math or LLVM dependency is added.
- OvCore Helpers/ReflectionProperties and ReflectionDrawer consume immutable
  descriptors and capability flags. Inherited properties are visited once; Order
  controls display/archive order. Integer XML and text controls never convert
  through floating point; bool, float/double, strings, enum choice controls,
  FVector3 and FQuaternion are supported. Enum values outside the declared choices
  but within the underlying range survive XML; UI shows an unknown choice without
  rewriting it. XML-incompatible control characters fail instead of truncating.
- Missing = Keep (also the absence of Missing) leaves absent fields unchanged.
  Missing = Error reports a missing required value as Invalid. Missing = Default
  requires a quoted Default and is the only policy that permits it. Defaults are
  explicit literals: scalar text, raw string (including empty), or comma-separated
  3/4 floats for vector/quaternion values. Enum defaults are decimal underlying
  values. Invalid defaults/ranges, nonpositive Step and Min > Max fail with source
  diagnostics. No component constructor is executed to infer archive defaults.
- Present/defaulted, missing-kept, read-only, invalid, unsupported, access-denied
  and expired states are distinct. Read-only fields serialize but are skipped on
  load and rendered disabled. Canonical archive keys win over aliases. The XML
  adapter stages all parsing and enum/type validation before setters. Individual
  setter exceptions propagate; arbitrary business side effects are not rolled back.
  Component void serializer entry points call Report::Require to propagate failures.
- Box.Size keeps base physical serialization and Inspector controls, the size XML
  key, UI bounds/step, and the existing SetSize Bullet side effects. The legacy
  missing size default is zero, including missing vector coordinates. UI limits
  never clamp XML or direct reflection writes.
- Transform has position, rotation, scale descriptors with Main access and ordered
  controls. Archives keep quaternion x/y/z/w. Drawer = "EulerAngles" performs the
  existing degree Euler/quaternion conversion. DeserializeHook = "TransformTRS"
  stages all values then calls GenerateMatricesLocal once; malformed values leave
  the complete TRS unchanged. Legacy missing position/scale defaults are zero;
  rotation defaults to (0,0,0,1). In particular missing scale does not silently
  become the constructor's unit scale. Partial vector/quaternion nodes fill from
  these explicit defaults, matching the old Serializer's value-returning overloads.
- Drawer, SerializeHook and DeserializeHook contain only opaque string IDs.
  Hosts supply immutable spans of functions at adapter call sites. Unknown or
  unprovided hooks return Unsupported, never silently fall back. OvCore supplies
  EulerAngles and TransformTRS; complex components keep their handwritten paths.
  Custom hooks are responsible for their own semantics and callback lifetimes.
- Persistent UI Binding objects retain a checked callback and weak component epoch,
  not ObjectRef. Each invocation checks application Main and epoch validity before
  constructing a call-scoped ObjectRef. Actor removal invalidates the component's
  epoch before removal notifications, and Actor destruction invalidates all epochs.
  Individual controls also invalidate copied dispatcher callbacks when destroyed.
- Inspector invalidates view callbacks and disables existing descendant controls
  immediately on structural changes, then coalesces deferred refreshes. Focus epochs
  reject old refreshes after target/scene switches or panel destruction; view epochs
  also guard actor controls, removal/reorder and escaped picker callbacks. Default
  scheduling/selection notifications still use EditorActions; explicit callback
  injection permits testing the real panel without initializing an entire editor.
- R5 migrates only Box.Size and Transform. It does not introduce global component
  serialization, deep copying, script binding, dynamic plugin ownership or R6 work.

## R6 Script generation

See [SCRIPT_ABI.md](SCRIPT_ABI.md) for the implemented v1 C/C# contract, host
authority, weak epochs, typed calls, exact overloads, string ownership, ABI hash,
collectible assembly restrictions, example project and executable verification.
Box.Size and Transform position/rotation/scale opt in explicitly; TranslateLocal
is the production method pilot. R6 does not replace existing Lua or host Behaviours.

## Generated component resource adapters

- `ResourceReference<T>` is recognized semantically as `resource-reference` only
  for the supported Model/Material/Sound/Texture/Shader resource classes. Bare
  pointers do not become serializable handles. `std::array<ResourceReference<T>, N>`
  has category `resource-array`, requires `ItemName` (an ASCII XML identifier), and
  has compile-time capacity 1–4096. Other containers and element types fail.
- These adapters currently require Serialize without Script/Inspect; typed asset
  pickers supply the Inspector workflow and guarded callbacks. The emitter builds
  OvCore's immutable ValueCodec table from the actual property value types. XML
  and manager dependencies stay in OvCore, outside the standard-only runtime.
- Resource values retain an unresolved path alongside the borrowed manager-owned
  pointer. XML writes the current asset path or `?`; missing assets keep their
  identity across saves. Explicit assignment/clear removes the previous identity.
  Reference defaults permit only empty/`?`, never implicit resource loads.
- Array codecs preserve holes and indices, trim only trailing empty references,
  reject unexpected child names/non-whitespace text/overflow, and clear all slots
  for an explicit empty list. Missing = Keep preserves the complete previous list.
  Input identities are bounded to 4096 bytes and reject control characters.
- Stage validates the complete XML batch before any resource lookup. Apply resolves
  all resource values before invoking any property setter. Invalid input or lookup
  exceptions leave component properties unchanged; manager cache changes and
  arbitrary setter exceptions are not rolled back.
- Model, Audio, Material and SkinnedMesh components inherit the default component
  codecs with their legacy keys. SkinnedMeshState performs one final animation
  normalization/rebuild after the generated batch. Remaining custom native UI,
  probe and postprocess codecs are documented in SERIALIZATION.zh-CN.md.
