#include <OvCore/UI/RenderResources.h>
#include <OvCore/UI/Geometry.h>
#include <OvCore/UI/Fonts/Font.h>
#include <OvCore/Global/ServiceLocator.h>
#include <OvCore/ResourceManagement/MaterialManager.h>
#include <OvCore/Resources/Material.h>
#include <OvRendering/Resources/Loaders/ShaderLoader.h>
#include <OvRendering/Resources/Mesh.h>
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace OvRendering;
namespace OvCore::UI
{
    RenderResources::~RenderResources()
    {
        if (m_shader) OvRendering::Resources::Loaders::ShaderLoader::Destroy(m_shader);
    }
    void RenderResources::Initialize()
    {
        if (m_quad) return;
        if (!m_shader)
        {
            m_shader = OvRendering::Resources::Loaders::ShaderLoader::CreateFromSource(R"(
#version 430 core
layout(location=0) in vec3 position;
layout(location=1) in vec2 texCoords;
layout(location=2) in vec4 uiColor;
layout(location=3) in vec3 normal;
layout(location=4) in vec4 tangent;
layout(location=5) in vec2 uv1;
layout(location=6) in vec2 uv2;
layout(location=7) in vec2 uv3;
layout (std140, binding = 0) uniform EngineUBO {
    mat4 ubo_Model; mat4 ubo_View; mat4 ubo_Projection; vec3 ubo_ViewPos;
    float ubo_Time; mat4 ubo_UserMatrix;
};
uniform vec4 _Rect;
uniform vec4 _UVRect;
uniform vec4 _ClipRect;
uniform vec2 _Viewport;
uniform bool _ScreenSpace;
uniform bool _UseRect;
out vec2 uv;
out vec4 vertexColor;
out vec2 canvasPosition;
out vec2 sdfParameters;
void main() {
    vec3 canvas = _UseRect ? vec3(_Rect.xy + position.xy * _Rect.zw, position.z) : position;
    gl_Position = _ScreenSpace ? vec4(2.0 * canvas.x / _Viewport.x - 1.0,
        2.0 * canvas.y / _Viewport.y - 1.0, 0.0, 1.0) :
        ubo_Projection * ubo_View * ubo_Model * vec4(canvas, 1.0);
    uv = _UseRect ? _UVRect.xy + position.xy * _UVRect.zw : texCoords;
    vertexColor = uiColor;
    canvasPosition = canvas.xy; sdfParameters = uv1;
}
)", R"(
#version 430 core
in vec2 uv;
in vec4 vertexColor;
in vec2 canvasPosition;
in vec2 sdfParameters;
uniform bool _FontSDF;
uniform bool _AlphaClip;
uniform sampler2D _Texture;
uniform vec4 _Color;
uniform vec4 _ClipRect;
out vec4 color;
void main() {
    if (canvasPosition.x < _ClipRect.x || canvasPosition.y < _ClipRect.y ||
        canvasPosition.x >= _ClipRect.x + _ClipRect.z || canvasPosition.y >= _ClipRect.y + _ClipRect.w) discard;
    vec4 sampled = texture(_Texture, uv);
    if (_FontSDF) {
        float d = sampled.r * sdfParameters.x + sdfParameters.y;
        float width = max(fwidth(d), 0.0001);
        sampled = vec4(1.0,1.0,1.0,smoothstep(-width*0.5,width*0.5,d));
    }
    color = sampled * _Color * vertexColor;
    if (_AlphaClip && color.a < 0.001) discard;
}
)");
            if (!m_shader) throw std::runtime_error("UI shader creation failed");
            m_material.SetShader(m_shader);
            m_material.SetBlendable(true);
            m_material.SetDepthTest(false);
            m_material.SetDepthWriting(false);
            m_material.SetBackfaceCulling(false);
            m_material.SetFrontfaceCulling(false);
        }
        const std::array<ECS::Components::UIVertex,4> vertices{{
            {{0,0,0},{0,0}},{{1,0,0},{1,0}},{{1,1,0},{1,1}},{{0,1,0},{0,1}}}};
        constexpr auto indices = std::to_array<uint32_t>({0,1,2,0,2,3});
        m_quad = std::make_shared<Geometry>(vertices,indices);
    }

