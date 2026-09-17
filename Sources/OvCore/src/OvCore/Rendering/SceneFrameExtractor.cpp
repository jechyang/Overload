#include <OvCore/Rendering/SceneFrameExtractor.h>
#include <OvCore/ECS/Components/CLight.h>
#include <OvCore/ECS/Components/CPostProcessStack.h>
#include <OvCore/ECS/Components/CReflectionProbe.h>
#include <OvCore/ECS/Components/CUI.h>
#include <OvCore/UI/RenderResources.h>
#include <algorithm>
#include <cstring>
#include <limits>
using namespace OvCore;
using namespace OvCore::Rendering;
using namespace OvRendering;

std::unique_ptr<SceneFrame> SceneFrameExtractor::Extract(const SceneFrameRequest& request,
    const std::shared_ptr<baregl::Texture>& white, const std::shared_ptr<baregl::Texture>& black,
    Core::MeshCommandRecorder::ProgramCache* programs, UI::RenderResources* uiResources)
{
    auto frame = std::unique_ptr<SceneFrame>(new SceneFrame);
    auto main = request.main;
    const auto lightingView = SceneRenderExtractor::CaptureView(main);
    const auto generation = main.sceneGeneration;
    uint64_t nextView = main.viewId;
    const auto viewId = [&] { if (nextView == std::numeric_limits<uint64_t>::max()) throw std::invalid_argument("View identity overflow"); return ++nextView; };
    const auto addResource = [&](const std::shared_ptr<baregl::Texture>& texture, uint16_t width, uint16_t height,
        uint8_t mask, uint8_t available, Data::ResourceOrigin origin) {
        const auto storage = texture->RetainStorage();
        auto& resources = frame->m_plan.resources;
        const auto found = std::find_if(resources.begin(), resources.end(), [&](const auto& r) { return r.storage.GetID() == storage.GetID(); });
        if (found != resources.end()) return size_t(found - resources.begin());
        resources.push_back({resources.size()+1, generation, storage, width, height, mask, available, origin});
        return resources.size()-1;
    };
    SceneRenderExtractor::MaterialCache materialCache;
    const auto addView = [&](const SceneRenderRequest& input, std::optional<baregl::Framebuffer::Snapshot> target,
        std::string name, std::vector<Data::ResourceUse> reads, std::vector<Data::ResourceUse> writes,
        std::span<const Data::ReflectionProbeInput> probes = {}) -> Data::RenderPlanPass& {
        auto scene = SceneRenderExtractor::ExtractImpl(input, white, black, true, probes, &materialCache, &lightingView, programs);
        const auto index = frame->m_views.size();
        frame->m_views.push_back(std::move(scene));
        frame->m_plan.passes.push_back({std::move(name),index,input.viewId,input.pipeline.stages,std::move(target),std::move(reads),std::move(writes),{}});
        return frame->m_plan.passes.back();
    };
    std::vector<Data::ResourceUse> mainReads;
    // Preserve production of every directional caster and selection of the first.
    bool receiverSelected = false;
    for (auto* component : main.scene.GetFastAccessComponents().lights)
    {
        auto& light = component->GetData();
        if (!component->owner.IsActive() || !light.castShadows || light.type != Settings::ELightType::DIRECTIONAL) continue;
        if (!request.generateShadows) continue;
        // This is the owner resource-preparation boundary; no worker calls it.
        auto camera = main.camera;
        light.PrepareForShadowRendering(camera.GetPosition());
        const auto& shadowCamera = *light.shadowCamera;
        const auto target = light.shadowBuffer->RecordTarget();
        const auto [width,height] = target.GetSize();
        const auto texture = light.shadowBuffer->RetainTextureAttachment(baregl::types::EFramebufferAttachment::DEPTH);
        const auto resource = addResource(texture,width,height,1,0,Data::ResourceOrigin::CurrentFrame);
        SceneRenderRequest shadow{{main.scene,shadowCamera,width,height,viewId(),generation,main.elapsedTime}};
        shadow.pipeline.stages = {Data::RenderStage::ShadowCaster}; shadow.shadowFallback = &request.shadowFallback;
        addView(shadow,target,"Shadows",{},{{resource,1}});
        if (!receiverSelected)
        {
            main.shadow = {texture,*light.lightSpaceMatrix};
            mainReads.push_back({resource,1}); receiverSelected = true;
        }
    }
    if (!receiverSelected && main.shadow.map)
    {
        const auto& desc = main.shadow.map->GetDesc();
        mainReads.push_back({addResource(main.shadow.map,static_cast<uint16_t>(desc.width),static_cast<uint16_t>(desc.height),1,1,Data::ResourceOrigin::Imported),1});
    }
    // Explicit neutral result when generation is disabled and no import exists.
    if (!receiverSelected && !main.shadow.map) main.shadow = {white,OvMaths::FMatrix4::Identity};
    const OvMaths::FVector3 rotations[] = {{0,-90,180},{0,90,180},{90,0,180},{-90,0,180},{0,0,180},{0,-180,180}};
    std::vector<Data::ReflectionProbeInput> probes;
    for (auto* probe : main.scene.GetFastAccessComponents().reflectionProbes)
    {
        if (!probe->owner.IsActive()) continue;
        if (frame->m_commands.UseHistory(probe->m_captureHistory))
        {
            probe->m_framebuffers.Reset(); probe->m_cubemapIterator.Reset();
            probe->m_isAnyCubemapComplete = false; probe->RequestCapture(true);
        }
        const auto firstFace = probe->m_captureFaceIndex;
        const bool hadComplete = probe->m_isAnyCubemapComplete;
        const auto faces = request.captureReflections ? probe->_GetCaptureFaceIndices() : std::vector<uint32_t>{};
        if (!faces.empty())
        {
            auto& target = probe->_GetTargetFramebuffer();
            const auto [width,height] = target.GetSize();
            const auto texture = target.RetainTextureAttachment(baregl::types::EFramebufferAttachment::COLOR);
            const auto previous = firstFace ? uint8_t((1u << firstFace)-1) : uint8_t{0};
            const auto resource = addResource(texture,width,height,63,previous,
                previous ? Data::ResourceOrigin::History : Data::ResourceOrigin::CurrentFrame);
            Entities::Camera camera;
            camera.SetPosition(probe->owner.transform.GetWorldPosition()+probe->GetCapturePosition()); camera.SetFov(90);
            for (const auto face : faces)
            {
                camera.SetRotation(OvMaths::FQuaternion{rotations[face]});
                SceneRenderRequest capture{{main.scene,camera,width,height,viewId(),generation,main.elapsedTime}};
                capture.pipeline.stages = {Data::RenderStage::ReflectionCapture}; capture.includeUI = false;
                auto& pass = addView(capture,target.RecordTarget().WithDrawBuffer(face),"ReflectionCapture",{},{{resource,uint8_t(1u<<face)}});
                if (face == 5)
                {
                    pass.generateMipmaps = texture;
                    probe->_NotifyCubemapComplete(false);
                }
            }
        }
        Data::ReflectionProbeInput input;
        input.position = probe->owner.transform.GetWorldPosition()+probe->GetCapturePosition();
        input.halfExtents = probe->GetInfluenceSize();
        input.inverseRotation = OvMaths::FMatrix4::Transpose(OvMaths::FMatrix4::Rotation(probe->owner.transform.GetWorldRotation()));
        input.local = probe->GetInfluencePolicy() == ECS::Components::CReflectionProbe::EInfluencePolicy::LOCAL;
        if (probe->m_isAnyCubemapComplete)
        {
            input.receiver.map = probe->GetCubemap();
            const auto bytes = probe->CaptureUniformData();
            std::memcpy(&input.receiver.constants,bytes.data(),bytes.size());
            const auto size = static_cast<uint16_t>(probe->GetCubemapResolution());
            const auto resource = addResource(input.receiver.map,size,size,63,hadComplete ? 63 : 0,
                hadComplete ? Data::ResourceOrigin::History : Data::ResourceOrigin::CurrentFrame);
            mainReads.push_back({resource,63});
        }
        // ON_DEMAND with no completed capture has explicit neutral fallback.
        probes.push_back(std::move(input));
    }
    if (main.reflection.map)
    {
        const auto& desc = main.reflection.map->GetDesc();
        mainReads.push_back({addResource(main.reflection.map,static_cast<uint16_t>(desc.width),static_cast<uint16_t>(desc.height),63,63,Data::ResourceOrigin::Imported),63});
    }
    std::optional<PostProcess::PostProcessSnapshot> settings;
    for (const auto* stack : main.scene.GetFastAccessComponents().postProcessStacks)
        if (stack && stack->owner.IsActive()) { settings = PostProcess::PostProcessSnapshot::Capture(stack->GetStack()); break; }
    if (settings)
    {
        if (!request.postProcess || !request.postProcessTarget || !request.target)
            throw std::invalid_argument("Post-processing requires a viewport owner and output");
        if (request.postProcessTarget->RecordTarget() != *request.target)
            throw std::invalid_argument("Post-processing output mismatch");
        auto geometry = main;
        geometry.includeUI = false;
        std::erase(geometry.pipeline.stages,Data::RenderStage::SceneUI);
        addView(geometry,request.target,"MainView",mainReads,{},probes);
        frame->m_plan.passes.back().postProcessAfter = true;
        frame->m_postProcessAfterView = frame->m_views.size()-1;
        frame->m_postProcessCommands = std::make_unique<Data::RenderCommandList>(request.postProcess->Record(*settings,*request.postProcessTarget,request.postProcessState));
        frame->m_info=request.postProcess->GetFrameInfo();
        if (main.includeUI && std::find(main.pipeline.stages.begin(),main.pipeline.stages.end(),Data::RenderStage::SceneUI) != main.pipeline.stages.end())
        {
            auto ui = main; ui.viewId = viewId(); ui.includeOpaque = ui.includeTransparent = false;
            ui.pipeline.stages = {Data::RenderStage::SceneUI}; ui.preserveOutput = true;
            materialCache = {}; // Final blit changes the material binding before UI.
            addView(ui,request.target,"SceneUI",mainReads,{},probes);
        }
    }
    else addView(main,request.target,"MainView",std::move(mainReads),{},probes);
    if (main.includeUI && std::find(main.pipeline.stages.begin(), main.pipeline.stages.end(), Data::RenderStage::SceneUI) != main.pipeline.stages.end())
    {
        const auto ui = UI::ExtractFrame(main.scene, {float(main.width), float(main.height)}, generation, &main.camera);
        if (!ui.images.empty() || !ui.meshes.empty())
        {
            UI::RenderResources localResources;
            std::vector<Data::ResourceUse> reads;
            const auto addTextureRead = [&](const std::shared_ptr<baregl::Texture>& texture) {
                if (!texture) return;
                const auto& desc = texture->GetDesc();
                const auto resource = addResource(texture, static_cast<uint16_t>(desc.width),
                    static_cast<uint16_t>(desc.height), 1, 1, Data::ResourceOrigin::Imported);
                if (std::none_of(reads.begin(), reads.end(), [&](const auto& r) { return r.resource == resource; }))
                    reads.push_back({resource, 1});
            };
            for (const auto& image : ui.images)
                if (image.texture && image.color.w > 0)
                    addTextureRead(image.texture);
            for (const auto& mesh : ui.meshes)
                addTextureRead(mesh.texture);
            const auto hasSceneUI = std::any_of(ui.images.begin(), ui.images.end(), [](const auto& image) {
                    return image.renderMode != ECS::Components::CCanvas::RenderMode::ScreenSpaceOverlay;
                }) || std::any_of(ui.meshes.begin(), ui.meshes.end(), [](const auto& mesh) {
                    return mesh.renderMode != ECS::Components::CCanvas::RenderMode::ScreenSpaceOverlay;
                });
            const auto hasOverlay = std::any_of(ui.images.begin(), ui.images.end(), [](const auto& image) {
                    return image.renderMode == ECS::Components::CCanvas::RenderMode::ScreenSpaceOverlay;
                }) || std::any_of(ui.meshes.begin(), ui.meshes.end(), [](const auto& mesh) {
                    return mesh.renderMode == ECS::Components::CCanvas::RenderMode::ScreenSpaceOverlay;
                });
            auto& resources = uiResources ? *uiResources : localResources;
            if (hasSceneUI)
            {
                auto sceneUI = resources.Extract(ui, viewId(), white, black, Data::RenderStage::SceneUI, &lightingView);
                frame->m_plan.passes.push_back({"CanvasUI", frame->m_views.size(), sceneUI->GetView().id,
                    {Data::RenderStage::SceneUI}, request.target, reads, {}, {}});
                frame->m_views.push_back(std::move(sceneUI));
            }
            if (hasOverlay)
            {
                auto overlay = resources.Extract(ui, viewId(), white, black);
                frame->m_plan.passes.push_back({"OverlayUI", frame->m_views.size(), overlay->GetView().id,
                    {Data::RenderStage::OverlayUI}, request.target, std::move(reads), {}, {}});
                frame->m_views.push_back(std::move(overlay));
            }
        }
    }
    frame->m_plan.Validate(frame->m_views);
    return frame;
}
