/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <OvRendering/HAL/DirectX12/DX12DescriptorHeap.h>

#include <OvDebug/Logger.h>

namespace OvRendering::HAL::DX12
{
	// ========================================================================
	// DX12DescriptorHeap Constructor
	// ========================================================================
	DX12DescriptorHeap::DX12DescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE p_type)
		: m_type(p_type)
	{
	}

	// ========================================================================
	// DX12DescriptorHeap Destructor
	// ========================================================================
	DX12DescriptorHeap::~DX12DescriptorHeap()
	{
		Shutdown();
	}

	// ========================================================================
	// Initialize
	// ========================================================================
	bool DX12DescriptorHeap::Initialize(uint32_t p_numDescriptors, D3D12_DESCRIPTOR_HEAP_FLAGS p_flags)
	{
		m_numDescriptors = p_numDescriptors;

		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
		heapDesc.Type = m_type;
		heapDesc.NumDescriptors = p_numDescriptors;
		heapDesc.Flags = p_flags;
		heapDesc.NodeMask = 0;

		HRESULT hr = DX12Device::GetDevice()->CreateDescriptorHeap(
			&heapDesc,
			IID_PPV_ARGS(&m_heap)
		);

		if (FAILED(hr))
		{
			OVLOG_ERROR("Failed to create descriptor heap");
			return false;
		}

		// Get handles and descriptor size
		m_cpuHandleStart = m_heap->GetCPUDescriptorHandleForHeapStart();
		m_descriptorSize = DX12Device::GetDevice()->GetDescriptorHandleIncrementSize(m_type);

		// Check if shader visible and get GPU handle
		m_shaderVisible = (p_flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) != 0;
		if (m_shaderVisible)
		{
			m_gpuHandleStart = m_heap->GetGPUDescriptorHandleForHeapStart();
		}

		// Initialize allocation tracking
		m_allocated.resize(p_numDescriptors, false);

		OVLOG_INFO("Descriptor heap initialized: Type=" + std::to_string(m_type) +
		           ", Descriptors=" + std::to_string(p_numDescriptors) +
		           ", Size=" + std::to_string(m_descriptorSize) + " bytes" +
		           (m_shaderVisible ? " (Shader Visible)" : ""));

		return true;
	}

	// ========================================================================
	// Shutdown
	// ========================================================================
	void DX12DescriptorHeap::Shutdown()
	{
		m_heap.Reset();
		m_allocated.clear();
	}

	// ========================================================================
	// AllocateDescriptor
	// ========================================================================
	int32_t DX12DescriptorHeap::AllocateDescriptor()
	{
		for (uint32_t i = 0; i < m_numDescriptors; ++i)
		{
			if (!m_allocated[i])
			{
				m_allocated[i] = true;
				return static_cast<int32_t>(i);
			}
		}

		OVLOG_WARNING("Descriptor heap out of descriptors!");
		return -1;
	}

	// ========================================================================
	// FreeDescriptor
	// ========================================================================
	void DX12DescriptorHeap::FreeDescriptor(uint32_t p_index)
	{
		if (p_index < m_numDescriptors)
		{
			m_allocated[p_index] = false;
		}
	}

	// ========================================================================
	// GetCPUDescriptor
	// ========================================================================
	D3D12_CPU_DESCRIPTOR_HANDLE DX12DescriptorHeap::GetCPUDescriptor(uint32_t p_index) const
	{
		D3D12_CPU_DESCRIPTOR_HANDLE handle = m_cpuHandleStart;
		handle.ptr += p_index * m_descriptorSize;
		return handle;
	}

	// ========================================================================
	// GetGPUDescriptor
	// ========================================================================
	D3D12_GPU_DESCRIPTOR_HANDLE DX12DescriptorHeap::GetGPUDescriptor(uint32_t p_index) const
	{
		D3D12_GPU_DESCRIPTOR_HANDLE handle = m_gpuHandleStart;
		handle.ptr += p_index * m_descriptorSize;
		return handle;
	}

	// ========================================================================
	// DX12RTVHeap::CreateRTV
	// ========================================================================
	void DX12RTVHeap::CreateRTV(
		ID3D12Resource* p_resource,
		uint32_t p_index,
		const D3D12_RENDER_TARGET_VIEW_DESC* p_desc
	)
	{
		if (p_index >= GetNumDescriptors())
		{
			OVLOG_ERROR("Invalid RTV heap index: " + std::to_string(p_index));
			return;
		}

		if (!p_resource)
		{
			OVLOG_ERROR("Null resource passed to CreateRTV");
			return;
		}

		D3D12_CPU_DESCRIPTOR_HANDLE handle = GetCPUDescriptor(p_index);
		DX12Device::GetDevice()->CreateRenderTargetView(p_resource, p_desc, handle);
	}

	// ========================================================================
	// DX12DSVHeap::CreateDSV
	// ========================================================================
	void DX12DSVHeap::CreateDSV(
		ID3D12Resource* p_resource,
		uint32_t p_index,
		const D3D12_DEPTH_STENCIL_VIEW_DESC* p_desc
	)
	{
		if (p_index >= GetNumDescriptors())
		{
			OVLOG_ERROR("Invalid DSV heap index: " + std::to_string(p_index));
			return;
		}

		if (!p_resource)
		{
			OVLOG_ERROR("Null resource passed to CreateDSV");
			return;
		}

		D3D12_CPU_DESCRIPTOR_HANDLE handle = GetCPUDescriptor(p_index);
		DX12Device::GetDevice()->CreateDepthStencilView(p_resource, p_desc, handle);
	}

	// ========================================================================
	// DX12CBVSRVUAVHeap::CreateCBV
	// ========================================================================
	void DX12CBVSRVUAVHeap::CreateCBV(
		ID3D12Resource* p_buffer,
		uint32_t p_bufferSize,
		uint32_t p_index
	)
	{
		if (p_index >= GetNumDescriptors())
		{
			OVLOG_ERROR("Invalid CBV heap index: " + std::to_string(p_index));
			return;
		}

		if (!p_buffer)
		{
			OVLOG_ERROR("Null buffer passed to CreateCBV");
			return;
		}

		// Create CBV description
		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
		cbvDesc.BufferLocation = p_buffer->GetGPUVirtualAddress();
		cbvDesc.SizeInBytes = (p_bufferSize + 255) & ~255; // Align to 256 bytes

		D3D12_CPU_DESCRIPTOR_HANDLE handle = GetCPUDescriptor(p_index);
		DX12Device::GetDevice()->CreateConstantBufferView(&cbvDesc, handle);
	}

	// ========================================================================
	// DX12CBVSRVUAVHeap::CreateSRV
	// ========================================================================
	void DX12CBVSRVUAVHeap::CreateSRV(
		ID3D12Resource* p_texture,
		uint32_t p_index,
		const D3D12_SHADER_RESOURCE_VIEW_DESC* p_desc
	)
	{
		if (p_index >= GetNumDescriptors())
		{
			OVLOG_ERROR("Invalid SRV heap index: " + std::to_string(p_index));
			return;
		}

		if (!p_texture)
		{
			OVLOG_ERROR("Null texture passed to CreateSRV");
			return;
		}

		D3D12_CPU_DESCRIPTOR_HANDLE handle = GetCPUDescriptor(p_index);
		DX12Device::GetDevice()->CreateShaderResourceView(p_texture, p_desc, handle);
	}

	// ========================================================================
	// DX12CBVSRVUAVHeap::CreateUAV
	// ========================================================================
	void DX12CBVSRVUAVHeap::CreateUAV(
		ID3D12Resource* p_texture,
		uint32_t p_index,
		const D3D12_UNORDERED_ACCESS_VIEW_DESC* p_desc
	)
	{
		if (p_index >= GetNumDescriptors())
		{
			OVLOG_ERROR("Invalid UAV heap index: " + std::to_string(p_index));
			return;
		}

		if (!p_texture)
		{
			OVLOG_ERROR("Null texture passed to CreateUAV");
			return;
		}

		D3D12_CPU_DESCRIPTOR_HANDLE handle = GetCPUDescriptor(p_index);
		DX12Device::GetDevice()->CreateUnorderedAccessView(p_texture, nullptr, p_desc, handle);
	}
}
