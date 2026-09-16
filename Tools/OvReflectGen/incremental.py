"""R3 single-writer generation; no third-party Python dependencies."""
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile
import time


def digest(data):
    return hashlib.sha256(data).hexdigest()


def json_bytes(value):
    return (json.dumps(value, indent=2, ensure_ascii=False) + "\n").encode("utf-8")


def replace_if_changed(path, data):
    path = Path(path)
    if path.is_file() and path.read_bytes() == data:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=".publish-", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        Path(temporary).unlink(missing_ok=True)
    return True


@contextmanager
def writer_lock(path, timeout=600):
    """OS-owned lock: process death releases it; never unlink an active lock inode."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a+b") as stream:
        if stream.tell() == 0:
            stream.write(b"0")
            stream.flush()
        started = time.monotonic()
        while True:
            try:
                stream.seek(0)
                if os.name == "nt":
                    import msvcrt
                    msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
                else:
                    import fcntl
                    fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
                break
            except OSError:
                if time.monotonic() - started >= timeout:
                    raise RuntimeError(f"Timed out waiting for reflection writer: {path}")
                time.sleep(0.1)
        try:
            yield
        finally:
            stream.seek(0)
            if os.name == "nt":
                msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(stream, fcntl.LOCK_UN)


def safe_file(directory, name):
    # Manifests own flat module outputs only. Never follow a symlink or a junction.
    directory = Path(directory)
    if directory.resolve() != directory.absolute():
        raise ValueError(f"Reflection module directory redirects through a link: {directory}")
    if not name or Path(name).name != name or "/" in name or "\\" in name or ":" in name or name in (".", ".."):
        raise ValueError(f"Unsafe reflection manifest entry: {name!r}")
    path = directory / name
    if path.resolve().parent != directory.resolve() or path.is_symlink() or path.is_dir():
        raise ValueError(f"Reflection output escapes its manifest directory: {path}")
    return path


def owned_files(r, directory):
    manifest = safe_file(directory, "manifest.json")
    if not manifest.exists():
        return []
    data = r.read_json(manifest)
    if data.get("module") != directory.name:
        raise ValueError(f"Manifest module mismatch: {manifest}")
    return [safe_file(directory, entry["path"]) for entry in data["files"]]


def invalidate(r, base):
    (base / "generation-state.json").unlink(missing_ok=True)
    # Only named protocol outputs inside recorded/known module directories.
    for manifest in base.glob("*/manifest.json"):
        for path in owned_files(r, manifest.parent):
            path.unlink(missing_ok=True)
        manifest.unlink(missing_ok=True)
    for module in r.modules():
        directory = base / module["name"]
        for name in (*r.CPP_OUTPUTS, "input-summary.json", "analysis-report.json", "schema.json", "dependencies.json"):
            safe_file(directory, name).unlink(missing_ok=True)


def tool_inputs(r):
    paths = [*r.TOOL.rglob("*.cpp"), *r.TOOL.rglob("*.h"), *r.TOOL.rglob("*.py"),
             *r.TOOL.rglob("*.lua"), r.TOOL / "CMakeLists.txt", r.TOOL / "toolchain.lock.json"]
    return [{"path": p.as_posix(), "sha256": digest(p.read_bytes())} for p in sorted(set(paths))]


def ensure_tool(r, args, tc):
    binary = r.ROOT / "Build/Tools/OvReflectGen"
    executable = Path(args.generator or binary / "Release/OvReflectGen.exe")
    if args.generator:
        if not executable.is_file():
            raise RuntimeError(f"Generator missing: {executable}")
        return executable
    with writer_lock(binary / "writer.lock"):
        receipt_path = binary / "build-receipt.json"
        key = {"toolchain": tc, "sources": tool_inputs(r)}
        receipt = r.read_json(receipt_path) if receipt_path.is_file() else {}
        if receipt.get("inputs") != key or not executable.is_file() or receipt.get("binary") != digest(executable.read_bytes()):
            receipt_path.unlink(missing_ok=True)
            r.build(args, tc)
            replace_if_changed(receipt_path, json_bytes({"inputs": key, "binary": digest(executable.read_bytes())}))
    return executable


def include_inventory(cfg, tc, r):
    # Entered buffers alone miss a newly created higher-priority include or __has_include.
    # Track names (not mtimes) in search directories, including real system directories.
    directories = [*cfg.get("includedirs", []), *cfg.get("externalincludedirs", []),
                   *tc["system_includes"], tc["resource_dir"] + "/include"]
    result = {}
    for name in sorted(set(directories)):
        directory = Path(name).resolve()
        if directory == r.ROOT or r.ROOT / "Build" in directory.parents:
            # Build-only generated include paths are stripped from scan options below.
            continue
        result[directory.as_posix()] = sorted(p.relative_to(directory).as_posix()
            for p in directory.rglob("*") if p.is_file()) if directory.is_dir() else None
    return digest(json_bytes(result))


def prepare(r, output, baked, tc, config):
    prepared = {}
    for module in r.modules():
        data = r.read_json(baked / (module["name"] + ".json"))
        for name, content in data.get("project_inputs", {}).items():
            # Lua edits require rebaking both compiler projects and analysis options.
            # Never regenerate a loaded solution underneath an active MSBuild graph.
            if (r.ROOT / name).read_bytes().decode("utf-8") != content:
                raise RuntimeError(f"Premake input changed: {name}; run gen_proj or premake5 vs2022 before building")
        cfg = next(c for c in data["configurations"] if c["configuration"] == config)
        directory = output / r.TARGET / config / module["name"]
        source = directory / "Reflection.scan.cpp"
        text = "// R0 module scan. Generated from scan-modules.json; do not edit.\n"
        text += "".join(f'#include <{h}>\n' for h in module["headers"])
        text += "".join(f'static_assert(sizeof(::{t}) > 0);\n' for t in module["types"])
        replace_if_changed(source, text.encode("utf-8"))
        options = ["/DOV_REFLECTION_ANALYSIS=1", "/I" + (r.ROOT / "Sources/OvReflection/include").as_posix(),
                   "/clang:-Werror=ignored-attributes", "/clang:-Werror=unknown-attributes"]
        command = r.compiler_arguments(cfg, tc) + options + [source.as_posix()]
        # These paths are for compiled consumers only; scans cannot use generated headers.
        command = [a for a in command if not (a.startswith("/I") and
            (Path(a[2:]).resolve() == r.ROOT or r.ROOT / "Build/Generated/Reflection" in Path(a[2:]).resolve().parents))]
        entry = {"directory": r.ROOT.as_posix(), "file": source.as_posix(), "arguments": command}
        r.write_json(directory / "compile_commands.json", [entry])
        provenance = {"schema_version": r.LOCK["schema_version"], "generator_version": r.LOCK["generator_version"],
                      "selected_configuration": cfg, "toolchain": tc, "scan_manifest": module,
                      "command": entry, "analysis_only_options": options}
        r.write_json(directory / "analysis-input.json", provenance)
        prepared[module["name"]] = {"directory": directory, "module": module, "entry": entry,
                                   "inventory": include_inventory(cfg, tc, r)}
    return prepared


def generate_config(r, args, config, *, bootstrap=False):
    output = Path(args.output).resolve()
    base = output / r.TARGET / config
    started = time.monotonic()
    # Lock resides outside the published tree; one writer for ALL modules per config.
    with writer_lock(output / ".locks" / (r.TARGET + "-" + config + ".lock")):
        stage = None
        try:
            if bootstrap:
                baked = Path(args.baked).resolve()
                # Exported projects are inputs, not outputs of the build-time generator.
                tc = r.discover(args)
                generator = ensure_tool(r, args, tc)
                prepared = prepare(r, output, baked, tc, config)
            else:
                tc = r.read_json(output / "toolchain.json")
                generator = Path(args.generator or r.ROOT / "Build/Tools/OvReflectGen/Release/OvReflectGen.exe")
                prepared = prepare(r, output, output / "Premake", tc, config)
            global_key = {"sources": tool_inputs(r), "generator": digest(generator.read_bytes()),
                          "manifest": digest((r.TOOL / "scan-modules.json").read_bytes()), "toolchain": tc}
            previous = r.read_json(base / "generation-state.json") if (base / "generation-state.json").is_file() else {}
            old_modules = previous.get("modules", {})
            changed, keys = [], {}
            # Hash each physical dependency once per invocation, even if modules share it.
            hashes = {}
            def current_hash(path):
                if path not in hashes:
                    hashes[path] = digest(Path(path).read_bytes()) if Path(path).is_file() else None
                return hashes[path]
            for name, item in prepared.items():
                directory = item["directory"]
                keys[name] = digest(json_bytes({"global": global_key, "inventory": item["inventory"],
                    "inputs": [digest((directory / n).read_bytes()) for n in
                               ("Reflection.scan.cpp", "compile_commands.json", "analysis-input.json")]}))
                old = old_modules.get(name, {})
                if (old.get("key") != keys[name] or
                    not old.get("dependencies") or not old.get("outputs") or
                    any(current_hash(d["path"]) != d["sha256"] for d in old.get("dependencies", [])) or
                    any(current_hash(str(safe_file(directory, d["path"]))) != d["sha256"] for d in old.get("outputs", []))):
                    changed.append(name)
            if not changed and set(old_modules) == set(prepared):
                print(f"R3 {config}: up-to-date; analyzed=0; published=0; seconds={time.monotonic()-started:.3f}")
                return 0
            # The commit marker is absent until the complete configuration is ready.
            (base / "generation-state.json").unlink(missing_ok=True)
            base.mkdir(parents=True, exist_ok=True)
            stage = Path(tempfile.mkdtemp(prefix=".transaction-", dir=base))
            documents = {}
            for name, item in prepared.items():
                directory, target = item["directory"], stage / name
                target.mkdir()
                if name in changed:
                    r.write_json(target / "compile_commands.json", [item["entry"]])
                    command = [generator, "-p", target, "--report=" + (target / "analysis-report.json").as_posix(),
                               "--source-root=" + r.ROOT.as_posix(), "--module=" + name, "--configuration=" + config,
                               *["--expect=" + t for t in item["module"]["types"]], item["entry"]["file"]]
                    result = r.execute(command, env=r.clean_environment(), check=False)
                    replace_if_changed(directory / "diagnostics.txt", (result.stdout + result.stderr).encode("utf-8"))
                    if result.returncode:
                        raise RuntimeError(f"{config}/{name} analysis failed:\n" + result.stdout + result.stderr)
                else:
                    for filename in ("schema.json", "analysis-report.json", "dependencies.json"):
                        shutil.copyfile(directory / filename, target / filename)
                documents[name] = r.read_json(target / "schema.json")
            owned, owners = {}, {}
            declared_owners = {}
            for name, item in prepared.items():
                for cpp_name in item["module"]["types"]:
                    if cpp_name in declared_owners:
                        raise ValueError("Duplicate C++ type ownership: " + cpp_name)
                    declared_owners[cpp_name] = name
            for name, document in documents.items():
                owned[name] = []
                for t in document["types"]:
                    owner = declared_owners.get(t["cpp_name"])
                    if owner is None:
                        source = t["source"]["path"].replace("\\", "/")
                        if not source or source.startswith(f"Sources/{name}/") or f"/Sources/{name}/" in source:
                            owner = name
                            declared_owners[t["cpp_name"]] = owner
                    if owner is None:
                        continue
                    if owner == name:
                        owned[name].append(t)
                    if t["cpp_name"] in owners and owners[t["cpp_name"]] != owner:
                        raise ValueError("Duplicate C++ type ownership: " + t["cpp_name"])
                    owners[t["cpp_name"]] = owner
            state, published = {"version": 1, "configuration": config, "modules": {}}, 0
            for name, document in documents.items():
                dependencies = set()
                for t in document["types"]:
                    if t["cpp_name"] not in owners:
                        raise ValueError("Annotated dependency has no scan-module owner: " + t["cpp_name"])
                    if owners[t["cpp_name"]] != name:
                        dependencies.add("../" + owners[t["cpp_name"]] + "/Reflection.generated.h")
                r.emit_cpp(document, stage / name, types=owned[name], dependencies=dependencies, known_types=owners)
                directory, target = base / name, stage / name
                dependency_files = r.read_json(target / "dependencies.json")["files"]
                # Reject inputs edited while LibTooling was reading them. Do not cache a mixed snapshot.
                if any(digest(Path(d["path"]).read_bytes()) != d["sha256"] for d in dependency_files):
                    raise RuntimeError("Reflection inputs changed during generation; retry the build")
                r.write_json(target / "input-summary.json", {"schema_version": r.LOCK["schema_version"],
                    "generator_version": r.LOCK["generator_version"], "algorithm": "sha256",
                    "input_sha256": keys[name], "dependencies": dependency_files})
                filenames = ["analysis-report.json", "schema.json", "dependencies.json", "input-summary.json", *r.CPP_OUTPUTS]
                # Include every owned protocol output in the manifest, for bounded cleanup.
                manifest = r.read_json(target / "manifest.json")
                manifest["files"] = [{"path": n, "sha256": digest((target / n).read_bytes())}
                                     for n in filenames if n != "manifest.json"]
                r.write_json(target / "manifest.json", manifest)
                state["modules"][name] = {"key": keys[name], "dependencies": dependency_files,
                    "outputs": [{"path": n, "sha256": digest((target / n).read_bytes())} for n in filenames]}
            # Validate all cleanup entries before mutating the published files.
            stale = []
            for manifest in base.glob("*/manifest.json"):
                names = {d["path"] for d in state["modules"].get(manifest.parent.name, {}).get("outputs", [])}
                stale += [p for p in owned_files(r, manifest.parent) if p.name not in names]
                if manifest.parent.name not in prepared:
                    stale.append(manifest)
            for name, receipt in state["modules"].items():
                for item in receipt["outputs"]:  # manifest is last; commit marker follows all modules.
                    published += replace_if_changed(safe_file(base / name, item["path"]), (stage / name / item["path"]).read_bytes())
            for path in stale:
                path.unlink(missing_ok=True)
            replace_if_changed(base / "generation-state.json", json_bytes(state))
            print(f"R3 {config}: analyzed={','.join(changed) or 'none'}; published={published}; seconds={time.monotonic()-started:.3f}")
            return 0
        except Exception:
            invalidate(r, base)
            raise
        finally:
            if stage is not None:
                # The only recursive removal is our freshly allocated transaction directory.
                if stage.resolve().parent != base.resolve() or not stage.name.startswith(".transaction-"):
                    raise RuntimeError("Invalid reflection transaction directory")
                shutil.rmtree(stage)


def generate(r, args, *, bootstrap=False):
    for config in args.config:
        generate_config(r, args, config, bootstrap=bootstrap)
    return 0
