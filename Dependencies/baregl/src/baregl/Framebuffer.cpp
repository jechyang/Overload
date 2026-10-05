/**
* @project: baregl
* @author: Adrien Givry
* @licence: MIT
*/

#include <baregl/ResourceExecutor.h>
#include <baregl/Framebuffer.h>
#include <stdexcept>

#include <baregl/debug/Assert.h>
#include <baregl/debug/Event.h>
#include <baregl/debug/Log.h>
#include <baregl/detail/glad/glad.h>
#include <baregl/detail/Types.h>
#include <baregl/Renderbuffer.h>

namespace
{
	constexpr uint32_t k_maxColorAttachmentCount = 32;
}

namespace baregl
{
	template<>
	void Framebuffer::Attach(
		std::shared_ptr<Renderbuffer> p_toAttach,
		types::EFramebufferAttachment p_attachment,
		uint32_t p_index,
		std::optional<uint32_t> p_layer
	)
	{
		return ResourceExecutor::Invoke([&]() -> void {
			BAREGL_ASSERT(p_toAttach != nullptr, "Cannot attach a null renderbuffer");
			BAREGL_ASSERT(!p_layer.has_value(), "Renderbuffer cannot use layers");
			BAREGL_ASSERT(p_index == 0 || p_attachment == types::EFramebufferAttachment::COLOR, "Only color attachments support indexing");
			BAREGL_ASSERT(p_index < k_maxColorAttachmentCount, "Color attachment index must be in range [0, 31]");

			const auto attachmentIndex = utils::EnumToValue<GLenum>(p_attachment) + static_cast<GLenum>(p_index);
			glNamedFramebufferRenderbuffer(m_id, attachmentIndex, GL_RENDERBUFFER, p_toAttach->GetID());
			m_attachments[attachmentIndex] = p_toAttach;
			m_layers[attachmentIndex] = p_layer;
			m_snapshot.reset();
		});
	}

	template<>
	void Framebuffer::Attach(
		std::shared_ptr<Texture> p_toAttach,
		types::EFramebufferAttachment p_attachment,
		uint32_t p_index,
		std::optional<uint32_t> p_layer
	)
	{
		return ResourceExecutor::Invoke([&]() -> void {
			BAREGL_ASSERT(p_toAttach != nullptr, "Cannot attach a null texture");
			BAREGL_ASSERT(p_index == 0 || p_attachment == types::EFramebufferAttachment::COLOR, "Only color attachments support indexing");
			BAREGL_ASSERT(p_index < k_maxColorAttachmentCount, "Color attachment index must be in range [0, 31]");

			const auto attachmentIndex = utils::EnumToValue<GLenum>(p_attachment) + static_cast<GLenum>(p_index);
			constexpr uint32_t k_mipMapLevel = 0;

			if (p_layer.has_value())
			{
				glNamedFramebufferTextureLayer(m_id, attachmentIndex, p_toAttach->GetID(), k_mipMapLevel, p_layer.value());
			}
			else
			{
				glNamedFramebufferTexture(m_id, attachmentIndex, p_toAttach->GetID(), k_mipMapLevel);
			}

			m_attachments[attachmentIndex] = p_toAttach;
			m_layers[attachmentIndex] = p_layer;
			m_snapshot.reset();
		});
	}

	Framebuffer::Framebuffer(std::string_view p_debugName) :
		detail::NativeObject(ResourceKind::FRAMEBUFFER),
		m_debugName{ p_debugName }
	{
		ResourceExecutor::Invoke([&]() -> void {
			glCreateFramebuffers(1, &m_id);
			AdoptNativeID();
			NOTIFY_FRAMEBUFFER_CREATED;
		});
	}

	Framebuffer::~Framebuffer()
	{
		NOTIFY_FRAMEBUFFER_DESTROYED;
	}

	void Framebuffer::Bind() const
	{
		return ResourceExecutor::Invoke([&]() -> void {
			glBindFramebuffer(GL_FRAMEBUFFER, m_id);
		});
	}

