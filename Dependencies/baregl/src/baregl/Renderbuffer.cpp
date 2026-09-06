/**
* @project: baregl
* @author: Adrien Givry
* @licence: MIT
*/

#include <baregl/ResourceExecutor.h>
#include <baregl/Renderbuffer.h>

#include <baregl/debug/Assert.h>
#include <baregl/debug/Event.h>
#include <baregl/detail/glad/glad.h>
#include <baregl/detail/Types.h>

namespace baregl
{
	Renderbuffer::Renderbuffer() : detail::NativeObject(ResourceKind::RENDERBUFFER)
	{
		ResourceExecutor::Invoke([&]() -> void {
			glCreateRenderbuffers(1, &m_id);
			AdoptNativeID();
			NOTIFY_RENDERBUFFER_CREATED;
		});
	}

	Renderbuffer::~Renderbuffer()
	{
		NOTIFY_RENDERBUFFER_DESTROYED;
	}

	void Renderbuffer::Bind() const
	{
		return ResourceExecutor::Invoke([&]() -> void {
			glBindRenderbuffer(GL_RENDERBUFFER, m_id);
		});
	}

	void Renderbuffer::Unbind() const
	{
		return ResourceExecutor::Invoke([&]() -> void {
			glBindRenderbuffer(GL_RENDERBUFFER, 0);
		});
	}

	void Renderbuffer::Allocate(uint16_t p_width, uint16_t p_height, types::EInternalFormat p_format)
	{
		return ResourceExecutor::Invoke([&]() -> void {
			m_width = p_width;
			m_height = p_height;
			m_format = p_format;

			glNamedRenderbufferStorage(m_id, utils::EnumToValue<GLenum>(m_format), m_width, m_height);

			m_allocated = true;
		});
	}

	bool Renderbuffer::IsValid() const
	{
		return m_allocated;
	}

	void Renderbuffer::Resize(uint16_t p_width, uint16_t p_height)
	{
		return ResourceExecutor::Invoke([&]() -> void {
			BAREGL_ASSERT(IsValid(), "Cannot resize a renderbuffer that has not been allocated");

			if (m_width == p_width && m_height == p_height)
			{
				return;
			}

			Allocate(p_width, p_height, m_format);
		});
	}


	uint16_t Renderbuffer::GetWidth() const
	{
		BAREGL_ASSERT(IsValid(), "Cannot get width of an invalid renderbuffer");
		return m_width;
	}

	uint16_t Renderbuffer::GetHeight() const
	{
		BAREGL_ASSERT(IsValid(), "Cannot get height of an invalid renderbuffer");
		return m_height;
	}
	std::shared_ptr<Renderbuffer> Renderbuffer::CreateResizedStorage(uint16_t p_width, uint16_t p_height) const
	{
		return ResourceExecutor::Invoke([&]() -> std::shared_ptr<Renderbuffer> {
			auto result = std::make_shared<Renderbuffer>();
			result->Allocate(p_width, p_height, m_format);
			return result;
		});
	}
}
