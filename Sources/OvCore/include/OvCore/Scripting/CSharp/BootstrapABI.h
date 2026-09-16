#pragma once
#include <OvCore/Scripting/CSharp/HostABI.h>

// Resident bootstrap transport, separate from the frozen project Host ABI and R6.
// Configure after GetHostApi. Call Detach before destroying the native context.
// Callbacks are synchronous, noexcept, Main-only; buffers are borrowed for the call.
typedef uint32_t (OVCS_CALL* OvCsIsMainFn)();
typedef void (OVCS_CALL* OvCsLogFn)(uint32_t level, OvCsUtf8View text);
typedef struct OvCsBootstrapServices
{
    uint32_t version;
    uint32_t struct_size;
    uint64_t native_context_id;
    OvCsIsMainFn is_main;
    OvCsLogFn log;
} OvCsBootstrapServices;
typedef OvCsStatus (OVCS_CALL* OvCsConfigureFn)(const OvCsBootstrapServices* services);
typedef OvCsStatus (OVCS_CALL* OvCsDetachFn)();
static_assert(sizeof(OvCsBootstrapServices) == 32);
// S6 resident exports; Host ABI 1 and R6 remain unchanged.
typedef OvCsStatus (OVCS_CALL* OvCsPrepareReloadFn)(OvCsSessionHandle, OvCsInstanceHandle, OvCsSessionHandle, OvCsInstanceHandle);
typedef OvCsStatus (OVCS_CALL* OvCsRetireReloadFn)(OvCsSessionHandle);
typedef OvCsStatus (OVCS_CALL* OvCsCollectibleStatsFn)(uint32_t, uint64_t*, uint64_t*);
