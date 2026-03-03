/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

// Prevent Windows min/max macros from interfering with std::min/std::max
#ifndef NOMINMAX
#define NOMINMAX
#endif

// Reduce Windows.h macro pollution
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

// Windows Platform Header
#include <Windows.h>

// DirectX 12 Headers
#include <d3d12.h>
#include <dxgi1_4.h>
#include <DirectXMath.h>
#include <dxgiformat.h>
#include <d3dcommon.h>

// DirectX Shader Compiler
#include <dxcapi.h>
#include <d3d12shader.h>

// COM Smart Pointers
#include <wrl/client.h>

// Debug Layer
#include <dxgidebug.h>

// STL
#include <vector>
#include <array>
#include <string>
#include <optional>

// CD3DX12 helper classes (minimal implementation)
#include <OvRendering/HAL/DirectX12/DX12Helper.h>

namespace OvRendering::HAL
{
	// Microsoft WRL ComPtr for RAII
	using Microsoft::WRL::ComPtr;

	// DirectX 12 native types alias
	using DX12DevicePtr = ID3D12Device*;
	using DX12CommandQueuePtr = ID3D12CommandQueue*;
	using DX12CommandAllocatorPtr = ID3D12CommandAllocator*;
	using DX12GraphicsCommandListPtr = ID3D12GraphicsCommandList*;
	using DX12FencePtr = ID3D12Fence*;
	using DX12DescriptorHeapPtr = ID3D12DescriptorHeap*;
	using DX12RootSignaturePtr = ID3D12RootSignature*;
	using DX12PipelineStatePtr = ID3D12PipelineState*;
	using DX12ResourcePtr = ID3D12Resource*;
	using DX12HeapPtr = ID3D12Heap*;

	// DXGI types alias
	using DXGIFactoryPtr = IDXGIFactory4*;
	using IDXGISwapChainPtr = IDXGISwapChain3*;

	// Shader Compiler types
	using DXCCompilerPtr = IDxcCompiler3*;
	using DXCIncludeHandlerPtr = IDxcIncludeHandler*;
	using DXCOperationResultPtr = IDxcOperationResult*;
}

// Helper macro for HRESULT checking
#define DX12_CHECK_HR(hr, msg) \
    if (FAILED(hr)) { \
        OVLOG_ERROR(std::string(msg) + " HRESULT: " + std::to_string(hr)); \
        __debugbreak(); \
    }

// Helper macro for COM creation
#define DX12_COM_CREATE(comInterface, factoryMethod, ...) \
    ComPtr<comInterface> p##comInterface; \
    factoryMethod(__VA_ARGS__, IID_PPV_ARGS(&p##comInterface))
