/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <OvRendering/HAL/DirectX12/DX12Buffer.h>
#include <OvRendering/HAL/DirectX12/DX12Device.h>

#include <OvDebug/Logger.h>

namespace OvRendering::HAL::DX12
{
	// ========================================================================
	// DX12Buffer Constructor
	// ========================================================================
	DX12Buffer::DX12Buffer()
	{
	}

	// ========================================================================
	// DX12Buffer Destructor
	// ========================================================================
	DX12Buffer::~DX12Buffer()
	{
		Shutdown();
	}

	// ========================================================================
	// Initialize
	// ========================================================================
	bool DX12Buffer::Initialize(
		uint32_t p_size,
		Settings::EAccessSpecifier p_access,
		const void* p_initialData,
		D3D12_RESOURCE_FLAGS p_resourceFlags
	)
	{
		m_size = p_size;

		// Determine if buffer is dynamic
		m_isDynamic = (p_access == Settings::EAccessSpecifier::DYNAMIC_DRAW ||
		               p_access == Settings::EAccessSpecifier::DYNAMIC_READ ||
		               p_access == Settings::EAccessSpecifier::DYNAMIC_COPY ||
		               p_access == Settings::EAccessSpecifier::STREAM_COPY);

		// Create buffer resource
		if (!CreateBufferResource(p_resourceFlags))
		{
			OVLOG_ERROR("Failed to create buffer resource");
			return false;
		}

		// Set initial resource state
		m_resourceState = GetResourceStateFromAccess(p_access);

		// Upload initial data if provided
		if (p_initialData)
		{
			if (!UploadData(p_initialData, p_size))
			{
				OVLOG_ERROR("Failed to upload initial data");
				return false;
			}
		}

		return true;
	}

	// ========================================================================
	// Shutdown
	// ========================================================================
	void DX12Buffer::Shutdown()
	{
		if (m_mappedData)
		{
			Unmap();
		}

		m_buffer.Reset();
		m_uploadBuffer.Reset();
	}

	// ========================================================================
	// GetGPUAddress
	// ========================================================================
	D3D12_GPU_VIRTUAL_ADDRESS DX12Buffer::GetGPUAddress() const
	{
		return m_buffer ? m_buffer->GetGPUVirtualAddress() : 0;
	}

	// ========================================================================
	// Map
	// ========================================================================
	bool DX12Buffer::Map(void** p_data)
	{
		if (!m_buffer)
		{
			OVLOG_ERROR("Cannot map null buffer");
			return false;
		}

		if (m_mappedData)
		{
			OVLOG_WARNING("Buffer already mapped");
			if (p_data)
			{
				*p_data = m_mappedData;
			}
			return true;
		}

		CD3DX12_RANGE readRange(0, 0);
		HRESULT hr = m_buffer->Map(0, &readRange, &m_mappedData);
		if (FAILED(hr))
		{
			OVLOG_ERROR("Failed to map buffer");
			return false;
		}

		if (p_data)
		{
			*p_data = m_mappedData;
		}

		return true;
	}

	// ========================================================================
	// Unmap
	// ========================================================================
	void DX12Buffer::Unmap()
	{
		if (m_mappedData && m_buffer)
		{
			m_buffer->Unmap(0, nullptr);
			m_mappedData = nullptr;
		}
	}

	// ========================================================================
	// Update
	// ========================================================================
	void DX12Buffer::Update(const void* p_data, uint32_t p_size, uint32_t p_offset)
	{
		if (!p_data || !m_buffer)
		{
			return;
		}

		if (p_size == 0)
		{
			p_size = m_size;
		}

		if (m_isDynamic)
		{
			// For dynamic buffers, map and copy directly
			void* mappedData = nullptr;
			if (Map(&mappedData))
			{
				memcpy(static_cast<uint8_t*>(mappedData) + p_offset, p_data, p_size);
				// Keep mapped for dynamic buffers
			}
		}
		else
		{
			// For static buffers, use upload heap
			UploadData(p_data, p_size);
		}
	}

	// ========================================================================
	// SetResourceState
	// ========================================================================
	void DX12Buffer::SetResourceState(ID3D12GraphicsCommandList* p_commandList, D3D12_RESOURCE_STATES p_state)
	{
		if (p_commandList && m_resourceState != p_state)
		{
			DX12ResourceBarrier::Transition(p_commandList, m_buffer.Get(), m_resourceState, p_state);
			m_resourceState = p_state;
		}
	}

	// ========================================================================
	// CreateBufferResource
	// ========================================================================
	bool DX12Buffer::CreateBufferResource(D3D12_RESOURCE_FLAGS p_resourceFlags)
	{
		D3D12_HEAP_PROPERTIES heapProps = {};
		heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
		heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heapProps.CreationNodeMask = 1;
		heapProps.VisibleNodeMask = 1;

		D3D12_RESOURCE_DESC resourceDesc = {};
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		resourceDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
		resourceDesc.Width = m_size;
		resourceDesc.Height = 1;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels = 1;
		resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
		resourceDesc.SampleDesc = { 1, 0 };
		resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		resourceDesc.Flags = p_resourceFlags;

		HRESULT hr = DX12Device::GetDevice()->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			m_resourceState,
			nullptr,
			IID_PPV_ARGS(&m_buffer)
		);

