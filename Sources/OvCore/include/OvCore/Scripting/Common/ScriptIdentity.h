#pragma once
#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace OvCore::Scripting
{
    struct ScriptIdentity
    {
        std::string backend, assembly, type, id;
        static ScriptIdentity Lua(std::string path)
        {
            if (path.find_first_of("\x1f") != std::string::npos)
                throw std::invalid_argument("Invalid Lua path control character");
            std::replace(path.begin(), path.end(), '\\', '/');
            return {"lua", {}, std::filesystem::path(path).lexically_normal().generic_string(), {}};
        }
        static ScriptIdentity CSharp(std::string assembly, std::string type, std::string id = {})
        { return {"csharp", std::move(assembly), std::move(type), std::move(id)}; }
        static std::string Part(const std::string& value)
        { return std::to_string(value.size()) + ":" + value; }
        std::string Key() const
        {
            // Keep the legacy Lua map/order API readable. Non-Lua keys use a reserved
            // control prefix which is rejected by Lua(), never written into XML.
            if (backend == "lua") return Lua(type).type;
            return "\x1f" + Part(backend) + (id.empty() ? "T" + Part(assembly) + Part(type) : "I" + Part(id));
        }
        bool operator==(const ScriptIdentity& other) const { return Key() == other.Key(); }
    };
}
