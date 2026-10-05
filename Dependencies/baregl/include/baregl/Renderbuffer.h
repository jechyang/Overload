/**
* @project: baregl
* @author: Adrien Givry
* @licence: MIT
*/

#pragma once

#include <memory>
#include <baregl/detail/NativeObject.h>
#include <baregl/types/EInternalFormat.h>

namespace baregl
{
	/**
	* Represents a renderbuffer, used to store render data for the graphics context to use.
	*/
	class Renderbuffer final : public detail::NativeObject
	{
	public:
		/**
		* Create the render buffer
		*/
		Renderbuffer();

		/**
		* Destructor
		*/
		~Renderbuffer();

		/**
		* Bind the framebuffer
		*/
		void Bind() const;

		/**
		* Unbind the framebuffer
		*/
		void Unbind() const;

		/**
		* Uploads the buffer configuration to the GPU
		* @param p_width
		* @param p_height
		* @param p_format
		*/
		void Allocate(uint16_t p_width, uint16_t p_height, types::EInternalFormat p_format);

		/**
		* Returns true if the render buffer has been properly allocated
		*/
		bool IsValid() const;

		/**
		* Resizes the render buffer
		* @param p_width
		* @param p_height
		*/
		void Resize(uint16_t p_width, uint16_t p_height);

		std::shared_ptr<Renderbuffer> CreateResizedStorage(uint16_t p_width, uint16_t p_height) const;

		/**
		* Returns the width of the framebuffer
		*/
		uint16_t GetWidth() const;

		/**
		* Returns the width of the framebuffer
		*/
		uint16_t GetHeight() const;
        types::EInternalFormat GetFormat() const { return m_format; }

	private:
		uint16_t m_width = 0;
		uint16_t m_height = 0;
		types::EInternalFormat m_format = types::EInternalFormat::RGBA;
		bool m_allocated = false;
	};
}
