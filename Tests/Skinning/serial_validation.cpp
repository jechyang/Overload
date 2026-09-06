#include <OvCore/ECS/Components/CDirectionalLight.h>
#include <OvCore/ECS/Components/CReflectionProbe.h>
#include <OvCore/ECS/Components/CPostProcessStack.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <new>
#include <crtdbg.h>
#include <stdexcept>
#include <thread>
#include <vector>
#include <tracy/Tracy.hpp>
#include <baregl/detail/glad/glad.h>
#include <tracy/TracyOpenGL.hpp>
#include <OvCore/ECS/Components/CSkinnedMeshRenderer.h>
#include <OvCore/ECS/Components/CMaterialRenderer.h>
#include <OvCore/Global/ServiceLocator.h>
#include <OvCore/Rendering/FramebufferUtil.h>
#include <OvCore/Rendering/SceneRenderer.h>
#include <OvCore/ResourceManagement/ModelManager.h>
#include <OvCore/ResourceManagement/ShaderManager.h>
#include <OvCore/SceneSystem/Scene.h>
#include <OvMaths/FTransform.h>
#include <OvTools/Jobs/JobSystem.h>
#include <OvRendering/Context/Driver.h>
#include <OvWindowing/Context/Device.h>
#include <OvWindowing/Window.h>

// Fail only allocations on this test thread, never Tracy's background thread.
thread_local int allocationFailureCountdown = -1;
thread_local std::size_t allocationFailureSize = 0;
void* operator new(std::size_t size)
{
    if (size == allocationFailureSize && allocationFailureCountdown >= 0 && allocationFailureCountdown-- == 0) throw std::bad_alloc();
    if (auto* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }

using namespace OvCore::ECS::Components;
using namespace OvRendering::Animation;
using namespace OvMaths;
using Skin = CSkinnedMeshRenderer;
namespace OvRendering::Resources
{
    struct SkinningModelFixture : Model
    {
        explicit SkinningModelFixture(const std::string& name) : Model(name) {}
    };
}
using Model = OvRendering::Resources::SkinningModelFixture;
using Scene = OvCore::SceneSystem::Scene;
using OvCore::Global::ServiceLocator;

void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class T> void Write(std::ostream& out, const T& value) { out.write(reinterpret_cast<const char*>(&value), sizeof(value)); }
void WritePose(std::ostream& out, const std::vector<FMatrix4>& pose)
{
    Write(out, pose.size());
    for (const auto& matrix : pose) for (float value : matrix.data) Write(out, value);
}
void Snapshot(std::ostream& out, const Skin& s)
{
    WritePose(out, s.m_localPose); WritePose(out, s.m_globalPose);
    WritePose(out, s.m_boneMatrices); WritePose(out, s.m_boneMatricesTransposed);
    Write(out, s.GetPoseVersion()); Write(out, s.IsPlaying()); Write(out, s.IsLooping());
    Write(out, s.GetTime()); Write(out, s.m_currentTimeTicks); Write(out, s.GetPlaybackSpeed());
    Write(out, s.m_poseEvaluationAccumulator); Write(out, s.m_manualPoseOverride);
    Write(out, s.HasSkinningData()); Write(out, s.GetMeshBoundsScale());
    Write(out, s.GetActiveAnimationIndex().value_or(UINT32_MAX));
    Write(out, s.IsAnimationSourceCompatible());
    for (uint32_t i = 0; i < s.GetBoneCount(); ++i)
    {
        for (const auto value : {s.GetBoneLocalPosition(i), s.GetBoneLocalScale(i), s.GetBoneGlobalPosition(i), s.GetBoneGlobalScale(i)})
        {
            Write(out, value.has_value());
            if (value) { Write(out, value->x); Write(out, value->y); Write(out, value->z); }
        }
        for (const auto value : {s.GetBoneLocalRotation(i), s.GetBoneGlobalRotation(i)})
        {
            Write(out, value.has_value());
            if (value) { Write(out, value->x); Write(out, value->y); Write(out, value->z); Write(out, value->w); }
        }
    }
}
bool Equal(const std::vector<FMatrix4>& a, const std::vector<FMatrix4>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) if (std::memcmp(a[i].data, b[i].data, sizeof(a[i].data))) return false;
    return true;
}
void Populate(Model& model, bool reorder = false)
{
    Skeleton skeleton;
    for (uint32_t i = 0; i < 3; ++i)
    {
        const uint32_t logical = reorder && i > 0 ? 3 - i : i;
        SkeletonNode node;
        node.name = "node" + std::to_string(logical); node.parentIndex = i == 0 ? -1 : 0;
        node.boneIndex = static_cast<int32_t>(i);
        node.bindPosition = {float(logical), float(logical) * 0.5f, 0};
        node.localBindTransform = FTransform(node.bindPosition, node.bindRotation, node.bindScale).GetLocalMatrix();
        skeleton.nodeByName.emplace(node.name, i); skeleton.boneByName.emplace(node.name, i);
        skeleton.nodes.push_back(node);
        skeleton.bones.push_back({node.name, i, FMatrix4::Inverse(node.localBindTransform)});
    }
    model.m_skeleton = skeleton;
    SkeletalAnimation animation; animation.name = "motion"; animation.duration = 20; animation.ticksPerSecond = 10;
    for (uint32_t i = 0; i < 3; ++i)
    {
        const auto& node = skeleton.nodes[i];
        NodeAnimationTrack track; track.nodeIndex = i;
        track.positionKeys = {{2, node.bindPosition}, {7, node.bindPosition + FVector3{0, 2, 1}}, {17, node.bindPosition + FVector3{1, 0, 0}}};
        if (i != 1) track.rotationKeys = {{0, FQuaternion::Identity}, {9, FQuaternion(FVector3{20, 45, 10})}, {19, FQuaternion::Identity}};
        if (i != 2) track.scaleKeys = {{0, FVector3::One}, {10, FVector3{1.2f, 0.8f, 1.1f}}};
        animation.tracks.push_back(track);
    }
    // Invalid/unmapped tracks must be skipped, and one-key/default tracks retained.
    animation.tracks.push_back({999, {{0, {3, 4, 5}}}, {}, {}});
    model.m_animations.push_back(animation);
    animation.name = "zero-duration"; animation.duration = 0; model.m_animations.push_back(animation);
    animation.name = "no-tracks"; animation.duration = 20; animation.tracks.clear(); model.m_animations.push_back(animation);
    animation = model.m_animations.front(); animation.name = "fallback-tps"; animation.ticksPerSecond = 0;
    model.m_animations.push_back(animation);
}
Skin& Add(Scene& scene, OvRendering::Resources::Model* model)
{
    auto& actor = scene.CreateActor(); actor.AddComponent<CModelRenderer>().SetModel(model);
    return actor.AddComponent<Skin>();
}

