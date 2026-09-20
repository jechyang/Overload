#pragma once

#include <stddef.h>
#include <stdint.h>

#if !defined(_WIN32) || (!defined(_M_X64) && !defined(__x86_64__))
#error OvCSharp Host ABI 1 is frozen for Windows x64 only
#endif

#if defined(_MSC_VER)
#define OVCS_CALL __cdecl
#pragma pack(push, 8)
#else
#define OVCS_CALL
#endif

#define OVCS_HOST_ABI_VERSION UINT32_C(1)
#define OVCS_HOST_ABI_SIGNATURE_SIZE UINT32_C(32)
#define OVCS_HOST_ABI_SIGNATURE_SHA256 "1f7e282e2f1353962d004dfe3f9b88a52fe17311d6b4d51491d8622a529a1b10"
#define OVCS_HOST_ABI_SIGNATURE_BYTES \
    0x1f, 0x7e, 0x28, 0x2e, 0x2f, 0x13, 0x53, 0x96, \
    0x2d, 0x00, 0x4d, 0xfe, 0x3f, 0x9b, 0x88, 0xa5, \
    0x2f, 0xe1, 0x73, 0x11, 0xd6, 0xb4, 0xd5, 0x14, \
    0x91, 0xd8, 0x62, 0x2a, 0x52, 0x9a, 0x1b, 0x10

typedef uint32_t OvCsStatus;
#define OVCS_STATUS_SUCCESS ((OvCsStatus)0)
#define OVCS_STATUS_ABI_MISMATCH ((OvCsStatus)1)
#define OVCS_STATUS_INVALID_ARGUMENT ((OvCsStatus)2)
#define OVCS_STATUS_WRONG_THREAD ((OvCsStatus)3)
#define OVCS_STATUS_SESSION_NOT_FOUND ((OvCsStatus)4)
#define OVCS_STATUS_SESSION_CLOSING ((OvCsStatus)5)
#define OVCS_STATUS_SESSION_FAULTED ((OvCsStatus)6)
#define OVCS_STATUS_TYPE_NOT_FOUND ((OvCsStatus)7)
#define OVCS_STATUS_DUPLICATE_IDENTITY ((OvCsStatus)8)
#define OVCS_STATUS_INSTANCE_NOT_FOUND ((OvCsStatus)9)
#define OVCS_STATUS_INSTANCE_FAULTED ((OvCsStatus)10)
#define OVCS_STATUS_INVALID_LIFECYCLE ((OvCsStatus)11)
#define OVCS_STATUS_FIELD_NOT_FOUND ((OvCsStatus)12)
#define OVCS_STATUS_FIELD_TYPE_MISMATCH ((OvCsStatus)13)
#define OVCS_STATUS_UNSUPPORTED_FIELD ((OvCsStatus)14)
#define OVCS_STATUS_MANAGED_EXCEPTION ((OvCsStatus)15)
#define OVCS_STATUS_BUFFER_OWNERSHIP ((OvCsStatus)16)
#define OVCS_STATUS_NATIVE_ABI_MISMATCH ((OvCsStatus)17)
#define OVCS_STATUS_BUSY ((OvCsStatus)18)

typedef uint32_t OvCsLifecycle;
#define OVCS_LIFECYCLE_AWAKE ((OvCsLifecycle)1)
#define OVCS_LIFECYCLE_START ((OvCsLifecycle)2)
#define OVCS_LIFECYCLE_ENABLE ((OvCsLifecycle)3)
#define OVCS_LIFECYCLE_DISABLE ((OvCsLifecycle)4)
#define OVCS_LIFECYCLE_UPDATE ((OvCsLifecycle)5)
#define OVCS_LIFECYCLE_FIXED_UPDATE ((OvCsLifecycle)6)
#define OVCS_LIFECYCLE_LATE_UPDATE ((OvCsLifecycle)7)
#define OVCS_LIFECYCLE_COLLISION_ENTER ((OvCsLifecycle)8)
#define OVCS_LIFECYCLE_COLLISION_STAY ((OvCsLifecycle)9)
#define OVCS_LIFECYCLE_COLLISION_EXIT ((OvCsLifecycle)10)
#define OVCS_LIFECYCLE_TRIGGER_ENTER ((OvCsLifecycle)11)
#define OVCS_LIFECYCLE_TRIGGER_STAY ((OvCsLifecycle)12)
#define OVCS_LIFECYCLE_TRIGGER_EXIT ((OvCsLifecycle)13)
#define OVCS_LIFECYCLE_DESTROY ((OvCsLifecycle)14)
#define OVCS_LIFECYCLE_RELOAD_RESTORE ((OvCsLifecycle)15)

