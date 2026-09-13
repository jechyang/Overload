#!/usr/bin/env python3
"""Host-tool build, baked Premake export, R0 analysis and R1 schema (stdlib only)."""
import argparse
from contextlib import ExitStack
from copy import copy
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from Emitters.cpp import emit as emit_cpp, OUTPUTS as CPP_OUTPUTS
import incremental

if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "Tools/OvReflectGen"
LOCK = json.loads((TOOL / "toolchain.lock.json").read_text(encoding="utf-8"))
TARGET = LOCK["target"]


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def write_json(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    incremental.replace_if_changed(path, incremental.json_bytes(data))


def execute(args, *, env=None, check=True):
    result = subprocess.run([str(a) for a in args], cwd=ROOT, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            encoding="utf-8", errors="replace")
    if check and result.returncode:
        raise RuntimeError(f"Command failed ({result.returncode}): {subprocess.list2cmdline([str(a) for a in args])}\n"
                           + result.stdout + result.stderr)
    return result


def latest_directory(parent, required):
    candidates = [p for p in Path(parent).glob("*") if (p / required).exists()]
    if not candidates:
        raise RuntimeError(f"Cannot find {required} under {parent}")
    return max(candidates, key=lambda p: tuple(int(n) for n in re.findall(r"\d+", p.name)))


def discover(args):
    if os.name != "nt":
        raise RuntimeError("R0 target environment discovery currently requires Windows/MSVC.")
    vs = Path(args.vs_root) if args.vs_root else None
    if vs is None:
        vswhere = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / \
            "Microsoft Visual Studio/Installer/vswhere.exe"
        if vswhere.exists():
            result = execute([vswhere, "-latest", "-products", "*", "-requires",
                              "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                              "-property", "installationPath"])
            if result.stdout.strip():
                vs = Path(result.stdout.strip())
        if vs is None:
            for base in (Path("C:/Program Files/Microsoft Visual Studio/2022"),
                         Path("C:/Program Files (x86)/Microsoft Visual Studio/2022")):
                for candidate in base.glob("*"):
                    if (candidate / "VC/Tools/MSVC").is_dir():
                        vs = candidate
                        break
    if vs is None:
        raise RuntimeError("Visual Studio C++ tools not found; supply --vs-root.")
    msvc = Path(args.msvc_root) if args.msvc_root else latest_directory(vs / "VC/Tools/MSVC", "include/vector")
    sdk = Path(args.sdk_root or "C:/Program Files (x86)/Windows Kits/10")
    sdk_include = sdk / "Include" / args.sdk_version if args.sdk_version else latest_directory(sdk / "Include", "um/Windows.h")
    llvm = Path(args.llvm_root) if args.llvm_root else ROOT / "Build/Toolchains" / ("llvm-" + LOCK["llvm_version"])
    if not (llvm / "bin/clang.exe").is_file() and not args.llvm_root:
        llvm = vs / "VC/Tools/Llvm/x64"
    clang = llvm / "bin/clang.exe"
    if not clang.is_file():
        raise RuntimeError("clang.exe missing; run Scripts/Windows/InstallReflectionToolchain.ps1 or supply --llvm-root.")
    version = execute([clang, "--version"]).stdout
    if not re.search(r"clang version " + re.escape(LOCK["llvm_version"]) + r"\b", version):
        raise RuntimeError(f"Expected Clang {LOCK['llvm_version']}, found:\n{version}")
    resource_dir = Path(execute([clang, "-print-resource-dir"]).stdout.strip())
    system_includes = [msvc / "include", sdk_include / "ucrt", sdk_include / "shared",
                       sdk_include / "um", sdk_include / "winrt", sdk_include / "cppwinrt"]
    for include in [resource_dir / "include", *system_includes]:
        if not include.is_dir():
            raise RuntimeError(f"Required real toolchain include directory missing: {include}")
    cl = msvc / "bin/Hostx64/x64/cl.exe"
    if not cl.is_file():
        raise RuntimeError(f"MSVC compiler missing: {cl}")
    compiler_banner = execute([cl], check=False)
    required = ["include/clang/Tooling/Tooling.h", "lib/clangTooling.lib",
                "lib/cmake/llvm/LLVMConfig.cmake", "lib/cmake/clang/ClangConfig.cmake"]
    missing = [p for p in required if not (llvm / p).is_file()]
    return {
        "llvm_version": LOCK["llvm_version"], "llvm_root": llvm.resolve().as_posix(),
        "clang": clang.resolve().as_posix(), "clang_banner": version.strip(),
        "resource_dir": resource_dir.resolve().as_posix(),
        "vs_root": vs.resolve().as_posix(), "msvc_root": msvc.resolve().as_posix(),
        "msvc_version": msvc.name, "msvc_banner": (compiler_banner.stdout + compiler_banner.stderr).strip(),
        "sdk_root": sdk.resolve().as_posix(), "sdk_version": sdk_include.name,
        "system_includes": [p.resolve().as_posix() for p in system_includes],
        "target": TARGET, "libtooling_available": not missing, "missing_development_files": missing
    }


def compiler_arguments(cfg, tc):
    if cfg["system"] != "windows" or cfg["platform"] != "x64" or cfg["toolset"] != "msc":
        raise RuntimeError(f"Unsupported baked target: {cfg}")
    if cfg["cppdialect"] != "C++20":
        raise RuntimeError("R0 contract requires the target's C++20 dialect.")
    # Use the CL driver so MSVC command-line options retain their original meaning.
    args = [tc["clang"], "--driver-mode=cl", "--target=" + TARGET, "/nologo", "/TP", "/std:c++20",
            "/clang:-fsyntax-only", "/clang:-resource-dir=" + tc["resource_dir"],
            "/clang:-nostdinc"]
    # Full MSVC compatibility version follows the actual compiler, not a guessed default.
    match = re.search(r"(19\.\d+\.\d+)", tc["msvc_banner"])
    if not match:
        raise RuntimeError("Cannot extract MSVC compatibility version from cl.exe banner.")
    args += ["/clang:-fms-compatibility-version=" + match[1]]
    runtime = cfg.get("runtime") or ("Debug" if cfg.get("symbols") == "On" and cfg.get("optimize") in (None, "Off", "Debug") else "Release")
    args += [("/MT" if cfg.get("staticruntime") == "On" else "/MD") + ("d" if runtime == "Debug" else "")]
    args += ["/EHs-c-" if cfg.get("exceptionhandling") == "Off" else "/EHa" if cfg.get("exceptionhandling") == "SEH" else "/EHsc"]
    args += ["/GR-" if cfg.get("rtti") == "Off" else "/GR"]
    charset = cfg.get("characterset", "Unicode")
    if charset in ("Default", "Unicode"):
        args += ["/DUNICODE", "/D_UNICODE"]
    elif charset == "MBCS":
        args += ["/D_MBCS"]
    if cfg.get("nativewchar") == "Off":
        args += ["/Zc:wchar_t-"]
    if cfg.get("unsignedchar") == "On":
        args += ["/J"]
    if cfg.get("structmemberalign"):
        args += ["/Zp" + str(cfg["structmemberalign"])]
    if cfg.get("floatingpoint") in ("Fast", "Strict", "Precise"):
        args += ["/fp:" + cfg["floatingpoint"].lower()]
    if cfg.get("vectorextensions"):
        args += ["/arch:" + cfg["vectorextensions"]]
    if cfg.get("conformancemode") == "On":
        args += ["/permissive-"]
    if cfg.get("usestandardpreprocessor") == "On":
        args += ["/Zc:preprocessor"]
    # Premake stores escaped quote characters for IDE emission. JSON argv must contain
    # the actual quote character, with no shell escaping layer.
    args += ["/D" + value.replace('\\"', '"') for value in cfg.get("defines", [])]
    args += ["/U" + value for value in cfg.get("undefines", [])]
    args += ["/I" + str(value) for value in cfg.get("includedirs", [])]
    for value in [*cfg.get("externalincludedirs", []), tc["resource_dir"] + "/include", *tc["system_includes"]]:
        args += ["/clang:-isystem", "/clang:" + str(value)]
    args += ["/FI" + str(value) for value in cfg.get("forceincludes", [])]
    # Only discard output/concurrency switches; all other baked options reach Clang.
    args += [value for value in cfg.get("buildoptions", []) if value not in ("/FS", "/bigobj")]
    return args


def modules():
    manifest = read_json(TOOL / "scan-modules.json")
    if manifest["version"] != 1:
        raise RuntimeError("Unsupported scan manifest version.")
    entries = manifest["modules"]
    names = [m["name"] for m in entries]
    if len(set(names)) != len(names) or any(not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name) for name in names):
        raise ValueError("Scan modules need unique C++ identifier names")
    return entries


def export(args, tc):
    output = Path(args.output).resolve()
    with incremental.writer_lock(ROOT / "Build/Tools/premake-export.lock"), ExitStack() as stack:
        for config in ("Debug", "Release", "Publish"):
            stack.enter_context(incremental.writer_lock(output / ".locks" / (TARGET + "-" + config + ".lock")))
        return export_locked(args, tc)


def export_locked(args, tc):
    output = Path(args.output).resolve()
    baked = output / "Premake"
    result = execute([ROOT / "Dependencies/premake5/bin/premake5.exe",
                      "--reflection-config-out=" + baked.as_posix(), "--reflection-python=" + sys.executable, "vs2022"])
    print(result.stdout, end="")
    write_json(output / "toolchain.json", tc)
    for module in modules():
        data = read_json(baked / (module["name"] + ".json"))
        for cfg in data["configurations"]:
            directory = output / TARGET / cfg["configuration"] / module["name"]
            directory.mkdir(parents=True, exist_ok=True)
            translation_unit = directory / "Reflection.scan.cpp"
            # No generated reflection header and no fake dependency declarations.
            text = "// R0 module scan. Generated from scan-modules.json; do not edit.\n"
            text += "".join(f'#include <{header}>\n' for header in module["headers"])
            text += "".join(f'static_assert(sizeof(::{name}) > 0);\n' for name in module["types"])
            incremental.replace_if_changed(translation_unit, text.encode("utf-8"))
            analysis_options = ["/DOV_REFLECTION_ANALYSIS=1", "/I" + (ROOT / "Sources/OvReflection/include").as_posix(),
                                "/clang:-Werror=ignored-attributes", "/clang:-Werror=unknown-attributes"]
            command = compiler_arguments(cfg, tc) + analysis_options + [translation_unit.as_posix()]
            entry = {"directory": ROOT.as_posix(), "file": translation_unit.as_posix(), "arguments": command}
            write_json(directory / "compile_commands.json", [entry])
            write_json(directory / "analysis-input.json", {
                "schema_version": LOCK["schema_version"], "generator_version": LOCK["generator_version"],
                "premake": data, "selected_configuration": cfg, "toolchain": tc,
                "scan_manifest": module, "command": entry,
                "analysis_only_options": analysis_options,
                "omitted_nonsemantic_options": [x for x in cfg.get("buildoptions", []) if x in ("/FS", "/bigobj")]
            })
    print(f"Exported Debug/Release/Publish analysis inputs: {output}")


def clean_environment():
    env = os.environ.copy()
    # Do not let a developer prompt inject invisible command-line switches or headers.
    for name in ("CL", "_CL_", "INCLUDE", "CPATH", "CPLUS_INCLUDE_PATH", "C_INCLUDE_PATH"):
        env.pop(name, None)
    return env


def input_summary(directory, generator):
    """Evidence for R3, not an incremental cache or a promise to skip analysis."""
    directory = Path(directory)
    files = read_json(directory / "dependencies.json")["files"]
    inputs = []
    for name in ("compile_commands.json", "analysis-input.json"):
        path = directory / name
        if path.is_file():
            inputs.append({"path": path.as_posix(), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
    for path in [Path(generator), TOOL / "toolchain.lock.json", TOOL / "scan-modules.json",
                 *sorted(TOOL.rglob("*.cpp")), *sorted(TOOL.rglob("*.h")), *sorted(TOOL.rglob("*.py"))]:
        inputs.append({"path": path.resolve().as_posix(), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
    data = {"schema_version": LOCK["schema_version"], "generator_version": LOCK["generator_version"],
            "algorithm": "sha256", "dependencies": files, "inputs": sorted(inputs, key=lambda x: x["path"])}
    data["input_sha256"] = hashlib.sha256(json.dumps(data, sort_keys=True, separators=(",", ":")).encode("utf-8")).hexdigest()
    write_json(directory / "input-summary.json", data)


def analyze(args):
    output = Path(args.output).resolve()
    failures = 0
    for config in args.config:
        with incremental.writer_lock(output / ".locks" / (TARGET + "-" + config + ".lock")):
            local = copy(args)
            local.config = [config]
            (output / TARGET / config / "generation-state.json").unlink(missing_ok=True)
            failures += analyze_locked(local)
    return 1 if failures else 0


def analyze_locked(args):
    output = Path(args.output).resolve()
    failures = 0
    for config in args.config:
        for module in modules():
            directory = output / TARGET / config / module["name"]
            report_name = "syntax-report.json" if args.syntax_only else "analysis-report.json"
            report_path = directory / report_name
            report_path.unlink(missing_ok=True)
            if not args.syntax_only:
                for name in ("schema.json", "dependencies.json", "input-summary.json", *CPP_OUTPUTS):
                    (directory / name).unlink(missing_ok=True)
            entry = read_json(directory / "compile_commands.json")[0]
            if args.syntax_only:
                command = entry["arguments"]
            else:
                generator = Path(args.generator or (ROOT / "Build/Tools/OvReflectGen/Release/OvReflectGen.exe"))
                if not generator.is_file():
                    raise RuntimeError(f"OvReflectGen not built: {generator}. Build it first; --syntax-only is diagnostic evidence only.")
                command = [generator, "-p", directory, "--report=" + report_path.as_posix(),
                           "--source-root=" + ROOT.as_posix(), "--module=" + module["name"],
                           "--configuration=" + config, *["--expect=" + name for name in module["types"]], entry["file"]]
            result = execute(command, env=clean_environment(), check=False)
            log = result.stdout + result.stderr
            (directory / ("syntax-diagnostics.txt" if args.syntax_only else "diagnostics.txt")).write_text(log, encoding="utf-8")
            if args.syntax_only:
                write_json(report_path, {
                    "stage": "R0", "mode": "clang-syntax-only", "libtooling_verified": False,
                    "configuration": config, "module": module["name"],
                    "headers": module["headers"], "complete_types_checked": module["types"],
                    "exit_code": result.returncode, "success": result.returncode == 0,
                    "compile_commands_sha256": hashlib.sha256((directory / "compile_commands.json").read_bytes()).hexdigest()
                })
            elif result.returncode == 0:
                try:
                    input_summary(directory, generator)
                except (OSError, ValueError, KeyError):
                    for name in ("analysis-report.json", "schema.json", "dependencies.json", "input-summary.json"):
                        (directory / name).unlink(missing_ok=True)
                    raise
            print(f"{config}/{module['name']}: {'PASS' if result.returncode == 0 else 'FAIL'} ({report_name})")
            if result.returncode:
                failures += 1
                print(log, file=sys.stderr)
    return 1 if failures else 0


def generate(args):
    return incremental.generate(sys.modules[__name__], args)


def build(args, tc):
    if not tc["libtooling_available"]:
        raise RuntimeError("LLVM compiler is present, but LibTooling development package is missing:\n  "
                           + "\n  ".join(tc["missing_development_files"])
                           + "\nRun Scripts/Windows/InstallReflectionToolchain.ps1.")
    binary = ROOT / "Build/Tools/OvReflectGen"
    cmake = args.cmake or shutil.which("cmake")
    if not cmake:
        raise RuntimeError("CMake 3.24+ is required; supply --cmake.")
    # Explicit host environment avoids registry-dependent vcvars discovery and
    # keeps the host linker/CRT aligned with the recorded target installation.
    env = clean_environment()
    env["INCLUDE"] = ";".join(tc["system_includes"])
    lib_dirs = [Path(tc["msvc_root"]) / "lib/x64",
                Path(tc["sdk_root"]) / "Lib" / tc["sdk_version"] / "ucrt/x64",
                Path(tc["sdk_root"]) / "Lib" / tc["sdk_version"] / "um/x64"]
    for directory in lib_dirs:
        if not directory.is_dir():
            raise RuntimeError(f"Required host library directory missing: {directory}")
    env["LIB"] = ";".join(str(p) for p in lib_dirs)
    previous_path = os.environ.get("PATH", "")
    for key in list(env):
        if key.lower() == "path":
            del env[key]
    sdk_bin = Path(tc["sdk_root"]) / "bin" / tc["sdk_version"] / "x64"
    env["PATH"] = str(Path(tc["msvc_root"]) / "bin/Hostx64/x64") + os.pathsep + str(sdk_bin) + os.pathsep + previous_path
    ninja = Path(tc["vs_root"]) / "Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"
    result = execute([cmake, "-S", TOOL, "-B", binary, "-G", "Ninja",
                      "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_MAKE_PROGRAM=" + ninja.as_posix(),
                      "-DCMAKE_RC_COMPILER=" + (sdk_bin / "rc.exe").as_posix(),
                      "-DCMAKE_MT=" + (sdk_bin / "mt.exe").as_posix(),
                      "-DCMAKE_C_COMPILER=" + (Path(tc["msvc_root"]) / "bin/Hostx64/x64/cl.exe").as_posix(),
                      "-DCMAKE_CXX_COMPILER=" + (Path(tc["msvc_root"]) / "bin/Hostx64/x64/cl.exe").as_posix(),
                      "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=" + (binary / "Release").as_posix(),
                      "-DOV_DIA_LIBRARY=" + (Path(tc["vs_root"]) / "DIA SDK/lib/amd64/diaguids.lib").as_posix(),
                      "-DLLVM_DIR=" + tc["llvm_root"] + "/lib/cmake/llvm",
                      "-DClang_DIR=" + tc["llvm_root"] + "/lib/cmake/clang"], env=env)
    print(result.stdout, end="")
    result = execute([cmake, "--build", binary, "--parallel", "2"], env=env)
    print(result.stdout, end="")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("probe", "export", "build", "analyze", "generate", "ensure", "clean"))
    parser.add_argument("--llvm-root")
    parser.add_argument("--vs-root")
    parser.add_argument("--msvc-root", default=os.environ.get("OV_REFLECTION_MSVC_ROOT"))
    parser.add_argument("--sdk-root")
    parser.add_argument("--sdk-version", default=os.environ.get("OV_REFLECTION_SDK_VERSION"))
    parser.add_argument("--cmake")
    parser.add_argument("--output", default=str(ROOT / "Build/Generated/Reflection"))
    parser.add_argument("--config", nargs="+", choices=("Debug", "Release", "Publish"),
                        default=["Debug", "Release", "Publish"])
    parser.add_argument("--generator")
    parser.add_argument("--baked", default=str(TOOL / "Projects/Premake"))
    parser.add_argument("--syntax-only", action="store_true",
                        help="Diagnostic bootstrap with clang.exe, NOT LibTooling acceptance.")
    args = parser.parse_args()
    try:
        if args.action == "clean":
            for config in args.config:
                with incremental.writer_lock(Path(args.output) / ".locks" / (TARGET + "-" + config + ".lock")):
                    incremental.invalidate(sys.modules[__name__], Path(args.output) / TARGET / config)
            return 0
        if args.action == "ensure":
            return incremental.generate(sys.modules[__name__], args, bootstrap=True)
        if args.action == "generate":
            if args.syntax_only:
                raise ValueError("--syntax-only cannot generate C++")
            return generate(args)
        if args.action == "analyze":
            return analyze(args)
        tc = discover(args)
        if args.action == "probe":
            write_json(Path(args.output) / "toolchain.json", tc)
            print(json.dumps(tc, indent=2))
        elif args.action == "export":
            export(args, tc)
        else:
            incremental.ensure_tool(sys.modules[__name__], args, tc)
        return 0
    except (OSError, ValueError, KeyError, RuntimeError) as error:
        print(f"OvReflectGen: error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