void CheckFailureAtomicity(Skin& skin, Model& model)
{
#ifdef SKINNING_NEW_PATH
    std::puts("Fault validation starting");
    // Restrict faults to the four matrix buffers. Debug STL bookkeeping and
    // FTransform's notification containers are outside this transaction test.
    allocationFailureSize = model.m_skeleton->nodes.size() * sizeof(FMatrix4);
    skin.Pause(); skin.SetBoneLocalPosition(1, {2, 3, 4});
    const auto local = skin.m_localPose, global = skin.m_globalPose;
    const auto bones = skin.m_boneMatrices, transposed = skin.m_boneMatricesTransposed;
    const auto version = skin.GetPoseVersion();
    const auto* palette = skin.GetBoneMatricesTransposed().data();
    auto unchanged = [&]
    {
        Require(Equal(local, skin.m_localPose) && Equal(global, skin.m_globalPose) && Equal(bones, skin.m_boneMatrices) && Equal(transposed, skin.m_boneMatricesTransposed), "failed evaluation exposed partial pose");
        Require(version == skin.GetPoseVersion() && skin.m_manualPoseOverride && palette == skin.GetBoneMatricesTransposed().data(), "failed evaluation changed publication metadata");
    };
    int failures = 0;
    for (int point = 0; point < 4; ++point)
    {
        std::printf("Fault injection allocation %d\n", point);
        skin.m_poseScratch = {};
        allocationFailureCountdown = point;
        bool threw = false;
        try { skin.EvaluatePose(); } catch (const std::bad_alloc&) { threw = true; }
        allocationFailureCountdown = -1;
        Require(threw, "allocation fault was not reached"); unchanged(); ++failures;
    }
    skin.m_poseScratch = {};
    std::puts("Fault injection manual local pose allocation");
    allocationFailureCountdown = 0;
    bool threw = false;
    try { skin.SetBoneLocalPosition(1, {9, 9, 9}); } catch (const std::bad_alloc&) { threw = true; }
    allocationFailureCountdown = -1;
    Require(threw, "manual allocation fault was not reached"); unchanged(); ++failures;
    const auto parent = model.m_skeleton->nodes[2].parentIndex;
    std::puts("Fault injection invalid hierarchy");
    model.m_skeleton->nodes[2].parentIndex = 2;
    threw = false;
    try { skin.EvaluatePose(); } catch (const std::invalid_argument&) { threw = true; }
    Require(threw, "invalid hierarchy not rejected"); unchanged(); ++failures;
    threw = false;
    try { skin.SetBoneLocalScale(1, {2, 2, 2}); } catch (const std::invalid_argument&) { threw = true; }
    Require(threw, "manual invalid hierarchy not rejected"); unchanged(); ++failures;
    model.m_skeleton->nodes[2].parentIndex = parent;
    Skin::PoseEvaluationOutput unpublished;
    Skin::ComputePose(skin.PreparePoseEvaluation(), unpublished);
    unchanged();
    skin.EvaluatePose();
    Require(skin.GetPoseVersion() == version + 1 && !skin.m_manualPoseOverride, "failed scratch did not recover");
    Require(Equal(unpublished.localPose, skin.m_localPose) && Equal(unpublished.boneMatricesTransposed, skin.m_boneMatricesTransposed), "computed output not published intact");
    const auto recoveredVersion = skin.GetPoseVersion();
    model.m_skeleton->nodes[2].parentIndex = 2;
    threw = false;
    try { skin.NotifyModelChanged(); } catch (const std::invalid_argument&) { threw = true; }
    Require(threw && !skin.HasSkinningData() && skin.m_localPose.empty() && skin.m_globalPose.empty() && skin.m_boneMatrices.empty() && skin.m_boneMatricesTransposed.empty(), "failed model rebuild exposed a palette");
    Require(skin.GetPoseVersion() == recoveredVersion, "failed model rebuild advanced version");
    model.m_skeleton->nodes[2].parentIndex = parent;
    skin.NotifyModelChanged();
    Require(skin.GetPoseVersion() == recoveredVersion + 1 && skin.HasSkinningData(), "model rebuild did not recover");
    std::printf("PASS: %d allocation/hierarchy failures retained all published pose data\n", failures);
    std::puts("PASS: compute/publish isolation and model-rebuild failure/recovery");
#else
    (void)skin; (void)model;
#endif
}