typedef uint32_t OvCsFieldTag;
#define OVCS_FIELD_BOOL ((OvCsFieldTag)1)
#define OVCS_FIELD_INT8 ((OvCsFieldTag)2)
#define OVCS_FIELD_UINT8 ((OvCsFieldTag)3)
#define OVCS_FIELD_INT16 ((OvCsFieldTag)4)
#define OVCS_FIELD_UINT16 ((OvCsFieldTag)5)
#define OVCS_FIELD_INT32 ((OvCsFieldTag)6)
#define OVCS_FIELD_UINT32 ((OvCsFieldTag)7)
#define OVCS_FIELD_INT64 ((OvCsFieldTag)8)
#define OVCS_FIELD_UINT64 ((OvCsFieldTag)9)
#define OVCS_FIELD_FLOAT32 ((OvCsFieldTag)10)
#define OVCS_FIELD_FLOAT64 ((OvCsFieldTag)11)
#define OVCS_FIELD_STRING ((OvCsFieldTag)12)
#define OVCS_FIELD_ENUM_INT64 ((OvCsFieldTag)13)
#define OVCS_FIELD_ENUM_UINT64 ((OvCsFieldTag)14)
#define OVCS_FIELD_VECTOR3 ((OvCsFieldTag)15)
#define OVCS_FIELD_QUATERNION ((OvCsFieldTag)16)
#define OVCS_FIELD_ACTOR_REF ((OvCsFieldTag)17)
#define OVCS_FIELD_ASSET_REF ((OvCsFieldTag)18)
#define OVCS_FIELD_VECTOR2 ((OvCsFieldTag)19)
#define OVCS_FIELD_VECTOR4 ((OvCsFieldTag)20)
#define OVCS_FIELD_RECT ((OvCsFieldTag)21)
#define OVCS_FIELD_RECT_OFFSET ((OvCsFieldTag)22)
#define OVCS_FIELD_COLOR ((OvCsFieldTag)23)
#define OVCS_FIELD_COMPONENT_REF ((OvCsFieldTag)24) /* actor_guid + stable component type in text */

typedef struct OvCsUtf8View
{
    const uint8_t* data;
    uint64_t length;
} OvCsUtf8View;

typedef struct OvCsOwnedUtf8
{
    uint8_t* data;
    uint64_t length;
} OvCsOwnedUtf8;

typedef struct OvCsSessionHandle
{
    uint64_t id;
    uint64_t generation;
} OvCsSessionHandle;

typedef struct OvCsInstanceHandle
{
    uint64_t id;
    uint64_t session_generation;
} OvCsInstanceHandle;

typedef struct OvCsVector3
{
    float x;
    float y;
    float z;
} OvCsVector3;

typedef struct OvCsQuaternion
{
    float x;
    float y;
    float z;
    float w;
} OvCsQuaternion;

typedef union OvCsFieldScalar
{
    uint8_t boolean;
    int8_t int8_value;
    uint8_t uint8_value;
    int16_t int16_value;
    uint16_t uint16_value;
    int32_t int32_value;
    uint32_t uint32_value;
    int64_t int64_value;
    uint64_t uint64_value;
    float float32_value;
    double float64_value;
    OvCsVector3 vector3;
    OvCsQuaternion quaternion;
    uint64_t actor_guid;
    /* Tags 19/20: xy/xyzw; 21: min.x,min.y,size.x,size.y; 23: rgba. */
    float float_values[4];
    /* Tag 22: left,right,top,bottom. Size and ABI offsets remain unchanged. */
    int32_t int32_values[4];
} OvCsFieldScalar;

