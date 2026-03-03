/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <OvRendering/HAL/DirectX12/DX12RootSignature.h>
#include <OvRendering/HAL/DirectX12/DX12Device.h>

#include <OvDebug/Logger.h>

namespace OvRendering::HAL::DX12
{
	// ========================================================================
	// DX12RootSignature Constructor
	// ========================================================================
	DX12RootSignature::DX12RootSignature()
	{
	}

	// ========================================================================
	// DX12RootSignature Destructor
	// ========================================================================
	DX12RootSignature::~DX12RootSignature()
	{
		Shutdown();
	}

	// ========================================================================
	// Initialize
	// ========================================================================
	bool DX12RootSignature::Initialize(
		uint32_t p_numCBVDescriptors,
		uint32_t p_numSRVDescriptors,
		uint32_t p_numUAVDescriptors
	)
	{
		// Build root parameters
		std::vector<D3D12_ROOT_PARAMETER> rootParams;
		uint32_t rootParamIndex = 0;

		// Add CBV (Constant Buffer View)
		if (p_numCBVDescriptors > 0)
		{
			D3D12_ROOT_PARAMETER cbvParam = {};
			cbvParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
			cbvParam.Descriptor.ShaderRegister = 0;
			cbvParam.Descriptor.RegisterSpace = 0;
			cbvParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			rootParams.push_back(cbvParam);
		}

		// Add SRV (Shader Resource View) descriptor table
		if (p_numSRVDescriptors > 0)
		{
			D3D12_ROOT_PARAMETER srvParam = {};
			srvParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			srvParam.DescriptorTable.NumDescriptorRanges = 1;

			D3D12_DESCRIPTOR_RANGE* srvRange = new D3D12_DESCRIPTOR_RANGE;
			srvRange->RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
			srvRange->NumDescriptors = p_numSRVDescriptors;
			srvRange->BaseShaderRegister = 0;
			srvRange->RegisterSpace = 0;
			srvRange->OffsetInDescriptorsFromTableStart = 0;

			srvParam.DescriptorTable.pDescriptorRanges = srvRange;
			srvParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			rootParams.push_back(srvParam);

			// Store range for cleanup (simplified - in production use proper RAII)
		}

		// Add UAV (Unordered Access View) descriptor table
		if (p_numUAVDescriptors > 0)
		{
			D3D12_ROOT_PARAMETER uavParam = {};
			uavParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			uavParam.DescriptorTable.NumDescriptorRanges = 1;

			D3D12_DESCRIPTOR_RANGE* uavRange = new D3D12_DESCRIPTOR_RANGE;
			uavRange->RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
			uavRange->NumDescriptors = p_numUAVDescriptors;
			uavRange->BaseShaderRegister = 0;
			uavRange->RegisterSpace = 1;
			uavRange->OffsetInDescriptorsFromTableStart = 0;

			uavParam.DescriptorTable.pDescriptorRanges = uavRange;
			uavParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			rootParams.push_back(uavParam);
		}

		// Create root signature descriptor
		D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
		rootSigDesc.NumParameters = static_cast<UINT>(rootParams.size());
		rootSigDesc.pParameters = rootParams.data();
		rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

		// Serialize root signature
		ComPtr<ID3DBlob> signatureBlob;
		ComPtr<ID3DBlob> errorBlob;

		HRESULT hr = D3D12SerializeRootSignature(
			&rootSigDesc,
			D3D_ROOT_SIGNATURE_VERSION_1,
			&signatureBlob,
			&errorBlob
		);

		if (FAILED(hr))
		{
			if (errorBlob)
			{
				OVLOG_ERROR("Root signature serialization failed: " +
				           std::string(reinterpret_cast<const char*>(errorBlob->GetBufferPointer())));
			}
			else
			{
				OVLOG_ERROR("Root signature serialization failed");
			}
			return false;
		}

		// Create root signature
		hr = DX12Device::GetDevice()->CreateRootSignature(
			0,
			signatureBlob->GetBufferPointer(),
			signatureBlob->GetBufferSize(),
			IID_PPV_ARGS(&m_rootSignature)
		);

		if (FAILED(hr))
		{
			OVLOG_ERROR("Failed to create root signature");
			return false;
		}

		// Store blob for PSO creation
		m_rootSignatureBlob = signatureBlob;

		OVLOG_INFO("Root signature created: " + std::to_string(rootParams.size()) + " parameters");

		return true;
	}

	// ========================================================================
	// Shutdown
	// ========================================================================
	void DX12RootSignature::Shutdown()
	{
		m_rootSignature.Reset();
		m_rootSignatureBlob.Reset();
	}

	// ========================================================================
	// DX12PipelineStateObject Constructor
	// ========================================================================
	DX12PipelineStateObject::DX12PipelineStateObject()
	{
	}

	// ========================================================================
	// DX12PipelineStateObject Destructor
	// ========================================================================
	DX12PipelineStateObject::~DX12PipelineStateObject()
	{
		Shutdown();
	}

	// ========================================================================
	// Create
	// ========================================================================
	bool DX12PipelineStateObject::Create(const PSODescription& p_desc)
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};

		// Root signature
		psoDesc.pRootSignature = p_desc.rootSignature;

		// Vertex shader
		if (p_desc.vertexShaderCode && !p_desc.vertexShaderCode->empty())
		{
			psoDesc.VS.pShaderBytecode = p_desc.vertexShaderCode->data();
			psoDesc.VS.BytecodeLength = p_desc.vertexShaderCode->size();
		}

		// Pixel shader
		if (p_desc.pixelShaderCode && !p_desc.pixelShaderCode->empty())
		{
			psoDesc.PS.pShaderBytecode = p_desc.pixelShaderCode->data();
			psoDesc.PS.BytecodeLength = p_desc.pixelShaderCode->size();
		}

		// Input layout
		psoDesc.InputLayout.pInputElementDescs = p_desc.inputElements;
		psoDesc.InputLayout.NumElements = p_desc.numInputElements;

		// Primitive topology
		psoDesc.PrimitiveTopologyType = p_desc.primitiveTopology;

		// Render target formats
		psoDesc.NumRenderTargets = p_desc.numColorFormats;
		for (uint32_t i = 0; i < p_desc.numColorFormats && i < 8; ++i)
		{
			psoDesc.RTVFormats[i] = p_desc.colorFormats[i];
		}

		// Depth stencil format
		psoDesc.DSVFormat = p_desc.depthStencilFormat;

		// Sample count
		psoDesc.SampleDesc.Count = p_desc.sampleCount;
		psoDesc.SampleDesc.Quality = 0;

		// Rasterizer state (use defaults)
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC::Default();

		// Blend state (use defaults)
		psoDesc.BlendState = CD3DX12_BLEND_DESC::Default();

		// Depth stencil state (use defaults)
		psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC::Default();

		// Create PSO
		HRESULT hr = DX12Device::GetDevice()->CreateGraphicsPipelineState(
			&psoDesc,
			IID_PPV_ARGS(&m_pso)
		);

		if (FAILED(hr))
		{
			OVLOG_ERROR("Failed to create pipeline state object");
			return false;
		}

		OVLOG_INFO("Pipeline State Object created successfully");

		return true;
	}

	// ========================================================================
	// Shutdown
	// ========================================================================
	void DX12PipelineStateObject::Shutdown()
	{
		m_pso.Reset();
	}
}
