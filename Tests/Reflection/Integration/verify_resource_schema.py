#!/usr/bin/env python3
"""Exercise resource adapters through the real Clang AST and schema validator."""
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "Build/Tests/ResourceSchema"
OUT.mkdir(parents=True, exist_ok=True)
entry = json.loads((ROOT / "Build/Generated/Reflection/x86_64-pc-windows-msvc/Debug/OvCore/compile_commands.json").read_text())[0]
fixture = OUT / "Fixture.h"
entry["arguments"] = [str(fixture) if arg == entry["file"] else arg for arg in entry["arguments"]]
entry["file"] = str(fixture)
(OUT / "compile_commands.json").write_text(json.dumps([entry]), encoding="utf-8")
prefix = '''#include <OvReflection/Annotations.h>
#include <OvCore/Resources/ResourceReference.h>
#include <array>
#include <vector>
namespace OvRendering::Resources { class Model; }
using ModelRef = OvCore::Resources::ResourceReference<OvRendering::Resources::Model>;
struct OV_TYPE() Fixture {
'''
cases = [
    ("positive", 'OV_FIELD(Serialize, Missing = Default, Default = "?") ModelRef model;\nOV_FIELD(Serialize, ItemName = "material") std::array<ModelRef, 255> materials;', None),
    ("requires_item_name", 'OV_FIELD(Serialize) std::array<ModelRef, 255> materials;', "ItemName is required"),
    ("reject_wrong_item_name", 'OV_FIELD(Serialize, ItemName = "bad/name") std::array<ModelRef, 255> materials;', "ItemName must be"),
    ("reject_scalar_item_name", 'OV_FIELD(Serialize, ItemName = "model") ModelRef model;', "ItemName is required exclusively"),
    ("reject_raw_pointer", 'OV_FIELD(Serialize) OvRendering::Resources::Model* model;', "unsupported reflected type"),
    ("reject_script_resource", 'OV_FIELD(Serialize, Script) ModelRef model;', "require Serialize only"),
    ("reject_inspector_resource", 'OV_FIELD(Serialize, Inspect) ModelRef model;', "require Serialize only"),
    ("reject_unbounded_vector", 'OV_FIELD(Serialize) std::vector<ModelRef> models;', "unsupported reflected type"),
    ("reject_large_array", 'OV_FIELD(Serialize, ItemName = "model") std::array<ModelRef, 4097> models;', "unsupported reflected type"),
    ("reject_zero_array", 'OV_FIELD(Serialize, ItemName = "model") std::array<ModelRef, 0> models;', "unsupported reflected type"),
    ("reject_non_null_default", 'OV_FIELD(Serialize, Missing = Default, Default = "robot.fbx") ModelRef model;', "Default does not match"),
    ("reject_wrong_resource", 'OV_FIELD(Serialize) OvCore::Resources::ResourceReference<int> model;', "unsupported reflected type"),
    ("reject_const_resource", 'OV_FIELD(Serialize) OvCore::Resources::ResourceReference<const OvRendering::Resources::Model> model;', "unsupported reflected type"),
    ("reject_const_array_item", 'OV_FIELD(Serialize, ItemName = "asset") std::array<const ModelRef, 2> models;', "unsupported reflected type"),
    ("positive_capacity", 'OV_FIELD(Serialize, ItemName = "asset") std::array<ModelRef, 4096> models;', None),
]
results = []
for name, body, diagnostic in cases:
    fixture.write_text(prefix + body + "\n};\n", encoding="utf-8")
    report = OUT / "analysis-report.json"
    command = [str(ROOT / "Build/Tools/OvReflectGen/Release/OvReflectGen.exe"), "-p", str(OUT),
               "--report=" + str(report), "--source-root=" + str(ROOT), "--module=ResourceSchema",
               "--configuration=Debug", "--expect=Fixture", str(fixture)]
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, encoding="utf-8", errors="replace")
    text = result.stdout + result.stderr
    (OUT / (name + ".log")).write_text(text, encoding="utf-8")
    passed = (result.returncode == 0 and report.exists()) if diagnostic is None else (result.returncode != 0 and diagnostic in text and not report.exists())
    if not passed: raise RuntimeError(name + " failed:\n" + text)
    if name == "positive":
        schema = json.loads((OUT / "schema.json").read_text())
        categories = {p["type"]["category"] for t in schema["types"] for p in t["properties"]}
        if categories != {"resource-reference", "resource-array"}: raise RuntimeError("Lost typed adapter categories")
    results.append({"name": name, "passed": True})
    print("PASS: " + name, flush=True)
(OUT / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
