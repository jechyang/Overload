-- Opt-in export from the SAME baked configurations used by the VS2022 writer.
newoption {
    trigger = "reflection-config-out",
    value = "PATH",
    description = "Export baked reflection module configurations after VS2022 generation"
}

if _ACTION == "vs2022" or _OPTIONS["reflection-config-out"] then
    if _ACTION ~= "vs2022" then
        error("--reflection-config-out requires vs2022 (MSVC configuration parity)")
    end
    local root = path.getabsolute("../..", _SCRIPT_DIR)
    local manifestFile = assert(io.open(path.join(root, "Tools/OvReflectGen/scan-modules.json"), "r"))
    local manifest = json.decode(manifestFile:read("*a"))
    manifestFile:close()
    -- Baked list fields also contain hash lookup keys. Serialize their ordered
    -- numeric sequence only, preserving include search order.
    local function sequence(values)
        local result = {}
        for _, value in ipairs(values or {}) do table.insert(result, value) end
        return result
    end
    local selected = {}
    for _, module in ipairs(manifest.modules) do selected[module.name] = true end
    local output = path.getabsolute(_OPTIONS["reflection-config-out"] or path.join(root, "Tools/OvReflectGen/Projects/Premake"))
    local defaultOutput = path.join(root, "Tools/OvReflectGen/Projects/Premake")
    local projectInputs = {}
    local inputs = { path.join(root, "premake5.lua"), path.join(root, "VERSION.txt") }
    for _, pattern in ipairs({ "Sources/**/premake5.lua", "Dependencies/**/premake5.lua", "Resources/**.lua", "Tools/OvReflectGen/premake-*.lua" }) do
        for _, filename in ipairs(os.matchfiles(path.join(root, pattern))) do table.insert(inputs, filename) end
    end
    for _, filename in ipairs(inputs) do
        local file = assert(io.open(filename, "rb"))
        projectInputs[path.getrelative(root, filename)] = file:read("*a")
        file:close()
    end
    local action = premake.action.get("vs2022")
    local original = action.onProject
    action.onProject = function(prj)
        original(prj)
        if not selected[prj.name] then return end
        local configurations = {}
        for cfg in premake.project.eachconfig(prj) do
            local entry = {
                project = prj.name, project_location = prj.location,
                configuration = cfg.buildcfg, platform = cfg.platform,
                system = cfg.system, architecture = cfg.architecture,
                language = cfg.language, cppdialect = cfg.cppdialect,
                toolset = cfg.toolset,
                defines = sequence(cfg.defines), undefines = sequence(cfg.undefines),
                includedirs = sequence(cfg.includedirs), externalincludedirs = sequence(cfg.externalincludedirs),
                forceincludes = sequence(cfg.forceincludes), buildoptions = sequence(cfg.buildoptions),
                runtime = cfg.runtime, staticruntime = cfg.staticruntime,
                symbols = cfg.symbols, optimize = cfg.optimize,
                characterset = cfg.characterset, exceptionhandling = cfg.exceptionhandling,
                rtti = cfg.rtti, nativewchar = cfg.nativewchar,
                unsignedchar = cfg.unsignedchar, floatingpoint = cfg.floatingpoint,
                vectorextensions = cfg.vectorextensions,
                structmemberalign = cfg.structmemberalign,
                conformancemode = cfg.conformancemode,
                usestandardpreprocessor = cfg.usestandardpreprocessor,
                systemversion = cfg.systemversion, toolchainversion = cfg.toolchainversion
            }
            table.insert(configurations, entry)
        end
        local content = json.encode({
            export_version = 1, action = _ACTION, premake_version = _PREMAKE_VERSION,
            workspace = root, configurations = configurations, project_inputs = projectInputs
        })
        for _, directory in ipairs(output == defaultOutput and { output } or { output, defaultOutput }) do
            os.mkdir(directory)
            local filename = path.join(directory, prj.name .. ".json")
            local old = io.open(filename, "r")
            local previous = old and old:read("*a") or nil
            if old then old:close() end
            if previous ~= content then
                local file = assert(io.open(filename, "w"))
                file:write(content)
                file:close()
            end
        end
    end
end