typedef struct OvCsFieldValueView
{
    uint32_t struct_size;
    OvCsFieldTag tag;
    uint32_t flags;
    uint32_t auxiliary;
    OvCsFieldScalar scalar;
    OvCsUtf8View text;
} OvCsFieldValueView;

typedef struct OvCsOwnedFieldValue
{
    uint32_t struct_size;
    OvCsFieldTag tag;
    uint32_t flags;
    uint32_t auxiliary;
    OvCsFieldScalar scalar;
    OvCsOwnedUtf8 text;
} OvCsOwnedFieldValue;

typedef struct OvCsSessionCreateInfo
{
    uint32_t struct_size;
    uint32_t flags;
    uint64_t project_generation;
    OvCsUtf8View project_root;
    OvCsUtf8View assembly_path;
    OvCsUtf8View assembly_logical_name;
} OvCsSessionCreateInfo;

/* Optional type capabilities; unknown bits may be ignored by older consumers. */
#define OVCS_TYPE_UI ((uint32_t)1)
typedef struct OvCsTypeInfo
{
    uint32_t struct_size;
    uint32_t flags;
    uint64_t type_token;
    OvCsOwnedUtf8 stable_id;
    OvCsOwnedUtf8 assembly_logical_name;
    OvCsOwnedUtf8 canonical_type_name;
    uint32_t alias_count;
    uint32_t field_count;
} OvCsTypeInfo;

typedef struct OvCsFieldInfo
{
    uint32_t struct_size;
    uint32_t flags;
    uint64_t field_token;
    OvCsFieldTag tag;
    uint32_t auxiliary;
    OvCsOwnedUtf8 stable_id;
    OvCsOwnedUtf8 managed_name;
    OvCsOwnedUtf8 display_name;
    uint32_t alias_count;
    uint32_t reserved;
} OvCsFieldInfo;

typedef struct OvCsInstanceCreateInfo
{
    uint32_t struct_size;
    uint32_t flags;
    uint64_t type_token;
    uint64_t native_context_id;
    uint64_t owner_object_id;
    uint64_t actor_guid;
} OvCsInstanceCreateInfo;

typedef struct OvCsLifecycleCall
{
    uint32_t struct_size;
    OvCsLifecycle event;
    OvCsInstanceHandle instance;
    float delta_time;
    uint32_t flags;
    uint64_t other_object_id;
} OvCsLifecycleCall;

typedef OvCsStatus (OVCS_CALL* OvCsReleaseOwnedUtf8Fn)(OvCsOwnedUtf8* value);
typedef OvCsStatus (OVCS_CALL* OvCsReleaseFieldValueFn)(OvCsOwnedFieldValue* value);
typedef OvCsStatus (OVCS_CALL* OvCsTakeLastErrorFn)(OvCsSessionHandle session, OvCsOwnedUtf8* error);
typedef OvCsStatus (OVCS_CALL* OvCsCreateSessionFn)(const OvCsSessionCreateInfo* info, OvCsSessionHandle* session);
typedef OvCsStatus (OVCS_CALL* OvCsCloseSessionFn)(OvCsSessionHandle session);
typedef OvCsStatus (OVCS_CALL* OvCsBindNativeApiFn)(OvCsSessionHandle session, OvCsUtf8View module_name,
    uint32_t abi_version, uint32_t table_size, const uint8_t signature[32], uintptr_t entry_point);
typedef OvCsStatus (OVCS_CALL* OvCsGetTypeCountFn)(OvCsSessionHandle session, uint32_t* count);
typedef OvCsStatus (OVCS_CALL* OvCsGetTypeInfoFn)(OvCsSessionHandle session, uint32_t index, OvCsTypeInfo* info);
typedef OvCsStatus (OVCS_CALL* OvCsGetTypeAliasFn)(OvCsSessionHandle session, uint64_t type_token,
    uint32_t index, OvCsOwnedUtf8* alias);
