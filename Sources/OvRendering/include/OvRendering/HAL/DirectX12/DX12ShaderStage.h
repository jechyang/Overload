/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <OvRendering/HAL/DirectX12/DX12Prerequisites.h>
#include <OvRendering/Settings/EShaderType.h>

#include <vector>
#include <string>

namespace OvRendering::HAL::DX12
{
	/**
	* DX12 Shader Stage
	*/
	class DX12ShaderStage
	{
	public:
		/**
		* Constructor
		*/
		DX12ShaderStage();

		/**
		* Destructor
		*/
		~DX12ShaderStage();

		/**
		* Create shader stage from compiled bytecode
		* @param p_type Shader type (vertex/pixel/etc.)
		* @param p_bytecode Shader bytecode
		* @param p_bytecodeSize Size of bytecode
		* @return true if succeeded
		*/
		bool Create(
			Settings::EShaderType p_type,
			const void* p_bytecode,
			size_t p_bytecodeSize
		);

		/**
		* Create shader stage from file
		*/
		bool CreateFromFile(
			Settings::EShaderType p_type,
			const std::string& p_filePath,
			const std::string& p_entryPoint = "main"
		);

		/**
		* Cleanup
		*/
		void Shutdown();

		/**
		* Get the shader type
		*/
		Settings::EShaderType GetType() const { return m_type; }

		/**
		* Get the shader bytecode
		*/
		const std::vector<byte>& GetBytecode() const { return m_bytecode; }

		/**
		* Get the bytecode size
		*/
		size_t GetBytecodeSize() const { return m_bytecode.size(); }

		/**
		* Check if shader stage is valid
		*/
		bool IsValid() const { return !m_bytecode.empty(); }

	private:
		Settings::EShaderType m_type = Settings::EShaderType::VERTEX;
		std::vector<byte> m_bytecode;
	};

	/**
	* DX12 Shader Program (collection of shader stages)
	*/
	class DX12ShaderProgram
	{
	public:
		/**
		* Constructor
		*/
		DX12ShaderProgram();

		/**
		* Destructor
		*/
		~DX12ShaderProgram();

		/**
		* Attach a shader stage
		*/
		void AttachShaderStage(DX12ShaderStage* p_stage, Settings::EShaderType p_type);

		/**
		* Get vertex shader bytecode
		*/
		const std::vector<byte>& GetVertexShader() const { return m_vertexShaderBytecode; }

		/**
		* Get pixel shader bytecode
		*/
		const std::vector<byte>& GetPixelShader() const { return m_pixelShaderBytecode; }

		/**
		* Check if program has vertex shader
		*/
		bool HasVertexShader() const { return !m_vertexShaderBytecode.empty(); }

		/**
		* Check if program has pixel shader
		*/
		bool HasPixelShader() const { return !m_pixelShaderBytecode.empty(); }

		/**
		* Cleanup
		*/
		void Shutdown();

	private:
		std::vector<byte> m_vertexShaderBytecode;
		std::vector<byte> m_pixelShaderBytecode;
		std::vector<DX12ShaderStage*> m_shaderStages;
	};
}
