/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <vector>

#include <baregl/types/ETextureFilteringMode.h>
#include <baregl/Texture.h>

namespace OvRendering::Resources
{
	namespace Loaders { class TextureLoader; }

	/**
	* Texture saved on the disk
	*/
	class Texture
	{
		friend class Loaders::TextureLoader;

	public:
		/**
		* Returns the associated baregl::Texture instance
		*/
		baregl::Texture& GetTexture();

		// Pins this GPU generation across asset reload/destruction. All use and
		// final release remain on the GL owner, before its context is destroyed.
		std::shared_ptr<baregl::Texture> RetainTexture() const;
		uint64_t GetVersion() const;
		float SampleAlpha(float u, float v) const;
		void SetAlphaData(const void* rgba, uint32_t width, uint32_t height, bool hdr);

	private:
		Texture(const std::string p_path, std::unique_ptr<baregl::Texture>&& p_texture);
		~Texture() = default;
		void SetTexture(std::unique_ptr<baregl::Texture>&& p_texture);

	public:
		const std::string path;

	private:
		std::shared_ptr<baregl::Texture> m_texture;
		uint64_t m_version = 0;
		uint32_t m_alphaWidth=0,m_alphaHeight=0;
		std::vector<uint8_t> m_alpha;
	};
}
