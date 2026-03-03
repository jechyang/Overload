/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

// Disable deprecated function warnings for tmpnam
#define _CRT_SECURE_NO_WARNINGS

#include <OvRendering/HAL/DirectX12/DX12ShaderCompiler.h>

#include <OvDebug/Logger.h>

#include <dxcapi.h>

#include <fstream>
#include <cstdio>

#pragma comment(lib, "dxcompiler.lib")

// Helper function to load file as blob
namespace
{
	Microsoft::WRL::ComPtr<IDxcBlobEncoding> LoadFileAsBlob(const std::wstring& p_filePath)
	{
		// Create IDxcUtils to load file
		Microsoft::WRL::ComPtr<IDxcUtils> utils;
		HRESULT hr = DxcCreateInstance(__uuidof(IDxcUtils), IID_PPV_ARGS(&utils));
		if (FAILED(hr))
		{
			return nullptr;
		}

		// Load file using IDxcUtils::LoadFile
		Microsoft::WRL::ComPtr<IDxcBlobEncoding> blob;
		uint32_t codePage = CP_UTF8;
		hr = utils->LoadFile(p_filePath.c_str(), &codePage, &blob);
		if (FAILED(hr))
		{
			return nullptr;
		}

		return blob;
	}
}

namespace OvRendering::HAL::DX12
{
	// ========================================================================
	// DX12ShaderCompiler Constructor
	// ========================================================================
	DX12ShaderCompiler::DX12ShaderCompiler()
	{
	}

	// ========================================================================
	// DX12ShaderCompiler Destructor
	// ========================================================================
	DX12ShaderCompiler::~DX12ShaderCompiler()
	{
		Shutdown();
	}

	// ========================================================================
	// Initialize
	// ========================================================================
	bool DX12ShaderCompiler::Initialize()
	{
		if (m_initialized)
		{
			return true;
		}

		// Create DXC compiler
		HRESULT hr = DxcCreateInstance(__uuidof(IDxcCompiler), IID_PPV_ARGS(&m_compiler));
		if (FAILED(hr))
		{
			OVLOG_ERROR("Failed to create DXC compiler");
			return false;
		}

		// Create include handler
		hr = DxcCreateInstance(__uuidof(IDxcIncludeHandler), IID_PPV_ARGS(&m_includeHandler));
		if (FAILED(hr))
		{
			OVLOG_ERROR("Failed to create DXC include handler");
			return false;
		}

		m_initialized = true;
		OVLOG_INFO("DX12 Shader Compiler initialized");

		return true;
	}

	// ========================================================================
	// CompileFromFile
	// ========================================================================
	ShaderCompileResult DX12ShaderCompiler::CompileFromFile(
		const std::string& p_filePath,
		const std::string& p_entryPoint,
		const std::string& p_target,
		const std::vector<std::pair<std::string, std::string>>& p_defines
	)
	{
		ShaderCompileResult result;

		if (!m_initialized)
		{
			result.errorMessage = "Compiler not initialized";
			return result;
		}

		// Convert file path to wide string
		std::wstring wideFilePath(p_filePath.begin(), p_filePath.end());
		std::wstring wideEntryPoint(p_entryPoint.begin(), p_entryPoint.end());
		std::wstring wideTarget(p_target.begin(), p_target.end());

		// Create blob from file using helper function
		Microsoft::WRL::ComPtr<IDxcBlobEncoding> sourceBlob = LoadFileAsBlob(wideFilePath);
		if (!sourceBlob)
		{
			result.errorMessage = "Failed to read shader file: " + p_filePath;
			return result;
		}

		// Build arguments
		std::vector<LPCWSTR> arguments;
		arguments.push_back(wideEntryPoint.c_str());
		arguments.push_back(wideTarget.c_str());

		// Add defines
		for (const auto& define : p_defines)
		{
			std::wstring defineStr = L"-D" + std::wstring(define.first.begin(), define.first.end());
			if (!define.second.empty())
			{
				defineStr += L"=" + std::wstring(define.second.begin(), define.second.end());
			}
			arguments.push_back(defineStr.c_str());
		}

		// Create source buffer
		DxcBuffer sourceBuffer;
		sourceBuffer.Ptr = sourceBlob->GetBufferPointer();
		sourceBuffer.Size = sourceBlob->GetBufferSize();
		sourceBuffer.Encoding = CP_UTF8;

		// Compile using IDxcCompiler3
		Microsoft::WRL::ComPtr<IDxcOperationResult> compileResult;
		HRESULT hr = m_compiler->Compile(
			&sourceBuffer,
			arguments.data(),
			static_cast<uint32_t>(arguments.size()),
			m_includeHandler.Get(),
			IID_PPV_ARGS(&compileResult)
		);

		if (FAILED(hr))
		{
			result.errorMessage = "Failed to compile shader";
			return result;
		}

		// Check result
		HRESULT status = S_OK;
		compileResult->GetStatus(&status);

		if (FAILED(status))
		{
			// Get error message
			Microsoft::WRL::ComPtr<IDxcBlobEncoding> errorBlob;
			compileResult->GetErrorBuffer(&errorBlob);
			if (errorBlob)
			{
				result.errorMessage = std::string(
					reinterpret_cast<const char*>(errorBlob->GetBufferPointer()),
					errorBlob->GetBufferSize()
				);
			}
			else
			{
				result.errorMessage = "Shader compilation failed with no error message";
			}
			return result;
		}

		// Get shader bytecode
		Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob;
		compileResult->GetResult(&shaderBlob);

		if (shaderBlob)
		{
			result.bytecode.resize(shaderBlob->GetBufferSize());
			memcpy(result.bytecode.data(), shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize());
			result.success = true;
			OVLOG_INFO("Shader compiled successfully: " + p_filePath);
		}
		else
		{
			result.errorMessage = "Compilation succeeded but returned null bytecode";
		}

		return result;
	}

	// ========================================================================
	// CompileFromSource
	// ========================================================================
	ShaderCompileResult DX12ShaderCompiler::CompileFromSource(
		const std::string& p_source,
		const std::string& p_entryPoint,
		const std::string& p_target,
		const std::vector<std::pair<std::string, std::string>>& p_defines
	)
	{
		ShaderCompileResult result;

		if (!m_initialized)
		{
			result.errorMessage = "Compiler not initialized";
			return result;
		}

		// Write source to temp file
		char tempPath[MAX_PATH];
		std::tmpnam(tempPath);
		std::wstring wideTempPath(tempPath, tempPath + strlen(tempPath));
		std::ofstream tempFile(tempPath, std::ios::binary);
		if (!tempFile)
		{
			result.errorMessage = "Failed to create temp file for shader compilation";
			return result;
		}
		tempFile.write(p_source.c_str(), p_source.size());
		tempFile.close();

		// Compile from temp file
		result = CompileFromFile(
			tempPath,
			p_entryPoint,
			p_target,
			p_defines
		);

		// Delete temp file
		std::remove(tempPath);

		return result;
	}

	// ========================================================================
	// Shutdown
	// ========================================================================
	void DX12ShaderCompiler::Shutdown()
	{
		m_compiler.Reset();
		m_includeHandler.Reset();
		m_initialized = false;
	}
}
