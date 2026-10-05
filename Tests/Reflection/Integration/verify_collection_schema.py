#!/usr/bin/env python3
"""Exercise collection classification and rejection using the production LibTooling frontend."""
import json
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT / "Build/Tests/CollectionSchema"
OUT.mkdir(parents=True,exist_ok=True)
entry=json.loads((ROOT / "Build/Generated/Reflection/x86_64-pc-windows-msvc/Debug/OvCore/compile_commands.json").read_text())[0]
fixture=OUT / "Fixture.h"
entry["arguments"]=[str(fixture) if arg==entry["file"] else arg for arg in entry["arguments"]]
entry["file"]=str(fixture)
(OUT / "compile_commands.json").write_text(json.dumps([entry]),encoding="utf-8")
prefix='''#include <OvReflection/Annotations.h>
#include <OvMaths/FVector3.h>
#include <array>
#include <vector>
#include <string>
enum class OV_TYPE() Mode : unsigned long long { First=1, Last=18446744073709551615ULL };
struct OV_TYPE(Record) Entry { OV_FIELD(Serialize,Inspect) float weight=1; };
template<class T> struct Alloc : std::allocator<T> { template<class U> struct rebind { using other=Alloc<U>; }; };
'''
cases=[
    ("fixed_scalar",'OV_FIELD(Serialize,Inspect,ItemName="item") std::array<int,3> values;',None),
    ("list_bool",'OV_FIELD(Serialize,Inspect,ItemName="item",MaxItems=4) std::vector<bool> values;',None),
    ("list_enum",'OV_FIELD(Serialize,Inspect,ItemName="item",MaxItems=4) std::vector<Mode> values;',None),
    ("list_record",'OV_FIELD(Serialize,Inspect,ItemName="item",MaxItems=64) std::vector<Entry> values;',None),
    ("list_math",'OV_FIELD(Serialize,Inspect,ItemName="item",MaxItems=4) std::vector<OvMaths::FVector3> values;',None),
    ("list_string",'OV_FIELD(Serialize,Inspect,ItemName="item",MaxItems=4096) std::vector<std::string> values;',None),
    ("missing_item",'OV_FIELD(Serialize,MaxItems=4) std::vector<int> values;',"ItemName is required"),
    ("missing_limit",'OV_FIELD(Serialize,ItemName="item") std::vector<int> values;',"MaxItems is required"),
    ("zero_limit",'OV_FIELD(Serialize,ItemName="item",MaxItems=0) std::vector<int> values;',"MaxItems must"),
    ("huge_limit",'OV_FIELD(Serialize,ItemName="item",MaxItems=4097) std::vector<int> values;',"MaxItems must"),
    ("fraction_limit",'OV_FIELD(Serialize,ItemName="item",MaxItems=3.5) std::vector<int> values;',"MaxItems must"),
    ("quoted_limit",'OV_FIELD(Serialize,ItemName="item",MaxItems="4") std::vector<int> values;',"invalid number: MaxItems"),
    ("limit_on_scalar",'OV_FIELD(Serialize,MaxItems=4) int value;',"MaxItems is required exclusively"),
    ("limit_on_array",'OV_FIELD(Serialize,ItemName="item",MaxItems=4) std::array<int,3> values;',"MaxItems is required exclusively"),
    ("zero_array",'OV_FIELD(Serialize,ItemName="item") std::array<int,0> values;',"unsupported reflected type"),
    ("huge_array",'OV_FIELD(Serialize,ItemName="item") std::array<int,4097> values;',"unsupported reflected type"),
    ("const_elements",'OV_FIELD(Serialize,ItemName="item") std::array<const int,3> values;',"unsupported reflected type"),
    ("nested_lists",'OV_FIELD(Serialize,ItemName="item",MaxItems=4) std::vector<std::vector<int>> values;',"unsupported reflected type"),
    ("pointer_items",'OV_FIELD(Serialize,ItemName="item",MaxItems=4) std::vector<int*> values;',"unsupported reflected"),
    ("custom_allocator",'OV_FIELD(Serialize,ItemName="item",MaxItems=4) std::vector<int,Alloc<int>> values;',"unsupported reflected type"),
    ("scalar_range",'OV_FIELD(Serialize,ItemName="item",MaxItems=4,Min=0) std::vector<int> values;',"Collection properties do not support"),
    ("text_default",'OV_FIELD(Serialize,ItemName="item",MaxItems=4,Missing=Default,Default="0") std::vector<int> values;',"Collection properties do not support"),
    ("script_field",'OV_FIELD(Script,ItemName="item",MaxItems=4) std::vector<int> values;',"Collection value adapters cannot export Script"),
    ("script_method",'OV_METHOD(Script) void Set(std::vector<int> value) {}',"Collection value adapters cannot export Script"),
    ("power_two",'OV_FIELD(Serialize,Validate="PowerOfTwo",Missing=Default,Default="32") unsigned resolution;',None),
    ("power_two_default",'OV_FIELD(Serialize,Validate="PowerOfTwo",Missing=Default,Default="17") unsigned resolution;',"Default violates PowerOfTwo"),
    ("power_two_signed",'OV_FIELD(Serialize,Validate="PowerOfTwo") int resolution;',"Validate must be"),
]
results=[]
for name,body,diagnostic in cases:
    fixture.write_text(prefix+'struct OV_TYPE() Fixture { '+body+' };\n',encoding="utf-8")
    report=OUT / "analysis-report.json"
    command=[str(ROOT / "Build/Tools/OvReflectGen/Release/OvReflectGen.exe"),"-p",str(OUT),"--report="+str(report),
        "--source-root="+str(ROOT),"--module=CollectionSchema","--configuration=Debug","--expect=Fixture",str(fixture)]
    result=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,encoding="utf-8",errors="replace")
    output=result.stdout+result.stderr
    (OUT / (name+".log")).write_text(output,encoding="utf-8")
    passed=result.returncode==0 and report.exists() if diagnostic is None else result.returncode!=0 and diagnostic in output and not report.exists()
    if not passed: raise RuntimeError(name+" failed:\n"+output)
    if name in ("fixed_scalar","list_record"):
        schema=json.loads((OUT / "schema.json").read_text()); t=next(t for t in schema["types"] if t["cpp_name"]=="Fixture")["properties"][0]["type"]
        if t["category"]!=("value-array" if name=="fixed_scalar" else "value-list") or "element_type" not in t: raise RuntimeError("Lost typed collection facts")
        if name=="fixed_scalar" and t["capacity"]!=3: raise RuntimeError("Lost fixed array capacity")
        if name=="list_record" and t["element_type"]["category"]!="record-adapter": raise RuntimeError("Lost Record element adapter")
    results.append({"name":name,"passed":True}); print("PASS: "+name,flush=True)
(OUT / "results.json").write_text(json.dumps(results,indent=2),encoding="utf-8")
