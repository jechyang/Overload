project "OvRendering"
	kind "StaticLib"
	language "C++"
	cppdialect "C++20"
	targetdir (outputdir .. "%{cfg.buildcfg}/%{prj.name}")
	objdir (objoutdir .. "%{cfg.buildcfg}/%{prj.name}")
	fatalwarnings { "All" }

	files {
		"**.h",
		"**.inl",
		"**.cpp",
		"**.lua",
		"**.ini"
	}

	includedirs {
		-- Dependencies
		dependdir .. "assimp/include",
		dependdir .. "glad/include",
		dependdir .. "stb_image/include",
		dependdir .. "tracy",

		-- Overload SDK
		"%{wks.location}/Sources/OvDebug/include",
		"%{wks.location}/Sources/OvMaths/include",
		"%{wks.location}/Sources/OvTools/include",

		-- Current Project
		"include"
	}

	filter "system:windows"
	filter "configurations:Debug"
		defines { "DEBUG", "_DEBUG" }
		symbols "On"

	filter "system:windows"
	filter "configurations:Release"
		defines { "NDEBUG" }
		optimize "On"

	filter {}

	-- DX12 SDK - use Lua conditional to check for GRAPHICS_API_DIRECTX12
	if _OPTIONS["graphics-api"] == "directx12" then
		defines { "WIN32", "_WINDOWS", "UNICODE", "_UNICODE" }
		links {
			"d3d12.lib",
			"dxgi.lib",
			"dxguid.lib",
			"D3Dcompiler.lib"
		}
	end
