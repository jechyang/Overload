/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <OvRendering/HAL/DirectX12/DX12Device.h>

#include <OvDebug/Logger.h>
#include <OvDebug/Assertion.h>

#include <dxgi1_6.h>
#include <d3d12sdklayers.h>

namespace OvRendering::HAL::DX12
{
	// Static member definitions
	ComPtr<ID3D12Device> DX12Device::m_device;
	ComPtr<ID3D12CommandQueue> DX12Device::m_commandQueue;
	ComPtr<IDXGISwapChain3> DX12Device::m_swapChain;
	std::array<ComPtr<ID3D12CommandAllocator>, DX12Device::FrameCount> DX12Device::m_commandAllocators;
	ComPtr<ID3D12GraphicsCommandList> DX12Device::m_commandList;
	ComPtr<ID3D12Fence> DX12Device::m_fence;
	uint64_t DX12Device::m_fenceValues[DX12Device::FrameCount] = {};
	HANDLE DX12Device::m_fenceEvent = nullptr;
	uint32_t DX12Device::m_currentFrameIndex = 0;
	ComPtr<ID3D12DescriptorHeap> DX12Device::m_rtvHeap;
	std::array<ComPtr<ID3D12Resource>, DX12Device::FrameCount> DX12Device::m_renderTargets;
	D3D12_CPU_DESCRIPTOR_HANDLE DX12Device::m_rtvHandles[DX12Device::FrameCount] = {};
	ComPtr<ID3D12DescriptorHeap> DX12Device::m_dsvHeap;
	ComPtr<ID3D12Resource> DX12Device::m_depthStencil;
	D3D12_CPU_DESCRIPTOR_HANDLE DX12Device::m_dsvHandle = {};
	D3D12_VIEWPORT DX12Device::m_viewport = {};
	D3D12_RECT DX12Device::m_scissorRect = {};
	HWND DX12Device::m_windowHandle = nullptr;
	uint32_t DX12Device::m_width = 0;
	uint32_t DX12Device::m_height = 0;

	// ========================================================================
	// Initialize
	// ========================================================================
	bool DX12Device::Initialize(HWND p_windowHandle, uint32_t p_width, uint32_t p_height, bool p_useDebugLayer)
	{
		m_windowHandle = p_windowHandle;
		m_width = p_width;
		m_height = p_height;

		// Step 1: Create Device
		if (!CreateDevice(p_useDebugLayer))
		{
			OVLOG_ERROR("Failed to create DX12 device");
			return false;
		}
		OVLOG_INFO("DX12 Device created successfully");

		// Step 2: Create Command Queue
		if (!CreateCommandQueue())
		{
			OVLOG_ERROR("Failed to create command queue");
			return false;
		}
		OVLOG_INFO("Command queue created");

		// Step 3: Create Swap Chain
		if (!CreateSwapChain(p_windowHandle, p_width, p_height))
		{
			OVLOG_ERROR("Failed to create swap chain");
			return false;
		}
		OVLOG_INFO("Swap chain created");

		// Step 4: Create Render Target Views
		if (!CreateRenderTargetViews(p_width, p_height))
		{
			OVLOG_ERROR("Failed to create render target views");
			return false;
		}
		OVLOG_INFO("Render target views created");

		// Step 5: Create Depth Stencil View
		if (!CreateDepthStencilView(p_width, p_height))
		{
			OVLOG_ERROR("Failed to create depth stencil view");
			return false;
		}
		OVLOG_INFO("Depth stencil view created");

		// Step 6: Create Fence for synchronization
		if (!CreateFence())
		{
			OVLOG_ERROR("Failed to create fence");
			return false;
		}
		OVLOG_INFO("Fence created");

		// Step 7: Create Command List
		HRESULT hr = m_device->CreateCommandList(
			0,
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			m_commandAllocators[0].Get(),
			nullptr,
			IID_PPV_ARGS(&m_commandList)
		);
		if (FAILED(hr))
		{
			OVLOG_ERROR("Failed to create command list");
			return false;
		}
		m_commandList->Close();
		OVLOG_INFO("Command list created");

		// Setup viewport and scissor
		m_viewport = { 0.0f, 0.0f, static_cast<float>(p_width), static_cast<float>(p_height), 0.0f, 1.0f };
		m_scissorRect = { 0, 0, static_cast<LONG>(p_width), static_cast<LONG>(p_height) };

		OVLOG_INFO("DX12 Device initialization completed successfully");
		return true;
	}

