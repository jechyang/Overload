#!/usr/bin/env python3
"""R0 acceptance: fresh output, VS parameter parity, real headers, negative diagnostics."""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import sys
import tempfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[3]
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location("reflection", ROOT / "Tools/OvReflectGen/reflection.py")
reflection = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reflection)
NS = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--syntax-only", action="store_true")
    parser.add_argument("--llvm-root")
    parser.add_argument("--generator", default=str(ROOT / "Build/Tools/OvReflectGen/Release/OvReflectGen.exe"))
    args = parser.parse_args()
    output_parent = ROOT / "Build/Tests/Reflection"
    output_parent.mkdir(parents=True, exist_ok=True)
    # Keep evidence for diagnosis; no deletion of the user's generated directories.
    output = Path(tempfile.mkdtemp(prefix="r0-", dir=output_parent))
    cli = [sys.executable, ROOT / "Tools/OvReflectGen/reflection.py"]
    extra = ["--llvm-root", args.llvm_root] if args.llvm_root else []
    print(reflection.execute([*cli, "export", "--output", output, *extra]).stdout, end="")
    snapshots = {}
    for module in reflection.modules():
        vcx = ROOT / "Sources" / module["name"] / (module["name"] + ".vcxproj")
        xml = ET.parse(vcx)
        for group in xml.findall("m:ItemDefinitionGroup", NS):
            configuration = re.search(r"=='([^|]+)\|x64'", group.attrib["Condition"])[1]
            directory = output / reflection.TARGET / configuration / module["name"]
            entry = reflection.read_json(directory / "compile_commands.json")[0]
            cfg = reflection.read_json(directory / "analysis-input.json")["selected_configuration"]
            compiler = group.find("m:ClCompile", NS)
            expected_defines = compiler.findtext("m:PreprocessorDefinitions", namespaces=NS).split(";")[:-1]
            require(cfg["defines"] == expected_defines, "Defines differ from VS output")
            relative_includes = compiler.findtext("m:AdditionalIncludeDirectories", namespaces=NS).split(";")[:-1]
            expected_includes = [(vcx.parent / p).resolve().as_posix() for p in relative_includes]
            require(cfg["includedirs"] == expected_includes, "Include search order differs from VS output")
            require(compiler.findtext("m:LanguageStandard", namespaces=NS) == "stdcpp20", "Dialect drift")
            arguments = entry["arguments"]
            require(all("/D" + d in arguments for d in expected_defines), "Lost macro in clang argv")
            require("/DUNICODE" in arguments and "/D_UNICODE" in arguments, "Lost VS character-set defaults")
            require(("/MDd" if configuration == "Debug" else "/MD") in arguments, "CRT configuration drift")
            require(("/DTRACY_ENABLE" in arguments) == (configuration != "Publish"), "Publish Tracy leakage")
            text = Path(entry["file"]).read_text(encoding="utf-8")
            require("Reflection.generated" not in text, "Scan depends on generated reflection headers")
            snapshots[directory / "compile_commands.json"] = (directory / "compile_commands.json").read_bytes()
    # Export the same configuration twice: paths/args must remain deterministic.
    reflection.execute([*cli, "export", "--output", output, *extra])
    for path, before in snapshots.items():
        require(path.read_bytes() == before, f"Nondeterministic compiler arguments: {path}")
    mode = ["--syntax-only"] if args.syntax_only else ["--generator", args.generator]
    print(reflection.execute([*cli, "analyze", "--output", output, *mode]).stdout, end="")
    if not args.syntax_only:
        for module in reflection.modules():
            for config in ("Debug", "Release", "Publish"):
                directory = output / reflection.TARGET / config / module["name"]
                report_path = directory / "analysis-report.json"
                report = reflection.read_json(report_path)
                require(report["mode"] == "libtooling", "Unexpected analysis mode")
                require([t["cpp_name"] for t in report["types"]] == sorted(module["types"]), "Missing or duplicate complete types")
                for type_ in report["types"]:
                    require(not Path(type_["source"]).is_absolute() and type_["line"] > 0, "Invalid source identity")
                    require(type_["kind"] == ("struct" if type_["cpp_name"] == "OvMaths::FVector3" else "class"), "Lost AST declaration kind")
                    require((ROOT / type_["source"]).is_file(), "Unresolvable source path")
                snapshot = report_path.read_bytes()
                reflection.execute([*cli, "analyze", "--output", output, "--config", config, "--generator", args.generator])
                require(report_path.read_bytes() == snapshot, "Nondeterministic AST report")
    # Deliberate source error must propagate with the actual fixture path + location.
    directory = output / reflection.TARGET / "Debug/OvMaths"
    entry = reflection.read_json(directory / "compile_commands.json")[0]
    translation_unit = Path(entry["file"])
    report_path = directory / ("syntax-report.json" if args.syntax_only else "analysis-report.json")
    original = translation_unit.read_text(encoding="utf-8")
    try:
        translation_unit.write_text(original + '#include "' + (ROOT / "Tests/Reflection/Fixtures/SyntaxError.h").as_posix() + '"\n', encoding="utf-8")
        failure = reflection.execute([*cli, "analyze", "--output", output, "--config", "Debug", *mode], check=False)
        require(failure.returncode != 0, "Invalid syntax unexpectedly succeeded")
        require(re.search(r"SyntaxError\.h(?:\(|:)\d+", failure.stderr), "Failure omitted source location")
        if args.syntax_only:
            require(not reflection.read_json(report_path)["success"], "Stale syntax success")
        else:
            require(not report_path.exists(), "Failed analysis retained stale success report")
    finally:
        translation_unit.write_text(original, encoding="utf-8")
    # A missing dependency must fail, without substituting fake third-party types.
    try:
        translation_unit.write_text(original + '#include "OvReflectGenMissingDependency.h"\n', encoding="utf-8")
        failure = reflection.execute([*cli, "analyze", "--output", output, "--config", "Debug", *mode], check=False)
        require(failure.returncode != 0 and "OvReflectGenMissingDependency.h" in failure.stderr, "Missing dependency did not fail")
    finally:
        translation_unit.write_text(original, encoding="utf-8")
    if not args.syntax_only:
        missing = reflection.execute([args.generator, "-p", directory, "--report=" + str(report_path),
                                      "--source-root=" + str(ROOT), "--module=OvMaths", "--configuration=Debug",
                                      "--expect=OvMaths::DoesNotExist", translation_unit], check=False)
        require(missing.returncode != 0 and "expected complete type definition not found" in missing.stderr,
                "Missing target definition did not fail")
        require(not report_path.exists(), "Missing type retained stale success report")
    print(reflection.execute([*cli, "analyze", "--output", output, *mode]).stdout, end="")
    reflection.write_json(output / "verification.json", {
        "stage": "R0", "mode": "syntax-only" if args.syntax_only else "libtooling",
        "passed": True, "libtooling_verified": not args.syntax_only,
        "checks": ["fresh-output", "vs-parameter-parity", "deterministic-arguments",
                   "real-headers-all-configurations", "source-error-location",
                   "missing-dependency", "stale-success-rejected"]
    })
    print(f"PASS: {'diagnostic bootstrap only' if args.syntax_only else 'R0 LibTooling acceptance'}; evidence: {output}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError) as error:
        print(f"R0 verification failed: {error}", file=sys.stderr)
        sys.exit(1)
