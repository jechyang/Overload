#pragma once
#include <OvCore/UI/Frame.h>
#include <OvRendering/Data/PreparedFrameData.h>
#include <map>
#include <tuple>

namespace OvCore::UI
{
    // Owned by the RenderFrontend. Lazily prepares one shader and one quad on the
    // resource owner; per-frame snapshots pin resources without retaining this object.
    class RenderResources final
    {
    public:
        struct Statistics { uint64_t geometryUploads=0, geometryReuses=0, uploadedBytes=0, vertices=0, indices=0, draws=0; };
        const Statistics& GetStatistics() const { return m_statistics; }
        size_t GeometryCacheSize() const { return m_geometry.size(); }
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
        using GeometryKey=std::tuple<uint64_t,uint64_t,int,uint64_t,bool,uint32_t,uint32_t>;
        struct GeometryResource {
            std::vector<ECS::Components::UIVertex> vertices;
            std::vector<uint32_t> indices;
            std::shared_ptr<const OvRendering::Resources::IMesh> geometry;
            uint64_t lastUse=0;
        };
        std::map<GeometryKey,GeometryResource> m_geometry;
        uint64_t m_cacheTick=0;
        Statistics m_statistics;
        struct FontPageResource { std::weak_ptr<const Fonts::AtlasPage> page; std::shared_ptr<baregl::Texture> texture; };
        std::map<std::tuple<uint64_t,uint32_t,uint64_t>,FontPageResource> m_fontPages;
        OvRendering::Resources::Shader* m_shader = nullptr;
        OvRendering::Data::Material m_material;
        std::shared_ptr<const OvRendering::Resources::IMesh> m_quad;
    };
}
