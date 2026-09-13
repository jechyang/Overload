#include "Reflection.generated.h"
#include "Script.generated.h"
#include <OvReflection/ScriptBridge.h>
#include <thread>
#include <memory>
#include <stdexcept>
#include <iostream>
using namespace OvReflection;
using namespace OvReflection::Script;
namespace {
    std::unique_ptr<MainThread> mainThread;
    std::unique_ptr<Context> context;
    std::unique_ptr<R6::Values> values;
    std::unique_ptr<R6::Other> other;
    std::shared_ptr<const bool> epoch, otherEpoch;
    uint64_t valueHandle, otherHandle, readOnlyHandle;
}
extern "C" OV_SCRIPT_EXPORT uint64_t OV_SCRIPT_CALL R6_Start() {
    mainThread = std::make_unique<MainThread>();
    context = std::make_unique<Context>(*mainThread);
    values = std::make_unique<R6::Values>(); other = std::make_unique<R6::Other>();
    epoch = std::make_shared<const bool>(true); otherEpoch = std::make_shared<const bool>(true);
    valueHandle = context->Publish(*values, epoch); otherHandle = context->Publish(*other, otherEpoch);
    const auto& immutable = *values; readOnlyHandle = context->Publish(immutable, epoch);
    return context->Id();
}
extern "C" OV_SCRIPT_EXPORT uint64_t OV_SCRIPT_CALL R6_Handle(uint32_t which) { return which == 0 ? valueHandle : which == 1 ? otherHandle : readOnlyHandle; }
extern "C" OV_SCRIPT_EXPORT uint64_t OV_SCRIPT_CALL R6_Replace() {
    epoch.reset(); values.reset();
    values = std::make_unique<R6::Values>(); epoch = std::make_shared<const bool>(true);
    valueHandle = context->Publish(*values, epoch); return valueHandle;
}
extern "C" OV_SCRIPT_EXPORT void OV_SCRIPT_CALL R6_DestroyOther() { otherEpoch.reset(); other.reset(); }
extern "C" OV_SCRIPT_EXPORT void OV_SCRIPT_CALL R6_Stop() { context.reset(); epoch.reset(); otherEpoch.reset(); values.reset(); other.reset(); mainThread.reset(); }
#ifdef R6_NATIVE_EXECUTE
void require(bool condition) { if (!condition) throw std::runtime_error("R6 native assertion failed"); }
int main() {
    try {
        const auto id = R6_Start();
        auto get = &OvScript_GetFixtureApi;
        // Read the expected hash from the test command line's generated header-independent receipt.
        // Call wrappers directly through the native table after exact handshake.
        uint8_t hash[32] = {R6_SIGNATURE_BYTES};
        OvScriptFixtureTable table{};
        require(get(1, hash, sizeof(table), &table) == 0);
        auto good = table;
        hash[0] ^= 1; require(get(1, hash, sizeof(table), &table) == OV_SCRIPT_ABI_MISMATCH); require(!table.release_string);
        hash[0] ^= 1; require(get(2, hash, sizeof(table), &table) == OV_SCRIPT_ABI_MISMATCH);
        require(get(1, hash, sizeof(table)-1, &table) == OV_SCRIPT_ABI_MISMATCH);
        table = good;
        int32_t value = 99;
        require(table.R6_Values_set_i32(id,valueHandle,&value)==0);
        value=0; require(table.R6_Values_get_i32(id,valueHandle,&value)==0 && value==99);
        require(table.R6_Values_get_i32(id,otherHandle,&value)==OV_SCRIPT_WRONG_TYPE);
        require(table.R6_Values_set_i32(id,readOnlyHandle,&value)==OV_SCRIPT_READ_ONLY);
        OvScriptStatus workerStatus=0;
        std::thread worker([&]{workerStatus=table.R6_Values_get_i32(id,valueHandle,&value);}); worker.join();
        require(workerStatus==OV_SCRIPT_WRONG_THREAD);
        require(table.R6_Values_call_Raise(id,valueHandle)==OV_SCRIPT_NATIVE_EXCEPTION);
        require(table.R6_Values_call_RaiseUnknown(id,valueHandle)==OV_SCRIPT_NATIVE_EXCEPTION);
        uint8_t badBool=2; require(table.R6_Values_set_flag(id,valueHandle,&badBool)==OV_SCRIPT_INVALID_ARGUMENT);
        uint8_t badUtf8[]={0xc0,0x80}; OvScriptString bad{badUtf8,2};
        require(table.R6_Values_set_text(id,valueHandle,&bad)==OV_SCRIPT_INVALID_ARGUMENT);
        bad={nullptr,2}; require(table.R6_Values_set_text(id,valueHandle,&bad)==OV_SCRIPT_INVALID_ARGUMENT);
        bad={nullptr,0}; require(table.R6_Values_set_text(id,valueHandle,&bad)==0);
        require(table.R6_Values_get_i32(id,valueHandle,nullptr)==OV_SCRIPT_INVALID_ARGUMENT);
        auto old=valueHandle; auto fresh=R6_Replace(); require(old!=fresh);
        require(table.R6_Values_get_i32(id,old,&value)==OV_SCRIPT_INVALID_HANDLE);
        require(table.R6_Values_get_i32(id,fresh,&value)==0 && value==0);
        uint64_t handles[]={fresh,old,fresh}, completed=42; int32_t outputs[]={-1,-1,-1};
        require(table.R6_Values_get_i32_batch(id,handles,3,outputs,&completed)==OV_SCRIPT_INVALID_HANDLE && completed==1 && outputs[0]==0 && outputs[1]==-1);
        R6_Stop(); require(table.R6_Values_get_i32(id,fresh,&value)==OV_SCRIPT_INVALID_CONTEXT);
        auto next=R6_Start(); require(next!=id); require(table.R6_Values_get_i32(next,fresh,&value)==OV_SCRIPT_INVALID_HANDLE);
        R6_Stop(); std::cout << "PASS: native R6 ABI, lifetime, type, thread, exception and UTF-8 checks\n";
    } catch (const std::exception& e) { std::cerr << e.what(); return 1; }
}
#endif
