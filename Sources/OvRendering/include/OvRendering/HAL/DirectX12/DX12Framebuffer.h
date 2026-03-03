/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <OvRendering/HAL/DirectX12/DX12Prerequisites.h>
#include <OvRendering/HAL/DirectX12/DX12DescriptorHeap.h>
#include <OvRendering/Settings/EFramebufferAttachment.h>

#include <string>

namespace OvRendering::HAL::DX12
{
	class DX12Texture; // Forward declaration
	/**
	* DX12 Renderbuffer (for depth/stencil attachments)
	*/
	class DX12Renderbuffer
	{
	public:
		/**
		* Constructor
		*/
		DX12Renderbuffer();

		/**
		* Destructor
		*/
		~DX12Renderbuffer();

		/**
		* Initialize as depth-stencil buffer
		* @param p_width Width in pixels
		* @param p_height Height in pixels
		* @param p_format DXGI format (D24_UNORM_S8_UINT or D32_FLOAT)
		* @return true if initialization succeeded
		*/
		bool InitializeDepthStencil(uint32_t p_width, uint32_t p_height, DXGI_FORMAT p_format = DXGI_FORMAT_D24_UNORM_S8_UINT);

		/**
		* Initialize as color buffer
		*/
		bool InitializeColor(uint32_t p_width, uint32_t p_height, DXGI_FORMAT p_format = DXGI_FORMAT_R8G8B8A8_UNORM);

		/**
		* Cleanup resources
		*/
		void Shutdown();

		/**
		* Get the resource
		*/
		ID3D12Resource* GetResource() const { return m_renderbuffer.Get(); }

		/**
		* Get the width
		*/
		uint32_t GetWidth() const { return m_width; }

		/**
		* Get the height
		*/
		uint32_t GetHeight() const { return m_height; }

		/**
		* Get the format
		*/
		DXGI_FORMAT GetFormat() const { return m_format; }

		/**
		* Get the CPU descriptor handle for RTV/DSV
		*/
		D3D12_CPU_DESCRIPTOR_HANDLE GetViewDescriptor() const { return m_viewDescriptor; }

		/**
		* Set the view descriptor (created by Framebuffer)
		*/
		void SetViewDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE p_handle) { m_viewDescriptor = p_handle; }

	private:
		ComPtr<ID3D12Resource> m_renderbuffer;
		uint32_t m_width = 0;
		uint32_t m_height = 0;
		DXGI_FORMAT m_format = DXGI_FORMAT_UNKNOWN;
		D3D12_CPU_DESCRIPTOR_HANDLE m_viewDescriptor = {};
		bool m_isDepthStencil = false;
	};

	/**
	* DX12 Framebuffer
	*/
	class DX12Framebuffer
	{
	public:
		/**
		* Constructor
		*/
		DX12Framebuffer();

		/**
		* Destructor
		*/
		~DX12Framebuffer();

		/**
		* Initialize framebuffer
		* @param p_width Width in pixels
		* @param p_height Height in pixels
		* @param p_colorAttachments Number of color attachments
		* @param p_hasDepthStencil Include depth-stencil buffer
		* @return true if initialization succeeded
		*/
		bool Initialize(
			uint32_t p_width,
			uint32_t p_height,
			uint32_t p_colorAttachments = 1,
			bool p_hasDepthStencil = true
		);

		/**
		* Attach a texture as color attachment
		*/
		bool AttachColorTexture(DX12Texture* p_texture, uint32_t p_attachmentIndex = 0);

		/**
		* Attach a texture as depth-stencil
		*/
		bool AttachDepthStencilTexture(DX12Texture* p_texture);

		/**
		* Cleanup resources
		*/
		void Shutdown();

		/**
		* Get the framebuffer width
		*/
		uint32_t GetWidth() const { return m_width; }

		/**
		* Get the framebuffer height
		*/
		uint32_t GetHeight() const { return m_height; }

		/**
		* Get the RTV descriptor heap
		*/
		ID3D12DescriptorHeap* GetRTVHeap() const { return m_rtvHeap.Get(); }

		/**
		* Get the DSV descriptor heap
		*/
		ID3D12DescriptorHeap* GetDSVHeap() const { return m_dsvHeap.Get(); }

		/**
		* Get RTV handle for attachment
		*/
		D3D12_CPU_DESCRIPTOR_HANDLE GetRTVHandle(uint32_t p_index = 0) const;

		/**
		* Get DSV handle
		*/
		D3D12_CPU_DESCRIPTOR_HANDLE GetDSVHandle() const { return m_dsvHandle; }

		/**
		* Get number of color attachments
		*/
		uint32_t GetColorAttachmentCount() const { return m_colorAttachmentCount; }

		/**
		* Check if framebuffer has depth-stencil
		*/
		bool HasDepthStencil() const { return m_hasDepthStencil; }

		/**
		* Bind framebuffer for rendering
		*/
		void Bind(ID3D12GraphicsCommandList* p_commandList);

		/**
		* Get the debug name
		*/
		const std::string& GetDebugName() const { return m_debugName; }

		/**
		* Set the debug name
		*/
		void SetDebugName(const std::string& p_debugName) { m_debugName = p_debugName; }

	private:
		ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
		ComPtr<ID3D12DescriptorHeap> m_dsvHeap;

		uint32_t m_width = 0;
		uint32_t m_height = 0;
		uint32_t m_colorAttachmentCount = 0;
		bool m_hasDepthStencil = false;

		D3D12_CPU_DESCRIPTOR_HANDLE m_rtvHandles[8] = {};
		D3D12_CPU_DESCRIPTOR_HANDLE m_dsvHandle = {};

		std::array<DX12Texture*, 8> m_colorTextures = {};
		DX12Texture* m_depthStencilTexture = nullptr;

		std::string m_debugName;
	};
}
