#pragma once
#include <OvCore/Rendering/FramebufferUtil.h>
namespace OvCore::Rendering
{
    // Viewport resource owner. baregl dispatches creation/resize to ResourceExecutor;
    // snapshots retain the selected attachment generation across later resizes.
    class ViewportSurface final
    {
    public:
        ViewportSurface(const std::string& name,uint32_t width,uint32_t height) : m_target(name)
        { FramebufferUtil::SetupFramebuffer(m_target,width,height,true,true,false); }
        void Resize(uint32_t width,uint32_t height) { m_target.Resize(width,height); }
        baregl::Framebuffer::Snapshot GetTarget() const { return m_target.RecordTarget(); }
        std::shared_ptr<baregl::Texture> GetColor() const
        { return m_target.RetainTextureAttachment(baregl::types::EFramebufferAttachment::COLOR); }
        baregl::Framebuffer& GetFramebuffer() { return m_target; }
    private:
        baregl::Framebuffer m_target;
    };
}