	void Framebuffer::Unbind() const
	{
		return ResourceExecutor::Invoke([&]() -> void {
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
		});
	}

	bool Framebuffer::Validate()
	{
		return ResourceExecutor::Invoke([&]() -> bool {
			const GLenum status = glCheckNamedFramebufferStatus(m_id, GL_FRAMEBUFFER);

			if (status != GL_FRAMEBUFFER_COMPLETE)
			{
				BAREGL_LOG_ERROR("Framebuffer validation failed with status: " + std::to_string(status));
				return m_valid = false;
			}

			return m_valid = true;
		});
	}

	bool Framebuffer::IsValid() const
	{
		return m_valid;
	}

	template<>
	std::optional<std::reference_wrapper<Texture>> Framebuffer::GetAttachment(
		baregl::types::EFramebufferAttachment p_attachment,
		uint32_t p_index
	) const
	{
		const auto attachmentIndex = utils::EnumToValue<GLenum>(p_attachment) + static_cast<GLenum>(p_index);

		if (m_attachments.contains(attachmentIndex))
		{
			auto attachment = m_attachments.at(attachmentIndex);

			if (auto pval = std::get_if<std::shared_ptr<Texture>>(&attachment); pval && *pval)
			{
				return **pval;
			}
		}

		return std::nullopt;
	}

	template<>
	std::optional<std::reference_wrapper<Renderbuffer>> Framebuffer::GetAttachment(
		baregl::types::EFramebufferAttachment p_attachment,
		uint32_t p_index
	) const
	{
		const auto attachmentIndex = utils::EnumToValue<GLenum>(p_attachment) + static_cast<GLenum>(p_index);

		if (m_attachments.contains(attachmentIndex))
		{
			auto attachment = m_attachments.at(attachmentIndex);

			if (auto pval = std::get_if<std::shared_ptr<Renderbuffer>>(&attachment); pval && *pval)
			{
				return **pval;
			}
		}

		return std::nullopt;
	}

	std::shared_ptr<Texture> Framebuffer::RetainTextureAttachment(types::EFramebufferAttachment p_attachment, uint32_t p_index) const
	{
		const auto key = utils::EnumToValue<GLenum>(p_attachment) + static_cast<GLenum>(p_index);
		const auto it = m_attachments.find(key);
		if (it != m_attachments.end())
			if (const auto texture = std::get_if<std::shared_ptr<Texture>>(&it->second)) return *texture;
		return {};
	}

	void Framebuffer::ApplyAttachments()
	{
		for (const auto& [key, attachment] : m_attachments)
		{
			if (const auto texture = std::get_if<std::shared_ptr<Texture>>(&attachment))
			{
				const auto layer = m_layers.at(key);
				if (layer) glNamedFramebufferTextureLayer(m_id, key, (*texture)->GetID(), 0, *layer);
				else glNamedFramebufferTexture(m_id, key, (*texture)->GetID(), 0);
			}
			else glNamedFramebufferRenderbuffer(m_id, key, GL_RENDERBUFFER,
				std::get<std::shared_ptr<Renderbuffer>>(attachment)->GetID());
		}
		glNamedFramebufferDrawBuffer(m_id, m_drawBuffer ? GL_COLOR_ATTACHMENT0 + *m_drawBuffer : GL_NONE);
		glNamedFramebufferReadBuffer(m_id, m_readBuffer ? GL_COLOR_ATTACHMENT0 + *m_readBuffer : GL_NONE);
	}

	struct Framebuffer::SnapshotData
	{
		std::unordered_map<uint32_t, FramebufferAttachment> attachments;
		std::unordered_map<uint32_t, std::optional<uint32_t>> layers;
		std::optional<uint32_t> drawBuffer, readBuffer;
		std::pair<uint16_t, uint16_t> size;
		std::string debugName;
		std::unique_ptr<Framebuffer> realized;
	};

