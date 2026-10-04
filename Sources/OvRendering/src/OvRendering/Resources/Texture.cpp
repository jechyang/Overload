/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <OvDebug/Assertion.h>
#include <algorithm>
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

void OvRendering::Resources::Texture::SetAlphaData(const void* rgba,uint32_t width,uint32_t height,bool hdr)
{
    if (!rgba || !width || !height) { m_alpha.clear();m_alphaWidth=m_alphaHeight=0;return; }
    m_alphaWidth=width;m_alphaHeight=height;m_alpha.resize(size_t(width)*height);
    for(size_t i=0;i<m_alpha.size();++i)m_alpha[i]=hdr?uint8_t(std::clamp(static_cast<const float*>(rgba)[i*4+3],0.0f,1.0f)*255):static_cast<const uint8_t*>(rgba)[i*4+3];
    // Fully opaque textures need no retained CPU pixel plane.
    if (std::all_of(m_alpha.begin(),m_alpha.end(),[](uint8_t value){return value==255;})) std::vector<uint8_t>{}.swap(m_alpha);
}
float OvRendering::Resources::Texture::SampleAlpha(float u,float v) const
{
    if(m_alpha.empty()||!m_alphaWidth||!m_alphaHeight)return 1;
    const float x=std::clamp(u,0.0f,1.0f)*float(m_alphaWidth-1),y=std::clamp(v,0.0f,1.0f)*float(m_alphaHeight-1);
    const uint32_t x0=uint32_t(x),y0=uint32_t(y),x1=std::min(x0+1,m_alphaWidth-1),y1=std::min(y0+1,m_alphaHeight-1);
    const auto a=[&](uint32_t px,uint32_t py){return float(m_alpha[size_t(py)*m_alphaWidth+px])/255;};
    const auto lerp=[](float a,float b,float t){return a+(b-a)*t;};
    return lerp(lerp(a(x0,y0),a(x1,y0),x-x0),lerp(a(x0,y1),a(x1,y1),x-x0),y-y0);
}
