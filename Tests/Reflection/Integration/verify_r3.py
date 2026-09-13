#!/usr/bin/env python3
"""R3 content invalidation/transactions plus real Premake/MSBuild build-graph acceptance."""
import argparse
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True
from verify_r0 import reflection as r, require, ROOT


def snapshot(directory, pattern="*"):
    return {p.relative_to(directory).as_posix(): (hashlib.sha256(p.read_bytes()).hexdigest(), p.stat().st_mtime_ns)
            for p in directory.rglob(pattern) if p.is_file() and not p.name.endswith(".lock")}


@contextmanager
def restored(path):
    data = path.read_bytes()
    try:
        yield
    finally:
        path.write_bytes(data)


class Verification:
    def __init__(self, output):
        self.output = output
        self.checks = []
        self.env = r.clean_environment()
        # The desktop sandbox may omit these standard Windows environment values.
        # Keep ordinary MSBuild tracking enabled; do NOT set ForceRebuild/TrackFileAccess.
        temporary = output / "Temp"
        temporary.mkdir()
        self.env.update(TEMP=str(temporary), TMP=str(temporary))
        self.env.setdefault("ProgramData", "C:\\ProgramData")
        self.env.setdefault("ALLUSERSPROFILE", self.env["ProgramData"])
        self.env.setdefault("SystemDrive", "C:")
        self.env.setdefault("USERPROFILE", str(Path.home()))
        self.env.setdefault("PROCESSOR_ARCHITECTURE", "AMD64")
        self.env["MSBUILDDISABLENODEREUSE"] = "1"

    def run(self, label, command, success=True):
        log = self.output / (label + ".log")
        log.parent.mkdir(parents=True, exist_ok=True)
        r.write_json(log.with_suffix(".command.json"), {"argv": [str(a) for a in command], "cwd": str(ROOT)})
        started = time.monotonic()
        with log.open("wb") as stream:
            result = subprocess.run([str(a) for a in command], cwd=ROOT, env=self.env, stdout=stream, stderr=subprocess.STDOUT)
        text = log.read_text(encoding="utf-8", errors="replace")
        elapsed = time.monotonic() - started
        self.checks.append({"name": label, "exit": result.returncode, "seconds": round(elapsed, 3),
                            "generation": [line.strip() for line in text.splitlines() if "R3 " in line]})
        require((result.returncode == 0) == success, f"Unexpected result: {log}\n{text[-10000:]}")
        print(f"PASS: {label} ({elapsed:.2f}s)", flush=True)
        return text

    def parallel(self, label, commands):
        jobs = []
        started = time.monotonic()
        for index, command in enumerate(commands):
            log = self.output / f"{label}-{index}.log"
            stream = log.open("wb")
            r.write_json(log.with_suffix(".command.json"), {"argv": [str(a) for a in command]})
            jobs.append((subprocess.Popen([str(a) for a in command], cwd=ROOT, env=self.env,
                                          stdout=stream, stderr=subprocess.STDOUT), stream, log))
        texts = []
        for process, stream, log in jobs:
            code = process.wait()
            stream.close()
            text = log.read_text(encoding="utf-8", errors="replace")
            require(code == 0, f"Concurrent writer failed: {log}\n{text[-6000:]}")
            texts.append(text)
        self.checks.append({"name": label, "seconds": round(time.monotonic()-started, 3), "processes": len(jobs)})
        print(f"PASS: {label}", flush=True)
        return texts


