#pragma once
#include <OvReflection/ScriptBridge.h>
#include <string>
namespace OvCore::Scripting::CSharp
{
    // A reserves 80..99. All operations validate owner/Main through State.
    // 80 freezes snapshot, returns word count; 81 reads one word at argument.
    // 82 freezes clipboard UTF-8, returns byte count; 83 reads one packed word;
    // 84 writes clipboard; 85 queries a 2D shape; 86/87 query Bullet hits;
    // 88 projects a Game framebuffer point to a camera ray. 89..99 reserved.
    void QueryInput(OvReflection::Script::State&, uint64_t owner, uint32_t operation,
        uint64_t argument, const std::string& text, uint64_t& output);
}
