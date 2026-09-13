#!/usr/bin/env python3
"""R1 acceptance against the built LibTooling tool, not AST dumps or mocks."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
import tempfile

sys.dont_write_bytecode = True
from verify_r0 import reflection, require, ROOT

FIXTURES = ROOT / "Tests/Reflection/Fixtures/R1"
OUTPUTS = ("analysis-report.json", "schema.json", "dependencies.json", "input-summary.json")
NEGATIVES = {
    1: "Setter value type", 2: "duplicate ID", 3: "unsupported reflected type", 4: "non-public reflection access",
    5: "enum cannot", 6: "union", 7: "duplicate ID", 8: "inherited property conflict",
    9: "cannot infer property name", 10: "Setter must select", 11: "unsupported reflected type",
    12: "bitfields", 13: "unsupported reflection annotation target", 14: "overloaded OV_METHOD",
    15: "repeated metadata key", 16: "empty metadata value", 17: "ReadOnly property",
    18: "Setter value type", 19: "requires Setter", 20: "OV_TYPE owner", 21: "unsupported reflection annotation target",
    22: "non-public reflection access", 23: "duplicate property name", 24: "unknown",
    25: "non-virtual single inheritance", 26: "multiple inheritance", 27: "template", 28: "unsupported reflected method",
    29: "anonymous", 30: "local", 31: "complete definition", 32: "AComponent inheritance", 33: "enum cannot",
    34: "invalid annotation target", 35: "invalid annotation target", 36: "Setter must select",
    37: "invalid Setter signature", 38: "property getter", 39: "Thread must", 40: "flag-only",
    41: "non-public reflection access", 42: "non-virtual single inheritance", 43: "overloaded OV_METHOD",
    44: "ignored", 45: "enum value type requires", 46: "duplicate ID", 47: "duplicate ID",
    48: "property getter", 49: "unsupported reflected type", 50: "unsupported reflected type",
    51: "member annotations must be placed",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generator", default=str(ROOT / "Build/Tools/OvReflectGen/Release/OvReflectGen.exe"))
    parser.add_argument("--llvm-root")
    args = parser.parse_args()
    parent = ROOT / "Build/Tests/Reflection"
    parent.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="r1-", dir=parent))
    print(f"R1 evidence: {output}", flush=True)
    extra = ["--llvm-root", args.llvm_root] if args.llvm_root else []
    print(reflection.execute([sys.executable, ROOT / "Tools/OvReflectGen/reflection.py", "export", "--output", output / "Export", *extra]).stdout, end="", flush=True)
    tc = reflection.read_json(output / "Export/toolchain.json")

    def prepare(name, config, header, *, module="OvMaths", defines=(), duplicate=False, text=None):
        directory = output / name
        directory.mkdir(parents=True, exist_ok=True)
        source = directory / "Reflection.scan.cpp"
        if text is None:
            text = f'#include "{header.as_posix()}"\n' * (2 if duplicate else 1)
        source.write_text(text, encoding="utf-8")
        base = output / "Export" / reflection.TARGET / config / module
        entry = reflection.read_json(base / "compile_commands.json")[0]
        entry["file"] = source.as_posix()
        entry["arguments"][-1] = source.as_posix()
        entry["arguments"][1:1] = list(defines)
        reflection.write_json(directory / "compile_commands.json", [entry])
        reflection.write_json(directory / "analysis-input.json", {"fixture": str(header), "configuration": config, "command": entry})
        return directory

    def run(directory, config="Debug", *, failure=None):
        if failure:
            for name in OUTPUTS: (directory / name).write_text('{"stale_success":true}', encoding="utf-8")
        # Native invocation must also invalidate a previous driver's input summary.
        result = reflection.execute([args.generator, "-p", directory,
            "--source-root=" + ROOT.as_posix(), "--module=Fixture", "--configuration=" + config,
            "--report=" + (directory / "analysis-report.json").as_posix(), directory / "Reflection.scan.cpp"],
            env=reflection.clean_environment(), check=False)
        (directory / "diagnostics.txt").write_text(result.stdout + result.stderr, encoding="utf-8")
        if failure:
            require(result.returncode != 0, f"Negative fixture unexpectedly passed: {directory}")
            require(failure in result.stderr, f"Expected diagnostic '{failure}': {result.stderr}")
            require(re.search(r"Negative\.h(?:\(|:)\d+", result.stderr), f"Missing source location: {result.stderr}")
            require(not any((directory / name).exists() for name in OUTPUTS), "Failure left stale success outputs")
            return None
        require(result.returncode == 0, f"Schema generation failed: {result.stderr}")
        reflection.input_summary(directory, args.generator)
        return reflection.read_json(directory / "schema.json")

    def normal_compile(directory):
        entry = reflection.read_json(directory / "compile_commands.json")[0]
        arguments = [a for a in entry["arguments"] if a != "/DOV_REFLECTION_ANALYSIS=1"]
        clang = reflection.execute(arguments, env=reflection.clean_environment(), check=False)
        require(clang.returncode == 0, "Clang normal compilation failed: " + clang.stderr)
        # The same fixed header, including friend declarations, must compile with MSVC.
        include_args = [a for a in arguments if a.startswith("/I") or a.startswith("/D") or a.startswith("/U")]
        msvc = [Path(tc["msvc_root"]) / "bin/Hostx64/x64/cl.exe", "/nologo", "/TP", "/Zs", "/std:c++20", "/EHsc",
                next(a for a in arguments if a in ("/MD", "/MDd")), *include_args, entry["file"]]
        env = reflection.clean_environment()
        env["INCLUDE"] = ";".join(tc["system_includes"])
        result = reflection.execute(msvc, env=env, check=False)
        (directory / "normal-compilation.txt").write_text(result.stdout + result.stderr, encoding="utf-8")
        require(result.returncode == 0, "MSVC normal compilation failed: " + result.stdout + result.stderr)

    for config in ("Debug", "Release", "Publish"):
        directory = prepare("Positive/" + config, config, FIXTURES / "Positive.h", duplicate=True)
        document = run(directory, config)
        by_name = {t["cpp_name"]: t for t in document["types"]}
        require(len(by_name) == len(document["types"]) == 11, "Missing or duplicate explicitly annotated types")
        require([t["id"] for t in document["types"]] == sorted(t["id"] for t in document["types"]), "Unsorted schema")
        for name, kind in (("EmptyClass", "class"), ("EmptyStruct", "struct"), ("Plain", "enum"), ("Scoped", "enum class")):
            t = by_name["Fixture::Nested::" + name]
            require(t["kind"] == kind and t["id"] == t["cpp_name"], "Empty OV_TYPE kind/default identity mismatch")
        require(by_name["Fixture::Nested::Plain"]["enumerators"][0]["value"] == "-7", "Lost signed enum value")
        require(by_name["Fixture::Nested::Scoped"]["enumerators"][1]["value"] == "18446744073709551615", "64-bit enum rounded")
        value = by_name["Fixture::Nested::Value"]
        require(value["id"] == "fixture.stable" and value["aliases"] == ["fixture.old", "legacy.Type"], "Identity/aliases incorrect")
        properties = {p["name"]: p for p in value["properties"]}
        require("hidden" not in properties and len(properties) == 8, "Implicit member export")
        count = properties["count"]["type"]
        require(count["bits"] == 64 and not count["signed"] and count["display"] != count["canonical"], "Typedef identity or precision lost")
        require(properties["small"]["type"]["bits"] == 8 and properties["small"]["type"]["signed"], "int8 model lost")
        require(properties["label"]["type"]["category"] == "string", "String specialization not recognized")
        require(properties["size"]["setter"].endswith("::SetSize") and properties["size"]["id"] == "fixture.stable.size", "Property pairing/identity mismatch")
        require(properties["Ready"]["read_only"] and properties["constant"]["read_only"], "Read-only semantics lost")
        method = value["methods"][0]
        require(method["const"] and method["noexcept"] and method["ref_qualifier"] == "&" and len(method["parameters"]) == 2, "Method signature lost")
        require(by_name["Fixture::Nested::Derived"]["bases"][0]["cpp_name"] == "Fixture::Nested::Base", "Canonical inheritance lost")
        require(len(by_name["Fixture::Nested::Private"]["properties"]) == 2, "Explicit friend authorization failed")
        require(len(by_name["Fixture::Nested::Forward"]["properties"]) == 1, "Forward annotation/definition pairing failed")
        for t in document["types"]:
            require(t["source"]["line"] > 0 and not Path(t["source"]["path"]).is_absolute(), "Source location is not relative")
        before = {name: (directory / name).read_bytes() for name in OUTPUTS}
        run(directory, config)
        require(all((directory / name).read_bytes() == data for name, data in before.items()), "Nondeterministic schema or input evidence")
        dependencies = reflection.read_json(directory / "dependencies.json")["files"]
        paths = {d["path"] for d in dependencies}
        require(any(p.endswith("/Indirect.h") for p in paths) and any("/MSVC/" in p for p in paths), "Missing transitive/system dependencies")
        require(len(paths) == len(dependencies), "Duplicate include dependencies")
        for dependency in dependencies:
            require(hashlib.sha256(Path(dependency["path"]).read_bytes()).hexdigest() == dependency["sha256"], "Incorrect actual input hash")
        normal_compile(directory)
        real = prepare("Real/" + config, config, FIXTURES / "RealComponents.h", module="OvCore")
        real_doc = run(real, config)
        require({t["cpp_name"] for t in real_doc["types"]} == {
            "Fixture::Box", "Fixture::Transform", "OvMaths::FVector3", "OvCore::ECS::Components::CPhysicalBox",
            "OvCore::ECS::Components::CPhysicalObject", "OvCore::ECS::Components::CTransform"},
            "Unexpected explicitly annotated real types")
        box = next(t for t in real_doc["types"] if t["cpp_name"] == "Fixture::Box")
        size = box["properties"][0]
        require(size["type"]["value_type"] == "OvMaths::FVector3" and size["metadata"]["Thread"] == "Main", "Real component property model incorrect")
        normal_compile(real)
        print(f"PASS: {config} annotated schema, real ancestry/math, determinism, Clang/MSVC normal compilation", flush=True)

    for case, expected in NEGATIVES.items():
        directory = prepare(f"Negative/{case:02}", "Debug", FIXTURES / "Negative.h", defines=[f"/DCASE={case}"])
        run(directory, failure=expected)
    print(f"PASS: {len(NEGATIVES)} negative fixtures and stale-success rejection", flush=True)

    unsigned_char = prepare("UnsignedChar", "Debug", FIXTURES / "Positive.h", defines=["/J"])
    unsigned_doc = run(unsigned_char)
    unsigned_value = next(t for t in unsigned_doc["types"] if t["id"] == "fixture.stable")
    require(next(p for p in unsigned_value["properties"] if p["name"] == "label")["type"]["category"] == "string",
            "MSVC unsigned-char mode broke canonical std::string recognition")

    # Change an indirect input with a stable path and verify its semantic and hash effects.
    directory = output / "DependencyChange"
    directory.mkdir()
    leaf = directory / "Leaf.h"
    bridge = directory / "Bridge.h"
    leaf.write_text("using Selected = unsigned long long;\n", encoding="utf-8")
    bridge.write_text('#include "Leaf.h"\n', encoding="utf-8")
    source = '#include <OvReflection/Annotations.h>\n#include "Bridge.h"\nstruct OV_TYPE() Dependency { OV_FIELD() Selected value; };\n'
    prepare("DependencyChange", "Debug", bridge, text=source)
    first = run(directory)
    first_summary = reflection.read_json(directory / "input-summary.json")
    leaf.write_text("using Selected = unsigned int;\n", encoding="utf-8")
    second = run(directory)
    second_summary = reflection.read_json(directory / "input-summary.json")
    require(first["types"][0]["properties"][0]["type"]["bits"] == 64 and second["types"][0]["properties"][0]["type"]["bits"] == 32, "Indirect header edit did not affect schema")
    require(first_summary["input_sha256"] != second_summary["input_sha256"], "Indirect input hash did not change")
    entry = reflection.read_json(directory / "compile_commands.json")
    entry[0]["arguments"].insert(1, "/DR1_UNUSED_INPUT=1")
    reflection.write_json(directory / "compile_commands.json", entry)
    third = run(directory)
    require(third == second and reflection.read_json(directory / "input-summary.json")["input_sha256"] != second_summary["input_sha256"], "Command-line input not included in digest")
    reflection.write_json(output / "verification.json", {"stage": "R1", "passed": True, "mode": "libtooling", "negative_cases": len(NEGATIVES),
        "checks": ["all-declaration-kinds", "explicit-only-export", "canonical-types", "64-bit-enums", "identity-and-aliases", "property-pairing", "private-friend", "inheritance", "source-diagnostics", "stale-success-rejection", "deterministic-output", "transitive-system-dependencies", "input-digests", "real-components-all-configs", "clang-msvc-normal-mode"]})
    print(f"PASS: R1 LibTooling acceptance; evidence: {output}", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, RuntimeError) as error:
        print(f"R1 verification failed: {error}", file=sys.stderr)
        sys.exit(1)
