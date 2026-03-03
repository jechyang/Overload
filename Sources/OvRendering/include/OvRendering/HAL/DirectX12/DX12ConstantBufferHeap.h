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
	* Constant Buffer View Descriptor Heap for CBV management
	*/
	class DX12ConstantBufferHeap
	{
	public:
		/**
		* Initialize the CBV descriptor heap
		* @param p_numDescriptors Number of descriptors to allocate
		*/
		bool Initialize(uint32_t p_numDescriptors);

		/**
		* Cleanup resources
		*/
		void Shutdown();

		/**
		* Get the descriptor heap
		*/
		ID3D12DescriptorHeap* GetHeap() const { return m_heap.Get(); }

		/**
		* Get the GPU handle for the heap start
		*/
		D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHeapStart() const;

		/**
		* Get the CPU handle for the heap start
		*/
		D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHeapStart() const;

		/**
		* Get the descriptor size
		*/
		uint32_t GetDescriptorSize() const { return m_descriptorSize; }

		/**
		* Allocate a descriptor handle
		* @param p_outHandle Output handle
		* @return The index of the allocated descriptor, or -1 if failed
		*/
		int32_t AllocateDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE& p_outHandle);

		/**
		* Create a CBV for a buffer resource
		* @param p_buffer The buffer resource
		* @param p_bufferSize Size of the buffer
		* @param p_heapIndex Index in the heap (use AllocateDescriptor)
		*/
		void CreateCBV(ID3D12Resource* p_buffer, uint32_t p_bufferSize, uint32_t p_heapIndex);

		/**
		* Free a descriptor
		* @param p_index Index to free
		*/
		void FreeDescriptor(uint32_t p_index);

	private:
		ComPtr<ID3D12DescriptorHeap> m_heap;
		D3D12_CPU_DESCRIPTOR_HANDLE m_cpuHandleStart;
		D3D12_GPU_DESCRIPTOR_HANDLE m_gpuHandleStart;
		uint32_t m_descriptorSize = 0;
		uint32_t m_numDescriptors = 0;

		// Simple freelist for descriptor allocation
		std::vector<bool> m_allocated;
	};
}