    std::shared_ptr<const Data::RenderSceneSnapshot> RenderResources::Extract(const Frame& frame,
        uint64_t viewId, const std::shared_ptr<baregl::Texture>& white, const std::shared_ptr<baregl::Texture>& black,
        Data::RenderStage stage, const Data::RenderViewInput* sceneView)
    {
        if (stage != Data::RenderStage::OverlayUI && stage != Data::RenderStage::SceneUI)
            throw std::invalid_argument("Invalid UI render stage");
        if (stage == Data::RenderStage::SceneUI && !sceneView)
            throw std::invalid_argument("Scene UI requires a render view");
        if (!IsFinite(frame.size) || frame.size.x < 1 || frame.size.y < 1 ||
            frame.size.x > std::numeric_limits<uint16_t>::max() || frame.size.y > std::numeric_limits<uint16_t>::max())
            throw std::invalid_argument("Invalid UI framebuffer extent");
        Initialize();
        std::erase_if(m_fontPages,[](const auto& entry){return entry.second.page.expired();});
        auto result = std::unique_ptr<Data::RenderSceneSnapshot>(new Data::RenderSceneSnapshot);
        result->m_pipeline.stages = {stage};
        auto& view = result->m_view;
        if (sceneView) view = *sceneView;
        view.id = viewId; view.sceneGeneration = frame.sceneGeneration;
        view.width = static_cast<uint16_t>(frame.size.x); view.height = static_cast<uint16_t>(frame.size.y);
        view.cullGeometry = view.cullLights = false;
        view.clearColor = view.clearDepth = view.clearStencil = false;
        if (stage == Data::RenderStage::OverlayUI)
        {
            view.forward = {0,0,-1}; view.up = {0,1,0};
            view.matrices = Data::ViewMatrices{OvMaths::FMatrix4::Identity, OvMaths::FMatrix4::Identity};
        }
        m_material.SetDepthTest(stage == Data::RenderStage::SceneUI);
        struct DrawRef { bool mesh; size_t index; int order; uint64_t sequence; };
        std::vector<DrawRef> draws;
        for (size_t i = 0; i < frame.images.size(); ++i)
        {
            const auto& image = frame.images[i];
            const bool overlay = image.renderMode == ECS::Components::CCanvas::RenderMode::ScreenSpaceOverlay;
            if ((stage == Data::RenderStage::OverlayUI) != overlay) continue;
            if (image.color.w <= 0 || image.visibleRect.Empty() || image.rect.Empty()) continue;
            draws.push_back({false, i, image.sortingOrder, image.sequence});
        }
        for (size_t i = 0; i < frame.meshes.size(); ++i)
        {
            const auto& graphic = frame.meshes[i];
            const bool overlay = graphic.renderMode == ECS::Components::CCanvas::RenderMode::ScreenSpaceOverlay;
            if ((stage == Data::RenderStage::OverlayUI) != overlay || graphic.clip.Empty() ||
                graphic.vertices.empty() || graphic.indices.empty()) continue;
            draws.push_back({true, i, graphic.sortingOrder, graphic.sequence});
        }
        std::stable_sort(draws.begin(), draws.end(), [](const DrawRef& a, const DrawRef& b) {
            return a.order != b.order ? a.order < b.order : a.sequence < b.sequence;
        });
        for (const auto draw : draws)
        {
            if (draw.mesh)
            {
                const auto& graphic = frame.meshes[draw.index];
                const bool overlay = graphic.renderMode == ECS::Components::CCanvas::RenderMode::ScreenSpaceOverlay;
                auto batches = graphic.materialSlots;
                if (batches.empty()) batches.push_back({graphic.materialPath,graphic.texture,nullptr,0,static_cast<uint32_t>(graphic.indices.size())});
                for (const auto& batch : batches) {
                if (!batch.indexCount) continue;
                if (batch.firstIndex > graphic.indices.size() || batch.indexCount > graphic.indices.size()-batch.firstIndex)
                    throw std::invalid_argument("Invalid immutable UI batch");
                const auto geometry = std::make_shared<Geometry>(graphic.vertices,
                    std::span<const uint32_t>(graphic.indices).subspan(batch.firstIndex,batch.indexCount));
                auto texture = batch.texture ? batch.texture : white;
                if (batch.fontPage) {
                    const auto& page = *batch.fontPage;
                    if (!page.size || page.size > 16384 || page.pixels.size()!=size_t(page.size)*page.size)
                        throw std::invalid_argument("Invalid immutable font atlas page");
                    const auto key=std::tuple{page.generation,page.index,page.revision};
                    auto retained=m_fontPages[key].texture;
                    if (!retained) {
                        retained=std::make_shared<baregl::Texture>(baregl::types::ETextureType::TEXTURE_2D);
                        retained->Allocate({.width=page.size,.height=page.size,
                            .minFilter=baregl::types::ETextureFilteringMode::LINEAR,.magFilter=baregl::types::ETextureFilteringMode::LINEAR,
                            .internalFormat=baregl::types::EInternalFormat::R8,.useMipMaps=false});
                        retained->Upload(page.pixels.data(),baregl::types::EFormat::RED,baregl::types::EPixelDataType::UNSIGNED_BYTE);
                        m_fontPages[key]={batch.fontPage,retained};
                    }
                    texture=std::move(retained);
                }
                const std::array textures{texture};
                Data::Material* material = &m_material;
                if (!batch.materialPath.empty())
                    if (auto* manager = OvCore::Global::ServiceLocator::TryGet<OvCore::ResourceManagement::MaterialManager>())
                        if (auto* custom = manager->GetResource(batch.materialPath, false); custom && custom->IsValid()) material = custom;
                Data::Material::PropertyOverrides properties{
                    {"_ScreenSpace", overlay}, {"_UseRect", false}, {"_Viewport", frame.size},
                    {"_Rect", OvMaths::FVector4{}}, {"_UVRect", OvMaths::FVector4{0,0,1,1}},
                    {"_ClipRect", OvMaths::FVector4{graphic.clip.min.x, graphic.clip.min.y, graphic.clip.size.x, graphic.clip.size.y}},
                    {"_Color", OvMaths::FVector4{1,1,1,1}}, {"_Texture", texture.get()},
                    {"_FontSDF", batch.fontPage != nullptr}, {"_AlphaClip", graphic.stencil.replace}
                };
                Data::MeshInput mesh;
                mesh.actorId = graphic.actorGuid; mesh.collectionIndex = result->m_meshes.size();
                mesh.stage = mesh.sourceStage = stage; mesh.drawOrder = graphic.sortingOrder; mesh.transparent = true;
                mesh.model = overlay ? OvMaths::FMatrix4::Identity : graphic.canvasToWorld;
                mesh.user = OvMaths::FMatrix4::Identity; mesh.geometry = geometry;
                mesh.state = material->GenerateStateMask();
                Data::PipelineState uiState{};
                uiState.depthTest = !overlay; uiState.depthWriting=false; uiState.culling=false;
                uiState.scissorTest=false; uiState.blending=true; uiState.colorWriting.mask=graphic.stencil.colorWrite?0xf:0;
                uiState.blendingSrcFactor=baregl::types::EBlendingFactor::SRC_ALPHA;
                uiState.blendingDestFactor=baregl::types::EBlendingFactor::ONE_MINUS_SRC_ALPHA;
                uiState.blendingEquation=baregl::types::EBlendingEquation::FUNC_ADD;
                uiState.stencilTest=graphic.stencil.enabled;
                uiState.stencilFuncRef=graphic.stencil.reference;uiState.stencilFuncMask=graphic.stencil.readMask;uiState.stencilWriteMask=graphic.stencil.writeMask;
                uiState.stencilFuncOp=baregl::types::EComparaisonAlgorithm::EQUAL;
                uiState.stencilOpFail=uiState.depthOpFail=baregl::types::EOperation::KEEP;
                uiState.bothOpFail=graphic.stencil.replace?baregl::types::EOperation::REPLACE:baregl::types::EOperation::KEEP;
                mesh.uiPipelineState=uiState;
                mesh.material = material->PrepareSnapshot(std::nullopt, std::nullopt, white, black, textures, {}, true, true, false, properties);
                result->m_meshes.push_back(std::move(mesh));
                }
                continue;
            }
            const auto& image = frame.images[draw.index];
            const bool overlay = image.renderMode == ECS::Components::CCanvas::RenderMode::ScreenSpaceOverlay;
            const auto& r = image.visibleRect;
            m_material.SetProperty("_ScreenSpace", overlay);
            m_material.SetProperty("_UseRect", true);
            m_material.SetProperty("_FontSDF", false);
            m_material.SetProperty("_AlphaClip", false);
            m_material.SetProperty("_Viewport", frame.size);
            m_material.SetProperty("_Rect", OvMaths::FVector4{r.min.x, r.min.y, r.size.x, r.size.y});
            m_material.SetProperty("_ClipRect", OvMaths::FVector4{r.min.x, r.min.y, r.size.x, r.size.y});
            m_material.SetProperty("_UVRect", OvMaths::FVector4{
                (r.min.x-image.rect.min.x)/image.rect.size.x, (r.min.y-image.rect.min.y)/image.rect.size.y,
                r.size.x/image.rect.size.x, r.size.y/image.rect.size.y});
            m_material.SetProperty("_Color", image.color);
            const auto texture = image.texture ? image.texture : white;
            m_material.SetProperty("_Texture", texture.get());
            const std::array textures{texture};
            Data::MeshInput mesh;
            mesh.actorId = image.actorGuid; mesh.collectionIndex = result->m_meshes.size();
            mesh.stage = mesh.sourceStage = stage; mesh.drawOrder = image.sortingOrder; mesh.transparent = true;
            mesh.model = overlay ? OvMaths::FMatrix4::Identity : image.canvasToWorld;
            mesh.user = OvMaths::FMatrix4::Identity;
            if (!overlay)
            {
                const auto center = image.canvasToWorld * OvMaths::FVector4{r.min.x + r.size.x * 0.5f, r.min.y + r.size.y * 0.5f, 0, 1};
                mesh.position = {center.x, center.y, center.z};
            }
            mesh.geometry = m_quad; mesh.state = m_material.GenerateStateMask();
            mesh.material = m_material.PrepareSnapshot(std::nullopt, std::nullopt, white, black, textures, {}, true, true, false);
            result->m_meshes.push_back(std::move(mesh));
        }
        return std::shared_ptr<const Data::RenderSceneSnapshot>(std::move(result));
    }
}
