/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <OvRendering/HAL/DirectX12/DX12Prerequisites.h>
#include <OvRendering/HAL/DirectX12/DX12ResourceBarrier.h>
#include <OvRendering/Settings/EAccessSpecifier.h>

namespace OvRendering::HAL::DX12
{
	/**
	* DX12 Buffer resource base class
	*/
	class DX12Buffer
	{
	public:
		/**
		* Constructor
		*/
		DX12Buffer();

		/**
		* Destructor
		*/
		virtual ~DX12Buffer();

		/**
		* Initialize the buffer
		* @param p_size Size in bytes
		* @param p_access Access specifier (STREAM_DRAW, DYNAMIC_DRAW, etc.)
		* @param p_initialData Initial data (can be nullptr)
		* @param p_resourceFlags Resource flags
		* @return true if initialization succeeded
		*/
		virtual bool Initialize(
			uint32_t p_size,
			Settings::EAccessSpecifier p_access,
			const void* p_initialData = nullptr,
			D3D12_RESOURCE_FLAGS p_resourceFlags = D3D12_RESOURCE_FLAG_NONE
		);

		/**
		* Cleanup resources
		*/
		virtual void Shutdown();

		/**
		* Get the buffer resource
		*/
		ID3D12Resource* GetResource() const { return m_buffer.Get(); }

		/**
		* Get the GPU virtual address
		*/
		D3D12_GPU_VIRTUAL_ADDRESS GetGPUAddress() const;

		/**
		* Get the buffer size
		*/
		uint32_t GetSize() const { return m_size; }

		/**
		* Map the buffer to CPU memory
		* @param p_data Output pointer to CPU memory
		* @return true if succeeded
		*/
		bool Map(void** p_data);

		/**
		* Unmap the buffer from CPU memory
		*/
		void Unmap();

		/**
		* Update buffer data
		* @param p_data Data to upload
		* @param p_size Size of data (0 = full buffer)
		* @param p_offset Offset in bytes
		*/
		void Update(const void* p_data, uint32_t p_size = 0, uint32_t p_offset = 0);

		/**
		* Get the current resource state
		*/
		D3D12_RESOURCE_STATES GetResourceState() const { return m_resourceState; }

		/**
		* Set the resource state (use ResourceBarrier for transitions)
		*/
		void SetResourceState(ID3D12GraphicsCommandList* p_commandList, D3D12_RESOURCE_STATES p_state);

	protected:
		ComPtr<ID3D12Resource> m_buffer;
		ComPtr<ID3D12Resource> m_uploadBuffer;
		uint32_t m_size = 0;
		bool m_isDynamic = false;
		D3D12_RESOURCE_STATES m_resourceState = D3D12_RESOURCE_STATE_COMMON;
		void* m_mappedData = nullptr;

		/**
		* Create the buffer resource
		*/
		virtual bool CreateBufferResource(D3D12_RESOURCE_FLAGS p_resourceFlags);

		/**
		* Upload initial data
		*/
		virtual bool UploadData(const void* p_data, uint32_t p_size);

		/**
		* Get heap type from access specifier
		*/
		D3D12_HEAP_TYPE GetHeapTypeFromAccess(Settings::EAccessSpecifier p_access);

		/**
		* Get resource state from access specifier
		*/
		D3D12_RESOURCE_STATES GetResourceStateFromAccess(Settings::EAccessSpecifier p_access);
	};

	/**
	* DX12 Vertex Buffer specialization
	*/
	class DX12VertexBuffer : public DX12Buffer
	{
	public:
		/**
		* Get the vertex buffer view
		*/
		D3D12_VERTEX_BUFFER_VIEW GetView() const;

		/**
		* Get the stride in bytes
		*/
		uint32_t GetStride() const { return m_stride; }

		/**
		* Set the stride (call before Initialize)
		*/
		void SetStride(uint32_t p_stride) { m_stride = p_stride; }

	private:
		uint32_t m_stride = 0;
	};

	/**
	* DX12 Index Buffer specialization
	*/
	class DX12IndexBuffer : public DX12Buffer
	{
	public:
		/**
		* Get the index buffer view
		*/
		D3D12_INDEX_BUFFER_VIEW GetView() const;

		/**
		* Get the index format
		*/
		DXGI_FORMAT GetFormat() const { return m_format; }

		/**
		* Set the format (call before Initialize)
		*/
		void SetFormat(DXGI_FORMAT p_format) { m_format = p_format; }

	private:
		DXGI_FORMAT m_format = DXGI_FORMAT_R32_UINT;
	};
}
