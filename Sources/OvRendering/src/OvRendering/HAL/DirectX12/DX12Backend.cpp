/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <OvRendering/HAL/DirectX12/DX12Backend.h>

#include <OvDebug/Logger.h>
#include <OvDebug/Assertion.h>

#include <OvRendering/HAL/DirectX12/DX12Device.h>
#include <OvRendering/HAL/DirectX12/DX12Types.h>
#include <OvRendering/Utils/Conversions.h>

namespace OvRendering::HAL
{
	// ========================================================================
	// Init
	// ========================================================================
	template<>
	std::optional<Data::PipelineState> DX12Backend::Init(bool p_debug, void* p_windowHandle)
	{
#ifdef _WIN32
		// Cast window handle to HWND
		HWND hwnd = static_cast<HWND>(p_windowHandle);

		if (!hwnd)
		{
			OVLOG_ERROR("DX12: Invalid window handle");
			return std::nullopt;
		}

		// Default window size (will be updated by DX12Device::Initialize)
		uint32_t width = 1280;
		uint32_t height = 720;

		// Initialize DX12 device
		if (!DX12::DX12Device::Initialize(hwnd, width, height, p_debug))
		{
			OVLOG_ERROR("DX12: Failed to initialize device");
			return std::nullopt;
		}

		OVLOG_INFO("DX12 Backend initialized successfully");
#else
		OVLOG_ERROR("DX12 is only supported on Windows");
		return std::nullopt;
#endif

		// Return default pipeline state
		Data::PipelineState pso;
		return pso;
	}

	// ========================================================================
	// OnFrameStarted
	// ========================================================================
	template<>
	void DX12Backend::OnFrameStarted()
	{
#ifdef _WIN32
		// Wait for previous frame
		DX12::DX12Device::MoveToNextFrame();

		// Reset command allocator and list
		ID3D12CommandAllocator* allocator = DX12::DX12Device::GetCommandAllocator();
		ID3D12GraphicsCommandList* commandList = DX12::DX12Device::GetCommandList();

		allocator->Reset();
		commandList->Reset(allocator, nullptr);

		// Begin render pass
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = DX12::DX12Device::GetRTVDescriptorHeap()->GetCPUDescriptorHandleForHeapStart();
		rtvHandle.ptr += DX12::DX12Device::GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV) * DX12::DX12Device::GetCurrentFrameIndex();

		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = DX12::DX12Device::GetDSVHandle();

		commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

		// Set viewport and scissor from DX12Device
		commandList->RSSetViewports(1, &DX12::DX12Device::GetViewport());
		commandList->RSSetScissorRects(1, &DX12::DX12Device::GetScissorRect());
#endif
	}

	// ========================================================================
	// OnFrameCompleted
	// ========================================================================
	template<>
	void DX12Backend::OnFrameCompleted()
	{
#ifdef _WIN32
		ID3D12GraphicsCommandList* commandList = DX12::DX12Device::GetCommandList();

		// Close command list
		commandList->Close();

		// Execute command list
		ID3D12CommandQueue* commandQueue = DX12::DX12Device::GetCommandQueue();
		commandQueue->ExecuteCommandLists(1, reinterpret_cast<ID3D12CommandList* const*>(&commandList));

		// Present
		DX12::DX12Device::Present();
#endif
	}

	// ========================================================================
	// Clear
	// ========================================================================
	template<>
	void DX12Backend::Clear(bool p_colorBuffer, bool p_depthBuffer, bool p_stencilBuffer)
	{
#ifdef _WIN32
		ID3D12GraphicsCommandList* commandList = DX12::DX12Device::GetCommandList();

		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = DX12::DX12Device::GetRTVDescriptorHeap()->GetCPUDescriptorHandleForHeapStart();
		rtvHandle.ptr += DX12::DX12Device::GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV) * DX12::DX12Device::GetCurrentFrameIndex();

		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = DX12::DX12Device::GetDSVHandle();

		if (p_colorBuffer)
		{
			commandList->ClearRenderTargetView(rtvHandle, m_context.clearColor, 0, nullptr);
		}

		if (p_depthBuffer || p_stencilBuffer)
		{
			D3D12_CLEAR_FLAGS clearFlags = static_cast<D3D12_CLEAR_FLAGS>(0);
			if (p_depthBuffer) clearFlags = static_cast<D3D12_CLEAR_FLAGS>(clearFlags | D3D12_CLEAR_FLAG_DEPTH);
			if (p_stencilBuffer) clearFlags = static_cast<D3D12_CLEAR_FLAGS>(clearFlags | D3D12_CLEAR_FLAG_STENCIL);

			commandList->ClearDepthStencilView(dsvHandle, clearFlags, 1.0f, 0, 0, nullptr);
		}
