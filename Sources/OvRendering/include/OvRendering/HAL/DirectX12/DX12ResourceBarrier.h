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
	* Helper class for managing DX12 resource barriers
	*/
	class DX12ResourceBarrier
	{
	public:
		/**
		* Transition a resource from one state to another
		*/
		static void Transition(
			ID3D12GraphicsCommandList* p_commandList,
			ID3D12Resource* p_resource,
			D3D12_RESOURCE_STATES p_stateBefore,
			D3D12_RESOURCE_STATES p_stateAfter,
			D3D12_RESOURCE_BARRIER_FLAGS p_flags = D3D12_RESOURCE_BARRIER_FLAG_NONE
		);

		/**
		* Create a UAV barrier
		*/
		static void UAVBarrier(
			ID3D12GraphicsCommandList* p_commandList,
			ID3D12Resource* p_resource = nullptr
		);

		/**
		* Create an alias barrier
		*/
		static void AliasBarrier(
			ID3D12GraphicsCommandList* p_commandList,
			ID3D12Resource* p_resourceBefore,
			ID3D12Resource* p_resourceAfter,
			D3D12_RESOURCE_BARRIER_FLAGS p_flags = D3D12_RESOURCE_BARRIER_FLAG_NONE
		);

	private:
		/**
		* Helper to create a CD3DX12_RESOURCE_BARRIER
		*/
		static CD3DX12_RESOURCE_BARRIER CreateTransitionBarrier(
			ID3D12Resource* p_resource,
			D3D12_RESOURCE_STATES p_stateBefore,
			D3D12_RESOURCE_STATES p_stateAfter,
			D3D12_RESOURCE_BARRIER_FLAGS p_flags
		);
	};
}
