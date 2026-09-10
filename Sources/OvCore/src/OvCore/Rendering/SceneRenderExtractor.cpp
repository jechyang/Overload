#include <OvCore/Rendering/SceneRenderExtractor.h>
#include <OvCore/ECS/Components/CMaterialRenderer.h>
#include <OvCore/ECS/Components/CModelRenderer.h>
#include <OvCore/ECS/Components/CSkinnedMeshRenderer.h>
#include <OvCore/Rendering/SkinningUtils.h>
#include <OvRendering/Core/MeshCommandRecorder.h>
#include <OvCore/ECS/Components/CLight.h>
#include <OvRendering/Core/ViewPreparer.h>
#include <unordered_map>
#include <cmath>
#include <algorithm>
#include <stdexcept>

using namespace OvCore;
using namespace OvRendering;

std::shared_ptr<const Data::RenderSceneSnapshot> Rendering::SceneRenderExtractor::Extract(
    const StaticOpaqueRequest& p_request,
    const std::shared_ptr<baregl::Texture>& p_white, const std::shared_ptr<baregl::Texture>& p_black)
{
    SceneRenderRequest request{p_request};
    request.pipeline.stages = {Data::RenderStage::Opaque};
    return ExtractImpl(request, true, p_white, p_black);
}

std::shared_ptr<const Data::RenderSceneSnapshot> Rendering::SceneRenderExtractor::ExtractScene(
    const SceneRenderRequest& p_request,
    const std::shared_ptr<baregl::Texture>& p_white, const std::shared_ptr<baregl::Texture>& p_black)
{
    return ExtractImpl(p_request, false, p_white, p_black);
}

