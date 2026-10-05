#!/usr/bin/env python3
"""Validate aggregate records and flattened wire names through real Clang analysis."""
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "Build/Tests/RecordSchema"
OUT.mkdir(parents=True, exist_ok=True)
entry = json.loads((ROOT / "Build/Generated/Reflection/x86_64-pc-windows-msvc/Debug/OvCore/compile_commands.json").read_text())[0]
fixture = OUT / "Fixture.h"
entry["arguments"] = [str(fixture) if arg == entry["file"] else arg for arg in entry["arguments"]]
entry["file"] = str(fixture)
(OUT / "compile_commands.json").write_text(json.dumps([entry]), encoding="utf-8")
prefix = '''#include <OvReflection/Annotations.h>
#include <OvMaths/FVector2.h>
#include <OvMaths/FVector4.h>
struct OV_TYPE(Record) Base { OV_FIELD(Serialize, Inspect) bool enabled = true; };
struct OV_TYPE(Record) Settings : Base { OV_FIELD(Serialize, Inspect, Alias = "old") float amount = 1; };
'''
cases = [
    ("inherited_nested", 'struct OV_TYPE() Fixture { OV_FIELD(Serialize, Inspect) Settings settings; };', None),
    ("flat_prefix", 'struct OV_TYPE() Fixture { OV_FIELD(Serialize, Inspect, Flatten, Prefix = "effect_") Settings first; OV_FIELD(Serialize, Flatten, Prefix = "other_") Settings second; };', None),
    ("flat_empty_prefix", 'struct OV_TYPE() Fixture { OV_FIELD(Serialize, Flatten, Prefix = "") Settings settings; };', None),
    ("math_vectors", 'struct OV_TYPE() Fixture { OV_FIELD(Serialize, Inspect, Min = 0, Max = 1) OvMaths::FVector2 position; OV_FIELD(Serialize, Inspect) OvMaths::FVector4 color; };', None),
    ("scalar_flatten", 'struct OV_TYPE() Fixture { OV_FIELD(Serialize, Flatten) float value; };', "Flatten requires"),
    ("prefix_without_flatten", 'struct OV_TYPE() Fixture { OV_FIELD(Serialize, Prefix = "x_") Settings value; };', "Prefix requires Flatten"),
    ("invalid_prefix", 'struct OV_TYPE() Fixture { OV_FIELD(Serialize, Flatten, Prefix = "x/y") Settings value; };', "Prefix must contain"),
    ("numeric_prefix", 'struct OV_TYPE() Fixture { OV_FIELD(Serialize, Flatten, Prefix = "123_") Settings value; };', "Prefix must start"),
    ("flatten_alias", 'struct OV_TYPE() Fixture { OV_FIELD(Serialize, Flatten, Alias = "old_group") Settings value; };', "Flatten aliases belong"),
    ("two_flat_collide", 'struct OV_TYPE() Fixture { OV_FIELD(Serialize, Flatten, Prefix = "x_") Settings first; OV_FIELD(Serialize, Flatten, Prefix = "x_") Settings second; };', "serialized key/alias conflict"),
    ("flat_leaf_collide", 'struct OV_TYPE() Fixture { OV_FIELD(Serialize, Flatten, Prefix = "x_") Settings first; OV_FIELD(Serialize, Name = "x_amount") float second; };', "serialized key/alias conflict"),
    ("flat_alias_collide", 'struct OV_TYPE() Fixture { OV_FIELD(Serialize, Flatten, Prefix = "x_") Settings first; OV_FIELD(Serialize, Name = "x_old") float second; };', "serialized key/alias conflict"),
    ("inherited_flat_collide", 'struct OV_TYPE() Parent { OV_FIELD(Serialize, Name = "x_enabled") bool active; }; struct OV_TYPE() Fixture : Parent { OV_FIELD(Serialize, Flatten, Prefix = "x_") Settings first; };', "serialized key/alias conflict"),
    ("non_aggregate", 'struct OV_TYPE(Record) Fixture { Fixture() {} OV_FIELD(Serialize) int value; };', "non-component aggregate"),
    ("unannotated_field", 'struct OV_TYPE(Record) Fixture { int value; };', "public writable OV_FIELD"),
    ("readonly_field", 'struct OV_TYPE(Record) Fixture { OV_FIELD(Serialize, ReadOnly) int value; };', "public writable OV_FIELD"),
    ("unreflected_base", 'struct Parent { int value; }; struct OV_TYPE(Record) Fixture : Parent { OV_FIELD(Serialize) int other; };', "Record bases must"),
    ("property_in_record", 'struct OV_TYPE(Record) Fixture { OV_FIELD(Serialize) int value; OV_PROPERTY(Serialize, Setter = Set) int Get() const { return value; } void Set(int v) { value = v; } };', "Record reflection supports fields only"),
    ("script_record", 'struct OV_TYPE() Fixture { OV_FIELD(Script) Settings value; };', "Record value adapters cannot export Script"),
    ("script_record_argument", 'struct OV_TYPE() Fixture { OV_METHOD(Script) void Set(Settings value) {} };', "Record value adapters cannot export Script"),
    ("record_default", 'struct OV_TYPE() Fixture { OV_FIELD(Serialize, Missing = Default, Default = "0") Settings value; };', "Default does not match"),
    ("positive_default", 'struct OV_TYPE() Fixture { OV_FIELD(Serialize, Validate = "Positive", Missing = Default, Default = "0") float value; };', "Default violates Positive"),
    ("wrong_validation", 'struct OV_TYPE() Fixture { OV_FIELD(Serialize, Validate = "Positive") int value; };', "Validate must be"),
]
results = []
for name, body, diagnostic in cases:
    fixture.write_text(prefix + body + "\n", encoding="utf-8")
    report = OUT / "analysis-report.json"
    command = [str(ROOT / "Build/Tools/OvReflectGen/Release/OvReflectGen.exe"), "-p", str(OUT),
               "--report=" + str(report), "--source-root=" + str(ROOT), "--module=RecordSchema",
               "--configuration=Debug", "--expect=Fixture", str(fixture)]
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, encoding="utf-8", errors="replace")
    output = result.stdout + result.stderr
    (OUT / (name + ".log")).write_text(output, encoding="utf-8")
    passed = result.returncode == 0 and report.exists() if diagnostic is None else result.returncode != 0 and diagnostic in output and not report.exists()
    if not passed: raise RuntimeError(name + " failed:\n" + output)
    if name == "inherited_nested":
        schema = json.loads((OUT / "schema.json").read_text())
        record = next(t for t in schema["types"] if t["cpp_name"] == "Fixture")
        if record["properties"][0]["type"]["category"] != "record-adapter": raise RuntimeError("Lost typed Record adapter")
    results.append({"name": name, "passed": True})
    print("PASS: " + name, flush=True)
(OUT / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
