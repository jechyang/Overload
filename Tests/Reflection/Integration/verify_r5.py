#!/usr/bin/env python3
"""R5 native component registration, real MSBuild and scene/Prefab compatibility."""
import argparse
import hashlib
import json
import os
from pathlib import Path
from evidence import evidence_root
import sys
import tempfile
sys.dont_write_bytecode = True
from verify_r3 import Verification, snapshot, r, require, ROOT

PROJECTS = ["OvReflection", "OvAudio", "OvCore", "OvDebug", "OvMaths", "OvPhysics", "OvRendering", "OvTools", "OvUI", "OvWindowing", "assimp", "baregl", "bullet3", "freetype", "glfw", "ImGui", "lua", "soloud", "tinyxml2", "tracy"]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", nargs="+", default=["Debug", "Release", "Publish"], choices=["Debug", "Release", "Publish"])
    parser.add_argument("--skip-build", action="store_true", help="Use already built native libraries")
    parser.add_argument("--engine", action="store_true", help="Run GL/audio/Bullet scene behavior tests (requires OpenGL 4.5)")
    args = parser.parse_args()
    output = Path(tempfile.mkdtemp(prefix="r5-", dir=evidence_root(ROOT)))
    print("R5 evidence: " + str(output), flush=True)
    v = Verification(output)
    v.run("export", [sys.executable, r.TOOL / "reflection.py", "export", "--output", output / "Export"])
    tc = r.read_json(output / "Export/toolchain.json")
    compiler = Path(tc["msvc_root"]) / "bin/Hostx64/x64"
    v.env["INCLUDE"] = ";".join(tc["system_includes"])
    v.env["LIB"] = ";".join(str(p) for p in [Path(tc["msvc_root"]) / "lib/x64", Path(tc["sdk_root"]) / "Lib" / tc["sdk_version"] / "ucrt/x64", Path(tc["sdk_root"]) / "Lib" / tc["sdk_version"] / "um/x64"])
    prior = next((v.env.pop(k) for k in list(v.env) if k.lower() == "path"), "")
    v.env["PATH"] = str(compiler) + os.pathsep + prior
    msbuild = Path(tc["vs_root"]) / "MSBuild/Current/Bin/amd64/MSBuild.exe"
    v.run("premake", [ROOT / "Dependencies/premake5/bin/premake5.exe", "--reflection-python=" + sys.executable, "vs2022"])
    negatives = {
        "missing-policy": ('struct OV_TYPE() Bad { OV_FIELD(Serialize, Missing = Guess) int value; };', "Missing must"),
        "missing-default": ('struct OV_TYPE() Bad { OV_FIELD(Serialize, Missing = Default) int value; };', "requires an explicit Default"),
        "invalid-default": ('struct OV_TYPE() Bad { OV_FIELD(Serialize, Missing = Default, Default = "999") unsigned char value; };', "Default does not match"),
        "invalid-enum-default": ('enum class OV_TYPE() E : unsigned char { A }; struct OV_TYPE() Bad { OV_FIELD(Serialize, Missing = Default, Default = "256") E value; };', "Default does not match"),
        "invalid-step": ('struct OV_TYPE() Bad { OV_FIELD(Inspect, Step = 0) float value; };', "Step must be positive"),
        "invalid-range": ('struct OV_TYPE() Bad { OV_FIELD(Inspect, Min = 2, Max = 1) float value; };', "Min must not exceed"),
    }
    for name, (source, diagnostic) in negatives.items():
        directory = output / "Negative" / name
        directory.mkdir(parents=True)
        scan = directory / "bad.cpp"
        scan.write_text('#include <OvReflection/Annotations.h>\n' + source + '\n', encoding="utf-8")
        entry = r.read_json(output / "Export" / r.TARGET / "Debug/OvMaths/compile_commands.json")[0]
        entry["file"] = scan.as_posix()
        entry["arguments"][-1] = scan.as_posix()
        r.write_json(directory / "compile_commands.json", [entry])
        log = v.run("negative-" + name, [ROOT / "Build/Tools/OvReflectGen/Release/OvReflectGen.exe", "-p", directory, "--source-root=" + ROOT.as_posix(), "--module=Fixture", "--configuration=Debug", "--report=" + (directory / "analysis-report.json").as_posix(), scan], success=False)
        require(diagnostic in log and ("bad.cpp:" in log or "bad.cpp(2," in log), "Missing diagnostic/source location")
    def source_receipt():
        return {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                for folder in ["Sources", "Tools/OvReflectGen", "Tests/Reflection"]
                for p in (ROOT / folder).rglob("*")
                if p.is_file() and p.suffix in [".h", ".cpp", ".inl", ".lua", ".py", ".json"] and "Projects" not in p.parts}
    receipt = source_receipt()
    r.write_json(output / "source-receipt.json", receipt)
    for config in args.config:
        build = output / config
        build.mkdir()
        command = [msbuild, ROOT / "Overload.sln", "/nologo", "/m:2", "/nr:false", "/v:minimal", "/t:Build", "/p:Configuration=" + config, "/p:Platform=x64", "/p:PreferredToolArchitecture=x64", "/p:PROCESSOR_ARCHITECTURE=AMD64", "/p:VcpkgEnabled=false", "/p:VcpkgApplocalDeps=false", "/p:VCToolsVersion=" + tc["msvc_version"], "/p:WindowsTargetPlatformVersion=" + tc["sdk_version"]]
        if not args.skip_build:
            v.run(config + "/build", command)
            before = snapshot(ROOT / "Bin-Int" / config, "*.obj")
            generated_before = snapshot(ROOT / "Build/Generated/Reflection" / r.TARGET / config)
            v.run(config + "/no-change", command)
            require(snapshot(ROOT / "Bin-Int" / config, "*.obj") == before, "No-change build compiled objects")
            require(snapshot(ROOT / "Build/Generated/Reflection" / r.TARGET / config) == generated_before, "No-change build modified generated outputs")
        generated = ROOT / "Build/Generated/Reflection" / r.TARGET / config
        base = output / "Export" / r.TARGET / config / "OvCore"
        entry = r.read_json(base / "compile_commands.json")[0]
        options = [a for a in entry["arguments"] if a.startswith(("/I", "/D", "/U")) and a != "/DOV_REFLECTION_ANALYSIS=1"]
        common = ["/nologo", "/std:c++20", "/EHsc", "/MDd" if config == "Debug" else "/MD", "/Od" if config == "Debug" else "/O2", "/I" + ROOT.as_posix(), "/I" + generated.as_posix(), "/I" + (ROOT / "Dependencies/glfw/include").as_posix(), "/I" + (ROOT / "Dependencies/bullet3/src").as_posix(), *options]
        fixture = build / "Fixture"
        fixture.mkdir()
        scan = fixture / "Reflection.scan.cpp"
        scan.write_text('#include "Tests/Reflection/Fixtures/R5/Values.h"\n', encoding="utf-8")
        entry["file"] = scan.as_posix()
        entry["arguments"][-1] = scan.as_posix()
        entry["arguments"].insert(1, "/I" + ROOT.as_posix())
        r.write_json(fixture / "compile_commands.json", [entry])
        v.run(config + "/fixture-generate", [ROOT / "Build/Tools/OvReflectGen/Release/OvReflectGen.exe", "-p", fixture, "--source-root=" + ROOT.as_posix(), "--module=Fixture", "--configuration=" + config, "--report=" + (fixture / "analysis-report.json").as_posix(), scan])
        schema = r.read_json(fixture / "schema.json")
        # The scan includes production dependencies; own only this fixture's types.
        r.emit_cpp(schema, fixture, types=[t for t in schema["types"] if t["cpp_name"].startswith("R5::")])
        common += ["/I" + fixture.as_posix()]
        libraries = [ROOT / "Bin" / config / project / (project + ".lib") for project in PROJECTS]
        system_libs = ["opengl32.lib", "dbghelp.lib", "ws2_32.lib", "winmm.lib", "imm32.lib", "version.lib", "user32.lib", "gdi32.lib", "shell32.lib", "ole32.lib", "oleaut32.lib", "advapi32.lib", "comdlg32.lib", "uuid.lib"]
        for tool, compiler_command in [("MSVC", [compiler / "cl.exe"]), ("Clang", [tc["clang"], "--driver-mode=cl"])]:
            objects = []
            for name, source in [("generated", fixture / "Reflection.generated.cpp"), ("properties", ROOT / "Tests/Reflection/Integration/property_validation.cpp")]:
                obj = build / (tool + "-" + name + ".obj")
                v.run(config + "/" + tool + "-" + name, [*compiler_command, *common, "/c", source, "/Fo" + obj.as_posix()])
                objects.append(obj)
            exe = build / (tool + "-properties.exe")
            v.run(config + "/" + tool + "-link", [compiler / "link.exe", "/nologo", "/OPT:REF", "/OPT:ICF", "/OUT:" + exe.as_posix(), *objects, *libraries, *system_libs])
            require("PASS:" in v.run(config + "/" + tool + "-run", [exe]), "Property behavior test did not finish")
        for name in ["ReflectionProperties", "ReflectionDrawer"]:
            v.run(config + "/clang-" + name, [tc["clang"], "--driver-mode=cl", *common, "/c", ROOT / ("Sources/OvCore/src/OvCore/Helpers/" + name + ".cpp"), "/Fo" + (build / ("clang-" + name + ".obj")).as_posix()])
        editor_options = ["/I" + (ROOT / "Sources/OvEditor/include").as_posix()]
        inspector_obj = build / "inspector-test.obj"
        v.run(config + "/inspector-compile", [compiler / "cl.exe", *common, *editor_options, "/c", ROOT / "Tests/Reflection/Integration/inspector_validation.cpp", "/Fo" + inspector_obj.as_posix()])
        editor_archive = build / "Editor.lib"
        editor_objects = [p for p in (ROOT / "Bin-Int" / config / "OvEditor").glob("*.obj") if p.name != "Main.obj"]
        v.run(config + "/editor-archive", [compiler / "lib.exe", "/nologo", "/OUT:" + editor_archive.as_posix(), *editor_objects])
        inspector_exe = build / "inspector.exe"
        v.run(config + "/inspector-link", [compiler / "link.exe", "/nologo", "/OPT:REF", "/OPT:ICF", "/OUT:" + inspector_exe.as_posix(), inspector_obj, editor_archive, *libraries, *system_libs])
        require("PASS:" in v.run(config + "/inspector-run", [inspector_exe]), "Inspector test did not finish")
        if args.engine:
            for name, source in [("properties", "property_engine_validation.cpp"), ("components", "component_validation.cpp")]:
                obj = build / (name + ".obj")
                v.run(config + "/" + name + "-compile", [compiler / "cl.exe", *common, "/c", ROOT / "Tests/Reflection/Integration" / source, "/Fo" + obj.as_posix()])
                exe = build / (name + ".exe")
                libraries = [ROOT / "Bin" / config / project / (project + ".lib") for project in PROJECTS]
                v.run(config + "/" + name + "-link", [compiler / "link.exe", "/nologo", "/OPT:REF", "/OPT:ICF", "/OUT:" + exe.as_posix(), obj, *libraries, "opengl32.lib", "dbghelp.lib", "ws2_32.lib", "winmm.lib", "imm32.lib", "version.lib", "user32.lib", "gdi32.lib", "shell32.lib", "ole32.lib", "oleaut32.lib", "advapi32.lib", "comdlg32.lib", "uuid.lib"])
                for mode in ["default", "render-thread"]:
                    run_output = build / mode
                    text = v.run(config + "/" + name + "-" + mode, [exe, run_output, mode])
                    require("PASS:" in text, "Behavior test did not finish")
    require(source_receipt() == receipt, "Sources changed during verification; rerun on a fixed source revision")
    r.write_json(output / "verification.json", {"stage": "R5", "success": True, "engine": args.engine, "checks": v.checks})
    print("PASS: R5 evidence: " + str(output / "verification.json"), flush=True)

if __name__ == "__main__":
    try: main()
    except (OSError, ValueError, RuntimeError, KeyError) as error:
        print("R5 verification failed: " + str(error), file=sys.stderr)
        sys.exit(1)
