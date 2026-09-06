/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <OvDebug/Assertion.h>
#include <OvRendering/Resources/Texture.h>

baregl::Texture& OvRendering::Resources::Texture::GetTexture()
{
	OVASSERT(m_texture != nullptr, "Trying to access a null Texture");
	return *m_texture;
}

OvRendering::Resources::Texture::Texture(const std::string p_path, std::unique_ptr<baregl::Texture>&& p_texture) : path(p_path)
{
	SetTexture(std::move(p_texture));
}

void OvRendering::Resources::Texture::SetTexture(std::unique_ptr<baregl::Texture>&& p_texture)
{
	OVASSERT(p_texture != nullptr, "Cannot assign an invalid texture!");
	m_texture = std::move(p_texture);
	++m_version;
}

std::shared_ptr<baregl::Texture> OvRendering::Resources::Texture::RetainTexture() const
{
	return m_texture;
}

uint64_t OvRendering::Resources::Texture::GetVersion() const
{
	return m_version;
}
