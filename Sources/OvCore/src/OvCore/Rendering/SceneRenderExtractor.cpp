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
#include <limits>
#include <algorithm>
#include <stdexcept>

using namespace OvCore;
using namespace OvRendering;

std::shared_ptr<const Data::RenderSceneSnapshot> Rendering::SceneRenderExtractor::ExtractScene(
    const SceneRenderRequest& p_request,
    const std::shared_ptr<baregl::Texture>& p_white, const std::shared_ptr<baregl::Texture>& p_black, Core::MeshCommandRecorder::ProgramCache* programs)
{
    return ExtractImpl(p_request, p_white, p_black, p_request.pipeline.stages == std::vector{Data::RenderStage::Picking}, {}, nullptr, nullptr, programs);
}

std::shared_ptr<const Data::RenderSceneSnapshot> Rendering::SceneRenderExtractor::ExtractImpl(
    const SceneRenderRequest& p_request,
    const std::shared_ptr<baregl::Texture>& p_white, const std::shared_ptr<baregl::Texture>& p_black, bool p_planned, std::span<const Data::ReflectionProbeInput> p_probes, MaterialCache* p_cache, const Data::RenderViewInput* p_lightView, Core::MeshCommandRecorder::ProgramCache* programs)
{
    p_request.pipeline.Validate();
    const bool picking = p_request.pipeline.stages == std::vector{Data::RenderStage::Picking};
    if (picking && !p_request.pickingFallback) throw std::invalid_argument("Picking requires its explicit fallback provider");
    const bool shadow = p_request.pipeline.stages == std::vector{Data::RenderStage::ShadowCaster};
    const bool capture = p_request.pipeline.stages == std::vector{Data::RenderStage::ReflectionCapture};
    for (auto stage : p_request.pipeline.stages)
        if ((stage == Data::RenderStage::ShadowCaster || stage == Data::RenderStage::ReflectionCapture || stage == Data::RenderStage::Picking) && !shadow && !capture && !picking)
            throw std::invalid_argument("Auxiliary stages require an exclusive view");
    const auto& components = p_request.scene.GetFastAccessComponents();
    const auto anyActive = [](const auto& list) {
        return std::any_of(list.begin(), list.end(), [](const auto* c) { return c->owner.IsActive(); });
    };
    if ((!p_planned && anyActive(components.reflectionProbes)) || (!p_planned && anyActive(components.postProcessStacks)))
        throw std::invalid_argument("ExtractScene: reflection/post-processing require ExtractFrame");
    auto builder = std::unique_ptr<Data::RenderSceneSnapshot>(new Data::RenderSceneSnapshot);
    builder->m_pipeline = p_request.pipeline;
    auto& view = builder->m_view;
    view = CaptureView(p_request);
    if (p_request.preserveOutput) view.clearColor = view.clearDepth = view.clearStencil = false;
    if (shadow || capture || picking) { view.clearColor = true; view.clearDepth = true; view.clearStencil = true; view.clearValue = {}; }
    if (shadow)
    {
        view.cullGeometry = false; view.frustumOverride.reset();
        if (p_planned) view.matrices = Data::ViewMatrices{p_request.camera.GetViewMatrix(),p_request.camera.GetProjectionMatrix()};
    }
    if (p_lightView)
    {
        view.cullLights = p_lightView->cullLights;
        if (view.cullLights)
        {
            view.lightFrustumOverride = p_lightView->frustumOverride;
            if (!view.lightFrustumOverride)
            {
                const auto lightView = OvRendering::Core::ViewPreparer::BuildView(*p_lightView);
                view.lightFrustumOverride.emplace();
                view.lightFrustumOverride->CalculateFrustum(lightView.projection * lightView.view);
            }
        }
    }
    // Source material identities are used only within Extract, then replaced with
    // relative groups preserving the old address order. Never sort snapshot addresses.
    for (auto* component : components.lights)
    {
        if (!component->owner.IsActive()) continue;
        const auto& light = component->GetData();
        if (light.castShadows && !p_request.shadow.map && !p_planned && !shadow && !capture)
            throw std::invalid_argument("ExtractScene: shadow receiver requires an imported map or ExtractFrame");
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
    for (const auto& probe : p_probes) if (probe.receiver.map) receiverTextures.push_back(probe.receiver.map);
    uint64_t ordinal = 0;
    Core::MeshCommandRecorder::ProgramCache localPrograms;
    auto& programCache=programs?*programs:localPrograms;
    MaterialCache localMaterialCache;
    auto& materialCache = p_cache ? *p_cache : localMaterialCache;
    for (const auto* renderer : components.modelRenderers)
    {
        const auto& owner = renderer->owner;
        if (!owner.IsActive() || !renderer->GetModel()) continue;
        const auto* materials = owner.GetComponent<ECS::Components::CMaterialRenderer>();
        if (!materials || !Rendering::SatisfiesVisibility(materials->GetVisibilityFlags(), shadow ? Rendering::EVisibilityFlags::SHADOW : capture ? Rendering::EVisibilityFlags::REFLECTION : Rendering::EVisibilityFlags::GEOMETRY)) continue;
        const auto* skin = owner.GetComponent<ECS::Components::CSkinnedMeshRenderer>();
        const bool hasSkin = Rendering::SkinningUtils::IsSkinningActive(skin);
        const auto& transform = owner.transform.GetFTransform();
        // One shared palette per actor/pose; all meshes and later views reuse it.
        std::shared_ptr<const std::vector<OvMaths::FMatrix4>> sharedPalette;
        for (const auto* mesh : renderer->GetModel()->GetMeshes())
        {
            const auto collection = ordinal++;
            auto* sourceMaterial = mesh->GetMaterialIndex() < kMaxMaterialCount ?
                materials->GetMaterials().at(mesh->GetMaterialIndex()) : nullptr;
            auto* material = p_request.overrideMaterial ? p_request.overrideMaterial :
                (sourceMaterial ? sourceMaterial : p_request.fallbackMaterial);
            if (!material || !material->IsValid()) continue;
            const bool originalSkinning = material->SupportsFeature("SKINNING");
            if (shadow)
            {
                if (!sourceMaterial || !sourceMaterial->IsValid() || !sourceMaterial->IsShadowCaster()) continue;
                material = sourceMaterial->HasPass("SHADOW_PASS") ? sourceMaterial : p_request.shadowFallback;
                if (!material || !material->IsValid()) throw std::invalid_argument("Missing shadow fallback material");
            }
            if (capture && (material->IsUserInterface() || !material->IsCapturedByReflectionProbes())) continue;
            const auto category = material->IsUserInterface() ? Data::RenderStage::SceneUI :
                material->IsBlendable() ? Data::RenderStage::Transparent : Data::RenderStage::Opaque;
            auto* sortingMaterial = material;
            const auto stage = picking ? Data::RenderStage::Picking : shadow ? Data::RenderStage::ShadowCaster : capture ? Data::RenderStage::ReflectionCapture : category;
            if (!shadow && material != p_request.fallbackMaterial &&
                ((category == Data::RenderStage::SceneUI && !p_request.includeUI) ||
                 (category == Data::RenderStage::Transparent && !p_request.includeTransparent) ||
                 (category == Data::RenderStage::Opaque && !p_request.includeOpaque))) continue;
            if (std::find(p_request.pipeline.stages.begin(), p_request.pipeline.stages.end(), stage) == p_request.pipeline.stages.end()) continue;
            const auto description = Data::DescribeStage(stage);
            if ((!shadow && !material->SupportsProjectionMode(view.projectionMode)) || material->GetGPUInstances() <= 0) continue;
            const bool pickingSkinned = picking && hasSkin && mesh->HasSkinningData() && originalSkinning;
            if (picking)
            {
                material = !pickingSkinned && material->HasPass("PICKING_PASS") ? material : p_request.pickingFallback;
                if (!material || !material->IsValid()) throw std::invalid_argument("Picking requires its fallback material");
                if (!material->SupportsProjectionMode(view.projectionMode) || material->GetGPUInstances() <= 0) continue;
            }
            if (typeid(*mesh) != typeid(OvRendering::Resources::Mesh))
                throw std::invalid_argument("Scene requires standard retained geometry");
            for (const auto& [name, property] : material->GetProperties())
                if (property.singleUse)
                {
                    resolveSingleUse = true;
                }
            std::optional<Data::FeatureSet> features;
            const bool hasPalette = hasSkin && (shadow || mesh->HasSkinningData());
            if (hasPalette && (!picking || pickingSkinned) && (!shadow || originalSkinning) && material->SupportsFeature("SKINNING"))
                features = Rendering::SkinningUtils::BuildFeatureSet(&material->GetFeatures());
            const auto featureRef = features ? OvTools::Utils::OptRef<const Data::FeatureSet>(*features) : std::nullopt;
            const auto program=material->GetShader()->RetainVariant(description.technique,features?*features:material->GetFeatures());
            const bool programSkinning=programCache.Validate(program);
            Data::MeshInput item;
            item.stage = stage; item.sourceStage = category;
            item.transparent = material->IsBlendable();
            item.skinning.enabled = programSkinning || (features && features->contains("SKINNING")) || material->HasFeature("SKINNING");
            if (item.skinning.enabled && hasPalette)
            {
                item.skinning.poseVersion = skin->GetPoseVersion();
                if (!sharedPalette)
                {
                    const auto key = std::pair{static_cast<const void*>(skin), item.skinning.poseVersion};
                    const auto found = materialCache.palettes.find(key);
                    if (found != materialCache.palettes.end()) sharedPalette = found->second;
                    else
                    {
                        const auto& matrices = skin->GetBoneMatricesTransposed();
                        sharedPalette = std::make_shared<const std::vector<OvMaths::FMatrix4>>(matrices.begin(), matrices.end());
                        materialCache.palettes.emplace(key, sharedPalette);
                    }
                }
                item.skinning.matrices = sharedPalette;
            }
            auto reflection = p_request.reflection;
            item.actorId = owner.GetGUID(); item.collectionIndex = collection;
            item.drawOrder = sortingMaterial->GetDrawOrder(); item.position = transform.GetWorldPosition();
            item.model = transform.GetWorldMatrix(); item.user = materials->GetUserMatrix();
            if (description.reflectionReceiver && material->IsReflectionReceiver())
            {
                const Data::ReflectionProbeInput* bestLocal = nullptr;
                const Data::ReflectionProbeInput* bestGlobal = nullptr;
                float localDistance = std::numeric_limits<float>::max(), globalDistance = localDistance;
                const auto bounds = mesh->GetBoundingSphere();
                const auto center = item.model * OvMaths::FVector4(bounds.position, 1.0f);
                const auto scale = std::max({
                    OvMaths::FVector3::Length({item.model.data[0],item.model.data[1],item.model.data[2]}),
                    OvMaths::FVector3::Length({item.model.data[4],item.model.data[5],item.model.data[6]}),
                    OvMaths::FVector3::Length({item.model.data[8],item.model.data[9],item.model.data[10]})});
                for (const auto& probe : p_probes)
                {
                    const auto distance = OvMaths::FVector3::Distance(item.position, probe.position);
                    auto& best = probe.local ? bestLocal : bestGlobal;
                    auto& bestDistance = probe.local ? localDistance : globalDistance;
                    if ((!probe.local && bestLocal) || distance > bestDistance) continue;
                    if (probe.local)
                    {
                        const auto local = probe.inverseRotation * (OvMaths::FVector3{center.x,center.y,center.z} - probe.position);
                        const auto extent = probe.halfExtents;
                        const auto closest = OvMaths::FVector3{std::max(-extent.x,std::min(extent.x,local.x)), std::max(-extent.y,std::min(extent.y,local.y)), std::max(-extent.z,std::min(extent.z,local.z))};
                        const auto d = local - closest;
                        const auto radius = bounds.radius * scale;
                        if (d.x*d.x+d.y*d.y+d.z*d.z > radius*radius) continue;
                    }
                    best = &probe; bestDistance = distance;
                }
                if (const auto* probe = bestLocal ? bestLocal : bestGlobal) reflection = probe->receiver;
                item.reflection = reflection.constants;
            }
            item.state = material->GenerateStateMask(); item.instances = static_cast<uint32_t>(material->GetGPUInstances());
            if (picking) item.state.frontfaceCulling = item.state.backfaceCulling = false;
            if (shadow)
            {
                item.state.blendable = false; item.state.depthTest = true;
                item.state.colorWriting = false; item.state.depthWriting = true;
                item.state.frontfaceCulling = false; item.state.backfaceCulling = false;
            }
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
            if (picking && material->HasProperty("_PickingColor"))
            {
                const auto id = static_cast<uint32_t>(owner.GetID());
                overrides["_PickingColor"] = OvMaths::FVector4{float(id & 255)/255.0f,float((id>>8)&255)/255.0f,float((id>>16)&255)/255.0f,1};
            }
            if (description.shadowReceiver && material->IsShadowReceiver() && p_request.shadow.map &&
                material->HasProperty("_ShadowMap") && material->HasProperty("_LightSpaceMatrix"))
            {
                overrides.emplace("_ShadowMap", p_request.shadow.map.get());
                overrides.emplace("_LightSpaceMatrix", p_request.shadow.lightSpace);
            }
            if (description.reflectionReceiver && material->IsReflectionReceiver() && material->HasProperty("_EnvironmentMap"))
                overrides.emplace("_EnvironmentMap", reflection.map.get());
            item.material = material->PrepareSnapshot(description.technique, featureRef, p_white, p_black, receiverTextures, {}, true, true, false, overrides);
            overridesToResolve.push_back(std::move(overrides));
            materialsToResolve.push_back(material);
            featuresToResolve.push_back(std::move(features));
            identities.push_back(reinterpret_cast<uintptr_t>(sortingMaterial));
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
        // Runs only when a single-use property was observed; otherwise the first
        // non-consuming snapshots already hold the full values.
        const auto preparedView = OvRendering::Core::ViewPreparer::BuildView(view);
        auto& previousSignature = materialCache.signature;
        auto& previous = materialCache.snapshot;
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

Data::RenderViewInput Rendering::SceneRenderExtractor::CaptureView(const SceneViewRequest& p_request)
{
    Data::RenderViewInput view;
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
    return view;
}