def fixture_matrix(v, tc, generator):
    fixture = v.output / "Fixture"
    tool = fixture / "Tools/OvReflectGen"
    shutil.copytree(r.TOOL, tool, ignore=shutil.ignore_patterns("Projects", "__pycache__"))
    manifest = tool / "scan-modules.json"
    r.write_json(manifest, {"version": 1, "modules": [
        {"name": name, "headers": [f"{name}/Value.h"], "types": [name + "::Value"]} for name in ("Values", "Other")]})
    baked = tool / "Projects/Premake"
    original = r.read_json(r.TOOL / "Projects/Premake/OvMaths.json")
    for name in ("Values", "Other"):
        include = fixture / "Sources" / name / "include"
        (include / name).mkdir(parents=True)
        (include / name / "Indirect.h").write_text("#pragma once\nusing Number = int ;\n", encoding="utf-8")
        (include / name / "Value.h").write_text(
            '#pragma once\n#include <OvReflection/Annotations.h>\n#include "Indirect.h"\n'
            f'namespace {name} {{ struct OV_TYPE() Value {{ OV_FIELD() Number value;\n'
            '#if R3_FEATURE\n OV_FIELD() bool feature;\n#endif\n}; }\n', encoding="utf-8")
        data = json.loads(json.dumps(original))
        data.pop("project_inputs", None)
        for cfg in data["configurations"]:
            cfg["project"] = name
            cfg["includedirs"] = [include.as_posix(), (ROOT / "Sources/OvReflection/include").as_posix()]
            cfg["defines"] += ["R3_FEATURE=0"]
        r.write_json(baked / (name + ".json"), data)
    output = fixture / "Build/Generated/Reflection"
    base = output / r.TARGET / "Debug"
    cli = [sys.executable, tool / "reflection.py"]
    common = ["--generator", generator, "--llvm-root", tc["llvm_root"], "--msvc-root", tc["msvc_root"],
              "--sdk-version", tc["sdk_version"]]
    command = [*cli, "ensure", *common, "--config", "Debug"]
    v.run("fixture-clean", command)
    before = snapshot(base)
    text = v.run("fixture-no-change", command)
    require("analyzed=0" in text and snapshot(base) == before, "No-op changed timestamps or ran the AST")

    direct = fixture / "Sources/Values/include/Values/Value.h"
    indirect = direct.with_name("Indirect.h")
    other_before = snapshot(base / "Other")
    direct.write_text(direct.read_text().replace("Number value;", "Number renamed;"), encoding="utf-8")
    text = v.run("fixture-direct", command)
    require("analyzed=Values;" in text and snapshot(base / "Other") == other_before, "Direct edit regenerated unrelated module")
    require("Read_renamed" in (base / "Values/Reflection.generated.h").read_text(), "Direct edit was not published")

    # Same timestamp and same byte length: hashing must catch this, timestamps cannot.
    stat = indirect.stat()
    indirect.write_text("#pragma once\nusing Number = bool;\n", encoding="utf-8")
    require(indirect.stat().st_size == stat.st_size, "Same-size invalidation fixture changed length")
    os.utime(indirect, ns=(stat.st_atime_ns, stat.st_mtime_ns))
    text = v.run("fixture-indirect", command)
    require("analyzed=Values;" in text and "bool& output" in (base / "Values/Reflection.generated.h").read_text(), "Indirect edit missed")

    header_before = snapshot(base, "Reflection.generated.*")
    indirect.write_text(indirect.read_text() + "// comment-only change\n", encoding="utf-8")
    v.run("fixture-semantic-no-change", command)
    require(snapshot(base, "Reflection.generated.*") == header_before, "Identical C++ changed its timestamp")

    config_path = baked / "Values.json"
    data = r.read_json(config_path)
    data["configurations"][0]["defines"] = [d.replace("R3_FEATURE=0", "R3_FEATURE=1") for d in data["configurations"][0]["defines"]]
    r.write_json(config_path, data)
    v.run("fixture-macro", command)
    require("Read_feature" in (base / "Values/Reflection.generated.h").read_text(), "Macro edit missed")

    # New include discovered by __has_include was absent from the old entered-buffer list.
    direct.write_text(direct.read_text() + '\n#if __has_include("Optional.h")\n#include "Optional.h"\n#endif\n', encoding="utf-8")
    v.run("fixture-optional-absent", command)
    optional = direct.with_name("Optional.h")
    optional.write_text('namespace Values { struct OV_TYPE() Added { OV_FIELD() int extra; }; }\n', encoding="utf-8")
    v.run("fixture-add", command)
    require("Values::Added" in (base / "Values/Reflection.generated.h").read_text(), "New include was not discovered")
    renamed = direct.with_name("Renamed.h")
    optional.rename(renamed)
    direct.write_text(direct.read_text().replace('Optional.h', 'Renamed.h'), encoding="utf-8")
    v.run("fixture-rename", command)
    renamed.unlink()
    v.run("fixture-delete", command)
    require("Values::Added" not in (base / "Values/Reflection.generated.h").read_text(), "Removed type remained")

    doc = r.read_json(manifest)
    doc["modules"][0]["headers"].append("Values/Extra.h")
    extra = direct.with_name("Extra.h")
    extra.write_text('namespace Values { enum class OV_TYPE() Mode { One }; }\n', encoding="utf-8")
    r.write_json(manifest, doc)
    v.run("fixture-scan-list", command)
    require("Values::Mode" in (base / "Values/Reflection.generated.h").read_text(), "Scan manifest edit missed")

    # Ownership migration removes only previously listed files, preserving user neighbors.
    owned_manifest = base / "Values/manifest.json"
    old = r.read_json(owned_manifest)
    stale, neighbor = base / "Values/Legacy.generated.cpp", base / "Values/notes.txt"
    stale.write_text("old generator output")
    neighbor.write_text("must survive")
    old["files"].append({"path": stale.name, "sha256": hashlib.sha256(stale.read_bytes()).hexdigest()})
    r.write_json(owned_manifest, old)
    v.run("fixture-stale-owned-file", command)
    require(not stale.exists() and neighbor.read_text() == "must survive", "Cleanup escaped manifest ownership")

    outside = base / "outside.txt"
    outside.write_text("not owned by any module")
    with restored(owned_manifest):
        unsafe = r.read_json(owned_manifest)
        unsafe["files"].append({"path": "../outside.txt", "sha256": "invalid"})
        r.write_json(owned_manifest, unsafe)
        v.run("fixture-reject-escaping-manifest", command, success=False)
        require(outside.read_text() == "not owned by any module" and not (base / "generation-state.json").exists(),
                "Unsafe cleanup path was followed or left a committed result")
    v.run("fixture-manifest-recovery", command)

    other_neighbor = base / "Other/notes.txt"
    other_neighbor.write_text("keep removed module neighbor")
    with restored(manifest):
        removed = r.read_json(manifest)
        removed["modules"] = [m for m in removed["modules"] if m["name"] != "Other"]
        r.write_json(manifest, removed)
        v.run("fixture-remove-module", command)
        require(not list((base / "Other").glob("Reflection.generated.*")) and other_neighbor.exists(), "Removed module cleanup failed")
    v.run("fixture-restore-module", command)

    with restored(config_path):
        stale_config = r.read_json(config_path)
        stale_config["project_inputs"] = {"Tools/OvReflectGen/premake-build.lua": "stale project input"}
        r.write_json(config_path, stale_config)
        text = v.run("fixture-stale-premake", command, success=False)
        require("Premake input changed" in text, "Stale compiler export was silently accepted")
    v.run("fixture-rebaked-recovery", command)

    with restored(direct):
        direct.write_text(direct.read_text() + "\n#error R3_expected_generation_failure\n", encoding="utf-8")
        text = v.run("fixture-error", command, success=False)
        require("R3_expected_generation_failure" in text and not (base / "generation-state.json").exists(), "Failure did not block commit")
        require(not list(base.glob("*/Reflection.generated.*")), "Failed generation retained old C++")
    v.run("fixture-error-recovery", command)

    # Corrupted/missing outputs must regenerate even when all inputs are unchanged.
    (base / "Values/Reflection.generated.h").write_text("invalid")
    v.run("fixture-corrupted-output", command)
    (base / "Values/Reflection.generated.cpp").unlink()
    v.run("fixture-missing-output", command)

    # Both generator implementation and version/Schema lock participate in invalidation.
    emitter = tool / "Emitters/cpp.py"
    emitter.write_text(emitter.read_text() + "\n# R3 invalidation probe\n", encoding="utf-8")
    require("analyzed=Values,Other" in v.run("fixture-generator-source", command), "Generator source edit missed")
    lock = tool / "toolchain.lock.json"
    lock_data = r.read_json(lock)
    lock_data["generator_version"] += "+r3-test"
    r.write_json(lock, lock_data)
    require("analyzed=Values,Other" in v.run("fixture-generator-version", command), "Generator version edit missed")
    lock_data["schema_version"] += 1
    r.write_json(lock, lock_data)
    require("analyzed=Values,Other" in v.run("fixture-schema-version", command), "Schema version edit missed")

    v.run("fixture-clean-owned", [*cli, "clean", "--config", "Debug"])
    texts = v.parallel("fixture-same-config", [command, command, command])
    require(sum("analyzed=Values,Other" in t for t in texts) == 1 and sum("analyzed=0" in t for t in texts) == 2,
            "Concurrent same-config generation had multiple writers")
    v.parallel("fixture-three-configs", [[*cli, "ensure", *common, "--config", c] for c in ("Debug", "Release", "Publish")])
    for config in ("Debug", "Release", "Publish"):
        state = r.read_json(output / r.TARGET / config / "generation-state.json")
        require(state["configuration"] == config, "Cross-config publication")

    # Kill a holder of the actual OS lock; the next process must recover without manual unlock.
    ready = fixture / "lock-ready"
    program = ("import sys,time; from pathlib import Path; sys.path.insert(0,sys.argv[1]); import incremental; "
               "lock=incremental.writer_lock(Path(sys.argv[2])); lock.__enter__(); Path(sys.argv[3]).write_text('ready'); time.sleep(60)")
    holder = subprocess.Popen([sys.executable, "-c", program, str(tool),
        str(output / ".locks" / (r.TARGET + "-Debug.lock")), str(ready)], env=v.env)
    try:
        deadline = time.monotonic() + 10
        while not ready.exists() and time.monotonic() < deadline:
            time.sleep(0.05)
        require(ready.exists(), "Lock holder failed to start")
    finally:
        holder.kill()
        holder.wait()
    v.run("fixture-process-death-lock", command)
    require(not list(base.glob(".transaction-*")), "Transaction directory leaked")


