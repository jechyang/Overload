# Generated native API bindings

R6 produces C ABI and C# wrappers for explicit Script annotations. See
[the bridge contract](../../Tools/OvReflectGen/Schema/SCRIPT_ABI.md).

1. Generate VS projects and build the desired native configuration as usual.
2. With a .NET 8 SDK, run `dotnet build Examples/ReflectionScript/Overload.NativeApi.csproj -p:ReflectionConfiguration=Debug`.
3. The native host creates `OvReflection::Script::Context` using the application's
   MainThread authority, publishes a component through
   `OvCore::Scripting::PublishComponent`, and supplies the context ID, component
   handle and resident `OvScript_GetOvCoreApi` entry address to managed code.

```csharp
var api = new Overload.Generated.OvCore.Api(entryPoint, contextId);
api.OvCore_ECS_Components_CPhysicalBox_set_size(boxHandle,
    new Overload.Script.Vector3 { x = 2, y = 3, z = 4 });
```

The constructor verifies the ABI signature before accepting function pointers.
The native library must remain loaded for the lifetime of these wrappers. Component
removal, scene replacement and Context shutdown invalidate their corresponding IDs.
The wrappers own neither native components nor the native session.

This project packages native API wrappers only. Engine CoreCLR hosting/bootstrap,
C# Behaviour metadata/lifecycle and Lua replacement are separate integration work.
The executable acceptance examples are under Tests/Reflection/Integration:
ScriptValidation.cs and ScriptEngineValidation.cs. The verifier uses the installed
VS C# compiler plus a real CoreCLR runtime and requires no downloaded packages.
