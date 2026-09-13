#!/usr/bin/env python3
"""R2: real LibTooling generation, MSVC/Clang object compilation, static linking and execution."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True
from verify_r0 import reflection, require, ROOT


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generator", default=str(ROOT / "Build/Tools/OvReflectGen/Release/OvReflectGen.exe"))
    parser.add_argument("--llvm-root")
    parser.add_argument("--engine", action="store_true", help="Build SDK libraries and run real CPhysicalBox and GL RenderThread acceptance")
    parser.add_argument("--config", nargs="+", choices=("Debug", "Release", "Publish"), default=["Debug", "Release", "Publish"])
    args = parser.parse_args()
    parent = ROOT / "Build/Tests/Reflection"
    parent.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="r2-", dir=parent))
    print(f"R2 evidence: {output}", flush=True)
    cli = [sys.executable, ROOT / "Tools/OvReflectGen/reflection.py"]
    extra = ["--llvm-root", args.llvm_root] if args.llvm_root else []
    print(reflection.execute([*cli, "export", "--output", output / "Export", *extra]).stdout, end="", flush=True)
    print(reflection.execute([*cli, "generate", "--output", output / "Export", "--generator", args.generator, "--config", *args.config]).stdout, end="", flush=True)
    tc = reflection.read_json(output / "Export/toolchain.json")
    env = reflection.clean_environment()
    env["INCLUDE"] = ";".join(tc["system_includes"])
    env["LIB"] = ";".join(str(p) for p in [Path(tc["msvc_root"]) / "lib/x64",
        Path(tc["sdk_root"]) / "Lib" / tc["sdk_version"] / "ucrt/x64",
        Path(tc["sdk_root"]) / "Lib" / tc["sdk_version"] / "um/x64"])
    compiler_bin = Path(tc["msvc_root"]) / "bin/Hostx64/x64"
    # Windows environment names are case insensitive.
    for key in list(env):
        if key.lower() == "path":
            prior_path = env.pop(key)
            break
    else:
        prior_path = ""
    env["PATH"] = str(compiler_bin) + os.pathsep + prior_path
    checks = []

    def execute(command, log, *, success=True, cwd=ROOT):
        reflection.write_json(Path(log).with_suffix(".command.json"), {"argv": [str(a) for a in command], "cwd": str(cwd)})
        with Path(log).open("wb") as stream:
            result = subprocess.run([str(a) for a in command], cwd=cwd, env=env, stdout=stream, stderr=subprocess.STDOUT)
        result.stdout = Path(log).read_text(encoding="utf-8", errors="replace")
        result.stderr = ""
        require((result.returncode == 0) == success, f"Unexpected exit {result.returncode}: {log}\n" + result.stdout[-6000:] + result.stderr[-6000:])
        return result

    def options(config, module="OvCore"):
        base = output / "Export" / reflection.TARGET / config / module
        entry = reflection.read_json(base / "compile_commands.json")[0]
        argv = entry["arguments"]
        common = [a for a in argv if a.startswith(("/I", "/D", "/U")) and a != "/DOV_REFLECTION_ANALYSIS=1"]
        return ["/nologo", "/std:c++20", "/EHsc", "/MDd" if config == "Debug" else "/MD",
                "/Od" if config == "Debug" else "/O2", "/I" + ROOT.as_posix(), *common]

    def compile_source(compiler, common, source, destination, *, strict=False):
        destination.parent.mkdir(parents=True, exist_ok=True)
        command = ([tc["clang"], "--driver-mode=cl"] if compiler == "Clang" else [compiler_bin / "cl.exe"])
        command += [*common, "/c", "/Gy", "/Fo" + destination.as_posix()]
        if strict:
            command += ["/W4", "/WX"]
        execute([*command, source], destination.with_suffix(".log"))
        return destination

    for config in args.config:
        directory = output / "Fixture" / config
        directory.mkdir(parents=True)
        source = directory / "Reflection.scan.cpp"
        source.write_text('#include "Tests/Reflection/Fixtures/R2/Values.h"\n', encoding="utf-8")
        base = output / "Export" / reflection.TARGET / config / "OvMaths"
        entry = reflection.read_json(base / "compile_commands.json")[0]
        entry["file"] = source.as_posix()
        entry["arguments"][-1] = source.as_posix()
        entry["arguments"].insert(1, "/I" + ROOT.as_posix())
        reflection.write_json(directory / "compile_commands.json", [entry])
        native = [args.generator, "-p", directory, "--source-root=" + ROOT.as_posix(), "--module=Fixture",
                  "--configuration=" + config, "--report=" + (directory / "analysis-report.json").as_posix(), source]
        execute(native, directory / "analysis.log")
        schema = reflection.read_json(directory / "schema.json")
        reflection.emit_cpp(schema, directory)
        before = {n: (directory / n).read_bytes() for n in reflection.CPP_OUTPUTS}
        reflection.emit_cpp(schema, directory)
        require(all((directory / n).read_bytes() == data for n, data in before.items()), "Nondeterministic generated C++")
        header = (directory / "Reflection.generated.h").read_text(encoding="utf-8")
        require("Read_unmarked" not in header and "Write_fixed" not in header and "Write_policyReadOnly" not in header, "Unauthorized typed accessor")
        common = options(config, "OvMaths") + ["/I" + directory.as_posix()]
        for compiler in ("MSVC", "Clang"):
            build = directory / compiler
            runtime = compile_source(compiler, common, ROOT / "Sources/OvReflection/src/OvReflection/Reflection.cpp", build / "runtime.obj", strict=True)
            bridge = compile_source(compiler, common, ROOT / "Sources/OvReflection/src/OvReflection/ScriptBridge.cpp", build / "bridge.obj", strict=True)
            generated = compile_source(compiler, common, directory / "Reflection.generated.cpp", build / "generated.obj", strict=True)
            consumer = compile_source(compiler, common, ROOT / "Tests/Reflection/Integration/runtime_validation.cpp", build / "consumer.obj", strict=True)
            library = build / "Reflection.lib"
            execute([compiler_bin / "lib.exe", "/nologo", "/OUT:" + library.as_posix(), runtime, bridge, generated], build / "archive.log")
            executable = build / "runtime.exe"
            # No whole-archive flag: explicit RegisterFixtureTypes must retain generated descriptions.
            execute([compiler_bin / "link.exe", "/nologo", "/OPT:REF", "/OPT:ICF", "/OUT:" + executable.as_posix(), consumer, library], build / "link.log")
            result = execute([executable], build / "run.log")
            print(f"PASS: {config}/{compiler} {result.stdout.strip()}", flush=True)
            id_consumer = compile_source(compiler, common, ROOT / "Tests/Reflection/Integration/registry_only_validation.cpp", build / "registry-only.obj", strict=True)
            id_executable = build / "registry-only.exe"
            execute([compiler_bin / "link.exe", "/nologo", "/OPT:REF", "/OPT:ICF", "/OUT:" + id_executable.as_posix(), id_consumer, library], build / "registry-only-link.log")
            execute([id_executable], build / "registry-only-run.log")
            # Real production descriptors/accessors must also compile in both target compilers.
            for module in ("OvMaths", "OvCore"):
                actual = output / "Export" / reflection.TARGET / config / module
                compile_source(compiler, options(config), actual / "Reflection.generated.cpp", build / (module + ".obj"))
            checks.append({"configuration": config, "compiler": compiler, "runtime": True, "production_objects": True})
        # A failed native rerun must invalidate all generated successes, not just JSON.
        source.write_text('#include "Tests/Reflection/Fixtures/SyntaxError.h"\n', encoding="utf-8")
        execute(native, directory / "failure.log", success=False)
        require(not any((directory / n).exists() for n in reflection.CPP_OUTPUTS), "Native failure left stale C++")
        source.write_text('#include "Tests/Reflection/Fixtures/R2/Values.h"\nstruct OV_TYPE() Conflict : R2::Gap { OV_FIELD() int base; };\n', encoding="utf-8")
        result = execute(native, directory / "inherited-conflict.log", success=False)
        require("inherited property conflict" in result.stdout, "Unannotated gap hid an inherited property conflict")
        source.write_text('#include "Tests/Reflection/Fixtures/R2/Values.h"\n', encoding="utf-8")
        execute(native, directory / "restored.log")
        reflection.emit_cpp(reflection.read_json(directory / "schema.json"), directory)

    if args.engine:
        verify_engine(args, output, tc, env, options, compile_source, execute, checks)
    reflection.write_json(output / "verification.json", {"stage": "R2", "success": True, "engine": args.engine, "checks": checks})
    print(f"PASS: R2 evidence: {output / 'verification.json'}", flush=True)


def verify_engine(args, output, tc, env, options, compile_source, execute, checks):
    projects = ["OvReflection", "OvAudio", "OvCore", "OvDebug", "OvMaths", "OvPhysics", "OvRendering",
                "OvTools", "OvUI", "OvWindowing", "assimp", "baregl", "bullet3", "freetype", "glfw",
                "ImGui", "lua", "soloud", "tinyxml2", "tracy"]
    msbuild = Path(tc["vs_root"]) / "MSBuild/Current/Bin/amd64/MSBuild.exe"
    for config in args.config:
        build = output / "Engine" / config
        build.mkdir(parents=True, exist_ok=True)
        project = ET.Element("Project")
        items = ET.SubElement(project, "ItemGroup")
        for name in projects:
            parent = "Sources" if name.startswith("Ov") else "Dependencies"
            ET.SubElement(items, "SDKProject", Include=str(ROOT / parent / name / (name + ".vcxproj")))
        target = ET.SubElement(project, "Target", Name="Build")
        ET.SubElement(target, "MSBuild", Projects="@(SDKProject)", Targets="Build", BuildInParallel="true")
        ET.ElementTree(project).write(build / "sdk.proj", encoding="utf-8", xml_declaration=True)
        execute([msbuild, build / "sdk.proj", "/nologo", "/m:2", "/v:minimal",
                 "/t:Build", "/p:Configuration=" + config, "/p:Platform=x64",
                 "/p:TrackFileAccess=false", "/p:ForceRebuild=true", "/p:PreferredToolArchitecture=x64",
                 "/p:PROCESSOR_ARCHITECTURE=AMD64",
                 "/p:VcpkgEnabled=false", "/p:VcpkgApplocalDeps=false",
                 "/p:VCToolsVersion=" + tc["msvc_version"], "/p:WindowsTargetPlatformVersion=" + tc["sdk_version"]],
                build / "sdk-build.log")
        common = options(config) + ["/I" + (ROOT / "Dependencies/glfw/include").as_posix(),
                                   "/I" + (ROOT / "Dependencies/bullet3/src").as_posix(),
                                   "/I" + (output / "Export" / reflection.TARGET / config).as_posix()]
        objects = []
        for module in ("OvMaths", "OvCore"):
            objects.append(compile_source("MSVC", common, output / "Export" / reflection.TARGET / config / module / "Reflection.generated.cpp", build / (module + ".obj")))
        generated_library = build / "Generated.lib"
        compiler_bin = Path(tc["msvc_root"]) / "bin/Hostx64/x64"
        execute([compiler_bin / "lib.exe", "/nologo", "/OUT:" + generated_library.as_posix(), *objects], build / "archive.log")
        consumer = compile_source("MSVC", common, ROOT / "Tests/Reflection/Integration/physical_box_validation.cpp", build / "consumer.obj")
        libraries = [ROOT / "Bin" / config / p / (p + ".lib") for p in projects]
        executable = build / "physical_box.exe"
        execute([compiler_bin / "link.exe", "/nologo", "/OPT:REF", "/OUT:" + executable.as_posix(), consumer, generated_library,
                 *libraries, "opengl32.lib", "dbghelp.lib", "ws2_32.lib", "winmm.lib", "imm32.lib", "version.lib",
                 "user32.lib", "gdi32.lib", "shell32.lib", "ole32.lib", "oleaut32.lib", "advapi32.lib",
                 "comdlg32.lib", "uuid.lib"],
                build / "link.log")
        for mode in ("default", "render-thread"):
            result = execute([executable, mode], build / (mode + ".log"))
            print(f"PASS: {config}/{mode} {result.stdout.strip()}", flush=True)
            checks.append({"configuration": config, "engine_mode": mode, "physical_box": True})


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, KeyError) as error:
        print(f"R2 verification failed: {error}", file=sys.stderr)
        sys.exit(1)