std::shared_ptr<const Data::RenderSceneSnapshot> Rendering::SceneRenderExtractor::ExtractImpl(
    const SceneRenderRequest& p_request, bool p_opaqueOnly,
    const std::shared_ptr<baregl::Texture>& p_white, const std::shared_ptr<baregl::Texture>& p_black)
{
    p_request.pipeline.Validate();
    const auto& components = p_request.scene.GetFastAccessComponents();
    const auto anyActive = [](const auto& list) {
        return std::any_of(list.begin(), list.end(), [](const auto* c) { return c->owner.IsActive(); });
    };
    if ((p_opaqueOnly && anyActive(components.lights)) || anyActive(components.reflectionProbes) || anyActive(components.postProcessStacks))
        throw std::invalid_argument("Static scene view does not own lighting, reflection or post-processing");
    auto builder = std::unique_ptr<Data::RenderSceneSnapshot>(new Data::RenderSceneSnapshot);
    builder->m_pipeline = p_request.pipeline;
    auto& view = builder->m_view;
    const auto& camera = p_request.camera;
    view.id = p_request.viewId; view.sceneGeneration = p_request.sceneGeneration;
    view.width = p_request.width; view.height = p_request.height; view.elapsedTime = p_request.elapsedTime;
    view.position = camera.GetPosition();
    view.forward = camera.transform->GetWorldForward(); view.up = camera.transform->GetWorldUp();
    view.fov = camera.GetFov(); view.size = camera.GetSize();
    view.nearPlane = camera.GetNear(); view.farPlane = camera.GetFar(); view.projectionMode = camera.GetProjectionMode();
    view.cullLights = camera.HasFrustumLightCulling();
    view.cullGeometry = camera.HasFrustumGeometryCulling(); view.frustumOverride = p_request.frustumOverride;
    view.clearColor = camera.GetClearColorBuffer(); view.clearDepth = camera.GetClearDepthBuffer();
    view.clearStencil = camera.GetClearStencilBuffer(); view.clearValue = {camera.GetClearColor(), 1.0f};
    // Source material identities are used only within Extract, then replaced with
    // relative groups preserving the old address order. Never sort snapshot addresses.
    for (auto* component : components.lights)
    {
        if (!component->owner.IsActive()) continue;
        const auto& light = component->GetData();
        if (light.castShadows && !p_request.shadow.map)
            throw std::invalid_argument("Shadow producer requires an explicit imported receiver map until A4");
        builder->m_lights.push_back({light.transform->GetWorldPosition(), light.CalculateEffectRange(), light.GenerateMatrix()});
    }
    std::vector<uintptr_t> identities;
    std::vector<Data::Material*> materialsToResolve;
    std::vector<std::optional<Data::FeatureSet>> featuresToResolve;
    bool resolveSingleUse = false;
    std::vector<Data::Material::PropertyOverrides> overridesToResolve;
    std::vector<std::shared_ptr<baregl::Texture>> receiverTextures;
    if (p_request.shadow.map) receiverTextures.push_back(p_request.shadow.map);
    if (p_request.reflection.map) receiverTextures.push_back(p_request.reflection.map);
    uint64_t ordinal = 0;
    std::unordered_map<const baregl::ShaderProgram*, bool> validatedPrograms;
    for (const auto* renderer : components.modelRenderers)
    {
        const auto& owner = renderer->owner;
        if (!owner.IsActive() || !renderer->GetModel()) continue;
        const auto* materials = owner.GetComponent<ECS::Components::CMaterialRenderer>();
        if (!materials || !Rendering::SatisfiesVisibility(materials->GetVisibilityFlags(), Rendering::EVisibilityFlags::GEOMETRY)) continue;
        const auto* skin = owner.GetComponent<ECS::Components::CSkinnedMeshRenderer>();
        const bool hasSkin = Rendering::SkinningUtils::IsSkinningActive(skin);
        if (p_opaqueOnly && hasSkin) throw std::invalid_argument("Static opaque adapter does not own skinning");
        const auto& transform = owner.transform.GetFTransform();
        for (const auto* mesh : renderer->GetModel()->GetMeshes())
        {
            const auto collection = ordinal++;
            auto* sourceMaterial = mesh->GetMaterialIndex() < kMaxMaterialCount ?
                materials->GetMaterials().at(mesh->GetMaterialIndex()) : nullptr;
            auto* material = p_request.overrideMaterial ? p_request.overrideMaterial :
                (sourceMaterial ? sourceMaterial : p_request.fallbackMaterial);
            if (!material || !material->IsValid()) continue;
            if (p_opaqueOnly && (material->IsUserInterface() || material->IsBlendable()))
                throw std::invalid_argument("Static scene view does not own Transparent or SceneUI");
            const auto stage = material->IsUserInterface() ? Data::RenderStage::SceneUI :
                material->IsBlendable() ? Data::RenderStage::Transparent : Data::RenderStage::Opaque;
            if (material != p_request.fallbackMaterial &&
                ((stage == Data::RenderStage::SceneUI && !p_request.includeUI) ||
                 (stage == Data::RenderStage::Transparent && !p_request.includeTransparent) ||
                 (stage == Data::RenderStage::Opaque && !p_request.includeOpaque))) continue;
            if (std::find(p_request.pipeline.stages.begin(), p_request.pipeline.stages.end(), stage) == p_request.pipeline.stages.end()) continue;
            const auto description = Data::DescribeStage(stage);
            if (!material->SupportsProjectionMode(view.projectionMode) || material->GetGPUInstances() <= 0) continue;
            if (typeid(*mesh) != typeid(OvRendering::Resources::Mesh))
                throw std::invalid_argument("Scene requires standard retained geometry");
            if (p_opaqueOnly && (material->HasFeature("SKINNING") ||
                (description.shadowReceiver && material->IsShadowReceiver()) ||
                (description.reflectionReceiver && material->IsReflectionReceiver())))
                throw std::invalid_argument("Static opaque adapter does not own feature bindings");
            for (const auto& [name, property] : material->GetProperties())
                if (property.singleUse)
                {
                    if (p_opaqueOnly) throw std::invalid_argument("Static opaque adapter does not own single-use properties");
                    resolveSingleUse = true;
                }
            std::optional<Data::FeatureSet> features;
            const bool hasPalette = hasSkin && mesh->HasSkinningData();
            if (hasPalette && material->SupportsFeature("SKINNING"))
                features = Rendering::SkinningUtils::BuildFeatureSet(&material->GetFeatures());
            const auto featureRef = features ? OvTools::Utils::OptRef<const Data::FeatureSet>(*features) : std::nullopt;
            auto& program = material->GetVariant(description.technique, featureRef).value();
            if (!validatedPrograms.contains(&program))
                validatedPrograms.emplace(&program, OvRendering::Core::MeshCommandRecorder::ValidateProgram(program));
            Data::MeshInput item;
            item.stage = stage;
            item.skinning.enabled = validatedPrograms.at(&program) || (features && features->contains("SKINNING")) || material->HasFeature("SKINNING");
            if (item.skinning.enabled && hasPalette)
            {
                item.skinning.poseVersion = skin->GetPoseVersion();
                const auto& matrices = skin->GetBoneMatricesTransposed();
                item.skinning.matrices.assign(matrices.begin(), matrices.end());
            }
            if (description.reflectionReceiver && material->IsReflectionReceiver())
                item.reflection = p_request.reflection.constants;
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
                if (hasPalette) bounds->radius *= skin->GetMeshBoundsScale();
                bounds->radius *= maximum;
                item.worldBounds = bounds;
            }
            item.geometry = mesh->RetainGeometry();
            // Capture without consumption; the ordered compatibility boundary below
            // alone owns source singleUse consumption. Overrides are separate owned values.
            Data::Material::PropertyOverrides overrides;
            if (description.shadowReceiver && material->IsShadowReceiver() && p_request.shadow.map &&
                material->HasProperty("_ShadowMap") && material->HasProperty("_LightSpaceMatrix"))
            {
                overrides.emplace("_ShadowMap", p_request.shadow.map.get());
                overrides.emplace("_LightSpaceMatrix", p_request.shadow.lightSpace);
            }
            if (description.reflectionReceiver && material->IsReflectionReceiver() && material->HasProperty("_EnvironmentMap"))
                overrides.emplace("_EnvironmentMap", p_request.reflection.map.get());
            item.material = material->PrepareSnapshot(description.technique, featureRef, p_white, p_black, receiverTextures, {}, true, true, false, overrides);
            overridesToResolve.push_back(std::move(overrides));
            materialsToResolve.push_back(material);
            featuresToResolve.push_back(std::move(features));
            identities.push_back(reinterpret_cast<uintptr_t>(material));
            builder->m_meshes.push_back(std::move(item));
        }
    }
    auto groups = identities;
    std::sort(groups.begin(), groups.end()); groups.erase(std::unique(groups.begin(), groups.end()), groups.end());
    for (size_t i = 0; i < identities.size(); ++i)
        builder->m_meshes[i].materialGroup = std::lower_bound(groups.begin(), groups.end(), identities[i]) - groups.begin();
    if (resolveSingleUse)
    {
        // Compatibility only: source consumption is ordered on Main before workers.
        // Reuse the same culling and sorting rules as Prepare; no Material leaves Extract.
        const auto preparedView = OvRendering::Core::ViewPreparer::BuildView(view);
        std::optional<Data::MaterialSignatureSet> previousSignature;
        Data::Material::PreparedSnapshot previous;
        for (const auto stage : builder->m_pipeline.stages)
        {
            std::vector<OvRendering::Core::VisibleStaticMesh> ordered;
            for (size_t i = 0; i < builder->m_meshes.size(); ++i)
            {
                const auto& mesh = builder->m_meshes[i];
                if (mesh.stage != stage) continue;
                if (preparedView.frustum && mesh.worldBounds)
                {
                    const auto& b = *mesh.worldBounds;
                    if (!preparedView.frustum->SphereInFrustum(b.position.x, b.position.y, b.position.z, b.radius)) continue;
                }
                ordered.push_back({i, OvMaths::FVector3::Distance(mesh.position, view.position)});
            }
            OvRendering::Core::ViewPreparer::SortStage(ordered, builder->m_meshes, stage);
            for (const auto& draw : ordered)
            {
                auto& mesh = builder->m_meshes[draw.sourceIndex];
                auto* material = materialsToResolve[draw.sourceIndex];
                const auto& features = featuresToResolve[draw.sourceIndex];
                const auto featureRef = features ? OvTools::Utils::OptRef<const Data::FeatureSet>(*features) : std::nullopt;
                const auto description = Data::DescribeStage(stage);
                const auto binding = material->PrepareBinding(description.technique, featureRef, p_white.get(), p_black.get(), previousSignature);
                const bool stable = !previousSignature || binding.signature.stablePropertySignature != previousSignature->stablePropertySignature;
                const bool once = !overridesToResolve[draw.sourceIndex].empty() || !previousSignature || binding.signature.singleUsePropertySignature != previousSignature->singleUsePropertySignature;
                mesh.material = material->PrepareSnapshot(description.technique, featureRef, p_white, p_black, receiverTextures, previous, stable, once, true, overridesToResolve[draw.sourceIndex]);
                previous = mesh.material;
                previousSignature = binding.signature;
            }
        }
    }
    return std::shared_ptr<const Data::RenderSceneSnapshot>(std::move(builder));
}
