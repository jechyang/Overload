/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <OvRendering/HAL/DirectX12/DX12ShaderStage.h>
#include <OvRendering/HAL/DirectX12/DX12ShaderCompiler.h>

#include <OvDebug/Logger.h>

namespace OvRendering::HAL::DX12
{
	// ========================================================================
	// DX12ShaderStage Constructor
	// ========================================================================
	DX12ShaderStage::DX12ShaderStage()
	{
	}

	// ========================================================================
	// DX12ShaderStage Destructor
	// ========================================================================
	DX12ShaderStage::~DX12ShaderStage()
	{
		Shutdown();
	}

	// ========================================================================
	// Create
	// ========================================================================
	bool DX12ShaderStage::Create(
		Settings::EShaderType p_type,
		const void* p_bytecode,
		size_t p_bytecodeSize
	)
	{
		if (!p_bytecode || p_bytecodeSize == 0)
		{
			OVLOG_ERROR("Invalid shader bytecode");
			return false;
		}

		m_type = p_type;
		m_bytecode.resize(p_bytecodeSize);
		memcpy(m_bytecode.data(), p_bytecode, p_bytecodeSize);

		return true;
	}

	// ========================================================================
	// CreateFromFile
	// ========================================================================
	bool DX12ShaderStage::CreateFromFile(
		Settings::EShaderType p_type,
		const std::string& p_filePath,
		const std::string& p_entryPoint
	)
	{
		// Determine target profile
		std::string target;
		switch (p_type)
		{
		case Settings::EShaderType::VERTEX:
			target = "vs_6_0";
			break;
		case Settings::EShaderType::FRAGMENT:
			target = "ps_6_0";
			break;
		default:
			OVLOG_ERROR("Unsupported shader type");
			return false;
		}

		// Compile shader
		DX12ShaderCompiler compiler;
		if (!compiler.Initialize())
		{
			OVLOG_ERROR("Failed to initialize shader compiler");
			return false;
		}

		ShaderCompileResult result = compiler.CompileFromFile(p_filePath, p_entryPoint, target);

		if (!result.success)
		{
			OVLOG_ERROR("Shader compilation failed: " + result.errorMessage);
			return false;
		}

		return Create(p_type, result.bytecode.data(), result.bytecode.size());
	}

	// ========================================================================
	// Shutdown
	// ========================================================================
	void DX12ShaderStage::Shutdown()
	{
		m_bytecode.clear();
	}

	// ========================================================================
	// DX12ShaderProgram Constructor
	// ========================================================================
	DX12ShaderProgram::DX12ShaderProgram()
	{
	}

	// ========================================================================
	// DX12ShaderProgram Destructor
	// ========================================================================
	DX12ShaderProgram::~DX12ShaderProgram()
	{
		Shutdown();
	}

	// ========================================================================
	// AttachShaderStage
	// ========================================================================
	void DX12ShaderProgram::AttachShaderStage(DX12ShaderStage* p_stage, Settings::EShaderType p_type)
	{
		if (!p_stage || !p_stage->IsValid())
		{
			return;
		}

		m_shaderStages.push_back(p_stage);

		// Copy bytecode based on type
		if (p_type == Settings::EShaderType::VERTEX)
		{
			m_vertexShaderBytecode = p_stage->GetBytecode();
		}
		else if (p_type == Settings::EShaderType::FRAGMENT)
		{
			m_pixelShaderBytecode = p_stage->GetBytecode();
		}
	}

	// ========================================================================
	// Shutdown
	// ========================================================================
	void DX12ShaderProgram::Shutdown()
	{
		m_vertexShaderBytecode.clear();
		m_pixelShaderBytecode.clear();
		m_shaderStages.clear();
	}
}
