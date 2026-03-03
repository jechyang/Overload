/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <OvRendering/HAL/DirectX12/DX12Texture.h>
#include <OvRendering/HAL/DirectX12/DX12Device.h>
#include <OvRendering/HAL/DirectX12/DX12ResourceBarrier.h>

#include <OvDebug/Logger.h>

#include <algorithm>

namespace OvRendering::HAL::DX12
{
	// ========================================================================
	// DX12Texture Constructor
	// ========================================================================
	DX12Texture::DX12Texture()
	{
	}

	// ========================================================================
	// DX12Texture Destructor
	// ========================================================================
	DX12Texture::~DX12Texture()
	{
		Shutdown();
	}

	// ========================================================================
	// Initialize2D
	// ========================================================================
	bool DX12Texture::Initialize2D(
		uint32_t p_width,
		uint32_t p_height,
		DXGI_FORMAT p_format,
		const void* p_data,
		uint32_t p_mipLevels
	)
	{
		m_width = p_width;
		m_height = p_height;
		m_format = p_format;
		m_mipLevels = p_mipLevels > 0 ? p_mipLevels : 1;
		m_textureType = Settings::ETextureType::TEXTURE_2D;

		// Calculate mip levels if auto
		if (p_mipLevels == 0 && p_data)
		{
			m_mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(p_width, p_height)))) + 1;
		}

		// Create texture resource
		if (!CreateTextureResource())
		{
			OVLOG_ERROR("Failed to create texture resource");
			return false;
		}

		// Upload data if provided
		if (p_data)
		{
			uint32_t dataSize = CalculateDataSize();
			if (!UploadTextureData(p_data, dataSize))
			{
				OVLOG_ERROR("Failed to upload texture data");
				return false;
			}
		}

		return true;
	}

	// ========================================================================
	// InitializeCubemap
	// ========================================================================
	bool DX12Texture::InitializeCubemap(
		uint32_t p_size,
		DXGI_FORMAT p_format,
		const void** p_data
	)
	{
		m_width = p_size;
		m_height = p_size;
		m_format = p_format;
		m_mipLevels = 1;
		m_textureType = Settings::ETextureType::TEXTURE_CUBE;

		// Create cubemap resource
		D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
		if (!CreateTextureResource(flags))
		{
			OVLOG_ERROR("Failed to create cubemap resource");
			return false;
		}

		// Upload data if provided
		if (p_data)
		{
			uint32_t faceSize = p_size * p_size * GetBytesPerPixel();
			for (int face = 0; face < 6; ++face)
			{
				if (p_data[face])
				{
					// Upload each face separately
					// Note: Simplified implementation
				}
			}
		}

		return true;
	}

	// ========================================================================
	// Shutdown
	// ========================================================================
	void DX12Texture::Shutdown()
	{
		m_texture.Reset();
		m_uploadBuffer.Reset();
	}

	// ========================================================================
	// TransitionTo
	// ========================================================================
	void DX12Texture::TransitionTo(ID3D12GraphicsCommandList* p_commandList, D3D12_RESOURCE_STATES p_state)
	{
		if (p_commandList && m_resourceState != p_state)
		{
			DX12ResourceBarrier::Transition(p_commandList, m_texture.Get(), m_resourceState, p_state);
			m_resourceState = p_state;
		}
	}

	// ========================================================================
	// CreateTextureResource
	// ========================================================================
	bool DX12Texture::CreateTextureResource(D3D12_RESOURCE_FLAGS p_flags)
	{
		CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);

		D3D12_RESOURCE_DESC resourceDesc = {};
		resourceDesc.Dimension = m_textureType == Settings::ETextureType::TEXTURE_CUBE
			? D3D12_RESOURCE_DIMENSION_TEXTURE2D
			: D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resourceDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
		resourceDesc.Width = m_width;
		resourceDesc.Height = m_height;
		resourceDesc.DepthOrArraySize = m_textureType == Settings::ETextureType::TEXTURE_CUBE ? 6 : 1;
		resourceDesc.MipLevels = m_mipLevels;
		resourceDesc.Format = m_format;
		resourceDesc.SampleDesc = { 1, 0 };
		resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		resourceDesc.Flags = p_flags;

		if (m_textureType == Settings::ETextureType::TEXTURE_CUBE)
		{
			resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		}

		m_resourceState = D3D12_RESOURCE_STATE_COPY_DEST;

		HRESULT hr = DX12Device::GetDevice()->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			m_resourceState,
			nullptr,
			IID_PPV_ARGS(&m_texture)
		);

		if (FAILED(hr))
		{
			OVLOG_ERROR("Failed to create texture resource");
			return false;
		}

		return true;
	}

	// ========================================================================
	// UploadTextureData
	// ========================================================================
	bool DX12Texture::UploadTextureData(const void* p_data, uint32_t p_dataSize, bool p_isCubemap)
	{
		if (!p_data || !m_texture)
		{
			return false;
		}

		// Create upload buffer
		CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);

		D3D12_RESOURCE_DESC uploadDesc = {};
		uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		uploadDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
		uploadDesc.Width = p_dataSize;
		uploadDesc.Height = 1;
		uploadDesc.DepthOrArraySize = 1;
		uploadDesc.MipLevels = 1;
		uploadDesc.Format = DXGI_FORMAT_UNKNOWN;
		uploadDesc.SampleDesc = { 1, 0 };
		uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		uploadDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

		HRESULT hr = DX12Device::GetDevice()->CreateCommittedResource(
			&uploadHeapProps,
			D3D12_HEAP_FLAG_NONE,
			&uploadDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_uploadBuffer)
		);

		if (FAILED(hr))
		{
			OVLOG_ERROR("Failed to create upload buffer");
			return false;
		}

		// Map and copy
		void* mappedData = nullptr;
		CD3DX12_RANGE readRange(0, 0);
		hr = m_uploadBuffer->Map(0, &readRange, &mappedData);
		if (FAILED(hr))
		{
			OVLOG_ERROR("Failed to map upload buffer");
			return false;
		}

		memcpy(mappedData, p_data, p_dataSize);
		m_uploadBuffer->Unmap(0, nullptr);

		// Copy using command list
		ID3D12GraphicsCommandList* commandList = DX12Device::GetCommandList();
		ID3D12CommandAllocator* commandAllocator = DX12Device::GetCommandAllocator();

		commandAllocator->Reset();
		commandList->Reset(commandAllocator, nullptr);

		// Copy to texture
		D3D12_SUBRESOURCE_DATA subresource = {};
		subresource.pData = mappedData;
		subresource.RowPitch = m_width * GetBytesPerPixel();
		subresource.SlicePitch = subresource.RowPitch * m_height;

		// Use UploadTextureData helper (simplified - direct copy)
		commandList->CopyResource(m_texture.Get(), m_uploadBuffer.Get());

		// Transition to pixel shader resource
		DX12ResourceBarrier::Transition(
			commandList,
			m_texture.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
		);
		m_resourceState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

		commandList->Close();

		// Execute
		ID3D12CommandQueue* commandQueue = DX12Device::GetCommandQueue();
		commandQueue->ExecuteCommandLists(1, reinterpret_cast<ID3D12CommandList* const*>(&commandList));

		// Cleanup upload buffer
		m_uploadBuffer.Reset();

		return true;
	}

	// ========================================================================
	// GetBytesPerPixel
	// ========================================================================
	uint32_t DX12Texture::GetBytesPerPixel() const
	{
		switch (m_format)
		{
		case DXGI_FORMAT_R8_UNORM: return 1;
		case DXGI_FORMAT_R8G8_UNORM: return 2;
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
			return 4;
		case DXGI_FORMAT_R16_FLOAT: return 2;
		case DXGI_FORMAT_R16G16_FLOAT: return 4;
		case DXGI_FORMAT_R16G16B16A16_FLOAT: return 8;
		case DXGI_FORMAT_R32_FLOAT: return 4;
		case DXGI_FORMAT_R32G32_FLOAT: return 8;
		case DXGI_FORMAT_R32G32B32A32_FLOAT: return 16;
		default:
			return 4; // Default to 4 bytes
		}
	}

	// ========================================================================
	// CalculateDataSize
	// ========================================================================
	uint32_t DX12Texture::CalculateDataSize() const
	{
		uint32_t bytesPerPixel = GetBytesPerPixel();
		uint32_t totalSize = 0;

		uint32_t width = m_width;
		uint32_t height = m_height;

		for (uint32_t mip = 0; mip < m_mipLevels; ++mip)
		{
			totalSize += width * height * bytesPerPixel;
			width = std::max(1u, width / 2);
			height = std::max(1u, height / 2);
		}

		return totalSize;
	}

	// ========================================================================
	// DX12TextureHandle Constructor
	// ========================================================================
	DX12TextureHandle::DX12TextureHandle()
	{
	}

	// ========================================================================
	// DX12TextureHandle Destructor
	// ========================================================================
	DX12TextureHandle::~DX12TextureHandle()
	{
		Shutdown();
	}

	// ========================================================================
	// Create
	// ========================================================================
	bool DX12TextureHandle::Create(
		DX12Texture* p_texture,
		Settings::ETextureFilteringMode p_filtering,
		Settings::ETextureWrapMode p_wrapMode,
		int32_t p_heapIndex
	)
	{
		if (!p_texture || p_heapIndex < 0)
		{
			return false;
		}

		m_texture = p_texture;
		m_heapIndex = p_heapIndex;

		// Create SRV in descriptor heap
		// Note: This requires a global SRV heap - to be implemented
		// For now, this is a placeholder

		return true;
	}

	// ========================================================================
	// Shutdown
	// ========================================================================
	void DX12TextureHandle::Shutdown()
	{
		m_heapIndex = -1;
		m_texture = nullptr;
	}
}