#endif
	}

	// ========================================================================
	// ReadPixels
	// ========================================================================
	template<>
	void DX12Backend::ReadPixels(
		uint32_t p_x,
		uint32_t p_y,
		uint32_t p_width,
		uint32_t p_height,
		Settings::EPixelDataFormat p_format,
		Settings::EPixelDataType p_type,
		void* p_data
	)
	{
		// TODO: Implement using Map/Unmap on readback buffer
		OVLOG_WARNING("DX12::ReadPixels is not implemented yet");
	}

	// ========================================================================
	// DrawElements
	// ========================================================================
	template<>
	void DX12Backend::DrawElements(Settings::EPrimitiveMode p_primitiveMode, uint32_t p_indexCount)
	{
#ifdef _WIN32
		ID3D12GraphicsCommandList* commandList = DX12::DX12Device::GetCommandList();

		// Set primitive topology
		D3D_PRIMITIVE_TOPOLOGY topology = OvRendering::Utils::Conversions::ToD3D12PrimitiveTopology(p_primitiveMode);
		commandList->IASetPrimitiveTopology(topology);

		// Draw indexed
		commandList->DrawIndexedInstanced(p_indexCount, 1, 0, 0, 0);
#endif
	}

	// ========================================================================
	// DrawElementsInstanced
	// ========================================================================
	template<>
	void DX12Backend::DrawElementsInstanced(
		Settings::EPrimitiveMode p_primitiveMode,
		uint32_t p_indexCount,
		uint32_t p_instances
	)
	{
#ifdef _WIN32
		ID3D12GraphicsCommandList* commandList = DX12::DX12Device::GetCommandList();

		// Set primitive topology
		D3D_PRIMITIVE_TOPOLOGY topology = OvRendering::Utils::Conversions::ToD3D12PrimitiveTopology(p_primitiveMode);
		commandList->IASetPrimitiveTopology(topology);

		// Draw indexed instanced
		commandList->DrawIndexedInstanced(p_indexCount, p_instances, 0, 0, 0);
#endif
	}

	// ========================================================================
	// DrawArrays
	// ========================================================================
	template<>
	void DX12Backend::DrawArrays(Settings::EPrimitiveMode p_primitiveMode, uint32_t p_vertexCount)
	{
#ifdef _WIN32
		ID3D12GraphicsCommandList* commandList = DX12::DX12Device::GetCommandList();

		// Set primitive topology
		D3D_PRIMITIVE_TOPOLOGY topology = OvRendering::Utils::Conversions::ToD3D12PrimitiveTopology(p_primitiveMode);
		commandList->IASetPrimitiveTopology(topology);

		// Draw
		commandList->DrawInstanced(p_vertexCount, 1, 0, 0);
#endif
	}

	// ========================================================================
	// DrawArraysInstanced
	// ========================================================================
	template<>
	void DX12Backend::DrawArraysInstanced(
		Settings::EPrimitiveMode p_primitiveMode,
		uint32_t p_vertexCount,
		uint32_t p_instances
	)
	{
#ifdef _WIN32
		ID3D12GraphicsCommandList* commandList = DX12::DX12Device::GetCommandList();

		// Set primitive topology
		D3D_PRIMITIVE_TOPOLOGY topology = OvRendering::Utils::Conversions::ToD3D12PrimitiveTopology(p_primitiveMode);
		commandList->IASetPrimitiveTopology(topology);

		// Draw instanced
		commandList->DrawInstanced(p_vertexCount, p_instances, 0, 0);
#endif
	}

	// ========================================================================
	// SetClearColor
	// ========================================================================
	template<>
	void DX12Backend::SetClearColor(float p_red, float p_green, float p_blue, float p_alpha)
	{
		m_context.clearColor[0] = p_red;
		m_context.clearColor[1] = p_green;
		m_context.clearColor[2] = p_blue;
		m_context.clearColor[3] = p_alpha;
	}

	// ========================================================================
	// SetRasterizationLinesWidth
	// ========================================================================
	template<>
	void DX12Backend::SetRasterizationLinesWidth(float p_width)
	{
		// DX12 does not support wide lines directly
		// Need to use geometry shader or compute shader for wide lines
		OVLOG_WARNING("SetRasterizationLinesWidth: DX12 does not support wide lines");
		m_context.lineWidth = p_width;
	}

	// ========================================================================
	// SetRasterizationMode
	// ========================================================================
	template<>
	void DX12Backend::SetRasterizationMode(Settings::ERasterizationMode p_rasterizationMode)
	{
		m_context.rasterizationMode = p_rasterizationMode;
		// Note: In DX12, rasterization mode is part of PSO
		// This would require recreating the PSO or using dynamic state (DX12 Ultimate)
	}

	// ========================================================================
	// SetCapability
	// ========================================================================
	template<>
	void DX12Backend::SetCapability(Settings::ERenderingCapability p_capability, bool p_value)
	{
		m_context.capabilities.set(static_cast<size_t>(p_capability), p_value);
		// Note: In DX12, most capabilities are part of PSO
		// They would require recreating the PSO or using dynamic state
	}

	// ========================================================================
	// GetCapability
	// ========================================================================
	template<>
	bool DX12Backend::GetCapability(Settings::ERenderingCapability p_capability)
	{
		return m_context.capabilities.test(static_cast<size_t>(p_capability));
	}

	// ========================================================================
	// SetStencilAlgorithm
	// ========================================================================
	template<>
	void DX12Backend::SetStencilAlgorithm(
		Settings::EComparaisonAlgorithm p_algorithm,
		int32_t p_reference,
		uint32_t p_mask
	)
	{
		m_context.stencilFunc = p_algorithm;
		m_context.stencilRef = p_reference;
		m_context.stencilMask = p_mask;
		// Note: Stencil state is part of PSO in DX12
	}

	// ========================================================================
	// SetDepthAlgorithm
	// ========================================================================
	template<>
	void DX12Backend::SetDepthAlgorithm(Settings::EComparaisonAlgorithm p_algorithm)
	{
		m_context.depthFunc = p_algorithm;
		// Note: Depth state is part of PSO in DX12
	}

	// ========================================================================
	// SetStencilMask
	// ========================================================================
	template<>
	void DX12Backend::SetStencilMask(uint32_t p_mask)
	{
		m_context.stencilWriteMask = p_mask;
	}

	// ========================================================================
	// SetStencilOperations
	// ========================================================================
	template<>
	void DX12Backend::SetStencilOperations(
		Settings::EOperation p_stencilFail,
		Settings::EOperation p_depthFail,
		Settings::EOperation p_bothPass
	)
	{
		m_context.stencilOpFail = p_stencilFail;
		m_context.depthOpFail = p_depthFail;
		m_context.bothOpFail = p_bothPass;
	}

	// ========================================================================
	// SetBlendingFunction
	// ========================================================================
	template<>
	void DX12Backend::SetBlendingFunction(
		Settings::EBlendingFactor p_sourceFactor,
		Settings::EBlendingFactor p_destinationFactor
	)
	{
		m_context.blendSrc = p_sourceFactor;
		m_context.blendDst = p_destinationFactor;
		// Note: Blend state is part of PSO in DX12
	}

	// ========================================================================
	// SetBlendingEquation
	// ========================================================================
	template<>
	void DX12Backend::SetBlendingEquation(Settings::EBlendingEquation p_equation)
	{
		m_context.blendEquation = p_equation;
		// Note: Blend state is part of PSO in DX12
	}

	// ========================================================================
	// SetCullFace
	// ========================================================================
	template<>
	void DX12Backend::SetCullFace(Settings::ECullFace p_cullFace)
	{
		m_context.cullFace = p_cullFace;
		// Note: Cull mode is part of PSO in DX12
	}

	// ========================================================================
	// SetDepthWriting
	// ========================================================================
	template<>
	void DX12Backend::SetDepthWriting(bool p_enable)
	{
		m_context.depthWriteEnabled = p_enable;
		// Note: Depth write mask is part of PSO in DX12
	}

	// ========================================================================
	// SetColorWriting
	// ========================================================================
	template<>
	void DX12Backend::SetColorWriting(bool p_enableRed, bool p_enableGreen, bool p_enableBlue, bool p_enableAlpha)
	{
		m_context.colorWriteMask.r = p_enableRed;
		m_context.colorWriteMask.g = p_enableGreen;
		m_context.colorWriteMask.b = p_enableBlue;
		m_context.colorWriteMask.a = p_enableAlpha;
		// Note: Render target write mask is part of PSO in DX12
	}

	// ========================================================================
	// SetViewport
	// ========================================================================
	template<>
	void DX12Backend::SetViewport(uint32_t p_x, uint32_t p_y, uint32_t p_width, uint32_t p_height)
	{
		D3D12_VIEWPORT viewport = {
			static_cast<float>(p_x),
			static_cast<float>(p_y),
			static_cast<float>(p_width),
			static_cast<float>(p_height),
			0.0f,
			1.0f
		};
		DX12::DX12Device::SetViewport(viewport);

		D3D12_RECT scissorRect = {
			0,
			0,
			static_cast<LONG>(p_width),
			static_cast<LONG>(p_height)
		};
		DX12::DX12Device::SetScissorRect(scissorRect);

#ifdef _WIN32
		// Set viewport and scissor immediately on command list if recording
		ID3D12GraphicsCommandList* commandList = DX12::DX12Device::GetCommandList();
		if (commandList)
		{
			commandList->RSSetViewports(1, &viewport);
			commandList->RSSetScissorRects(1, &scissorRect);
		}
#endif
	}

	// ========================================================================
	// GetVendor
	// ========================================================================
	template<>
	std::string DX12Backend::GetVendor()
	{
#ifdef _WIN32
		// Get adapter description via IDXGIDevice
		ComPtr<IDXGIDevice> dxgiDevice;
		if (SUCCEEDED(DX12::DX12Device::GetDevice()->QueryInterface(IID_PPV_ARGS(&dxgiDevice))))
		{
			ComPtr<IDXGIAdapter> adapter;
			if (SUCCEEDED(dxgiDevice->GetAdapter(&adapter)))
			{
				DXGI_ADAPTER_DESC desc;
				adapter->GetDesc(&desc);

				// Convert wchar_t* to string using WideCharToMultiByte
				int size = WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, nullptr, 0, nullptr, nullptr);
				std::string vendorName(size - 1, 0); // -1 to exclude null terminator
				WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, &vendorName[0], size, nullptr, nullptr);
				return vendorName;
			}
		}
