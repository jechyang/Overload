#include <OvCore/ECS/Components/CModelRenderer.h>
#include <OvCore/ECS/Components/CMaterialRenderer.h>
#include <OvCore/ECS/Components/CReflectionProbe.h>
#include <OvCore/Rendering/FramebufferUtil.h>
#include <OvCore/Rendering/RenderFrontend.h>

// Inspect bindings through the public ExtractFrame path, using real lab models
// in a separate scene. Brightness values identify the chosen probe unambiguously.
void ValidateLabProbeSelection(OvCore::SceneSystem::Scene& lab) {
    using namespace OvRendering;
    using namespace OvCore::Rendering;
    using namespace OvCore::ECS::Components;
    auto* sourceFloor = lab.FindActorByName("Floor");
    auto* sourceSphere = lab.FindActorByName("Mirror sphere 6");
    if (!sourceFloor || !sourceSphere) throw std::runtime_error("Missing probe selection fixtures");
    const auto assets = std::filesystem::current_path() / "Data/Engine";
    OvCore::SceneSystem::Scene scene(assets, assets);
    auto& floor = scene.CreateActor("oversized floor");
    auto& sphere = scene.CreateActor("probe receiver");
    auto* material = sourceFloor->GetComponent<CMaterialRenderer>()->GetMaterialAtIndex(0);
    floor.AddComponent<CModelRenderer>().SetModel(sourceFloor->GetComponent<CModelRenderer>()->GetModel());
    floor.AddComponent<CMaterialRenderer>().FillWithMaterial(*material);
    floor.transform.SetLocalScale({48, .35f, 40});
    sphere.AddComponent<CModelRenderer>().SetModel(sourceSphere->GetComponent<CModelRenderer>()->GetModel());
    sphere.AddComponent<CMaterialRenderer>().FillWithMaterial(*material);
    sphere.transform.SetLocalPosition({6, 2.1f, 0});
    sphere.transform.SetLocalScale({2.1f, 2.1f, 2.1f});
    const auto makeProbe = [&](const char* name, float brightness, bool local) -> CReflectionProbe& {
        auto& probe = scene.CreateActor(name).AddComponent<CReflectionProbe>();
        probe.SetBrightness(brightness); probe.SetCubemapResolution(16);
        probe.SetRefreshMode(CReflectionProbe::ERefreshMode::ONCE);
        probe.SetCaptureSpeed(CReflectionProbe::ECaptureSpeed::SIX_FACES);
        probe.SetInfluencePolicy(local ? CReflectionProbe::EInfluencePolicy::LOCAL : CReflectionProbe::EInfluencePolicy::GLOBAL);
        return probe;
    };
    auto& global = makeProbe("global", 1, false);
    auto& local = makeProbe("local", 2, true);
    auto& boundary = makeProbe("boundary", 3, true);
    local.owner.transform.SetLocalPosition({6, 2.1f, 0}); local.SetInfluenceSize({10, 10, 10});
    boundary.owner.transform.SetLocalPosition({6, 2.1f, 0}); boundary.SetInfluenceSize({.25f, .25f, .25f});
    boundary.owner.SetActive(false);
    RenderFrontend frontend;
    Entities::Camera camera;
    baregl::Framebuffer target;
    FramebufferUtil::SetupFramebuffer(target, 32, 32, true, true, false);
    SceneFrameRequest request{{{scene, camera, 32, 32}}, target.RecordTarget(), *material, false, true};
    const auto check = [&](float expectedSphere, float expectedFloor, const char* failure) {
        for (auto* probe : {&global, &local, &boundary}) if (probe->owner.IsActive()) probe->RequestCapture(true);
        const auto frame = frontend.ExtractFrame(request);
        bool sawFloor = false, sawSphere = false;
        for (const auto& view : frame->GetViews()) {
            if (view->GetPipeline().stages != request.main.pipeline.stages) continue;
            for (const auto& mesh : view->GetMeshes()) {
                if (mesh.actorId == floor.GetGUID()) {
                    sawFloor = true;
                    if (mesh.reflection.brightness != expectedFloor) throw std::runtime_error(failure);
                }
                if (mesh.actorId == sphere.GetGUID()) {
                    sawSphere = true;
                    if (mesh.reflection.brightness != expectedSphere) throw std::runtime_error(failure);
                }
            }
        }
        if (!sawFloor || !sawSphere) throw std::runtime_error("Probe fixtures were not extracted");
    };
    local.owner.SetActive(false);
    check(1, 1, "Global probe did not cover the floor and sphere");
    local.owner.SetActive(true);
    check(2, 1, "Oversized floor did not fall back to global IBL");
    local.owner.SetActive(false); boundary.owner.SetActive(true);
    check(1, 1, "Partially contained mesh selected local IBL");
    global.owner.SetActive(false);
    check(0, 0, "Boundary without global probe did not use neutral fallback");
    global.owner.SetActive(true); local.owner.SetActive(true);
    check(2, 1, "Ineligible local probe hid an eligible one");
    boundary.owner.SetActive(false);
    local.owner.transform.SetLocalPosition({0, 2.1f, 0}); local.SetInfluenceSize({4, 8, 10});
    check(1, 1, "Unrotated narrow probe incorrectly covered the sphere");
    local.owner.transform.SetLocalRotation(OvMaths::FQuaternion(OvMaths::FVector3{0, 90, 0}));
    check(2, 1, "Rotated probe containment ignored its orientation");
    global.owner.SetActive(false); local.owner.SetActive(false);
    check(0, 0, "Missing probes did not retain neutral fallback");
    std::cout << "FrameGraphLab probe selection verification completed\n";
}
