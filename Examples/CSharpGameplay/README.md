# S3 minimal gameplay example

Build the native Debug configuration, then from the repository root:

```text
node Scripts/Windows/BuildCSharp.mjs --compat Sources/OvManaged/Overload.ManagedHost/Overload.ManagedHost.csproj --rebuild
node Scripts/Windows/BuildCSharp.mjs --compat Examples/CSharpGameplay/GameScripts.csproj --rebuild
```

SDK 10.0.401, runtime 10.0.12 and net10.0 remain locked. This is the explicit full
MSBuild compatibility path; standard dotnet build still fails with CET on the
recorded machine. Configure Editor/Game's existing optional C# host settings with
the actual managed output directories and logical assembly name `GameScripts`.

Programmatic attachment, after loading the project through ScriptEngine:

```cpp
auto& actor = scene.CreateActor("Mixed gameplay");
actor.AddComponent<OvCore::ECS::Components::CPhysicalBox>().SetMass(0);
actor.AddBehaviour(OvCore::Scripting::ScriptIdentity::CSharp(
    "GameScripts", "Gameplay.MoveAndResize"));
actor.AddBehaviour("observer.lua"); // existing project-relative Lua script
scene.Play();
```

`MoveAndResize.OnUpdate` moves local X by `2 * deltaTime` and grows Box.Size.X by
`deltaTime`. Owner.Transform and GetComponent<Box> use validated native handles;
there are no native addresses in the user API. `Overload.Maths.Vector3` uses X/Y/Z.

The S3 acceptance compiles this same source alongside lifecycle fixtures, then
checks native Transform/Box values and the Lua interpreter in the same scene. It
also runs production Editor/EditorActions and Game scheduling in both rendering
modes. Run `node Tests/Scripting/Integration/verify_s3.mjs --python <absolute Python 3>`.

No C# type picker, build UI, field editor/serialization, hot reload, or deployable
runtime packaging is included (S4–S7).
