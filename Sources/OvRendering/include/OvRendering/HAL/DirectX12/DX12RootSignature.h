/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <OvRendering/HAL/DirectX12/DX12Prerequisites.h>
#include <OvRendering/HAL/DirectX12/DX12ShaderStage.h>

#include <vector>

namespace OvRendering::HAL::DX12
{
	/**
	* DX12 Root Signature
	*/
	class DX12RootSignature
	{
	public:
		/**
		* Constructor
		*/
		DX12RootSignature();

		/**
		* Destructor
		*/
		~DX12RootSignature();

		/**
		* Initialize root signature
		* @param p_numCBVDescriptors Number of CBV descriptors
		* @param p_numSRVDescriptors Number of SRV descriptors
		* @param p_numUAVDescriptors Number of UAV descriptors
		* @return true if succeeded
		*/
		bool Initialize(
			uint32_t p_numCBVDescriptors = 1,
			uint32_t p_numSRVDescriptors = 4,
			uint32_t p_numUAVDescriptors = 0
		);

		/**
		* Cleanup
		*/
		void Shutdown();

		/**
		* Get the root signature
		*/
		ID3D12RootSignature* GetRootSignature() const { return m_rootSignature.Get(); }

		/**
		* Check if root signature is valid
		*/
		bool IsValid() const { return m_rootSignature != nullptr; }

	private:
		ComPtr<ID3D12RootSignature> m_rootSignature;
		ComPtr<ID3DBlob> m_rootSignatureBlob;
	};

	/**
	* DX12 Pipeline State Object (PSO)
	*/
	class DX12PipelineStateObject
	{
	public:
		/**
		* Constructor
		*/
		DX12PipelineStateObject();

		/**
		* Destructor
		*/
		~DX12PipelineStateObject();

		/**
		* PSO Description
		*/
		struct PSODescription
		{
			// Shaders
			const std::vector<byte>* vertexShaderCode = nullptr;
			const std::vector<byte>* pixelShaderCode = nullptr;

			// Input layout
			const D3D12_INPUT_ELEMENT_DESC* inputElements = nullptr;
			uint32_t numInputElements = 0;

			// Root signature
			ID3D12RootSignature* rootSignature = nullptr;

			// Render target formats
			DXGI_FORMAT colorFormats[8] = { DXGI_FORMAT_UNKNOWN };
			uint32_t numColorFormats = 0;
			DXGI_FORMAT depthStencilFormat = DXGI_FORMAT_UNKNOWN;

			// Rasterizer state
			D3D12_RASTERIZER_DESC rasterizerState;

			// Blend state
			D3D12_BLEND_DESC blendState;

			// Depth stencil state
			D3D12_DEPTH_STENCIL_DESC depthStencilState;

			// Primitive topology
			D3D12_PRIMITIVE_TOPOLOGY_TYPE primitiveTopology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

			// Sample count
			uint32_t sampleCount = 1;
		};

		/**
		* Create PSO
		*/
		bool Create(const PSODescription& p_desc);

		/**
		* Cleanup
		*/
		void Shutdown();

		/**
		* Get the PSO
		*/
		ID3D12PipelineState* GetPipelineState() const { return m_pso.Get(); }

		/**
		* Check if PSO is valid
		*/
		bool IsValid() const { return m_pso != nullptr; }

	private:
		ComPtr<ID3D12PipelineState> m_pso;
	};
}
