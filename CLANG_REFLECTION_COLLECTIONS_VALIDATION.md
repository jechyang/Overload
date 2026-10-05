# Generated native collections and Reflection Probe

Date: 2026-10-05. Generator 0.9.0, Schema 1, LLVM 18.1.8, Debug/x64,
MSVC 14.42.34433 and Windows SDK 10.0.22621.0.

## Result

Reflection Probe's eight legacy fields now come from annotated Settings and
enum descriptors. The generated default component codec keeps the existing XML
keys and excludes framebuffers, textures, capture requests and capture history.
Loading resolution previously only changed a member, leaving GPU allocation at
the previous size. The batch settings setter now validates resolution before
mutation and rebuilds resources when resolution/buffering changes. Unchanged
resources retain their identity. Resolution choices, corrected Brightness label,
conditional local-influence controls and lifetime-guarded Capture Now remain.

Clang recognizes supported std::array/std::vector values from semantic template
arguments, records element types/capacity and rejects unsupported configurations.
The emitter produces typed collection operations and transitively includes Record
element adapters. Serialization validates every element on owned copies before
publishing a component, including exact enum decimals and vector<bool> proxies.
Record elements retain unresolved resource identities and defer resource lookup
until the complete staged batch validates.

Inspector collection controls support element editing, bounded Size/Add/Clear,
duplicate/remove/reorder, 32-item pages, mixed-value/length feedback, whole-value
copy/paste/reset and scene history. Multi-edit replays operations against each
target, preserving unrelated elements and unchanged Record leaves/math axes.
Structural changes immediately expire old element callbacks; child widgets are
rebuilt outside draw iteration. Main, component and widget lifetimes guard access.

## Scope

- Fixed arrays: 1..4096 items, exactly N items required in present XML.
- Lists: standard allocator and explicit MaxItems 1..4096; present empty XML
  clears the list, absent fields keep current values.
- Elements: supported scalars, strings, reflected enums, math adapters, Records.
  Partial Record/math items keep prior index fields or new-slot defaults.
- Collections reject nested collections, custom allocators, pointer/const/volatile
  elements, Script signatures and scalar range/validation/text-default metadata.
  Element Record fields can retain their own metadata and resource references.
- Min/Max are UI bounds; MaxItems is enforced by collection XML/UI operations.
  Probe resolution has an additional component guard (positive power of two,
  maximum 4096). Business setter exceptions retain the existing propagation
  contract; no general rollback of arbitrary setters is claimed.
- C# source-generated serialization remains covered by regression tests. This
  change adds native collection UI; C# collection UI unification is follow-up work.

## Validation

Commands executed:

```powershell
python Tools/OvReflectGen/reflection.py ensure --config Debug
node Build/GeneratedFields/build-native.mjs
python Tests/Reflection/Integration/verify_collection_schema.py
python Tests/Reflection/Integration/verify_record_schema.py
python Tests/Reflection/Integration/verify_resource_schema.py
node Tests/Editor/run-panel-validation.mjs
node Tests/Scripting/Integration/verify_generated_fields.mjs --native
node Tests/UI/run-layout-validation.mjs --runtime
```

Use Python 3.12 and Node 20 for these commands in this workspace; the system
default Python is older. build-native.mjs is an ignored local MSBuild driver.
Normal checked-in build integration remains Premake/MSBuild.

- Clang policies: collection 27, Record 23, resource 15; all pass. Covers real
  template classification, limits, item names, const/pointer/custom allocator/
  nested container rejection, Script boundaries and PowerOfTwo validation.
- Editor: 547 PASS lines in each of default and RenderThread. Real generated fixture components
  exercise scene factories, nested XML, lossless scalar/enum/string data,
  unresolved Record resources, malformed input atomicity, add/remove/duplicate/
  reorder, fixed/read-only limits, mixed values/lengths, multi-target leaf/axis
  preservation, clipboard/defaults, undo/redo, pagination/search and expired
  callbacks. Probe tests query actual OpenGL texture storage on its owner thread
  and check texture descriptions after load/edit/undo,
  unchanged allocation identity, invalid settings and conditional controls.
- C# generated field runtime: 30 assertions (31 PASS lines including completion).
  Native S4: 199 default / 200 RenderThread PASS lines.
- UI layout: 9145 assertions, native driven-property ownership/restoration,
  and 26 actual GPU frames with exact byte equality between render modes.
  Frame SHA256: 478d28c87cea52e8dca33b1579e9d3b7149e1ae1477dbb2f22096e33c9d470d4.

Evidence is isolated below Build/EditorPanels/Validation, Build/Tests/CollectionSchema,
Build/Tests/RecordSchema, Build/Tests/ResourceSchema, Build/Tests/GeneratedFields,
and Build/UGUI-D/layout-Ldslxj. Actual collection/Record screenshots were inspected.
The Editor fixture composes production and Clang-generated game-module descriptors
only in its test executable; shipped tables and menus do not contain test types.
Production source/descriptors are reused without editing managed generated outputs.

Local Debug executable is published to Build/Debug/OvEditor.exe from
Bin/Debug/OvEditor/OvEditor.exe and the two files are verified byte-identical.
SHA256: EFD6AD0B6B85B8B0B2F34955598791C10A40F315F8371E15CF1F6B96E9D67EE2.
Only this change's files are committed; existing workspace edits remain separate.
No push is performed.
