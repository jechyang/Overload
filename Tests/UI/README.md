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
