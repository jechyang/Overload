#!/usr/bin/env python3
"""Compose a real generated game-module fixture with the production core descriptors.

Only the validation executable uses this composition; production tables stay unchanged.
"""
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "Tools/OvReflectGen"))
from Emitters import cpp

def generate(output):
    output.mkdir(parents=True, exist_ok=True)
    core = ROOT / "Build/Generated/Reflection/x86_64-pc-windows-msvc/Debug/OvCore"
    fixture = ROOT / "Tests/Reflection/Fixtures/CollectionFields.h"
    entry = json.loads((core / "compile_commands.json").read_text())[0]
    entry["arguments"] = [str(fixture) if arg == entry["file"] else arg for arg in entry["arguments"]]
    entry["file"] = str(fixture)
    (output / "compile_commands.json").write_text(json.dumps([entry]), encoding="utf-8")
    subprocess.run([str(ROOT / "Build/Tools/OvReflectGen/Release/OvReflectGen.exe"), "-p",str(output),
        "--report="+str(output / "analysis-report.json"),"--source-root="+str(ROOT),"--module=CollectionFixture",
        "--configuration=Debug","--expect=ReflectionFixture::CCollection",str(fixture)], cwd=ROOT, check=True)
    schema=json.loads((output / "schema.json").read_text())
    types=[t for t in schema["types"] if t["cpp_name"].startswith("ReflectionFixture::")]
    cpp.emit(schema,output,types=types,dependencies=["OvCore/Reflection.generated.h"],
        known_types=[t["cpp_name"] for t in schema["types"]],value_codecs=True,component_adapters=True)
    original=(core / "Reflection.generated.cpp").read_text(encoding="utf-8")
    for old,new in [("RegisterOvCoreTypes","RegisterProductionTypes"),("GetOvCoreComponents","GetProductionComponents"),("GetOvCoreValueCodecs","GetProductionValueCodecs")]:
        original=original.replace(old,new)
    original=original.replace('#include "Reflection.generated.h"','#include <OvCore/Reflection.generated.h>')
    original=original.replace('#include "Script.generated.inc"','#include <OvCore/Script.generated.inc>')
    (output / "Production.generated.cpp").write_text(original,encoding="utf-8")
    (output / "Joined.cpp").write_text('''#include "Reflection.generated.h"
#include <OvCore/ECS/ComponentRegistry.h>
#include <OvCore/Helpers/ReflectionProperties.h>
namespace OvReflection {
Error RegisterProductionTypes(Registry&);
Error RegisterOvCoreTypes(Registry& r) { auto e=RegisterProductionTypes(r); return e==Error::None?RegisterCollectionFixtureTypes(r):e; }
}
namespace OvCore::ECS {
std::span<const ComponentDesc> GetProductionComponents();
std::span<const ComponentDesc> GetCollectionFixtureComponents();
std::span<const ComponentDesc> GetOvCoreComponents() {
static const auto values=[] { auto a=GetProductionComponents(),b=GetCollectionFixtureComponents(); std::vector<ComponentDesc> result; result.reserve(a.size()+b.size()); for (auto& value:a) result.push_back(value); for (auto& value:b) result.push_back(value); return result; }(); return values;
}}
namespace OvCore::Helpers::ReflectionProperties {
std::span<const ValueCodec> GetProductionValueCodecs();
std::span<const ValueCodec> GetCollectionFixtureValueCodecs();
std::span<const ValueCodec> GetOvCoreValueCodecs() {
static const auto values=[] { auto a=GetProductionValueCodecs(),b=GetCollectionFixtureValueCodecs(); std::vector<ValueCodec> result(a.begin(),a.end()); result.insert(result.end(),b.begin(),b.end()); return result; }(); return values;
}}
''',encoding="utf-8")

if __name__ == "__main__":
    generate(Path(sys.argv[1]))
