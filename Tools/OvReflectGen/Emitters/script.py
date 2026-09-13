"""R6 C ABI and C# emitter. Only explicit Script members cross the boundary."""
import hashlib
import json
import re

OUTPUTS = ("Script.generated.h", "Script.generated.inc", "Script.generated.cs", "script-abi.json")

def ident(name):
    return name.replace("::", "_")

def token(item):
    return "m" + hashlib.sha256(item["id"].encode()).hexdigest()[:16]

def access_methods(t):
    lines = []
    owner = "::" + t["cpp_name"]
    for m in t["methods"]:
        if "Script" not in m["metadata"]: continue
        args = [p["type"]["canonical"] for p in m["parameters"]]
        result = m["return_type"]["canonical"]
        qualifiers = (" const" if m["const"] else "") + (" " + m["ref_qualifier"] if m["ref_qualifier"] else "") + (" noexcept" if m["noexcept"] else "")
        parameters = ", ".join([("const " if m["const"] else "") + owner + "& object"] + [f"{a} arg{i}" for i,a in enumerate(args)])
        lines += [f"        static {result} Invoke_{token(m)}({parameters})", "        {",
            f"            using Selected = {result} ({owner}::*)({', '.join(args)}){qualifiers};",
            f"            return (object.*static_cast<Selected>(&{owner}::{m['cpp_name'].split('::')[-1]}))({', '.join('arg'+str(i) for i in range(len(args)))});", "        }"]
    return lines