void StateValidation(const std::filesystem::path& dir)
{
    Model target("target"), source("source"), incompatible("incompatible"), missing("missing"), staticModel("static");
    Populate(target); Populate(source, true); Populate(incompatible); Populate(missing);
    incompatible.m_skeleton->nodes[0].localBindTransform.data[3] = 42;
    missing.m_animations.clear();
    const auto immutableAnimations = target.m_animations;
    std::ofstream output(dir / "states.bin", std::ios::binary);
    size_t snapshots = 0;
    auto record = [&](const Skin& skin) { Snapshot(output, skin); ++snapshots; };
    for (float rate : {0.0f, 30.0f, 60.0f, 240.0f}) for (bool looping : {false, true}) for (float speed : {-2.0f, 0.0f, 1.0f, 3.0f})
    {
        Scene scene;
        auto& a = Add(scene, &target); auto& b = Add(scene, &target);
        a.SetAnimation(std::optional<uint32_t>{0u}); b.SetAnimation(std::optional<uint32_t>{0u}); b.SetTime(0.75f); b.SetPlaybackSpeed(-0.75f);
        a.m_poseEvaluationRate = rate; a.SetLooping(looping); a.SetPlaybackSpeed(speed);
        a.SetTime(speed < 0 ? 1.8f : 0.1f);
        record(a); record(b);
        for (int frame = 0; frame < 180; ++frame)
        {
            if (frame == 10) a.Pause();
            if (frame == 12) a.SetTime(-0.3f);
            if (frame == 14) { a.SetBoneLocalPosition(1, {2, 3, 4}); a.SetBoneLocalRotation(1, FQuaternion(FVector3{10, 30, 15})); a.SetBoneLocalScale(2, {2, 1, 1}); }
            if (frame == 16) a.Play();
            if (frame == 24) a.Stop();
            if (frame == 25) a.Play();
            if (frame == 30) a.SetAnimation(std::nullopt);
            if (frame == 32) a.SetBoneLocalPosition(1, {1, 1, 1});
            if (frame == 34) a.SetAnimation(std::optional<uint32_t>{0u});
            if (frame == 40) a.owner.SetActive(false);
            if (frame == 44) a.owner.SetActive(true);
            if (frame == 50) a.SetAnimationSourceModel(&source);
            if (frame == 52) a.SetAnimation("motion");
            if (frame == 60) a.SetAnimationSourceModel(&incompatible);
            if (frame == 64) { a.SetAnimationSourceModel(nullptr); a.SetAnimation(std::optional<uint32_t>{0u}); }
            if (frame == 70) a.SetAnimation(1u);
            if (frame == 74) a.SetAnimation(2u);
            if (frame == 78) a.SetAnimation(3u);
            if (frame == 82) a.SetAnimationSourceModel(&missing);
            if (frame == 86) { a.SetAnimationSourceModel(nullptr); a.SetAnimation(std::optional<uint32_t>{0u}); }
            if (frame == 90) a.owner.GetComponent<CModelRenderer>()->SetModel(&staticModel);
            if (frame == 94) a.owner.GetComponent<CModelRenderer>()->SetModel(nullptr);
            if (frame == 98) { a.owner.GetComponent<CModelRenderer>()->SetModel(&target); a.SetAnimation(std::optional<uint32_t>{0u}); }
            if (frame == 110) { a.SetTime(100); a.SetMeshBoundsScale(0.5f); }
            if (frame == 112) { a.SetTime(-100); a.SetMeshBoundsScale(2.5f); }
            if (frame == 114) Require(!a.SetAnimation(999u) && !a.SetBoneLocalPosition(999, {}), "invalid index accepted");
            // Include zero, negative and sub-rate deltas, with independent shared-model times.
            const float delta = std::array<float, 6>{0, 1.0f/240, 1.0f/120, 1.0f/60, 0.13f, -0.01f}[frame % 6];
            scene.Update(delta); record(a); record(b);
            const auto version = a.GetPoseVersion(); scene.LateUpdate(delta);
            Require(version == a.GetPoseVersion(), "LateUpdate advanced pose twice");
        }
        auto& transient = Add(scene, &target); transient.SetAnimation(std::optional<uint32_t>{0u}); transient.OnUpdate(0.02f); record(transient);
        auto& actor = transient.owner; Require(actor.RemoveComponent<Skin>(), "remove component failed");
        actor.AddComponent<Skin>().SetAnimation(std::optional<uint32_t>{0u}); scene.Update(0.02f); Require(scene.DestroyActor(actor), "destroy actor failed");
    }
    // Explicitly verify evaluation did not write into shared animation tracks.
    for (size_t i = 0; i < immutableAnimations.size(); ++i)
        for (size_t j = 0; j < immutableAnimations[i].tracks.size(); ++j)
            for (size_t k = 0; k < immutableAnimations[i].tracks[j].positionKeys.size(); ++k)
            {
                const auto& a = immutableAnimations[i].tracks[j].positionKeys[k]; const auto& b = target.m_animations[i].tracks[j].positionKeys[k];
                Require(a.time == b.time && a.value.x == b.value.x && a.value.y == b.value.y && a.value.z == b.value.z, "shared animation mutated");
            }
    Scene failureScene; auto& failureSkin = Add(failureScene, &target); failureSkin.SetAnimation(std::optional<uint32_t>{0u});
    CheckFailureAtomicity(failureSkin, target);
    Require(output.good(), "snapshot output failed");
    std::printf("PASS: %zu snapshots, 32 playback/rate scenarios, shared resources and lifetimes\n", snapshots);
}

