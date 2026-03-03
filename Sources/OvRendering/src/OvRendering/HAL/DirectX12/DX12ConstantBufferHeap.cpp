/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <OvRendering/HAL/DirectX12/DX12ConstantBufferHeap.h>
#include <OvRendering/HAL/DirectX12/DX12Device.h>

#include <OvDebug/Logger.h>

namespace OvRendering::HAL::DX12
{
	// ========================================================================
	// Initialize
	// ========================================================================
	bool DX12ConstantBufferHeap::Initialize(uint32_t p_numDescriptors)
	{
		m_numDescriptors = p_numDescriptors;

		// Create CBV descriptor heap
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		heapDesc.NumDescriptors = p_numDescriptors;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		heapDesc.NodeMask = 0;

		HRESULT hr = DX12Device::GetDevice()->CreateDescriptorHeap(
			&heapDesc,
			IID_PPV_ARGS(&m_heap)
		);

		if (FAILED(hr))
		{
			OVLOG_ERROR("Failed to create CBV descriptor heap");
			return false;
		}

		// Get handles and descriptor size
		m_cpuHandleStart = m_heap->GetCPUDescriptorHandleForHeapStart();
		m_gpuHandleStart = m_heap->GetGPUDescriptorHandleForHeapStart();
		m_descriptorSize = DX12Device::GetDevice()->GetDescriptorHandleIncrementSize(
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
		);

		// Initialize allocation tracking
		m_allocated.resize(p_numDescriptors, false);

		OVLOG_INFO("CBV Heap initialized: " + std::to_string(p_numDescriptors) +
		           " descriptors, " + std::to_string(m_descriptorSize) + " bytes each");

		return true;
	}

	// ========================================================================
	// Shutdown
	// ========================================================================
	void DX12ConstantBufferHeap::Shutdown()
	{
		m_heap.Reset();
		m_allocated.clear();
	}

	// ========================================================================
	// GetGPUDescriptorHeapStart
	// ========================================================================
	D3D12_GPU_DESCRIPTOR_HANDLE DX12ConstantBufferHeap::GetGPUDescriptorHeapStart() const
	{
		return m_gpuHandleStart;
	}

	// ========================================================================
	// GetCPUDescriptorHeapStart
	// ========================================================================
	D3D12_CPU_DESCRIPTOR_HANDLE DX12ConstantBufferHeap::GetCPUDescriptorHeapStart() const
	{
		return m_cpuHandleStart;
	}

	// ========================================================================
	// AllocateDescriptor
	// ========================================================================
	int32_t DX12ConstantBufferHeap::AllocateDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE& p_outHandle)
	{
		for (uint32_t i = 0; i < m_numDescriptors; ++i)
		{
			if (!m_allocated[i])
			{
				m_allocated[i] = true;
				p_outHandle.ptr = m_cpuHandleStart.ptr + (i * m_descriptorSize);
				return static_cast<int32_t>(i);
			}
		}

		OVLOG_WARNING("CBV Heap out of descriptors!");
		return -1;
	}

	// ========================================================================
	// CreateCBV
	// ========================================================================
	void DX12ConstantBufferHeap::CreateCBV(
		ID3D12Resource* p_buffer,
		uint32_t p_bufferSize,
		uint32_t p_heapIndex
	)
	{
		if (p_heapIndex >= m_numDescriptors)
		{
			OVLOG_ERROR("Invalid CBV heap index: " + std::to_string(p_heapIndex));
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

		// Calculate handle for this descriptor
		D3D12_CPU_DESCRIPTOR_HANDLE handle;
		handle.ptr = m_cpuHandleStart.ptr + (p_heapIndex * m_descriptorSize);

		// Create the CBV
		DX12Device::GetDevice()->CreateConstantBufferView(&cbvDesc, handle);
	}

	// ========================================================================
	// FreeDescriptor
	// ========================================================================
	void DX12ConstantBufferHeap::FreeDescriptor(uint32_t p_index)
	{
		if (p_index < m_numDescriptors)
		{
			m_allocated[p_index] = false;
		}
	}
}