def emit(schema, directory, types):
    module = schema["module"]
    all_types = {t["cpp_name"]:t for t in schema["types"]}
    def fail(item, message):
        s = item["source"]
        raise ValueError(f"{s['path']}:{s['line']}:{s['column']}: {message}")
    def cs_enum(t): return "E_" + ident(t["value_type"])
    def abi(t, output=False):
        c = t["category"]
        if c == "void": return "void", "void"
        if c == "bool": return "uint8_t", "byte"
        if c == "integer":
            bits=t["bits"]; signed=t["signed"]
            return ("int" if signed else "uint")+str(bits)+"_t", {(8,True):"sbyte",(8,False):"byte",(16,True):"short",(16,False):"ushort",(32,True):"int",(32,False):"uint",(64,True):"long",(64,False):"ulong"}[bits,signed]
        if c == "float": return ("float", "float") if t["bits"]==32 else ("double", "double")
        if c == "enum": return abi(all_types[t["value_type"]]["underlying_type"])
        if c == "object-handle": return "uint64_t", "ulong"
        if c == "string": return ("OvScriptOwnedString", "NativeString") if output else ("OvScriptString", "NativeString")
        if t["value_type"] == "OvMaths::FVector3": return "OvScriptVector3", "Vector3"
        if t["value_type"] == "OvMaths::FQuaternion": return "OvScriptQuaternion", "Quaternion"
        raise ValueError("Unsupported Script ABI type: " + str(t))
    def managed(t):
        if t["category"] == "bool": return "bool"
        if t["category"] == "string": return "string"
        if t["category"] == "enum": return cs_enum(t)
        return abi(t)[1]
    def from_native(t, expression):
        if t["category"] == "bool": return expression + " != 0"
        if t["category"] == "enum": return "(" + managed(t) + ")" + expression
        if t["category"] == "string": return "Bridge.Decode(" + expression + ")"
        return expression
    def cpp_input(t, expr, metadata):
        c=t["category"]; value=t["value_type"]
        if c=="string": return "Decode("+expr+")"
        if c=="bool": return expr+" != 0"
        if c=="object-handle": return "state.Get<"+value.rstrip().removesuffix("*").strip()+">("+expr+", "+str(metadata.get("Nullable")=="Allow").lower()+")"
        if c=="value-adapter":
            fields="xyzw" if value.endswith("FQuaternion") else "xyz"
            return value+"{"+", ".join(expr+"."+x for x in fields)+"}"
        return "static_cast<"+value+">("+expr+")"
    def cpp_output(t, expr, metadata):
        c=t["category"]
        if c=="string": return "Encode("+expr+")"
        if c=="object-handle": return "state.Find("+expr+", "+str(metadata.get("Nullable")=="Allow").lower()+")"
        if c=="value-adapter":
            fields="xyzw" if t["value_type"].endswith("FQuaternion") else "xyz"
            return abi(t,True)[0]+"{"+", ".join(expr+"."+x for x in fields)+"}"
        return "static_cast<"+abi(t,True)[0]+">("+expr+")"
    operations=[]; names=set(); used_enums={}
    void={"category":"void","value_type":"void"}
    for t in types:
        for kind, members in [("property",t["properties"]),("method",t["methods"])]:
            for m in members:
                if "Script" not in m["metadata"]: continue
                if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*",m["name"]): fail(m,"Script Name must be an identifier")
                for mode in (["get"] + ([] if m["read_only"] else ["set"])) if kind=="property" else ["call"]:
                    args = ([] if mode=="get" else [m["type"]]) if kind=="property" else [p["type"] for p in m["parameters"]]
                    result=m["type"] if mode=="get" else void if mode=="set" else m["return_type"]
                    name=ident(t["cpp_name"])+"_"+mode+"_"+m["name"]
                    if name in names: fail(m,"colliding generated Script name: "+name)
                    names.add(name)
                    for value in args+[result]:
                        abi(value)
                        if value["category"]=="enum": used_enums[value["value_type"]]=value
                    operations.append(dict(name=name,kind=kind,mode=mode,owner=t,member=m,args=args,result=result))
                    if mode=="get" and result["category"] not in ("string","bool","enum","object-handle"):
                        operations.append(dict(name=name+"_batch",kind=kind,mode="batch",owner=t,member=m,args=[],result=result))
    emitted_names=set()
    for operation in operations:
        name=operation["name"]
        if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*",name): fail(operation["member"],"Script owner/name must produce an ASCII identifier")
        if name in emitted_names: fail(operation["member"],"colliding generated Script operation: "+name)
        emitted_names.add(name)
    enum_names=set()
    for n,t in used_enums.items():
        name=cs_enum(t)
        if name in enum_names: fail(all_types[n],"colliding generated Script enum: "+name)
        enum_names.add(name)
    # This canonical contract excludes source paths/configuration, includes all ABI semantics and enum values.
    contract={"abi_version":1,"target":"x86_64-pc-windows-msvc","calling_convention":"cdecl","packing":8,
              "string":"strict-utf8-length-u64-owned-output-release-v1","handle":"session-scoped-monotonic-borrowed-main-v1",
              "layouts":{"Vector3":["f32",0,4,8,12],"Quaternion":["f32",0,4,8,12,16]},
              "enums":[{"name":n,"underlying":all_types[n]["underlying_type"],"values":[[e["name"],e["value"]] for e in all_types[n]["enumerators"]]} for n in sorted(used_enums)],
              "operations":[{"name":o["name"],"owner":o["owner"]["id"],"id":o["member"]["id"],"mode":o["mode"],"args":o["args"],"result":o["result"],"policies":{k:v for k,v in o["member"]["metadata"].items() if k in ("Return","Ownership","Nullable","Overload","Thread")}} for o in operations]}
    signature=hashlib.sha256(json.dumps(contract,sort_keys=True,separators=(",",":")).encode()).hexdigest()
    header=['// Generated. Do not edit.','#pragma once','#include <OvReflection/ScriptABI.h>', f'#define OV_SCRIPT_{module.upper()}_SIGNATURE "{signature}"', '#pragma pack(push, 8)', '#ifdef __cplusplus','extern "C" {','#endif',f'typedef struct OvScript{module}Table {{', '    void (OV_SCRIPT_CALL *release_string)(OvScriptOwnedString*);']
    native=['// Generated. Included by Reflection.generated.cpp.','#include "Script.generated.h"','#include <OvReflection/ScriptBridge.h>','#include <cstring>',f'namespace OvScript{module}Detail {{','using namespace OvReflection;','using namespace OvReflection::Script;']
    cs=['// Generated. Do not edit.','#nullable enable','using System;','using System.Runtime.InteropServices;','using Overload.Script;',f'namespace Overload.Generated.{module}', '{']
    for n,t in sorted(used_enums.items()):
        cs += [f'public enum {cs_enum(t)} : {abi(t)[1]} {{']+[f'    @{e["name"]} = {e["value"]}{"UL" if abi(t)[1]=="ulong" else "L" if abi(t)[1]=="long" else ""},' for e in all_types[n]["enumerators"]]+['}']
    cs += ['public sealed unsafe class Api {',f'    public const string Signature = "{signature}";', f'    public const uint TableSize = {(len(operations)+1)*8};', '    private readonly ulong context;','    [StructLayout(LayoutKind.Sequential, Pack = 8)]','    private struct Table {','        public delegate* unmanaged[Cdecl]<NativeString*, void> release_string;']
    bodies=[]
    for o in operations:
        name=o["name"]; m=o["member"]; owner="::"+o["owner"]["cpp_name"]; result=o["result"]; args=o["args"]; batch=o["mode"]=="batch"; returns=result["category"]!="void"; meta=m["metadata"]
        params=['uint64_t context','uint64_t handle']+[f'const {abi(t)[0]}* arg{i}' for i,t in enumerate(args)]+([f'{abi(result,True)[0]}* output'] if returns else [])
        csargs=['ulong','ulong']+[abi(t)[1]+'*' for t in args]+([abi(result,True)[1]+'*'] if returns else [])
        if batch:
            params=['uint64_t context','const uint64_t* handles','uint64_t count',f'{abi(result,True)[0]}* output','uint64_t* completed']
            csargs=['ulong','ulong*','ulong',abi(result,True)[1]+'*','ulong*']
        header.append(f'    OvScriptStatus (OV_SCRIPT_CALL *{name})({", ".join(params)});')
        cs.append(f'        public delegate* unmanaged[Cdecl]<{", ".join(csargs+["int"])}> {name};')
        native += [f'static OvScriptStatus OV_SCRIPT_CALL {name}({", ".join(params)}) noexcept {{']
        if batch:
            native += ['    if (completed) *completed = 0;', '    return Guard(context, [&](State& state) {', '        Require(completed && count <= 0x7fffffff && (!count || (handles && output)));', '        for (uint64_t i = 0; i < count; ++i) {',f'            auto* object = state.Get<const {owner}>(handles[i]);',f'            {result["value_type"]} value{{}};',f'            Check(Access<{owner}>::Read_{m["cpp_name"].split("::")[-1]}(state.main, *object, value));',f'            output[i] = {cpp_output(result,"value",meta)};', '            *completed = i + 1;','        }','    });','}']
            bodies += [f'    public void @{name}(ReadOnlySpan<ulong> handles, Span<{managed(result)}> output) {{','        if (output.Length < handles.Length) throw new ArgumentException("Output is too small");','        ulong completed = 0;',f'        fixed (ulong* h = handles) fixed ({managed(result)}* p = output) {{',f'            int status = table.{name}(context, h, (ulong)handles.Length, p, &completed);','            if (status != 0) throw new BridgeException(status, completed);','        }','    }']
            continue
        if returns: native += ['    if (output) *output = {};']
        native += ['    return Guard(context, [&](State& state) {']
        if returns: native += ['        Require(output != nullptr);']
        for i,t in enumerate(args):
            native += [f'        Require(arg{i} != nullptr);']
            if t["category"]=="bool": native += [f'        Require(*arg{i} <= 1);']
            native += [f'        auto value{i} = {cpp_input(t,"(*arg"+str(i)+")",meta)};']
        const=o["mode"]=="get" or (o["mode"]=="call" and m["const"])
        native += [f'        auto* object = state.Get<{"const " if const else ""}{owner}>(handle);']
        if o["kind"]=="property":
            member=m["cpp_name"].split("::")[-1]
            if o["mode"]=="get": native += [f'        {result["value_type"]} value{{}};',f'        Check(Access<{owner}>::Read_{member}(state.main, *object, value));']
            else: native += [f'        Check(Access<{owner}>::Write_{member}(state.main, *object, value0));']
        else:
            call=f'Access<{owner}>::Invoke_{token(m)}(*object'+''.join(', value'+str(i) for i in range(len(args)))+')'
            native += ['        '+('auto value = ' if returns else '')+call+';']
        if returns: native += [f'        *output = {cpp_output(result,"value",meta)};']
        native += ['    });','}']
        bodies += [f'    public {managed(result)} @{name}(ulong handle'+''.join(f', {managed(t)} arg{i}' for i,t in enumerate(args))+') {']
        for i,t in enumerate(args):
            if t["category"]=="string": bodies += [f'        byte[] bytes{i} = Bridge.Utf8.GetBytes(arg{i} ?? throw new ArgumentNullException(nameof(arg{i})));']
            else: bodies += [f'        {abi(t)[1]} native{i} = '+(f'(byte)(arg{i} ? 1 : 0)' if t["category"]=="bool" else f'({abi(t)[1]})arg{i}')+';']
        for i,t in enumerate(args):
            if t["category"]=="string": bodies += [f'        fixed (byte* pointer{i} = bytes{i}) {{',f'        NativeString native{i} = new NativeString {{ Data = pointer{i}, Size = (ulong)bytes{i}.Length }};']
        if returns: bodies += [f'        {abi(result,True)[1]} output = default;']
        string_result=result["category"]=="string"
        if string_result: bodies += ['        try {']
        bodies += [f'        Bridge.Check(table.{name}(context, handle'+''.join(', &native'+str(i) for i in range(len(args)))+(', &output' if returns else '')+'));']
        if returns: bodies += [f'        return {from_native(result,"output")};']
        if string_result: bodies += ['        } finally { table.release_string(&output); }']
        bodies += ['        }' for t in args if t["category"]=="string"]+['    }']
    header += [f'}} OvScript{module}Table;',f'OV_SCRIPT_EXPORT OvScriptStatus OV_SCRIPT_CALL OvScript_Get{module}Api(uint32_t version, const uint8_t* signature, uint64_t size, OvScript{module}Table* output);','#ifdef __cplusplus','}','#endif','#pragma pack(pop)']
    native += ['}',f'extern "C" OV_SCRIPT_EXPORT OvScriptStatus OV_SCRIPT_CALL OvScript_Get{module}Api(uint32_t version, const uint8_t* signature, uint64_t size, OvScript{module}Table* output) {{', '    static const uint8_t expected[32] = {'+', '.join('0x'+signature[i:i+2] for i in range(0,64,2))+'};',f'    if (!output || size != sizeof(OvScript{module}Table)) return OV_SCRIPT_ABI_MISMATCH;','    *output = {};','    if (version != 1 || !signature || std::memcmp(signature, expected, 32) != 0) return OV_SCRIPT_ABI_MISMATCH;',f'    *output = OvScript{module}Table{{&OvReflection::Script::ReleaseString'+''.join(', &OvScript'+module+'Detail::'+o['name'] for o in operations)+'};','    return OV_SCRIPT_OK;','}']
    cs += ['    }','    private readonly Table table;', '    public Api(nint entryPoint, ulong context) {','        if (IntPtr.Size != 8 || entryPoint == 0) throw new ArgumentException("Expected a resident x64 native entry point");','        this.context = context;','        Table loaded = default;', '        byte[] signature = Convert.FromHexString(Signature);', '        fixed (byte* hash = signature) {', '            Bridge.Check(((delegate* unmanaged[Cdecl]<uint, byte*, ulong, Table*, int>)entryPoint)(1, hash, (ulong)sizeof(Table), &loaded));','        }','        table = loaded;', '    }',*bodies,'}','}']
    for filename,lines in [('Script.generated.h',header),('Script.generated.inc',native),('Script.generated.cs',cs)]:
        (directory/filename).write_text('\n'.join(lines)+'\n',encoding='utf-8',newline='\n')
    (directory/'script-abi.json').write_text(json.dumps({"signature_sha256":signature,**contract},indent=2)+'\n',encoding='utf-8',newline='\n')