typedef OvCsStatus (OVCS_CALL* OvCsReleaseTypeInfoFn)(OvCsTypeInfo* info);
typedef OvCsStatus (OVCS_CALL* OvCsGetFieldInfoFn)(OvCsSessionHandle session, uint64_t type_token,
    uint32_t index, OvCsFieldInfo* info);
typedef OvCsStatus (OVCS_CALL* OvCsGetFieldAliasFn)(OvCsSessionHandle session, uint64_t type_token,
    uint64_t field_token, uint32_t index, OvCsOwnedUtf8* alias);
typedef OvCsStatus (OVCS_CALL* OvCsReleaseFieldInfoFn)(OvCsFieldInfo* info);
typedef OvCsStatus (OVCS_CALL* OvCsCreateInstanceFn)(OvCsSessionHandle session,
    const OvCsInstanceCreateInfo* info, OvCsInstanceHandle* instance);
typedef OvCsStatus (OVCS_CALL* OvCsDestroyInstanceFn)(OvCsSessionHandle session, OvCsInstanceHandle instance);
typedef OvCsStatus (OVCS_CALL* OvCsDispatchLifecycleFn)(OvCsSessionHandle session, const OvCsLifecycleCall* call);
typedef OvCsStatus (OVCS_CALL* OvCsReadFieldFn)(OvCsSessionHandle session, OvCsInstanceHandle instance,
    uint64_t field_token, OvCsOwnedFieldValue* value);
typedef OvCsStatus (OVCS_CALL* OvCsWriteFieldFn)(OvCsSessionHandle session, OvCsInstanceHandle instance,
    uint64_t field_token, const OvCsFieldValueView* value);

typedef struct OvCsHostApi
{
    uint32_t abi_version;
    uint32_t table_size;
    uint8_t signature[OVCS_HOST_ABI_SIGNATURE_SIZE];
    OvCsReleaseOwnedUtf8Fn release_owned_utf8;
    OvCsReleaseFieldValueFn release_field_value;
    OvCsTakeLastErrorFn take_last_error;
    OvCsCreateSessionFn create_session;
    OvCsCloseSessionFn close_session;
    OvCsBindNativeApiFn bind_native_api;
    OvCsGetTypeCountFn get_type_count;
    OvCsGetTypeInfoFn get_type_info;
    OvCsGetTypeAliasFn get_type_alias;
    OvCsReleaseTypeInfoFn release_type_info;
    OvCsGetFieldInfoFn get_field_info;
    OvCsGetFieldAliasFn get_field_alias;
    OvCsReleaseFieldInfoFn release_field_info;
    OvCsCreateInstanceFn create_instance;
    OvCsDestroyInstanceFn destroy_instance;
    OvCsDispatchLifecycleFn dispatch_lifecycle;
    OvCsReadFieldFn read_field;
    OvCsWriteFieldFn write_field;
} OvCsHostApi;

typedef OvCsStatus (OVCS_CALL* OvCsGetHostApiFn)(uint32_t requested_version, uint32_t requested_size,
    const uint8_t expected_signature[OVCS_HOST_ABI_SIGNATURE_SIZE], OvCsHostApi* api);

#define OVCS_HOST_API_SIZE ((uint32_t)sizeof(OvCsHostApi))

#if defined(_MSC_VER)
#pragma pack(pop)
#endif

#if defined(__cplusplus)
static_assert(sizeof(OvCsVector3) == 12);
static_assert(sizeof(OvCsQuaternion) == 16);
static_assert(sizeof(OvCsFieldScalar) == 16);
static_assert(sizeof(OvCsFieldValueView) == 48);
static_assert(sizeof(OvCsOwnedFieldValue) == 48);
static_assert(sizeof(OvCsSessionCreateInfo) == 64);
static_assert(sizeof(OvCsTypeInfo) == 72);
static_assert(sizeof(OvCsFieldInfo) == 80);
static_assert(sizeof(OvCsInstanceCreateInfo) == 40);
static_assert(sizeof(OvCsLifecycleCall) == 40);
static_assert(offsetof(OvCsHostApi, release_owned_utf8) == 40);
static_assert(sizeof(OvCsHostApi) == 184);
#endif
