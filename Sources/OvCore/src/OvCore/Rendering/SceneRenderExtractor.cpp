#include <OvCore/Rendering/SceneRenderExtractor.h>
#include <OvCore/ECS/Components/CMaterialRenderer.h>
#include <OvCore/ECS/Components/CModelRenderer.h>
#include <OvCore/ECS/Components/CSkinnedMeshRenderer.h>
#include <OvCore/Rendering/SkinningUtils.h>
#include <OvRendering/Core/StaticOpaqueCommandRecorder.h>
#include <unordered_set>
#include <algorithm>
#include <stdexcept>

using namespace OvCore;
using namespace OvRendering;

std::shared_ptr<const Data::RenderSceneSnapshot> Rendering::SceneRenderExtractor::Extract(
    const StaticOpaqueRequest& p_request,
    const std::shared_ptr<baregl::Texture>& p_white, const std::shared_ptr<baregl::Texture>& p_black)
{
    const auto& components = p_request.scene.GetFastAccessComponents();
    const auto anyActive = [](const auto& list) {
        return std::any_of(list.begin(), list.end(), [](const auto* c) { return c->owner.IsActive(); });
    };
    if (anyActive(components.lights) || anyActive(components.reflectionProbes) || anyActive(components.postProcessStacks))
        throw std::invalid_argument("A1 static view does not own lighting, reflection or post-processing");
    auto builder = std::unique_ptr<Data::RenderSceneSnapshot>(new Data::RenderSceneSnapshot);
    auto& view = builder->m_view;
    const auto& camera = p_request.camera;
    view.id = p_request.viewId; view.sceneGeneration = p_request.sceneGeneration;
    view.width = p_request.width; view.height = p_request.height; view.elapsedTime = p_request.elapsedTime;
    view.position = camera.GetPosition();
    view.forward = camera.transform->GetWorldForward(); view.up = camera.transform->GetWorldUp();
    view.fov = camera.GetFov(); view.size = camera.GetSize();
    view.nearPlane = camera.GetNear(); view.farPlane = camera.GetFar(); view.projectionMode = camera.GetProjectionMode();
    view.cullGeometry = camera.HasFrustumGeometryCulling(); view.frustumOverride = p_request.frustumOverride;
    view.clearColor = camera.GetClearColorBuffer(); view.clearDepth = camera.GetClearDepthBuffer();
    view.clearStencil = camera.GetClearStencilBuffer(); view.clearValue = {camera.GetClearColor(), 1.0f};
    // Source material identities are used only within Extract, then replaced with
    // relative groups preserving the old address order. Never sort snapshot addresses.
    std::vector<uintptr_t> identities;
    uint64_t ordinal = 0;
    std::unordered_set<const baregl::ShaderProgram*> validatedPrograms;
    for (const auto* renderer : components.modelRenderers)
    {
        const auto& owner = renderer->owner;
        if (!owner.IsActive() || !renderer->GetModel()) continue;
        const auto* materials = owner.GetComponent<ECS::Components::CMaterialRenderer>();
        if (!materials || !Rendering::SatisfiesVisibility(materials->GetVisibilityFlags(), Rendering::EVisibilityFlags::GEOMETRY)) continue;
        if (Rendering::SkinningUtils::IsSkinningActive(owner.GetComponent<ECS::Components::CSkinnedMeshRenderer>()))
            throw std::invalid_argument("A1 static view does not own skinning");
        const auto& transform = owner.transform.GetFTransform();
        for (const auto* mesh : renderer->GetModel()->GetMeshes())
        {
            const auto collection = ordinal++;
            if (mesh->GetMaterialIndex() >= kMaxMaterialCount) continue;
            auto* material = materials->GetMaterials().at(mesh->GetMaterialIndex());
            if (!material || !material->IsValid()) continue;
            if (material->IsUserInterface() || material->IsBlendable())
                throw std::invalid_argument("A1 static view does not own Transparent or SceneUI");
            if (!material->SupportsProjectionMode(view.projectionMode) || material->GetGPUInstances() <= 0) continue;
            if (typeid(*mesh) != typeid(OvRendering::Resources::Mesh) || material->HasFeature("SKINNING") ||
                material->IsShadowReceiver() || material->IsReflectionReceiver())
                throw std::invalid_argument("A1 requires standard static geometry without shadow/reflection receivers");
            // Do not consume singleUse out of the old filtered draw order. A3 owns
            // that compatibility adapter; reject before PrepareSnapshot touches it.
            for (const auto& [name, property] : material->GetProperties())
                if (property.singleUse) throw std::invalid_argument("A1 does not yet own single-use material properties");
            auto& program = material->GetVariant().value();
            if (validatedPrograms.insert(&program).second)
                OvRendering::Core::StaticOpaqueCommandRecorder::ValidateProgram(program);
            Data::StaticMeshInput item;
            item.actorId = owner.GetGUID(); item.collectionIndex = collection;
            item.drawOrder = material->GetDrawOrder(); item.position = transform.GetWorldPosition();
            item.model = transform.GetWorldMatrix(); item.user = materials->GetUserMatrix();
            item.state = material->GenerateStateMask(); item.instances = static_cast<uint32_t>(material->GetGPUInstances());
            using Behaviour = ECS::Components::CModelRenderer::EFrustumBehaviour;
            std::optional<Geometry::BoundingSphere> bounds;
            switch (renderer->GetFrustumBehaviour())
            {
            case Behaviour::MESH_BOUNDS: bounds = mesh->GetBoundingSphere(); break;
            case Behaviour::DEPRECATED_MODEL_BOUNDS: bounds = renderer->GetModel()->GetBoundingSphere(); break;
            case Behaviour::CUSTOM_BOUNDS: bounds = renderer->GetCustomBoundingSphere(); break;
            default: break;
            }
            if (bounds)
            {
                // Preserve the existing max(non-absolute scale, 0) rule, including
                // negative scales and the legacy rotated/scaled sphere offset.
                const auto scale = transform.GetWorldScale();
                const float maximum = std::max(std::max(std::max(scale.x, scale.y), scale.z), 0.0f);
                bounds->position = item.position + OvMaths::FQuaternion::RotatePoint(bounds->position, transform.GetWorldRotation()) * maximum;
                bounds->radius *= maximum;
                item.worldBounds = bounds;
            }
            item.geometry = mesh->RetainGeometry();
            item.material = material->PrepareSnapshot(std::nullopt, std::nullopt, p_white, p_black, {}, {}, true, true);
            identities.push_back(reinterpret_cast<uintptr_t>(material));
            builder->m_meshes.push_back(std::move(item));
        }
    }
    auto groups = identities;
    std::sort(groups.begin(), groups.end()); groups.erase(std::unique(groups.begin(), groups.end()), groups.end());
    for (size_t i = 0; i < identities.size(); ++i)
        builder->m_meshes[i].materialGroup = std::lower_bound(groups.begin(), groups.end(), identities[i]) - groups.begin();
    return std::shared_ptr<const Data::RenderSceneSnapshot>(std::move(builder));
}