	Framebuffer::Snapshot Framebuffer::RecordTarget() const
	{
		if (!IsValid()) throw std::logic_error("Cannot record an invalid framebuffer");
		if (!m_snapshot)
		{
			auto data = std::make_shared<SnapshotData>();
			data->attachments = m_attachments;
			data->layers = m_layers;
			data->drawBuffer = m_drawBuffer;
			data->readBuffer = m_readBuffer;
			data->size = GetSize();
			data->debugName = m_debugName;
			m_snapshot = std::move(data);
		}
		return Snapshot{ m_snapshot };
	}

	uint32_t Framebuffer::Snapshot::GetID() const
	{
		return ResourceExecutor::Invoke([&]() -> uint32_t {
			if (!m_data) throw std::logic_error("Empty framebuffer snapshot");
			if (!m_data->realized)
			{
				auto target = std::make_unique<Framebuffer>(m_data->debugName);
				target->m_attachments = m_data->attachments;
				target->m_layers = m_data->layers;
				target->m_drawBuffer = m_data->drawBuffer;
				target->m_readBuffer = m_data->readBuffer;
				target->ApplyAttachments();
				if (!target->Validate()) throw std::runtime_error("Recorded framebuffer is incomplete");
				m_data->realized = std::move(target);
			}
			return m_data->realized->GetID();
		});
	}

	Framebuffer::Snapshot Framebuffer::Snapshot::WithDrawBuffer(std::optional<uint32_t> p_index) const
	{
		if (!m_data || (p_index && *p_index >= k_maxColorAttachmentCount))
			throw std::invalid_argument("Invalid recorded draw buffer selection");
		auto data = std::make_shared<SnapshotData>();
		data->attachments = m_data->attachments;
		data->layers = m_data->layers;
		data->drawBuffer = p_index;
		data->readBuffer = m_data->readBuffer;
		data->size = m_data->size;
		data->debugName = m_data->debugName;
		return Snapshot(std::move(data));
	}

	std::vector<Framebuffer::Snapshot::TextureAttachmentInfo> Framebuffer::Snapshot::GetTextureAttachments() const
	{
		if (!m_data) throw std::logic_error("Empty framebuffer snapshot");
		std::vector<TextureAttachmentInfo> result;
		for (const auto& [key, attachment] : m_data->attachments)
			if (const auto* texture = std::get_if<std::shared_ptr<Texture>>(&attachment))
				result.push_back({(*texture)->RetainStorage(), m_data->layers.at(key),
					key < GL_COLOR_ATTACHMENT0 || key >= GL_COLOR_ATTACHMENT0 + k_maxColorAttachmentCount ||
					(m_data->drawBuffer && key == GL_COLOR_ATTACHMENT0 + *m_data->drawBuffer)});
		return result;
	}
    std::vector<Framebuffer::Snapshot::RenderbufferAttachmentInfo> Framebuffer::Snapshot::GetRenderbufferAttachments() const
    {
        std::vector<RenderbufferAttachmentInfo> result;
        if (!m_data) return result;
        for (const auto& [key,attachment] : m_data->attachments)
            if (const auto* storage=std::get_if<std::shared_ptr<Renderbuffer>>(&attachment))
                result.push_back({*storage,utils::ValueToEnum<types::EFramebufferAttachment>(key)});
        return result;
    }

	void Framebuffer::Snapshot::Bind() const
	{
		return ResourceExecutor::Invoke([&]() -> void {
			glBindFramebuffer(GL_FRAMEBUFFER, GetID());
		});
	}

	std::pair<uint16_t, uint16_t> Framebuffer::Snapshot::GetSize() const
	{
		if (!m_data) throw std::logic_error("Empty framebuffer snapshot");
		return m_data->size;
	}