#endif
		return "DirectX 12";
	}

	// ========================================================================
	// GetHardware
	// ========================================================================
	template<>
	std::string DX12Backend::GetHardware()
	{
#ifdef _WIN32
		// Get adapter description via IDXGIDevice
		ComPtr<IDXGIDevice> dxgiDevice;
		if (SUCCEEDED(DX12::DX12Device::GetDevice()->QueryInterface(IID_PPV_ARGS(&dxgiDevice))))
		{
			ComPtr<IDXGIAdapter> adapter;
			if (SUCCEEDED(dxgiDevice->GetAdapter(&adapter)))
			{
				DXGI_ADAPTER_DESC desc;
				adapter->GetDesc(&desc);

				// Convert wchar_t* to string using WideCharToMultiByte
				int size = WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, nullptr, 0, nullptr, nullptr);
				std::string hwName(size - 1, 0); // -1 to exclude null terminator
				WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, &hwName[0], size, nullptr, nullptr);
				return hwName;
			}
		}
#endif
		return "DX12 Device";
	}

	// ========================================================================
	// GetVersion
	// ========================================================================
	template<>
	std::string DX12Backend::GetVersion()
	{
		return "DirectX 12";
	}

	// ========================================================================
	// GetShadingLanguageVersion
	// ========================================================================
	template<>
	std::string DX12Backend::GetShadingLanguageVersion()
	{
		return "HLSL 6.0";
	}
}