def project_matrix(v, tc, configurations, full_solution):
    msbuild = Path(tc["vs_root"]) / "MSBuild/Current/Bin/amd64/MSBuild.exe"
    flags = ["/nologo", "/m:2", "/nr:false", "/v:normal", "/t:Build", "/p:Platform=x64", "/p:PreferredToolArchitecture=x64",
             "/p:PROCESSOR_ARCHITECTURE=AMD64", "/p:VcpkgEnabled=false", "/p:VcpkgApplocalDeps=false",
             "/p:VCToolsVersion=" + tc["msvc_version"], "/p:WindowsTargetPlatformVersion=" + tc["sdk_version"]]
    cli = [sys.executable, ROOT / "Tools/OvReflectGen/reflection.py"]
    v.run("premake", [ROOT / "Dependencies/premake5/bin/premake5.exe", "--reflection-python=" + sys.executable, "vs2022"])
    ns = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}
    for name in ("OvMaths", "OvCore", "OvEditor", "OvGame"):
        project = ET.parse(ROOT / "Sources" / name / (name + ".vcxproj"))
        references = [e.attrib["Include"] for e in project.findall(".//m:ProjectReference", ns)]
        require(any("OvReflectionGen.vcxproj" in p for p in references), f"{name} cannot build without solution ordering")
        if name in ("OvMaths", "OvCore"):
            sources = [e for e in project.findall(".//m:ClCompile", ns) if "Reflection.generated.cpp" in e.attrib.get("Include", "")]
            require(len(sources) == 3 and all(len(s.findall("m:ExcludedFromBuild", ns)) == 2 for s in sources), "Generated sources not configuration isolated")

    generated = ROOT / "Build/Generated/Reflection"
    # Deliberately move the entire generated tree away: scans and baked export must
    # bootstrap without any generated file. Preserve the old tree as test evidence.
    if generated.exists():
        require(generated.resolve() == (ROOT / "Build/Generated/Reflection").resolve(), "Unexpected clean path")
        generated.rename(v.output / "generated-before-clean")
    executable = ROOT / "Build/Tools/OvReflectGen/Release/OvReflectGen.exe"
    if executable.exists():
        executable.rename(v.output / "generator-before-clean.exe")
    target = ROOT / "Overload.sln" if full_solution else ROOT / "Sources/OvMaths/OvMaths.vcxproj"
    for config in configurations:
        command = [msbuild, target, *flags, "/p:Configuration=" + config]
        v.run("msbuild-clean-" + config, command)
        require((generated / r.TARGET / config / "generation-state.json").is_file(), "Clean consumer build did not generate")
        before = snapshot(ROOT / "Bin-Int" / config, "*.obj")
        artifacts = snapshot(generated / r.TARGET / config)
        text = v.run("msbuild-no-change-" + config, command)
        after = snapshot(ROOT / "Bin-Int" / config, "*.obj")
        recompiled = [p for p in set(before) | set(after) if before.get(p) != after.get(p)]
        v.checks[-1]["recompiled_objects"] = recompiled
        require(not recompiled, "No-op build recompiled: " + str(recompiled))
        require(snapshot(generated / r.TARGET / config) == artifacts, "No-op MSBuild modified generated outputs")
        require("analyzed=0" in text, "MSBuild skipped required generator check")

    config = configurations[0]
    # A real production metadata edit changes generated output and lets MSBuild's
    # normal dependency tracker select the affected translation units.
    physical = ROOT / "Sources/OvCore/include/OvCore/ECS/Components/CPhysicalBox.h"
    command = [msbuild, ROOT / "Sources/OvCore/OvCore.vcxproj", *flags, "/p:Configuration=" + config]
    v.run("msbuild-core-baseline", command)
    before = snapshot(ROOT / "Bin-Int" / config, "*.obj")
    with restored(physical):
        physical.write_bytes(physical.read_bytes().replace(b'DisplayName = "Physical Box"', b'DisplayName = "R3 Validation Box"'))
        v.run("msbuild-direct-metadata", command)
        after = snapshot(ROOT / "Bin-Int" / config, "*.obj")
        recompiled = [p for p in set(before) | set(after) if before.get(p) != after.get(p)]
        v.checks[-1]["recompiled_objects"] = recompiled
        require(any("Reflection.generated" in p for p in recompiled), "Generated descriptor was not recompiled")
        require(not any(p.startswith("OvMaths/") for p in recompiled), "Core edit rebuilt unrelated Maths objects")
        physical.write_bytes(physical.read_bytes().replace(b'Setter = SetSize', b'Setter = R3MissingSetter'))
        previous_objects = snapshot(ROOT / "Bin-Int" / config, "*.obj")
        v.run("msbuild-generation-error", command, success=False)
        require(snapshot(ROOT / "Bin-Int" / config, "*.obj") == previous_objects, "Consumer compiled after generator failure")
        require(not list((generated / r.TARGET / config).glob("*/Reflection.generated.*")), "Failed project build reused stale generated code")
    v.run("msbuild-error-recovery", command)
    v.parallel("msbuild-parallel-projects", [
        [msbuild, ROOT / "Sources" / module / (module + ".vcxproj"), *flags, "/p:Configuration=" + config]
        for module in ("OvMaths", "OvCore")])
    v.parallel("msbuild-parallel-configs", [
        [msbuild, ROOT / "Sources/OvMaths/OvMaths.vcxproj", *flags, "/p:Configuration=" + config]
        for config in configurations])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--full-solution", action="store_true", help="Build Editor/Game and all SDK/dependency projects")
    parser.add_argument("--fixtures-only", action="store_true")
    parser.add_argument("--config", nargs="+", default=["Debug", "Release", "Publish"], choices=("Debug", "Release", "Publish"))
    args = parser.parse_args()
    parent = ROOT / "Build/Tests/Reflection"
    parent.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="r3-", dir=parent))
    print(f"R3 evidence: {output}", flush=True)
    v = Verification(output)
    cli = [sys.executable, ROOT / "Tools/OvReflectGen/reflection.py"]
    v.run("export", [*cli, "export", "--output", output / "Export"])
    tc = r.read_json(output / "Export/toolchain.json")
    generator = ROOT / "Build/Tools/OvReflectGen/Release/OvReflectGen.exe"
    fixture_matrix(v, tc, generator)
    if not args.fixtures_only:
        project_matrix(v, tc, args.config, args.full_solution)
    r.write_json(output / "verification.json", {"stage": "R3", "success": True,
        "full_solution": args.full_solution, "fixtures_only": args.fixtures_only, "checks": v.checks})
    print(f"PASS: R3 evidence: {output / 'verification.json'}", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, KeyError) as error:
        print(f"R3 verification failed: {error}", file=sys.stderr)
        sys.exit(1)