    void Framebuffer::Snapshot::ReadRGBA8(std::span<uint8_t> p_pixels) const
    {
        return ResourceExecutor::Invoke([&]() -> void {
            const auto [width, height] = GetSize();
            if (!width || !height || p_pixels.size() != size_t(width) * height * 4)
                throw std::invalid_argument("Invalid readback storage size");
            const auto target = GetID();
            GLint readTarget, packBuffer, alignment, rowLength, skipRows, skipPixels;
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readTarget);
            glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &packBuffer);
            glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
            glGetIntegerv(GL_PACK_ROW_LENGTH, &rowLength);
            glGetIntegerv(GL_PACK_SKIP_ROWS, &skipRows);
            glGetIntegerv(GL_PACK_SKIP_PIXELS, &skipPixels);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, target);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glPixelStorei(GL_PACK_ROW_LENGTH, 0);
            glPixelStorei(GL_PACK_SKIP_ROWS, 0);
            glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
            glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, p_pixels.data());
            glPixelStorei(GL_PACK_ALIGNMENT, alignment);
            glPixelStorei(GL_PACK_ROW_LENGTH, rowLength);
            glPixelStorei(GL_PACK_SKIP_ROWS, skipRows);
            glPixelStorei(GL_PACK_SKIP_PIXELS, skipPixels);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, packBuffer);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, readTarget);
        });
    }

	void Framebuffer::BindDefault()
	{
		return ResourceExecutor::Invoke([&]() -> void {
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
		});
	}

	void Framebuffer::Resize(uint16_t p_width, uint16_t p_height)
	{
		// The producer owns source attachment metadata. Avoid a synchronous owner
		// task on every unchanged post-process target; real changes still dispatch.
		if (IsValid() && p_width && p_height && GetSize() == std::pair{p_width, p_height}) return;
		return ResourceExecutor::Invoke([&]() -> void {
			if (!IsValid() || !p_width || !p_height) throw std::invalid_argument("Invalid framebuffer resize: " + m_debugName + " " + std::to_string(p_width) + "x" + std::to_string(p_height));
			if (GetSize() == std::pair{p_width, p_height}) return;
			Framebuffer candidate(m_debugName);
			candidate.m_layers = m_layers;
			candidate.m_drawBuffer = m_drawBuffer;
			candidate.m_readBuffer = m_readBuffer;
			std::unordered_map<const Texture*, std::shared_ptr<Texture>> textures;
			std::unordered_map<const Renderbuffer*, std::shared_ptr<Renderbuffer>> renderbuffers;
			for (const auto& [key, attachment] : m_attachments)
			{
				if (const auto texture = std::get_if<std::shared_ptr<Texture>>(&attachment))
				{
					auto& replacement = textures[texture->get()];
					if (!replacement) replacement = (*texture)->CreateResizedStorage(p_width, p_height);
					candidate.m_attachments.emplace(key, replacement);
				}
				else
				{
					const auto& renderbuffer = std::get<std::shared_ptr<Renderbuffer>>(attachment);
					auto& replacement = renderbuffers[renderbuffer.get()];
					if (!replacement) replacement = renderbuffer->CreateResizedStorage(p_width, p_height);
					candidate.m_attachments.emplace(key, replacement);
				}
			}
			candidate.ApplyAttachments();
			if (!candidate.Validate()) throw std::runtime_error("Resized framebuffer is incomplete");
			// Preserve this FBO's native identity; publish only after allocation and validation.
			m_attachments.swap(candidate.m_attachments);
			ApplyAttachments();
			m_snapshot.reset();
		});
	}

	void Framebuffer::SetTargetDrawBuffer(std::optional<uint32_t> p_index)
	{
		return ResourceExecutor::Invoke([&]() -> void {
			BAREGL_ASSERT(IsValid(), "Invalid framebuffer");
			BAREGL_ASSERT(!p_index.has_value() || p_index < k_maxColorAttachmentCount, "Color attachment index must be in range [0, 31]");

			if (p_index.has_value())
			{
				glNamedFramebufferDrawBuffer(m_id, GL_COLOR_ATTACHMENT0 + p_index.value());
			}
			else
			{
				glNamedFramebufferDrawBuffer(m_id, GL_NONE);
			}
			if (m_drawBuffer != p_index) m_snapshot.reset();
			m_drawBuffer = p_index;
		});
	}

	void Framebuffer::SetTargetReadBuffer(std::optional<uint32_t> p_index)
	{
		return ResourceExecutor::Invoke([&]() -> void {
			BAREGL_ASSERT(IsValid(), "Invalid framebuffer");
			BAREGL_ASSERT(!p_index.has_value() || p_index < k_maxColorAttachmentCount, "Color attachment index must be in range [0, 31]");

			if (p_index.has_value())
			{
				glNamedFramebufferReadBuffer(m_id, GL_COLOR_ATTACHMENT0 + p_index.value());
			}
			else
			{
				glNamedFramebufferReadBuffer(m_id, GL_NONE);
			}
			if (m_readBuffer != p_index) m_snapshot.reset();
			m_readBuffer = p_index;
		});
	}

	std::pair<uint16_t, uint16_t> Framebuffer::GetSize() const
	{
		BAREGL_ASSERT(IsValid(), "Cannot get width of an invalid framebuffer");

		// Attachments are expected to be of the same size, other Validate() would fail.
		// We only need to return the size of the first attachment found, whether it's a texture of render buffer.
		for (auto& attachment : m_attachments)
		{
			if (const auto pval = std::get_if<std::shared_ptr<Texture>>(&attachment.second); pval && *pval)
			{
				return {
					(*pval)->GetDesc().width,
					(*pval)->GetDesc().height
				};
			}
			else if (const auto* pval = std::get_if<std::shared_ptr<Renderbuffer>>(&attachment.second); pval && *pval)
			{
				return {
					(*pval)->GetWidth(),
					(*pval)->GetHeight()
				};
			}
		}

		return { {}, {} }; // <-- not an emoji
	}

	void Framebuffer::BlitToBackBuffer(uint16_t p_backBufferWidth, uint16_t p_backBufferHeight) const
	{
		return ResourceExecutor::Invoke([&]() -> void {
			BAREGL_ASSERT(IsValid(), "Cannot blit an invalid framebuffer");

			auto [width, height] = GetSize();

			glBlitNamedFramebuffer(
				m_id, 0,
				0, 0, width, height,
				0, 0, p_backBufferWidth, p_backBufferHeight,
				GL_COLOR_BUFFER_BIT, GL_LINEAR
			);
		});
	}

	void Framebuffer::Snapshot::BlitToBackBuffer(uint16_t p_backBufferWidth, uint16_t p_backBufferHeight) const
	{
		return ResourceExecutor::Invoke([&]() -> void {
			if (!m_data) throw std::logic_error("Empty framebuffer snapshot");
			const auto [width, height] = m_data->size;
			glBlitNamedFramebuffer(
				GetID(), 0,
				0, 0, width, height,
				0, 0, p_backBufferWidth, p_backBufferHeight,
				GL_COLOR_BUFFER_BIT, GL_LINEAR
			);
		});
	}

	void Framebuffer::ReadPixels(
		uint32_t p_x,
		uint32_t p_y,
		uint32_t p_width,
		uint32_t p_height,
		types::EPixelDataFormat p_format,
		types::EPixelDataType p_type,
		void* p_data) const
	{
		ResourceExecutor::Invoke([&] {
			BAREGL_ASSERT(IsValid(), "Cannot read pixels from an invalid framebuffer");
			BAREGL_ASSERT(p_width > 0 && p_height > 0, "Invalid read size");

			Bind();
			glReadPixels(
				p_x, p_y,
				p_width,
				p_height,
				utils::EnumToValue<GLenum>(p_format),
				utils::EnumToValue<GLenum>(p_type),
				p_data
			);
			Unbind();
		});
	}

	const std::string& Framebuffer::GetDebugName() const
	{
		return m_debugName;
	}
}
