#pragma once
#include <OvReflection/ScriptBridge.h>
#include <span>

namespace OvCore::Scripting::CSharp
{
    // Dedicated binary in/out ABI, never an ordinary UTF-8 query. See
    // Tests/UI/E-FONT-CONTRACT.zh-CN.md for packet layout and lifetime rules.
    // managedSession MUST come from the host's session.Identity, not user data.
    void QueryFont(OvReflection::Script::State& state, uint64_t managedSession,
        uint64_t owner, uint32_t operation, std::span<uint8_t> packet, uint64_t& output);
}