void ImageValidation(const std::filesystem::path& dir, bool trace, bool recordDraws = false, bool recordCommands = false, bool dynamicPasses = false, bool recoverHistory = false)
{
    OvWindowing::Settings::DeviceSettings ds; ds.contextMajorVersion = 4; ds.contextMinorVersion = 5;
    OvWindowing::Context::Device device(ds);
    OvWindowing::Settings::WindowSettings ws; ws.visible = false; ws.focused = false; ws.width = 320; ws.height = 240; ws.samples = 1;
    OvWindowing::Window window(device, ws); window.MakeCurrentContext();
    OvRendering::Context::Driver driver(OvRendering::Settings::DriverSettings{true});
    OvCore::ResourceManagement::ShaderManager shaders; OvCore::ResourceManagement::ModelManager models;
    const auto assets = std::filesystem::current_path() / "Data/Engine";
    shaders.ProvideAssetPaths(assets, assets); models.ProvideAssetPaths(assets, assets);
    ServiceLocator::Provide(shaders); ServiceLocator::Provide(models);
    OvTools::Jobs::JobSystem jobs(4); ServiceLocator::Provide(jobs);
    auto* mannequin = models.GetResource(":Models/Mannequin.fbx");
    auto* shader = shaders.GetResource(":Shaders/Standard.ovfx");
    Require(mannequin && mannequin->IsSkinned() && !mannequin->GetAnimations().empty() && shader, "animated render assets missing");
    Scene scene(assets, assets);
    auto& skin = Add(scene, mannequin); skin.SetAnimation(std::optional<uint32_t>{0u});
    if (dynamicPasses)
    {
        auto& light=scene.CreateActor("dynamic light").AddComponent<OvCore::ECS::Components::CDirectionalLight>();
        light.GetData().castShadows=true;light.GetData().shadowMapResolution=128;light.owner.transform.SetLocalRotation(FQuaternion({30,45,0}));
        auto& probe=scene.CreateActor("dynamic probe").AddComponent<OvCore::ECS::Components::CReflectionProbe>();
        probe.SetCubemapResolution(16);probe.SetCaptureSpeed(OvCore::ECS::Components::CReflectionProbe::ECaptureSpeed::SIX_FACES);
        if(recoverHistory){probe.SetCaptureSpeed(OvCore::ECS::Components::CReflectionProbe::ECaptureSpeed::ONE_FACE);
            probe.SetRefreshMode(OvCore::ECS::Components::CReflectionProbe::ERefreshMode::ON_DEMAND);probe.RequestCapture(true);}
        probe.SetInfluencePolicy(OvCore::ECS::Components::CReflectionProbe::EInfluencePolicy::GLOBAL);
        auto& stack=scene.CreateActor("dynamic effects").AddComponent<OvCore::ECS::Components::CPostProcessStack>();
        auto exposure=stack.GetAutoExposureSettings();exposure.progressive=recoverHistory;stack.SetAutoExposureSettings(exposure);
        Require(stack.GetAutoExposureSettings().progressive==recoverHistory,"auto-exposure setter did not update active effect settings");
        auto bloom=stack.GetBloomSettings();bloom.passes=3;stack.SetBloomSettings(bloom);
    }
    OvCore::Resources::Material material; material.SetShader(shader);
    skin.owner.AddComponent<CMaterialRenderer>().FillWithMaterial(material);
    skin.owner.GetComponent<CModelRenderer>()->SetFrustumBehaviour(CModelRenderer::EFrustumBehaviour::DISABLED);
    OvCore::Rendering::SceneRenderer renderer(driver);
    renderer.SetDrawRecordingEnabled(recordDraws || recordCommands);
    OvRendering::Entities::Camera camera;
    const auto bounds = mannequin->GetBoundingSphere();
    camera.SetPosition(bounds.position + FVector3{0, 0, std::max(bounds.radius * 2.8f, 2.0f)});
    camera.SetRotation(FQuaternion(FVector3{0, 180, 0})); camera.SetFar(1000); camera.SetClearColor({0.15f, 0.2f, 0.3f}); camera.CacheMatrices(320, 240);
    baregl::Framebuffer framebuffer("Skinning Validation"); OvCore::Rendering::FramebufferUtil::SetupFramebuffer(framebuffer, 320, 240, true, true, false);
    OvRendering::Data::FrameDescriptor frame{.renderWidth=320, .renderHeight=240, .camera=camera, .outputBuffer=framebuffer};
    std::ofstream images(dir / "frames.bin", std::ios::binary);
    std::vector<unsigned char> previous; size_t changedFrames = 0, nonBackground = 0;
    for (int i = 0; i < 24; ++i)
    {
        skin.SetTime(skin.GetAnimationDurationSeconds() * float(i) / 24.0f);
        const auto version = skin.GetPoseVersion();
        if(recoverHistory)
        {
            auto recordDiscard=[&](bool fail) {
                renderer.AddDescriptor<OvCore::Rendering::SceneRenderer::SceneDescriptor>({.scene=scene,.fallbackMaterial=material});
                renderer.BeginCommandRecording();renderer.BeginFrame(frame);renderer.DrawFrame();
                if(fail)renderer.SubmitRenderOperation([](auto&){throw std::runtime_error("injected temporal failure");});
                renderer.EndFrame();return renderer.FinishCommandRecording();
            };
            auto discarded=recordDiscard(i%4==1);
            if(i%4==0)discarded.Cancel();
            if(i%4==1){bool failed=false;try{discarded.Execute(driver);}catch(const std::runtime_error&){failed=true;}
                Require(failed && discarded.GetExecutedCount()>0,"dynamic prefix failure was not injected");}
            if(i%4==2){auto abandoned=std::move(discarded);}
            if(i%4==3){auto dependent=recordDiscard(false);discarded.Cancel();bool failed=false;
                try{dependent.Execute(driver);}catch(const std::logic_error&){failed=true;}
                Require(failed && dependent.GetExecutedCount()==0,"dependent dynamic frame ran after cancelled history");}
        }
        renderer.AddDescriptor<OvCore::Rendering::SceneRenderer::SceneDescriptor>({.scene=scene, .fallbackMaterial=material});
        if (recordCommands) renderer.BeginCommandRecording();
        renderer.BeginFrame(frame); renderer.DrawFrame();
        if (recordCommands) { renderer.EndFrame(); auto commands=renderer.FinishCommandRecording(); commands.Execute(driver); framebuffer.Bind(); }
        std::vector<unsigned char> pixels(320 * 240 * 4); glReadPixels(0, 0, 320, 240, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        if (!recordCommands) renderer.EndFrame(); else framebuffer.Unbind();
        TracyGpuCollect;
        Require(version == skin.GetPoseVersion(), "render advanced pose");
        images.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
        if (!previous.empty() && pixels != previous) ++changedFrames;
        nonBackground = 0;
        for (size_t j = 4; j < pixels.size(); j += 4) if (std::memcmp(pixels.data(), pixels.data() + j, 3)) ++nonBackground;
        Require(nonBackground > 100, "empty skinned framebuffer");
        previous = std::move(pixels); FrameMark;
    }
    Require(changedFrames > 0 && images.good(), "animation image did not change/output failed");
    std::printf("PASS: 24 skinned frames, %zu moving frame transitions, %zu final non-background pixels, clip=%s\n", changedFrames, nonBackground, skin.GetActiveAnimationName()->c_str());
    std::vector<double> samples;
    for (int i = 0; i < 1000; ++i)
    {
        const auto start = std::chrono::steady_clock::now(); skin.SetTime(float(i) * 0.013f);
        samples.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count());
    }
    std::sort(samples.begin(), samples.end());
    std::printf("Single-instance SetTime median=%.3f us, p95=%.3f us, nodes=%zu, bones=%u\n", samples[500], samples[950], mannequin->GetSkeleton()->nodes.size(), skin.GetBoneCount());
    if (trace)
    {
        std::puts("TRACE READY");
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (std::chrono::steady_clock::now() < end) { skin.SetTime(skin.GetTime() + 0.01f); FrameMark; std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    }
}
int main(int argc, char** argv) try
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    Require(argc > 1, "pass an output directory");
    StateValidation(argv[1]); ImageValidation(argv[1], argc > 2); return 0;
}
catch (const std::exception& error) { allocationFailureCountdown = -1; std::fprintf(stderr, "FAIL: %s\n", error.what()); return 1; }
