/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <OvRendering/HAL/DirectX12/DX12ResourceBarrier.h>

namespace OvRendering::HAL::DX12
{
	// ========================================================================
	// Transition
	// ========================================================================
	void DX12ResourceBarrier::Transition(
		ID3D12GraphicsCommandList* p_commandList,
		ID3D12Resource* p_resource,
		D3D12_RESOURCE_STATES p_stateBefore,
		D3D12_RESOURCE_STATES p_stateAfter,
		D3D12_RESOURCE_BARRIER_FLAGS p_flags
	)
	{
		if (p_stateBefore == p_stateAfter)
			return;

		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			p_resource,
			p_stateBefore,
			p_stateAfter,
			p_flags
		);

		p_commandList->ResourceBarrier(1, &barrier.barrier);
	}

	// ========================================================================
	// UAVBarrier
	// ========================================================================
	void DX12ResourceBarrier::UAVBarrier(
		ID3D12GraphicsCommandList* p_commandList,
		ID3D12Resource* p_resource
	)
	{
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::UAV(p_resource);
		p_commandList->ResourceBarrier(1, &barrier.barrier);
	}

	// ========================================================================
	// AliasBarrier - Not supported in minimal CD3DX12 implementation
	// ========================================================================
	void DX12ResourceBarrier::AliasBarrier(
		ID3D12GraphicsCommandList* p_commandList,
		ID3D12Resource* p_resourceBefore,
		ID3D12Resource* p_resourceAfter,
		D3D12_RESOURCE_BARRIER_FLAGS p_flags
	)
	{
		// Note: Alias barrier not supported in minimal CD3DX12 implementation
		// Use Transition barriers instead for resource state changes
		(void)p_commandList;
		(void)p_resourceBefore;
		(void)p_resourceAfter;
		(void)p_flags;
	}

	// ========================================================================
	// CreateTransitionBarrier
	// ========================================================================
	CD3DX12_RESOURCE_BARRIER DX12ResourceBarrier::CreateTransitionBarrier(
		ID3D12Resource* p_resource,
		D3D12_RESOURCE_STATES p_stateBefore,
		D3D12_RESOURCE_STATES p_stateAfter,
		D3D12_RESOURCE_BARRIER_FLAGS p_flags
	)
	{
		return CD3DX12_RESOURCE_BARRIER::Transition(
			p_resource,
			p_stateBefore,
			p_stateAfter,
			p_flags
		);
	}
}
