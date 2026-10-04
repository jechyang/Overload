# UI validation

Build the matching native libraries first, then run:

    node Tests/UI/run-validation.mjs Debug --managed --scripting-regression

Without --managed, only native rendering/rectangle tests run. The managed option builds the real ScriptCore and ManagedHost using the repository's explicit Windows CET compatibility build, compiles project fixtures and runs them in the native CLR host. The optional scripting regression reuses the existing S3 fixture in default and render-thread modes.

Fresh evidence is written to Build/UIValidation/Debug-* including build logs, result.json, serialized scenes and native RGBA output. Managed UI tests fail on engine error logs as well as failed assertions.

Checks cover:

- Native anchors, clipping, group filtering, resource lifetime and exact default/RenderThread pixels.
- Overlay, screen-camera and world-space Canvas geometry, projected hit testing, render-pass routing and GPU pixels.
- Project C# layout overrides reaching production rendering; real pixel checks.
- Project C# Graphic overrides, VertexHelper/UIVertex mesh upload, per-vertex color, UV/texture sampling, and basic Image/RawImage pixels.
- Mesh-only OverlayUI and SceneUI passes, including screen-camera/world-space pixels and immutable texture generations.
- User ILayoutElement implementations, priority, fallback, weighted sizing and alignment.
- Managed vertical layout and ContentSizeFitter; self controllers before layout groups.
- Managed scaler modes, virtual DPI, physical units and viewport resize.
- User ICanvasElement callbacks after layout.
- Inspector-authored fields through XML roundtrip and hot reload preserving custom state.
- Component epoch invalidation, Main-thread checks and clean scene/session teardown.
- The shipped Examples/ManagedUI scene using its actual project classes.
- First edit-mode frame after deserialization, without Inspector preparation or Play; UI components and interface implementations initialize before rendering, while gameplay remains lazy and Awake runs only on Play.
- Existing C#/Lua lifecycle regression when --scripting-regression is requested.

See [the compatibility map](UGUI-COMPATIBILITY.zh-CN.md) for the pinned upstream reference, module ownership and remaining differences. No automated Unity-vs-Overload differential certification is claimed.

The complete pinned runtime inventory is [ugui-runtime-matrix.json](ugui-runtime-matrix.json).
It covers 84 source files and 178 declarations, including 148 publicly accessible
types, nested enums/delegates, obsolete interfaces and internal helpers. Existing
baseline implementations remain `partial`; `pending` denotes unimplemented work.
Neither status is a compatibility claim.

    node Tools/UI/reference-ugui.mjs --check
    node Tools/UI/reference-ugui.mjs --check --complete
    node Tests/UI/run-validation.mjs Debug --matrix-only
    node Tests/UI/run-validation.mjs Debug --u0 --managed --scripting-regression

`--matrix-only` checks the pinned inventory and negative schema tests without a
native compiler or GPU; its evidence explicitly says `scope: matrix-only` and
cannot substitute for runtime acceptance. `--u0` adds these checks to the native
and optional managed regression. `node Tools/UI/reference-ugui.mjs --check --complete`
is the U13 completion gate: it rejects any pending, partial or blocked type or
engine dependency, and requires a successful current-source receipt from
`node Tests/UI/run-completion-validation.mjs`. The receipt covers Debug/Release
foundation and scripting regressions, the dedicated UI suites, FlappyBird product
paths and editor camera checks. Classification alone cannot pass this gate.
TouchScreenKeyboard is `adapted` to the desktop character/IME
backend and is accepted by U11 input-controls.

U1 utility, fields and lifetime validation:

    node Tests/UI/run-validation.mjs Debug --managed --foundations --scripting-regression

This adds randomized IndexedSet mutations, pool ownership/reset checks, event
mutation and thread rejection, and real native-host tests for receiver/source
destruction, weak registries, three hot reloads and collectible project contexts.
`--u1` now invokes the codec and native foundation checks. All `--u0` through
`--u13` flags have runtime suite mappings; `--u13` runs the combined completion
suite. Most dedicated feature runners use Debug; the combined suite also runs
the core native/managed/scripting regression against Release libraries.

The input-controls suite verifies factory parent/child references, nested record
and array reference remapping during cloning, dropdown options and lifecycle,
Unicode limits/deletion, PIN masking, placeholder preservation, focus cleanup,
native pointer clicks, native Unicode/Backspace/Enter input and default/RenderThread
pixel agreement. Controls tests reject unchanged white for disabled ColorTint.
The native UI suite checks 120 static frames without geometry uploads and removal
while an older frame still retains its original immutable geometry.

When another task changes native headers, build a matching OvCore library in an
independent output directory and pass `--core-library=<absolute .lib path>`.
The override is recorded in evidence. It must match the current headers; this
option does not rebuild the library or make a stale library compatible.

## Flappy Bird acceptance

    node Tests/UI/run-flappy-validation.mjs --apps

Build matching Debug libraries and OvEditor/OvGame objects first. This loads the shipped FlappyBird scene and scripts, exercises native keyboard/mouse input through six pipe crossings and recycling, death/reset, focus loss, resize and hot reload, and compares Ready/Playing/Game Over GPU pixels between default and RenderThread modes. The optional --apps additionally links product Editor/Game objects to check Play/Pause/Stop/replay and standalone startup. Logs, source hashes, screenshots and result.json are saved under Build/FlappyValidation.