	// ========================================================================
	// Shutdown
	// ========================================================================
	void DX12Device::Shutdown()
	{
		// Wait for GPU to finish
		if (m_fence && m_commandQueue)
		{
			const uint64_t fenceValue = m_fenceValues[m_currentFrameIndex];
			if (m_fence->GetCompletedValue() < fenceValue)
			{
				m_fence->SetEventOnCompletion(fenceValue, m_fenceEvent);
				WaitForSingleObject(m_fenceEvent, INFINITE);
			}
		}

		// Close fence event
		if (m_fenceEvent)
		{
			CloseHandle(m_fenceEvent);
			m_fenceEvent = nullptr;
		}

		// Release resources in correct order
		m_renderTargets.fill(nullptr);
		m_rtvHeap.Reset();
		m_dsvHeap.Reset();
		m_depthStencil.Reset();
		m_swapChain.Reset();
		m_commandList.Reset();
		m_commandAllocators.fill(nullptr);
		m_commandQueue.Reset();
		m_device.Reset();
		m_fence.Reset();
	}

	// ========================================================================
	// CreateDevice
	// ========================================================================
	bool DX12Device::CreateDevice(bool p_useDebugLayer)
	{
		UINT dxgiFactoryFlags = 0;

		// Enable debug layer if requested
		if (p_useDebugLayer)
		{
			ComPtr<ID3D12Debug> debugController;
			if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
			{
				debugController->EnableDebugLayer();
				dxgiFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
				OVLOG_INFO("DX12 Debug layer enabled");
			}
			else
			{
				OVLOG_WARNING("Failed to enable DX12 debug layer");
			}
		}

		// Create DXGI Factory
		ComPtr<IDXGIFactory4> dxgiFactory;
		HRESULT hr = CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&dxgiFactory));
		if (FAILED(hr))
		{
			OVLOG_ERROR("Failed to create DXGI factory");
			return false;
		}

		// Find adapter (use EnumAdapters1 for compatibility)
		ComPtr<IDXGIAdapter1> adapter;
		HRESULT adapterResult = dxgiFactory->EnumAdapters1(0, &adapter);

		if (FAILED(adapterResult))
		{
			OVLOG_ERROR("No suitable adapter found");
			return false;
		}

		// Get adapter description
		DXGI_ADAPTER_DESC1 adapterDesc;
		adapter->GetDesc1(&adapterDesc);

		// Convert WCHAR to char using WideCharToMultiByte
		int size = WideCharToMultiByte(CP_UTF8, 0, adapterDesc.Description, -1, nullptr, 0, nullptr, nullptr);
		std::string adapterName(size - 1, 0);
		WideCharToMultiByte(CP_UTF8, 0, adapterDesc.Description, -1, &adapterName[0], size, nullptr, nullptr);

		OVLOG_INFO("Using adapter: " + adapterName);

		// Create D3D12 device
		hr = D3D12CreateDevice(
			adapter.Get(),
			D3D_FEATURE_LEVEL_12_0,
			IID_PPV_ARGS(&m_device)
		);

		if (FAILED(hr))
		{
			OVLOG_ERROR("Failed to create D3D12 device");
			return false;
		}

		// Enable debug object tracking
		if (p_useDebugLayer)
		{
			ComPtr<ID3D12InfoQueue> infoQueue;
			if (SUCCEEDED(m_device->QueryInterface(IID_PPV_ARGS(&infoQueue))))
			{
				infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
				infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);

				// Suppress specific messages if needed
				D3D12_MESSAGE_ID hide[] =
				{
					D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,
					D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE
				};
				D3D12_INFO_QUEUE_FILTER filter = {};
				filter.DenyList.NumIDs = _countof(hide);
				filter.DenyList.pIDList = hide;
				infoQueue->PushStorageFilter(&filter);
			}
		}

		return true;
	}

	// ========================================================================
	// CreateCommandQueue
	// ========================================================================
	bool DX12Device::CreateCommandQueue()
	{
		D3D12_COMMAND_QUEUE_DESC queueDesc = {};
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		queueDesc.NodeMask = 0;

		HRESULT hr = m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue));
		if (FAILED(hr))
		{
			OVLOG_ERROR("Failed to create command queue");
			return false;
		}

		return true;
	}

	// ========================================================================
	// CreateSwapChain
	// ========================================================================
	bool DX12Device::CreateSwapChain(HWND p_windowHandle, uint32_t p_width, uint32_t p_height)
	{
		// Get DXGI device
		ComPtr<IDXGIDevice> dxgiDevice;
		HRESULT hr = m_device->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
		if (FAILED(hr))
		{
			OVLOG_ERROR("Failed to get DXGI device");
			return false;
		}

		// Get DXGI adapter
		ComPtr<IDXGIAdapter> dxgiAdapter;
		hr = dxgiDevice->GetAdapter(&dxgiAdapter);
		if (FAILED(hr))
		{
			OVLOG_ERROR("Failed to get DXGI adapter");
			return false;
		}

		// Get DXGI factory
		ComPtr<IDXGIFactory4> dxgiFactory;
		hr = dxgiAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory));
		if (FAILED(hr))
		{
			OVLOG_ERROR("Failed to get DXGI factory");
			return false;
		}

		// Create swap chain
		DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
		swapChainDesc.Width = p_width;
		swapChainDesc.Height = p_height;
		swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		swapChainDesc.Stereo = false;
		swapChainDesc.SampleDesc = { 1, 0 };
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount = FrameCount;
		swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
		swapChainDesc.Flags = 0;

		ComPtr<IDXGISwapChain1> tempSwapChain;
		hr = dxgiFactory->CreateSwapChainForHwnd(
			m_commandQueue.Get(),
			p_windowHandle,
			&swapChainDesc,
			nullptr,
			nullptr,
			&tempSwapChain
		);

		if (FAILED(hr))
		{
			OVLOG_ERROR("Failed to create swap chain");
			return false;
		}

		// Disable Alt+Enter handling
		dxgiFactory->MakeWindowAssociation(p_windowHandle, DXGI_MWA_NO_ALT_ENTER);

		// Query for IDXGISwapChain3
		hr = tempSwapChain.As(&m_swapChain);
		if (FAILED(hr))
		{
			OVLOG_ERROR("Failed to get IDXGISwapChain3");
			return false;
		}

		m_currentFrameIndex = m_swapChain->GetCurrentBackBufferIndex();

		return true;
	}

	// ========================================================================
	// CreateRenderTargetViews
	// ========================================================================
	bool DX12Device::CreateRenderTargetViews(uint32_t p_width, uint32_t p_height)
	{
		// Create RTV descriptor heap
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.NumDescriptors = FrameCount;
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		rtvHeapDesc.NodeMask = 0;

		HRESULT hr = m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap));
		if (FAILED(hr))
		{
			OVLOG_ERROR("Failed to create RTV descriptor heap");
			return false;
		}

		// Get RTV descriptor size
		const UINT rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		// Create RTVs for each back buffer
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
		for (uint32_t i = 0; i < FrameCount; ++i)
		{
			hr = m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]));
			if (FAILED(hr))
			{
				OVLOG_ERROR("Failed to get back buffer " + std::to_string(i));
				return false;
			}

			m_device->CreateRenderTargetView(
				m_renderTargets[i].Get(),
				nullptr,
				rtvHandle
			);

			m_rtvHandles[i] = rtvHandle;
			rtvHandle.ptr += rtvDescriptorSize;
		}

		return true;
	}

	// ========================================================================
	// CreateDepthStencilView
	// ========================================================================
	bool DX12Device::CreateDepthStencilView(uint32_t p_width, uint32_t p_height)
	{
		// Create DSV descriptor heap
		D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
		dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		dsvHeapDesc.NumDescriptors = 1;
		dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		dsvHeapDesc.NodeMask = 0;

		HRESULT hr = m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap));
		if (FAILED(hr))
		{
			OVLOG_ERROR("Failed to create DSV descriptor heap");
			return false;
		}

		// Create depth stencil resource
		D3D12_RESOURCE_DESC depthDesc = {};
		depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		depthDesc.Alignment = 0;
		depthDesc.Width = p_width;
		depthDesc.Height = p_height;
		depthDesc.DepthOrArraySize = 1;
		depthDesc.MipLevels = 1;
		depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		depthDesc.SampleDesc = { 1, 0 };
		depthDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
		depthOptimizedClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
		depthOptimizedClearValue.DepthStencil.Stencil = 0;

		CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);

		hr = m_device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&depthDesc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&depthOptimizedClearValue,
			IID_PPV_ARGS(&m_depthStencil)
		);

		if (FAILED(hr))
		{
			OVLOG_ERROR("Failed to create depth stencil resource");
			return false;
		}

		// Create DSV
		m_device->CreateDepthStencilView(
			m_depthStencil.Get(),
			nullptr,
			m_dsvHeap->GetCPUDescriptorHandleForHeapStart()
		);

		m_dsvHandle = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();

		return true;
	}

	// ========================================================================
	// CreateFence
	// ========================================================================
	bool DX12Device::CreateFence()
	{
		HRESULT hr = m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
		if (FAILED(hr))
		{
			OVLOG_ERROR("Failed to create fence");
			return false;
		}

		m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (!m_fenceEvent)
		{
			OVLOG_ERROR("Failed to create fence event");
			return false;
		}

		// Initialize fence values
		for (uint32_t i = 0; i < FrameCount; ++i)
		{
			m_fenceValues[i] = 0;
		}

		return true;
	}

	// ========================================================================
	// GetCommandAllocator
	// ========================================================================
	ID3D12CommandAllocator* DX12Device::GetCommandAllocator()
	{
		return m_commandAllocators[m_currentFrameIndex].Get();
	}

	// ========================================================================
	// GetCurrentBackBuffer
	// ========================================================================
	ID3D12Resource* DX12Device::GetCurrentBackBuffer()
	{
		return m_renderTargets[m_currentFrameIndex].Get();
	}

	// ========================================================================
	// MoveToNextFrame
	// ========================================================================
	void DX12Device::MoveToNextFrame()
	{
		const uint32_t nextFrameIndex = (m_currentFrameIndex + 1) % FrameCount;

		// Signal fence
		m_commandQueue->Signal(m_fence.Get(), m_fenceValues[m_currentFrameIndex]);

		// Wait if GPU hasn't finished with next frame
		if (m_fence->GetCompletedValue() < m_fenceValues[nextFrameIndex])
		{
			m_fence->SetEventOnCompletion(m_fenceValues[nextFrameIndex], m_fenceEvent);
			WaitForSingleObject(m_fenceEvent, INFINITE);
		}

		m_fenceValues[nextFrameIndex] = ++m_fenceValues[m_currentFrameIndex];
		m_currentFrameIndex = nextFrameIndex;
	}

	// ========================================================================
	// Present
	// ========================================================================
	void DX12Device::Present()
	{
		m_swapChain->Present(1, 0);
	}

	// ========================================================================
	// ResizeBuffers
	// ========================================================================
	void DX12Device::ResizeBuffers(uint32_t p_width, uint32_t p_height)
	{
		// Release resources
		m_renderTargets.fill(nullptr);

		// Resize swap chain
		HRESULT hr = m_swapChain->ResizeBuffers(
			FrameCount,
			p_width,
			p_height,
			DXGI_FORMAT_UNKNOWN,
			0
		);

		if (FAILED(hr))
		{
			OVLOG_ERROR("Failed to resize swap chain");
			return;
		}

		m_currentFrameIndex = m_swapChain->GetCurrentBackBufferIndex();

		// Recreate RTVs
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
		const UINT rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		for (uint32_t i = 0; i < FrameCount; ++i)
		{
			hr = m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]));
			if (SUCCEEDED(hr))
			{
				m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, rtvHandle);
				m_rtvHandles[i] = rtvHandle;
				rtvHandle.ptr += rtvDescriptorSize;
			}
		}

		// Recreate depth stencil
		CreateDepthStencilView(p_width, p_height);

		// Update viewport
		m_viewport = { 0.0f, 0.0f, static_cast<float>(p_width), static_cast<float>(p_height), 0.0f, 1.0f };
		m_scissorRect = { 0, 0, static_cast<LONG>(p_width), static_cast<LONG>(p_height) };
	}

	// ========================================================================
	// GetDSVHandle
	// ========================================================================
	D3D12_CPU_DESCRIPTOR_HANDLE DX12Device::GetDSVHandle()
	{
		return m_dsvHandle;
	}
}
