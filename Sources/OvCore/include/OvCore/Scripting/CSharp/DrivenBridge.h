#pragma once
#include <OvReflection/ScriptBridge.h>
#include <string>
namespace OvCore::Scripting::CSharp
{
    // 60: argument=mask, text=existing token (empty creates); result=opaque token.
    // 61: argument=token, text=release mask (empty releases all); result=remaining mask.
    // 62: read component driven mask. Release is allowed while the managed session closes.
    void QueryDriven(OvReflection::Script::State&, uint64_t managedSession, uint64_t component, uint32_t operation,
        uint64_t argument, const std::string& text, uint64_t& output);
}
