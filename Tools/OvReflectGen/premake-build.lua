-- One utility project is the sole build-graph writer for a configuration.
-- Baked configuration sidecars live with generated projects, outside Build/Generated.
newoption {
    trigger = "reflection-python", value = "PATH",
    description = "Python 3.10+ executable for reflection builds (default: py -3)"
}

local root = path.getabsolute("../..", _SCRIPT_DIR)
local python = _OPTIONS["reflection-python"] and ('"' .. path.getabsolute(_OPTIONS["reflection-python"]) .. '"') or "py -3"
local generated = path.join(root, "Build/Generated/Reflection/x86_64-pc-windows-msvc")
local configs = { "Debug", "Release", "Publish" }

group "Build Tools"
project "OvReflectionGen"
    location(path.join(root, "Tools/OvReflectGen/Projects"))
    kind "Makefile"
    language "C++"
    objdir(path.join(root, "Bin-Int/%{cfg.buildcfg}/OvReflectionGen"))
    -- Always invoke the inexpensive content check: timestamp-only MSBuild inputs
    -- cannot discover transitive changes, deleted headers or new shadowing includes.
    buildcommands { python .. ' "' .. path.join(root, "Tools/OvReflectGen/reflection.py") .. '" ensure --config %{cfg.buildcfg}' }
    buildmessage "Checking and generating reflection metadata"
    rebuildcommands { python .. ' "' .. path.join(root, "Tools/OvReflectGen/reflection.py") .. '" ensure --config %{cfg.buildcfg}' }
    cleancommands { python .. ' "' .. path.join(root, "Tools/OvReflectGen/reflection.py") .. '" clean --config %{cfg.buildcfg}' }
group ""

for _, name in ipairs({ "OvMaths", "OvCore", "OvEditor", "OvGame" }) do
    project(name)
    filter {}
    dependson { "OvReflectionGen" }
    includedirs { root, generated .. "/%{cfg.buildcfg}" }
    links { "OvReflection", "OvReflectionGen" }
end

-- Known aggregate names are listed even on a clean checkout; no generated glob.
for _, name in ipairs({ "OvMaths", "OvCore" }) do
    project(name)
    filter {}
    for _, config in ipairs(configs) do
        local source = generated .. "/" .. config .. "/" .. name .. "/Reflection.generated.cpp"
        files { source }
        filter { "files:**/" .. config .. "/" .. name .. "/Reflection.generated.cpp", "configurations:not " .. config }
            flags { "ExcludeFromBuild" }
        filter {}
    end
end
