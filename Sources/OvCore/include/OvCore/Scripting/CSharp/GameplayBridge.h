#pragma once
#include <OvCore/Scripting/CSharp/HostABI.h>
#include <OvReflection/ScriptBridge.h>
namespace OvCore::Scripting::CSharp
{
    using GameplayFn = uint32_t(OVCS_CALL*)(uint64_t, uint64_t, uint32_t, uint64_t, OvCsUtf8View, uint64_t*);
    using ConfigureGameplayFn = OvCsStatus(OVCS_CALL*)(uint32_t, uint32_t, GameplayFn);
    uint32_t OVCS_CALL Gameplay(uint64_t context, uint64_t owner, uint32_t operation,
        uint64_t argument, OvCsUtf8View text, uint64_t* output) noexcept;
}
