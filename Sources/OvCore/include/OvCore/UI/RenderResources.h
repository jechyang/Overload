#pragma once
#include <OvCore/UI/Frame.h>
#include <OvRendering/Data/PreparedFrameData.h>

namespace OvCore::UI
{
    // Owned by the RenderFrontend. Lazily prepares one shader and one quad on the
    // resource owner; per-frame snapshots pin resources without retaining this object.
    class RenderResources final
    {
    public:
        RenderResources() = default;
        ~RenderResources();
        RenderResources(const RenderResources&) = delete;
        RenderResources& operator=(const RenderResources&) = delete;
        std::shared_ptr<const OvRendering::Data::RenderSceneSnapshot> Extract(
            const Frame& frame, uint64_t viewId,
            const std::shared_ptr<baregl::Texture>& white, const std::shared_ptr<baregl::Texture>& black,
            OvRendering::Data::RenderStage stage = OvRendering::Data::RenderStage::OverlayUI,
            const OvRendering::Data::RenderViewInput* sceneView = nullptr);
    private:
        void Initialize();
        OvRendering::Resources::Shader* m_shader = nullptr;
        OvRendering::Data::Material m_material;
        std::shared_ptr<const OvRendering::Resources::IMesh> m_quad;
    };
}
