/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <OvRendering/HAL/DirectX12/DX12Prerequisites.h>
#include <OvRendering/Settings/EShaderType.h>
#include <string>
#include <vector>

namespace OvRendering::HAL::DX12
{
	/**
	* Shader compilation result
	*/
	struct ShaderCompileResult
	{
		bool success = false;
		std::string errorMessage;
		std::vector<byte> bytecode;
	};

	/**
	* DX12 Shader Compiler - wraps DXC (DirectX Shader Compiler)
	*/
	class DX12ShaderCompiler
	{
	public:
		/**
		* Constructor
		*/
		DX12ShaderCompiler();

		/**
		* Destructor
		*/
		~DX12ShaderCompiler();

		/**
		* Initialize the compiler
		* @return true if succeeded
		*/
		bool Initialize();

		/**
		* Compile a shader from file
		* @param p_filePath Path to HLSL file
		* @param p_entryPoint Entry point function name
		* @param p_target Target profile (e.g., "vs_6_0", "ps_6_0")
		* @param p_defines Shader macro defines
		* @return Compilation result
		*/
		ShaderCompileResult CompileFromFile(
			const std::string& p_filePath,
			const std::string& p_entryPoint = "main",
			const std::string& p_target = "vs_6_0",
			const std::vector<std::pair<std::string, std::string>>& p_defines = {}
		);

		/**
		* Compile a shader from source string
		*/
		ShaderCompileResult CompileFromSource(
			const std::string& p_source,
			const std::string& p_entryPoint = "main",
			const std::string& p_target = "vs_6_0",
			const std::vector<std::pair<std::string, std::string>>& p_defines = {}
		);

		/**
		* Cleanup
		*/
		void Shutdown();

		/**
		* Check if compiler is initialized
		*/
		bool IsInitialized() const { return m_initialized; }

	private:
		ComPtr<IDxcCompiler3> m_compiler;
		ComPtr<IDxcIncludeHandler> m_includeHandler;
		bool m_initialized = false;
	};
}