		if (FAILED(hr))
		{
			OVLOG_ERROR("Failed to create buffer resource");
			return false;
		}

		return true;
	}

	// ========================================================================
	// UploadData
	// ========================================================================
	bool DX12Buffer::UploadData(const void* p_data, uint32_t p_size)
	{
		if (!p_data || !m_buffer)
		{
			return false;
		}

		// Create upload heap
		CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);

		D3D12_RESOURCE_DESC uploadResourceDesc = {};
		uploadResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		uploadResourceDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
		uploadResourceDesc.Width = p_size;
		uploadResourceDesc.Height = 1;
		uploadResourceDesc.DepthOrArraySize = 1;
		uploadResourceDesc.MipLevels = 1;
		uploadResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
		uploadResourceDesc.SampleDesc = { 1, 0 };
		uploadResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		uploadResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

		HRESULT hr = DX12Device::GetDevice()->CreateCommittedResource(
			&uploadHeapProps,
			D3D12_HEAP_FLAG_NONE,
			&uploadResourceDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_uploadBuffer)
		);

		if (FAILED(hr))
		{
			OVLOG_ERROR("Failed to create upload buffer");
			return false;
		}

		// Map and copy data
		void* mappedData = nullptr;
		CD3DX12_RANGE readRange(0, 0);
		hr = m_uploadBuffer->Map(0, &readRange, &mappedData);
		if (FAILED(hr))
		{
			OVLOG_ERROR("Failed to map upload buffer");
			return false;
		}

		memcpy(mappedData, p_data, p_size);
		m_uploadBuffer->Unmap(0, nullptr);

		// Copy from upload buffer to default buffer
		ID3D12GraphicsCommandList* commandList = DX12Device::GetCommandList();
		ID3D12CommandAllocator* commandAllocator = DX12Device::GetCommandAllocator();

		commandAllocator->Reset();
		commandList->Reset(commandAllocator, nullptr);

		// Transition to copy destination
		D3D12_RESOURCE_BARRIER copyBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
			m_buffer.Get(),
			m_resourceState,
			D3D12_RESOURCE_STATE_COPY_DEST
		);
		commandList->ResourceBarrier(1, &copyBarrier);

		// Copy
		commandList->CopyResource(m_buffer.Get(), m_uploadBuffer.Get());

		// Transition back
		D3D12_RESOURCE_BARRIER restoreBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
			m_buffer.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			m_resourceState
		);
		commandList->ResourceBarrier(1, &restoreBarrier);

		commandList->Close();

		// Execute and wait
		ID3D12CommandQueue* commandQueue = DX12Device::GetCommandQueue();
		commandQueue->ExecuteCommandLists(1, reinterpret_cast<ID3D12CommandList* const*>(&commandList));

		// For simplicity, wait immediately (should use fence in production)
		HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		ComPtr<ID3D12Fence> fence;
		DX12Device::GetDevice()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
		commandQueue->Signal(fence.Get(), 1);
		fence->SetEventOnCompletion(1, fenceEvent);
		WaitForSingleObject(fenceEvent, INFINITE);
		CloseHandle(fenceEvent);

		// Release upload buffer after copy
		m_uploadBuffer.Reset();

		return true;
	}

	// ========================================================================
	// GetHeapTypeFromAccess
	// ========================================================================
	D3D12_HEAP_TYPE DX12Buffer::GetHeapTypeFromAccess(Settings::EAccessSpecifier p_access)
	{
		switch (p_access)
		{
		case Settings::EAccessSpecifier::DYNAMIC_DRAW:
		case Settings::EAccessSpecifier::DYNAMIC_READ:
		case Settings::EAccessSpecifier::DYNAMIC_COPY:
			return D3D12_HEAP_TYPE_UPLOAD;

		default:
			return D3D12_HEAP_TYPE_DEFAULT;
		}
	}

	// ========================================================================
	// GetResourceStateFromAccess
	// ========================================================================
	D3D12_RESOURCE_STATES DX12Buffer::GetResourceStateFromAccess(Settings::EAccessSpecifier p_access)
	{
		switch (p_access)
		{
		case Settings::EAccessSpecifier::STATIC_DRAW:
			return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;

		case Settings::EAccessSpecifier::DYNAMIC_DRAW:
			return D3D12_RESOURCE_STATE_GENERIC_READ;

		default:
			return D3D12_RESOURCE_STATE_COMMON;
		}
	}

	// ========================================================================
	// DX12VertexBuffer::GetView
	// ========================================================================
	D3D12_VERTEX_BUFFER_VIEW DX12VertexBuffer::GetView() const
	{
		D3D12_VERTEX_BUFFER_VIEW view = {};
		view.BufferLocation = GetGPUAddress();
		view.SizeInBytes = GetSize();
		view.StrideInBytes = m_stride;
		return view;
	}

	// ========================================================================
	// DX12IndexBuffer::GetView
	// ========================================================================
	D3D12_INDEX_BUFFER_VIEW DX12IndexBuffer::GetView() const
	{
		D3D12_INDEX_BUFFER_VIEW view = {};
		view.BufferLocation = GetGPUAddress();
		view.SizeInBytes = GetSize();
		view.Format = m_format;
		return view;
	}
}
