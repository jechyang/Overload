#!/usr/bin/env python3
"""R4 native component registration, real MSBuild and scene/Prefab compatibility."""
import argparse
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
    output = Path(tempfile.mkdtemp(prefix="r4-", dir=evidence_root(ROOT)))
    print("R4 evidence: " + str(output), flush=True)
    v = Verification(output)
    v.run("export", [sys.executable, r.TOOL / "reflection.py", "export", "--output", output / "Export"])
    tc = r.read_json(output / "Export/toolchain.json")
    compiler = Path(tc["msvc_root"]) / "bin/Hostx64/x64"
    v.env["INCLUDE"] = ";".join(tc["system_includes"])
    v.env["LIB"] = ";".join(str(p) for p in [Path(tc["msvc_root"]) / "lib/x64", Path(tc["sdk_root"]) / "Lib" / tc["sdk_version"] / "ucrt/x64", Path(tc["sdk_root"]) / "Lib" / tc["sdk_version"] / "um/x64"])
    prior = next((v.env.pop(k) for k in list(v.env) if k.lower() == "path"), "")
    v.env["PATH"] = str(compiler) + os.pathsep + prior
    baseline = r.read_json(ROOT / "Tests/Reflection/Fixtures/R4/legacy-components.json")
    expected_menu = [c["display"] for c in baseline if 0 <= c["order"] < 16]
    msbuild = Path(tc["vs_root"]) / "MSBuild/Current/Bin/amd64/MSBuild.exe"
    # Invalid component policies must fail with source diagnostics before C++ emission.
    negatives = {
        "abstract-factory": ('#include <OvCore/ECS/Components/CLight.h>\nclass OV_TYPE(Component, Factory = AddComponent) Bad : public OvCore::ECS::Components::CLight {};\n', "concrete type"),
        "missing-strategy": ('#include <OvCore/ECS/Components/CPhysicalBox.h>\nclass OV_TYPE(Component) Bad : public OvCore::ECS::Components::CPhysicalBox {};\n', "exactly one"),
        "invalid-lookup": ('#include <OvCore/ECS/Components/CPhysicalBox.h>\nclass OV_TYPE(Component, Lookup = Create) Bad : public OvCore::ECS::Components::CPhysicalBox {};\n', "Lookup must be Existing"),
        "legacy-collision": ('#include <OvCore/ECS/Components/CPhysicalBox.h>\nclass OV_TYPE(Component, Lookup = Existing, Alias = "class OvCore::ECS::Components::CPhysicalBox") Bad : public OvCore::ECS::Components::CPhysicalBox {};\n', "duplicate ID"),
    }
    for name, (source, diagnostic) in negatives.items():
        directory = output / "Negative" / name
        directory.mkdir(parents=True)
        tu = directory / "bad.cpp"
        tu.write_text(source, encoding="utf-8")
        entry = r.read_json(output / "Export" / r.TARGET / "Debug/OvCore/compile_commands.json")[0]
        entry["file"] = tu.as_posix()
        entry["arguments"][-1] = tu.as_posix()
        r.write_json(directory / "compile_commands.json", [entry])
        log = v.run("negative-" + name, [ROOT / "Build/Tools/OvReflectGen/Release/OvReflectGen.exe", "-p", directory, "--source-root=" + ROOT.as_posix(), "--module=Fixture", "--configuration=Debug", "--report=" + (directory / "analysis-report.json").as_posix(), tu], success=False)
        require(diagnostic in log and ("bad.cpp:" in log or "bad.cpp(2," in log), "Missing component diagnostic/source location")
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
        schema = r.read_json(generated / "OvCore/schema.json")
        components = [t for t in schema["types"] if "Component" in t["metadata"]]
        require(len(components) == 19, "Native component discovery count")
        for previous in baseline:
            current = next(t for t in components if t["cpp_name"].endswith("::" + previous["name"]))
            require(previous["legacy"] in current["aliases"] and current["metadata"]["DisplayName"] == previous["display"] and float(current["metadata"]["Order"]) == previous["order"], "Legacy component metadata changed")
        base = output / "Export" / r.TARGET / config / "OvCore"
        entry = r.read_json(base / "compile_commands.json")[0]
        options = [a for a in entry["arguments"] if a.startswith(("/I", "/D", "/U")) and a != "/DOV_REFLECTION_ANALYSIS=1"]
        common = ["/nologo", "/std:c++20", "/EHsc", "/MDd" if config == "Debug" else "/MD", "/Od" if config == "Debug" else "/O2", "/I" + ROOT.as_posix(), "/I" + generated.as_posix(), "/I" + (ROOT / "Dependencies/glfw/include").as_posix(), "/I" + (ROOT / "Dependencies/bullet3/src").as_posix(), *options]
        # A second real compiler checks generated descriptors and factory templates.
        v.run(config + "/clang-generated", [tc["clang"], "--driver-mode=cl", *common, "/c", generated / "OvCore/Reflection.generated.cpp", "/Fo" + (build / "clang-generated.obj").as_posix()])
        if args.engine:
            for name, source in [("components", "component_validation.cpp"), ("physical-box", "physical_box_validation.cpp")]:
                obj = build / (name + ".obj")
                v.run(config + "/" + name + "-compile", [compiler / "cl.exe", *common, "/c", ROOT / "Tests/Reflection/Integration" / source, "/Fo" + obj.as_posix()])
                exe = build / (name + ".exe")
                libraries = [ROOT / "Bin" / config / project / (project + ".lib") for project in PROJECTS]
                v.run(config + "/" + name + "-link", [compiler / "link.exe", "/nologo", "/OPT:REF", "/OPT:ICF", "/OUT:" + exe.as_posix(), obj, *libraries, "opengl32.lib", "dbghelp.lib", "ws2_32.lib", "winmm.lib", "imm32.lib", "version.lib", "user32.lib", "gdi32.lib", "shell32.lib", "ole32.lib", "oleaut32.lib", "advapi32.lib", "comdlg32.lib", "uuid.lib"])
                for mode in ["default", "render-thread"]:
                    run_output = build / mode
                    text = v.run(config + "/" + name + "-" + mode, [exe, run_output, mode] if name == "components" else [exe, mode])
                    require("PASS:" in text, "Behavior test did not finish")
                    if name == "components":
                        require((run_output / "menu.txt").read_text().splitlines() == expected_menu, "Inspector menu names/order changed")
    r.write_json(output / "verification.json", {"stage": "R4", "success": True, "engine": args.engine, "checks": v.checks})
    print("PASS: R4 evidence: " + str(output / "verification.json"), flush=True)

if __name__ == "__main__":
    try: main()
    except (OSError, ValueError, RuntimeError, KeyError) as error:
        print("R4 verification failed: " + str(error), file=sys.stderr)
        sys.exit(1)
