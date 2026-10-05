#include <OvRendering/Data/RenderPlanResource.h>
#include <algorithm>
#include <stdexcept>
#include <sstream>
using namespace OvRendering::Data;
namespace
{
    unsigned PixelBytes(baregl::types::EInternalFormat format)
    {
        using enum baregl::types::EInternalFormat;
        switch(format) {
        case R8: case R8I: case R8UI: return 1;
        case R16F: case R16: case RG8: return 2;
        case RGB8: case SRGB8: return 3;
        case RGBA: case RGBA8: case SRGB8_ALPHA8: case R32F: case RG16F: case DEPTH_COMPONENT: case DEPTH_STENCIL: return 4;
        case RGB16F: return 6;
        case RG32F: case RGBA16F: case RGBA16: return 8;
        case RGB32F: return 12;
        case RGBA32F: return 16;
        default: throw std::invalid_argument("RenderPlan: unsupported resource format");
        }
    }
}
bool PlanResourceDesc::Compatible(const PlanResourceDesc& other) const
{
    const auto& a=texture; const auto& b=other.texture;
    const auto mutableEqual=(!a.mutableDesc&&!b.mutableDesc) || (a.mutableDesc&&b.mutableDesc&&
        a.mutableDesc->format==b.mutableDesc->format&&a.mutableDesc->type==b.mutableDesc->type);
    return kind==other.kind&&aspect==other.aspect&&mips==other.mips&&layers==other.layers&&samples==other.samples&&
        sampleable==other.sampleable&&attachment==other.attachment&&copyable==other.copyable&&
        a.width==b.width&&a.height==b.height&&a.internalFormat==b.internalFormat&&a.useMipMaps==b.useMipMaps&&
        a.minFilter==b.minFilter&&a.magFilter==b.magFilter&&a.horizontalWrap==b.horizontalWrap&&a.verticalWrap==b.verticalWrap&&mutableEqual;
}
uint64_t PlanResourceDesc::EstimatedBytes() const
{
    uint64_t bytes=0; auto w=texture.width,h=texture.height;
    for(unsigned mip=0;mip<mips;++mip) { bytes+=uint64_t(w)*h*PixelBytes(texture.internalFormat)*layers*samples; w=std::max(1u,w/2);h=std::max(1u,h/2); }
    return bytes;
}
void PlanResourceDesc::Validate() const
{
    const auto reject=[](bool valid,const char* error) { if(!valid)throw std::invalid_argument(std::string("RenderPlan descriptor: ")+error); };
    reject(texture.width&&texture.height&&texture.width<=65535&&texture.height<=65535,"invalid extent");
    reject(kind!=PlanResourceKind::Backbuffer||(!sampleable&&mips==1&&layers==1&&!texture.useMipMaps),"invalid backbuffer description");
    reject(samples==1&&mips&&layers,"unsupported samples or empty subresources");
    unsigned maxMips=1; for(auto size=std::max(texture.width,texture.height);size>1;size/=2) ++maxMips;
    reject(mips<=maxMips&&mips==(texture.useMipMaps?maxMips:1),"mip count differs from GL allocation");
    reject(layers==(kind==PlanResourceKind::TextureCube?6:1),"unsupported layers");
    reject(kind!=PlanResourceKind::TextureCube||(!texture.mutableDesc&&texture.width==texture.height),"invalid cubemap descriptor");
    reject(!texture.mutableDesc||!texture.mutableDesc->data,"transient/import description cannot borrow upload bytes");
    reject(kind!=PlanResourceKind::Renderbuffer||(!sampleable&&mips==1),"renderbuffer cannot be sampled or mipmapped");
    using enum baregl::types::EInternalFormat;
    const auto expected=texture.internalFormat==DEPTH_COMPONENT?ResourceAspect::Depth:
        texture.internalFormat==DEPTH_STENCIL?ResourceAspect::DepthStencil:ResourceAspect::Color;
    reject(aspect==expected,"aspect differs from format");
    (void)PixelBytes(texture.internalFormat);
}
PlanResourceDesc PlanResourceDesc::From(const baregl::Texture::Snapshot& storage)
{
    PlanResourceDesc result; result.texture=storage.GetDesc();
    result.kind=storage.GetType()==baregl::types::ETextureType::TEXTURE_CUBE?PlanResourceKind::TextureCube:PlanResourceKind::Texture2D;
    result.layers=result.kind==PlanResourceKind::TextureCube?6:1;
    if(result.texture.useMipMaps)for(auto size=std::max(result.texture.width,result.texture.height);size>1;size/=2)++result.mips;
    using enum baregl::types::EInternalFormat;
    result.aspect=result.texture.internalFormat==DEPTH_COMPONENT?ResourceAspect::Depth:
        result.texture.internalFormat==DEPTH_STENCIL?ResourceAspect::DepthStencil:ResourceAspect::Color;
    return result;
}
std::string CompiledRenderPlan::Dump() const
{
    std::ostringstream output; output<<"scope="<<scope<<" estimatedPeakBytes="<<estimatedPeakBytes<<'\n';
    for(size_t i=0;i<passNames.size();++i)output<<"pass "<<i<<" "<<passNames[i]<<'\n';
    for(size_t i=0;i<lifetimes.size();++i)output<<"resource "<<i<<" "<<resourceNames[i]<<" first="<<lifetimes[i].first<<" last="<<lifetimes[i].last<<" bytes="<<lifetimes[i].estimatedBytes<<" exported="<<lifetimes[i].exported<<" version="<<finalVersions[i]<<'\n';
    for(const auto& edge:dependencies)output<<"dependency "<<edge.before<<" -> "<<edge.after<<" resource="<<edge.resource<<" kind="<<int(edge.kind)<<'\n';
    return output.str();
}
