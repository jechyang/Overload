/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <OvRendering/HAL/DirectX12/DX12Prerequisites.h>
#include <OvRendering/HAL/DirectX12/DX12Device.h>

namespace OvRendering::HAL::DX12
{
	/**
	* Generic Descriptor Heap manager for all descriptor types
	*/
	class DX12DescriptorHeap
	{
	public:
		/**
		* Constructor
		* @param p_type Type of descriptor heap
		*/
		explicit DX12DescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE p_type);

		/**
		* Destructor
		*/
		~DX12DescriptorHeap();

		/**
		* Initialize the descriptor heap
		* @param p_numDescriptors Number of descriptors to allocate
		* @param p_flags Heap flags
		* @return true if initialization succeeded
		*/
		bool Initialize(uint32_t p_numDescriptors, D3D12_DESCRIPTOR_HEAP_FLAGS p_flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE);

		/**
		* Cleanup resources
		*/
		void Shutdown();

		/**
		* Get the descriptor heap
		*/
		ID3D12DescriptorHeap* GetHeap() const { return m_heap.Get(); }

		/**
		* Get the CPU handle for heap start
		*/
		D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHeapStart() const { return m_cpuHandleStart; }

		/**
		* Get the GPU handle for heap start (only for shader-visible heaps)
		*/
		D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHeapStart() const { return m_gpuHandleStart; }

		/**
		* Get the descriptor size
		*/
		uint32_t GetDescriptorSize() const { return m_descriptorSize; }

		/**
		* Get the number of descriptors
		*/
		uint32_t GetNumDescriptors() const { return m_numDescriptors; }

		/**
		* Get the descriptor type
		*/
		D3D12_DESCRIPTOR_HEAP_TYPE GetType() const { return m_type; }

		/**
		* Allocate a descriptor index
		* @return Index of allocated descriptor, or -1 if failed
		*/
		int32_t AllocateDescriptor();

		/**
		* Free a descriptor
		* @param p_index Index to free
		*/
		void FreeDescriptor(uint32_t p_index);

		/**
		* Get CPU handle at index
		*/
		D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptor(uint32_t p_index) const;

		/**
		* Get GPU handle at index (only for shader-visible heaps)
		*/
		D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptor(uint32_t p_index) const;

		/**
		* Check if heap is shader visible
		*/
		bool IsShaderVisible() const { return m_shaderVisible; }

	private:
		ComPtr<ID3D12DescriptorHeap> m_heap;
		D3D12_CPU_DESCRIPTOR_HANDLE m_cpuHandleStart = {};
		D3D12_GPU_DESCRIPTOR_HANDLE m_gpuHandleStart = {};
		uint32_t m_descriptorSize = 0;
		uint32_t m_numDescriptors = 0;
		D3D12_DESCRIPTOR_HEAP_TYPE m_type;
		bool m_shaderVisible = false;

		// Simple freelist for descriptor allocation
		std::vector<bool> m_allocated;
	};

	/**
	* RTV Descriptor Heap specialization
	*/
	class DX12RTVHeap : public DX12DescriptorHeap
	{
	public:
		DX12RTVHeap() : DX12DescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV) {}

		/**
		* Create RTV for a resource
		*/
		void CreateRTV(ID3D12Resource* p_resource, uint32_t p_index,
		               const D3D12_RENDER_TARGET_VIEW_DESC* p_desc = nullptr);
	};

	/**
	* DSV Descriptor Heap specialization
	*/
	class DX12DSVHeap : public DX12DescriptorHeap
	{
	public:
		DX12DSVHeap() : DX12DescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV) {}

		/**
		* Create DSV for a resource
		*/
		void CreateDSV(ID3D12Resource* p_resource, uint32_t p_index,
		               const D3D12_DEPTH_STENCIL_VIEW_DESC* p_desc = nullptr);
	};

	/**
	* CBV/SRV/UAV Descriptor Heap specialization (shader visible)
	*/
	class DX12CBVSRVUAVHeap : public DX12DescriptorHeap
	{
	public:
		DX12CBVSRVUAVHeap() : DX12DescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) {}

		/**
		* Initialize shader-visible heap
		*/
		bool Initialize(uint32_t p_numDescriptors)
		{
			return DX12DescriptorHeap::Initialize(
				p_numDescriptors,
				D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
			);
		}

		/**
		* Create CBV
		*/
		void CreateCBV(ID3D12Resource* p_buffer, uint32_t p_bufferSize, uint32_t p_index);

		/**
		* Create SRV for texture
		*/
		void CreateSRV(ID3D12Resource* p_texture, uint32_t p_index,
		               const D3D12_SHADER_RESOURCE_VIEW_DESC* p_desc = nullptr);

		/**
		* Create UAV for texture
		*/
		void CreateUAV(ID3D12Resource* p_texture, uint32_t p_index,
		               const D3D12_UNORDERED_ACCESS_VIEW_DESC* p_desc = nullptr);
	};
}
