/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <OvRendering/HAL/Common/TBackend.h>
#include <OvRendering/HAL/DirectX12/DX12Prerequisites.h>
#include <OvRendering/Settings/ERenderingCapability.h>

#include <array>
#include <bitset>
#include <string>

namespace OvRendering::HAL
{
	/**
	* DX12 Backend context structure holding all DX12 resources and state
	*/
	struct DX12BackendContext
	{
		// ====================================================================
		// DX12 Core Resources
		// ====================================================================

		// Device
		ComPtr<ID3D12Device> device;

		// Command Queue
		ComPtr<ID3D12CommandQueue> commandQueue;

		// Swap Chain
		ComPtr<IDXGISwapChain3> swapChain;

		// Command Allocators (one per frame for frame latency)
		static constexpr uint32_t FrameCount = 2;
		std::array<ComPtr<ID3D12CommandAllocator>, FrameCount> commandAllocators;

		// Command List
		ComPtr<ID3D12GraphicsCommandList> commandList;

		// Fences for synchronization
		ComPtr<ID3D12Fence> fence;
		uint64_t fenceValues[FrameCount] = {};
		std::array<HANDLE, FrameCount> fenceEvents;

		// Current frame index
		uint32_t currentFrameIndex = 0;

		// Viewport
		D3D12_VIEWPORT viewport = {};

		// Scissor rect
		D3D12_RECT scissorRect = {};

		// Pipeline State
		ComPtr<ID3D12PipelineState> currentPSO;

		// Root Signature
		ComPtr<ID3D12RootSignature> currentRootSignature;

		// Debug interface
		ComPtr<IDXGIInfoQueue> dxgiInfoQueue;

		// Back buffer count
		uint32_t backBufferCount = FrameCount;

		// ====================================================================
		// Descriptor Heaps
		// ====================================================================

		// RTV Heap
		ComPtr<ID3D12DescriptorHeap> rtvHeap;
		std::array<ComPtr<ID3D12Resource>, FrameCount> renderTargets;
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[FrameCount] = {};

		// DSV Heap
		ComPtr<ID3D12DescriptorHeap> dsvHeap;
		ComPtr<ID3D12Resource> depthStencil;
		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = {};

		// CBV Heap
		ComPtr<ID3D12DescriptorHeap> cbvHeap;

		// SRV Heap
		ComPtr<ID3D12DescriptorHeap> srvHeap;

		// ====================================================================
		// Render State (tracked for PSO creation)
		// ====================================================================

		// Clear color
		float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

		// Rasterization state
		Settings::ERasterizationMode rasterizationMode = Settings::ERasterizationMode::FILL;
		float lineWidth = 1.0f;

		// Capabilities bitmask
		std::bitset<static_cast<size_t>(Settings::ERenderingCapability::COUNT)> capabilities;

		// Depth stencil state
		Settings::EComparaisonAlgorithm depthFunc = Settings::EComparaisonAlgorithm::LESS_EQUAL;
		bool depthWriteEnabled = true;
		Settings::EComparaisonAlgorithm stencilFunc = Settings::EComparaisonAlgorithm::ALWAYS;
		int32_t stencilRef = 0;
		uint32_t stencilMask = 0xFFFFFFFF;
		uint32_t stencilWriteMask = 0xFFFFFFFF;
		Settings::EOperation stencilOpFail = Settings::EOperation::KEEP;
		Settings::EOperation depthOpFail = Settings::EOperation::KEEP;
		Settings::EOperation bothOpFail = Settings::EOperation::KEEP;

		// Blend state
		Settings::EBlendingFactor blendSrc = Settings::EBlendingFactor::ONE;
		Settings::EBlendingFactor blendDst = Settings::EBlendingFactor::ZERO;
		Settings::EBlendingEquation blendEquation = Settings::EBlendingEquation::FUNC_ADD;

		// Color write mask
		struct { bool r = true, g = true, b = true, a = true; } colorWriteMask;

		// Culling
		Settings::ECullFace cullFace = Settings::ECullFace::BACK;
	};

	using DX12Backend = TBackend<Settings::EGraphicsBackend::DIRECTX12, DX12BackendContext>;
}
