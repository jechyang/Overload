/**
* @project: baregl
* @author: Adrien Givry
* @licence: MIT
*/

#pragma once

#include <baregl/detail/NativeObject.h>
#include <baregl/types/EComparaisonAlgorithm.h>
#include <baregl/types/ECullFace.h>
#include <baregl/types/EFramebufferAttachment.h>
#include <baregl/types/EOperation.h>
#include <baregl/types/EPixelDataFormat.h>
#include <baregl/types/EPixelDataType.h>
#include <baregl/types/EPrimitiveMode.h>
#include <baregl/types/ERasterizationMode.h>
#include <baregl/types/ERenderingCapability.h>
#include <baregl/Renderbuffer.h>
#include <baregl/Texture.h>

#include <memory>
#include <span>
#include <optional>
#include <unordered_map>
#include <variant>

namespace baregl
{
	using FramebufferAttachment = std::variant<
		std::shared_ptr<Texture>,
		std::shared_ptr<Renderbuffer>
	>;

	/**
	* Represents a framebuffer, used to store render data for the graphics context to use.
	*/
	class Framebuffer final : public detail::NativeObject
	{
		struct SnapshotData;
		void ApplyAttachments();
	public:
		template<typename T>
		static constexpr bool IsSupportedAttachmentType =
			std::same_as<T, Texture> ||
			std::same_as<T, Renderbuffer>;

		/**
		* Creates a framebuffer.
		* @param p_debugName A name used to identify the framebuffer for debugging purposes
		*/
		Framebuffer(std::string_view p_debugName = std::string_view{});

		/**
		* Destroys the framebuffer.
		*/
		~Framebuffer();

		/**
		* Binds the framebuffer.
		*/
		void Bind() const;

		/**
		* Unbinds the framebuffer.
		*/
		void Unbind() const;

		/**
		* Validate the framebuffer. Must be executed at least once after the framebuffer creation.
		* @note It's recommended to call this method after each attachment change.
		* @return Returns true if the framebuffer has been validated successfully.
		*/
		bool Validate();

		/**
		* Returns true if the framebuffer is valid.
		*/
		bool IsValid() const;

		/**
		* Publishes new empty attachment storage of the given size. Old snapshots keep
		* the previous storage. Reacquire borrowed attachments/IDs after a size change.
		* @param p_width
		* @param p_height
		*/
		void Resize(uint16_t p_width, uint16_t p_height);

		/**
		* Attaches the given texture or render buffer to the framebuffer, at the given attachment point
		* @param p_toAttach must be a texture or a render buffer.
		* @param p_attachment
		* @param p_index (optional) useful when specifying multiple color attachments.
		* @param p_layer (optional) useful when specifying multiple layers in a texture (e.g. cubemap or 3D texture).
		*/
		template<class T>
			requires IsSupportedAttachmentType<T>
		void Attach(std::shared_ptr<T> p_toAttach,
			types::EFramebufferAttachment p_attachment,
			uint32_t p_index = 0,
			std::optional<uint32_t> p_layer = std::nullopt
		);

		/**
		* Returns the texture or render buffer associated with the given attachment point.
		* @param p_attachment
		* @param p_index (optional) useful when specifying multiple color attachments.
		*/
		template<class T>
			requires IsSupportedAttachmentType<T>
		std::optional<std::reference_wrapper<T>> GetAttachment(
			baregl::types::EFramebufferAttachment p_attachment,
			uint32_t p_index = 0
		) const;

		// CPU capture of attachment identities, layers, dimensions and read/draw selection.
		// Replay lazily creates an independent FBO on the GL owner. Contents still require
		// ordered writes; resize publishes new storage without changing existing snapshots.
		class Snapshot
		{
		public:
			Snapshot() = default;
            // CPU-side generation identity; comparing outputs must not realize a GL target.
            bool operator==(const Snapshot& other) const { return m_data == other.m_data; }
			void Bind() const;
			// GL owner or bound ResourceExecutor. Tightly packed full image; restores read target and pack state.
			void ReadRGBA8(std::span<uint8_t> p_pixels) const;
			uint32_t GetID() const; // Realizes the FBO on the GL owner (or through ResourceExecutor).
			std::pair<uint16_t, uint16_t> GetSize() const;
			struct TextureAttachmentInfo
			{
				Texture::Snapshot storage;
				std::optional<uint32_t> layer;
				bool writable;
			};
			// CPU-only attachment metadata for explicit resource validation.
			std::vector<TextureAttachmentInfo> GetTextureAttachments() const;
			// CPU-only selection for recorded passes; leaves the source FBO unchanged.
			Snapshot WithDrawBuffer(std::optional<uint32_t> p_index) const;
			// Color blit of this snapshot into the default back buffer. Execution stays
			// on the GL owner / ResourceExecutor; no product code should call glBlit*.
			void BlitToBackBuffer(uint16_t p_backBufferWidth, uint16_t p_backBufferHeight) const;
		private:
			explicit Snapshot(std::shared_ptr<SnapshotData> p_data) : m_data(std::move(p_data)) {}
			std::shared_ptr<SnapshotData> m_data;
			friend class Framebuffer;
		};
		Snapshot RecordTarget() const;
		static void BindDefault();

		// Owns the attachment object, not a frozen copy of its rendered contents.
		// Acquire/use/release on the context owner; writes/resizes still need ordering.
		std::shared_ptr<Texture> RetainTextureAttachment(types::EFramebufferAttachment p_attachment, uint32_t p_index = 0) const;

		/**
		* Selects which color attachment to draw to.
		* @param p_index index of the color attachment, if set to std::nullopt, no color will be drawn.
		*/
		void SetTargetDrawBuffer(std::optional<uint32_t> p_index);

		/**
		* Selects which color attachment to read from.
		* @param p_index index of the color attachment, if set to std::nullopt, no color attachment will be available for read.
		*/
		void SetTargetReadBuffer(std::optional<uint32_t> p_index);

		/**
		* Returns the size of the framebuffer
		* @param p_attachment
		* @param p_index
		*/
		std::pair<uint16_t, uint16_t> GetSize() const;

		/**
		* Blits the framebuffer to the back buffer.
		* @param p_backBufferWidth
		* @param p_backBufferHeight
		*/
		void BlitToBackBuffer(uint16_t p_backBufferWidth, uint16_t p_backBufferHeight) const;

		/**
		* Reads pixels from the framebuffer.
		* @param p_x The x-coordinate of the lower-left corner.
		* @param p_y The y-coordinate of the lower-left corner.
		* @param p_width The width of the pixel rectangle.
		* @param p_height The height of the pixel rectangle.
		* @param p_format The format of the pixel data.
		* @param p_type The data type of the pixel data.
		* @param p_data The destination buffer to store pixel data.
		*/
		void ReadPixels(
			uint32_t p_x,
			uint32_t p_y,
			uint32_t p_width,
			uint32_t p_height,
			types::EPixelDataFormat p_format,
			types::EPixelDataType p_type,
			void* p_data
		) const;

		/**
		* Returns the debug name of the framebuffer.
		*/
		const std::string& GetDebugName() const;

	protected:
		bool m_valid = false;
		std::string m_debugName;
		std::unordered_map<uint32_t, FramebufferAttachment> m_attachments;
		std::unordered_map<uint32_t, std::optional<uint32_t>> m_layers;
		std::optional<uint32_t> m_drawBuffer = 0;
		std::optional<uint32_t> m_readBuffer = 0;
		mutable std::shared_ptr<SnapshotData> m_snapshot;
	};
}
