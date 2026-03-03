/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <OvRendering/HAL/DirectX12/DX12Prerequisites.h>
#include <OvRendering/HAL/DirectX12/DX12DescriptorHeap.h>
#include <OvRendering/Settings/ETextureType.h>
#include <OvRendering/Settings/ETextureFilteringMode.h>
#include <OvRendering/Settings/ETextureWrapMode.h>

namespace OvRendering::HAL::DX12
{
	/**
	* DX12 Texture resource
	*/
	class DX12Texture
	{
	public:
		/**
		* Constructor
		*/
		DX12Texture();

		/**
		* Destructor
		*/
		~DX12Texture();

		/**
		* Initialize a 2D texture
		* @param p_width Width in pixels
		* @param p_height Height in pixels
		* @param p_format DXGI format
		* @param p_data Pixel data (nullptr for empty texture)
		* @param p_mipLevels Number of mip levels (0 = auto)
		* @return true if initialization succeeded
		*/
		bool Initialize2D(
			uint32_t p_width,
			uint32_t p_height,
			DXGI_FORMAT p_format,
			const void* p_data = nullptr,
			uint32_t p_mipLevels = 1
		);

		/**
		* Initialize a cubemap texture
		* @param p_size Face size in pixels
		* @param p_format DXGI format
		* @param p_data Array of 6 face data pointers
		* @return true if initialization succeeded
		*/
		bool InitializeCubemap(
			uint32_t p_size,
			DXGI_FORMAT p_format,
			const void** p_data = nullptr
		);

		/**
		* Cleanup resources
		*/
		void Shutdown();

		/**
		* Get the texture resource
		*/
		ID3D12Resource* GetResource() const { return m_texture.Get(); }

		/**
		* Get the texture width
		*/
		uint32_t GetWidth() const { return m_width; }

		/**
		* Get the texture height
		*/
		uint32_t GetHeight() const { return m_height; }

		/**
		* Get the texture format
		*/
		DXGI_FORMAT GetFormat() const { return m_format; }

		/**
		* Get the number of mip levels
		*/
		uint32_t GetMipLevels() const { return m_mipLevels; }

		/**
		* Get the texture type
		*/
		Settings::ETextureType GetTextureType() const { return m_textureType; }

		/**
		* Get the resource state
		*/
		D3D12_RESOURCE_STATES GetResourceState() const { return m_resourceState; }

		/**
		* Transition texture to a state
		*/
		void TransitionTo(ID3D12GraphicsCommandList* p_commandList, D3D12_RESOURCE_STATES p_state);

		/**
		* Get the debug name
		*/
		const std::string& GetDebugName() const { return m_debugName; }

		/**
		* Set the debug name
		*/
		void SetDebugName(const std::string& p_debugName) { m_debugName = p_debugName; }

	private:
		ComPtr<ID3D12Resource> m_texture;
		ComPtr<ID3D12Resource> m_uploadBuffer;
		uint32_t m_width = 0;
		uint32_t m_height = 0;
		DXGI_FORMAT m_format = DXGI_FORMAT_UNKNOWN;
		uint32_t m_mipLevels = 1;
		Settings::ETextureType m_textureType = Settings::ETextureType::TEXTURE_2D;
		D3D12_RESOURCE_STATES m_resourceState = D3D12_RESOURCE_STATE_COMMON;
		std::string m_debugName;

		/**
		* Create the texture resource
		*/
		bool CreateTextureResource(D3D12_RESOURCE_FLAGS p_flags = D3D12_RESOURCE_FLAG_NONE);

		/**
		* Upload texture data
		*/
		bool UploadTextureData(const void* p_data, uint32_t p_dataSize, bool p_isCubemap = false);

		/**
		* Get the number of bytes per pixel
		*/
		uint32_t GetBytesPerPixel() const;

		/**
		* Calculate size of texture data including mip levels
		*/
		uint32_t CalculateDataSize() const;
	};

	/**
	* DX12 Texture Handle (SRV wrapper)
	*/
	class DX12TextureHandle
	{
	public:
		/**
		* Constructor
		*/
		DX12TextureHandle();

		/**
		* Destructor
		*/
		~DX12TextureHandle();

		/**
		* Create from texture
		* @param p_texture The texture
		* @param p_filtering Filtering mode
		* @param p_wrapMode Wrap mode
		* @param p_heapIndex Index in the SRV heap
		* @return true if succeeded
		*/
		bool Create(
			DX12Texture* p_texture,
			Settings::ETextureFilteringMode p_filtering,
			Settings::ETextureWrapMode p_wrapMode,
			int32_t p_heapIndex
		);

		/**
		* Cleanup
		*/
		void Shutdown();

		/**
		* Get the heap index
		*/
		int32_t GetHeapIndex() const { return m_heapIndex; }

		/**
		* Check if handle is valid
		*/
		bool IsValid() const { return m_heapIndex >= 0; }

		/**
		* Get the debug name
		*/
		const std::string& GetDebugName() const { return m_texture ? m_texture->GetDebugName() : m_debugName; }

		/**
		* Set the debug name
		*/
		void SetDebugName(const std::string& p_debugName) { m_debugName = p_debugName; }

	private:
		int32_t m_heapIndex = -1;
		DX12Texture* m_texture = nullptr;
		std::string m_debugName;
	};
}
