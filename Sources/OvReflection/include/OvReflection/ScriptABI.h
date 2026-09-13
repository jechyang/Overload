#pragma once
#include <stdint.h>
/* R6 ABI v1: Windows x64, cdecl, default pack 8. No C++/CLR layout crosses here. */
#if defined(_WIN32)
#define OV_SCRIPT_CALL __cdecl
#define OV_SCRIPT_EXPORT __declspec(dllexport)
#else
#define OV_SCRIPT_CALL
#define OV_SCRIPT_EXPORT
#endif
#pragma pack(push, 8)
#ifdef __cplusplus
extern "C" {
#endif
typedef int32_t OvScriptStatus;
enum { OV_SCRIPT_OK = 0, OV_SCRIPT_INVALID_CONTEXT = 1, OV_SCRIPT_INVALID_HANDLE = 2,
       OV_SCRIPT_WRONG_TYPE = 3, OV_SCRIPT_WRONG_THREAD = 4, OV_SCRIPT_READ_ONLY = 5,
       OV_SCRIPT_INVALID_ARGUMENT = 6, OV_SCRIPT_NATIVE_EXCEPTION = 7, OV_SCRIPT_ABI_MISMATCH = 8 };
typedef struct OvScriptString { const uint8_t* data; uint64_t size; } OvScriptString;
typedef struct OvScriptOwnedString { uint8_t* data; uint64_t size; } OvScriptOwnedString;
typedef struct OvScriptVector3 { float x, y, z; } OvScriptVector3;
typedef struct OvScriptQuaternion { float x, y, z, w; } OvScriptQuaternion;
#pragma pack(pop)
#ifdef __cplusplus
}
static_assert(sizeof(void*) == 8, "R6 ABI requires a 64-bit target");
static_assert(sizeof(OvScriptString) == 16 && sizeof(OvScriptOwnedString) == 16);
static_assert(sizeof(OvScriptVector3) == 12 && sizeof(OvScriptQuaternion) == 16);
#endif
