#pragma once
#include <OvReflection/ScriptBridge.h>
#include <span>
#include <string>
namespace OvCore::Scripting::CSharp
{
    // Version 1 UI extension of Gameplay. Component handles retain native epochs.
    void QueryUI(OvReflection::Script::State&, uint64_t component, uint32_t operation,
        uint64_t argument, const std::string& property, uint64_t& output);
    void UploadUIMesh(OvReflection::Script::State&, uint64_t component,
        std::span<const uint8_t> payload, uint64_t& output);
}
