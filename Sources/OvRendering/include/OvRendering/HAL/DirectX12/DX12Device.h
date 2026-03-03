/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <OvRendering/HAL/DirectX12/DX12Prerequisites.h>

namespace OvRendering::HAL::DX12
{
	/**
	* DX12 Device initialization and management
	*/
	class DX12Device
	{
	public:
		/**
		* Initialize DX12 device, swap chain and command queue
		* @param p_windowHandle Window handle for swap chain creation
		* @param p_width Window width
		* @param p_height Window height
		* @param p_useDebugLayer Enable debug layer if true
		* @return true if initialization succeeded
		*/
		static bool Initialize(
			HWND p_windowHandle,
			uint32_t p_width,
			uint32_t p_height,
			bool p_useDebugLayer
		);

		/**
		* Cleanup DX12 resources
		*/
		static void Shutdown();

		/**
		* Get the DX12 device
		*/
		static ID3D12Device* GetDevice() { return m_device.Get(); }

		/**
		* Get the command queue
		*/
		static ID3D12CommandQueue* GetCommandQueue() { return m_commandQueue.Get(); }

		/**
		* Get the swap chain
		*/
		static IDXGISwapChain3* GetSwapChain() { return m_swapChain.Get(); }

		/**
		* Get command allocator for current frame
		*/
		static ID3D12CommandAllocator* GetCommandAllocator();

		/**
		* Get the graphics command list
		*/
		static ID3D12GraphicsCommandList* GetCommandList() { return m_commandList.Get(); }

		/**
		* Get current back buffer index
		*/
		static uint32_t GetCurrentFrameIndex() { return m_currentFrameIndex; }

		/**
		* Get current back buffer RTV
		*/
		static ID3D12Resource* GetCurrentBackBuffer();

		/**
		* Get RTV descriptor heap
		*/
		static ID3D12DescriptorHeap* GetRTVDescriptorHeap() { return m_rtvHeap.Get(); }

		/**
		* Get DSV descriptor heap
		*/
		static ID3D12DescriptorHeap* GetDSVDescriptorHeap() { return m_dsvHeap.Get(); }

		/**
		* Get depth stencil buffer
		*/
		static ID3D12Resource* GetDepthStencilBuffer() { return m_depthStencil.Get(); }

		/**
		* Move to next frame (handle synchronization)
		*/
		static void MoveToNextFrame();

		/**
		* Present the swap chain
		*/
		static void Present();

		/**
		* Resize buffers
		*/
		static void ResizeBuffers(uint32_t p_width, uint32_t p_height);

		/**
		* Get depth stencil view CPU handle
		*/
		static D3D12_CPU_DESCRIPTOR_HANDLE GetDSVHandle();

		/**
		* Get window width
		*/
		static uint32_t GetWidth() { return m_width; }

		/**
		* Get window height
		*/
		static uint32_t GetHeight() { return m_height; }

		/**
		* Get viewport
		*/
		static const D3D12_VIEWPORT& GetViewport() { return m_viewport; }

		/**
		* Get scissor rect
		*/
		static const D3D12_RECT& GetScissorRect() { return m_scissorRect; }

		/**
		* Set viewport
		*/
		static void SetViewport(const D3D12_VIEWPORT& p_viewport) { m_viewport = p_viewport; }

		/**
		* Set scissor rect
		*/
		static void SetScissorRect(const D3D12_RECT& p_scissorRect) { m_scissorRect = p_scissorRect; }

	private:
		// Device
		static ComPtr<ID3D12Device> m_device;

		// Command Queue
		static ComPtr<ID3D12CommandQueue> m_commandQueue;

		// Swap Chain
		static ComPtr<IDXGISwapChain3> m_swapChain;

		// Command Allocators (one per frame)
		static constexpr uint32_t FrameCount = 2;
		static std::array<ComPtr<ID3D12CommandAllocator>, FrameCount> m_commandAllocators;

		// Command List
		static ComPtr<ID3D12GraphicsCommandList> m_commandList;

		// Fences
		static ComPtr<ID3D12Fence> m_fence;
		static uint64_t m_fenceValues[FrameCount];
		static HANDLE m_fenceEvent;

		// Current frame index
		static uint32_t m_currentFrameIndex;

		// RTV Heap
		static ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
		static std::array<ComPtr<ID3D12Resource>, FrameCount> m_renderTargets;
		static D3D12_CPU_DESCRIPTOR_HANDLE m_rtvHandles[FrameCount];

		// DSV Heap
		static ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
		static ComPtr<ID3D12Resource> m_depthStencil;
		static D3D12_CPU_DESCRIPTOR_HANDLE m_dsvHandle;

		// Viewport and scissor
		static D3D12_VIEWPORT m_viewport;
		static D3D12_RECT m_scissorRect;

		// Window handle
		static HWND m_windowHandle;

		// Window dimensions
		static uint32_t m_width;
		static uint32_t m_height;

		// Private methods
		static bool CreateDevice(bool p_useDebugLayer);
		static bool CreateCommandQueue();
		static bool CreateSwapChain(HWND p_windowHandle, uint32_t p_width, uint32_t p_height);
		static bool CreateRenderTargetViews(uint32_t p_width, uint32_t p_height);
		static bool CreateDepthStencilView(uint32_t p_width, uint32_t p_height);
		static bool CreateFence();
		static void CreateDescriptorHeaps();
	};
}
