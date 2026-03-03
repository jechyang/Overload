/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <OvRendering/HAL/DirectX12/DX12Framebuffer.h>
#include <OvRendering/HAL/DirectX12/DX12Texture.h>
#include <OvRendering/HAL/DirectX12/DX12Device.h>

#include <OvDebug/Logger.h>

#include <algorithm>

namespace OvRendering::HAL::DX12
{
	// ========================================================================
	// DX12Renderbuffer Constructor
	// ========================================================================
	DX12Renderbuffer::DX12Renderbuffer()
	{
	}

	// ========================================================================
	// DX12Renderbuffer Destructor
	// ========================================================================
	DX12Renderbuffer::~DX12Renderbuffer()
	{
		Shutdown();
	}

	// ========================================================================
	// InitializeDepthStencil
	// ========================================================================
	bool DX12Renderbuffer::InitializeDepthStencil(uint32_t p_width, uint32_t p_height, DXGI_FORMAT p_format)
	{
		m_width = p_width;
		m_height = p_height;
		m_format = p_format;
		m_isDepthStencil = true;

		CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);

		D3D12_RESOURCE_DESC resourceDesc = {};
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resourceDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
		resourceDesc.Width = m_width;
		resourceDesc.Height = m_height;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels = 1;
		resourceDesc.Format = m_format;
		resourceDesc.SampleDesc = { 1, 0 };
		resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		D3D12_CLEAR_VALUE clearValue = {};
		clearValue.Format = m_format;
		clearValue.DepthStencil.Depth = 1.0f;
		clearValue.DepthStencil.Stencil = 0;

		HRESULT hr = DX12Device::GetDevice()->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&clearValue,
			IID_PPV_ARGS(&m_renderbuffer)
		);

		if (FAILED(hr))
		{
			OVLOG_ERROR("Failed to create depth-stencil renderbuffer");
			return false;
		}

		return true;
	}

	// ========================================================================
	// InitializeColor
	// ========================================================================
	bool DX12Renderbuffer::InitializeColor(uint32_t p_width, uint32_t p_height, DXGI_FORMAT p_format)
	{
		m_width = p_width;
		m_height = p_height;
		m_format = p_format;
		m_isDepthStencil = false;

		CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);

		D3D12_RESOURCE_DESC resourceDesc = {};
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resourceDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
		resourceDesc.Width = m_width;
		resourceDesc.Height = m_height;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels = 1;
		resourceDesc.Format = m_format;
		resourceDesc.SampleDesc = { 1, 0 };
		resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

		HRESULT hr = DX12Device::GetDevice()->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			nullptr,
			IID_PPV_ARGS(&m_renderbuffer)
		);

		if (FAILED(hr))
		{
			OVLOG_ERROR("Failed to create color renderbuffer");
			return false;
		}

		return true;
	}

	// ========================================================================
	// Shutdown
	// ========================================================================
	void DX12Renderbuffer::Shutdown()
	{
		m_renderbuffer.Reset();
	}

	// ========================================================================
	// DX12Framebuffer Constructor
	// ========================================================================
	DX12Framebuffer::DX12Framebuffer()
	{
	}

	// ========================================================================
	// DX12Framebuffer Destructor
	// ========================================================================
	DX12Framebuffer::~DX12Framebuffer()
	{
		Shutdown();
	}

	// ========================================================================
	// Initialize
	// ========================================================================
	bool DX12Framebuffer::Initialize(
		uint32_t p_width,
		uint32_t p_height,
		uint32_t p_colorAttachments,
		bool p_hasDepthStencil
	)
	{
		m_width = p_width;
		m_height = p_height;
		m_colorAttachmentCount = std::min(p_colorAttachments, 8u);
		m_hasDepthStencil = p_hasDepthStencil;

		// Create RTV heap
		if (m_colorAttachmentCount > 0)
		{
			D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
			rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			rtvHeapDesc.NumDescriptors = m_colorAttachmentCount;
			rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			rtvHeapDesc.NodeMask = 0;

			HRESULT hr = DX12Device::GetDevice()->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap));
			if (FAILED(hr))
			{
				OVLOG_ERROR("Failed to create RTV heap");
				return false;
			}
		}

		// Create DSV heap
		if (m_hasDepthStencil)
		{
			D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
			dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
			dsvHeapDesc.NumDescriptors = 1;
			dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			dsvHeapDesc.NodeMask = 0;

			HRESULT hr = DX12Device::GetDevice()->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap));
			if (FAILED(hr))
			{
				OVLOG_ERROR("Failed to create DSV heap");
				return false;
			}
		}

		return true;
	}

	// ========================================================================
	// AttachColorTexture
	// ========================================================================
	bool DX12Framebuffer::AttachColorTexture(DX12Texture* p_texture, uint32_t p_attachmentIndex)
	{
		if (!p_texture || p_attachmentIndex >= m_colorAttachmentCount)
		{
			return false;
		}

		m_colorTextures[p_attachmentIndex] = p_texture;

		// Create RTV for the texture
		if (m_rtvHeap)
		{
			D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
			rtvHandle.ptr += p_attachmentIndex * DX12Device::GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

			DX12Device::GetDevice()->CreateRenderTargetView(
				p_texture->GetResource(),
				nullptr,
				rtvHandle
			);

			m_rtvHandles[p_attachmentIndex] = rtvHandle;
		}

		return true;
	}

	// ========================================================================
	// AttachDepthStencilTexture
	// ========================================================================
	bool DX12Framebuffer::AttachDepthStencilTexture(DX12Texture* p_texture)
	{
		if (!p_texture || !m_hasDepthStencil)
		{
			return false;
		}

		m_depthStencilTexture = p_texture;

		// Create DSV for the texture
		if (m_dsvHeap)
		{
			m_dsvHandle = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();

			DX12Device::GetDevice()->CreateDepthStencilView(
				p_texture->GetResource(),
				nullptr,
				m_dsvHandle
			);
		}

		return true;
	}

	// ========================================================================
	// Shutdown
	// ========================================================================
	void DX12Framebuffer::Shutdown()
	{
		m_rtvHeap.Reset();
		m_dsvHeap.Reset();
		m_colorTextures.fill(nullptr);
		m_depthStencilTexture = nullptr;
	}

	// ========================================================================
	// GetRTVHandle
	// ========================================================================
	D3D12_CPU_DESCRIPTOR_HANDLE DX12Framebuffer::GetRTVHandle(uint32_t p_index) const
	{
		if (p_index < m_colorAttachmentCount)
		{
			return m_rtvHandles[p_index];
		}
		return {};
	}

	// ========================================================================
	// Bind
	// ========================================================================
	void DX12Framebuffer::Bind(ID3D12GraphicsCommandList* p_commandList)
	{
		if (!p_commandList)
		{
			return;
		}

		// Bind RTVs
		if (m_rtvHeap && m_colorAttachmentCount > 0)
		{
			p_commandList->OMSetRenderTargets(m_colorAttachmentCount, m_rtvHandles, FALSE, m_hasDepthStencil ? &m_dsvHandle : nullptr);
		}

		// Bind descriptor heaps
		ID3D12DescriptorHeap* heaps[] = { m_rtvHeap.Get(), m_dsvHeap.Get() };
		p_commandList->SetDescriptorHeaps(_countof(heaps), heaps);
	}
}
