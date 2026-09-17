#include <OvCore/UI/Frame.h>
#include <OvCore/ECS/Components/CUI.h>
#include <OvCore/ECS/Components/CCamera.h>
#include <OvCore/ECS/ComponentRegistry.h>
#include <OvCore/Rendering/RenderFrontend.h>
#include <OvCore/Rendering/FramebufferUtil.h>
#include <OvRendering/Context/Driver.h>
#include <OvRendering/Threading/RenderThread.h>
#include <OvWindowing/Context/Device.h>
#include <OvWindowing/Window.h>
#include <baregl/ResourceExecutor.h>
#include <baregl/detail/glad/glad.h>
#include <tinyxml2.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <crtdbg.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

using namespace OvCore::ECS::Components;
using Scene = OvCore::SceneSystem::Scene;
using Actor = OvCore::ECS::Actor;
using namespace OvCore::UI;
using Frontend = OvCore::Rendering::RenderFrontend;

static void Require(bool condition, const char* message)
{ if (!condition) throw std::runtime_error(message); }
template<class F> static void Reject(F&& fn, const char* message)
{ bool rejected = false; try { fn(); } catch (const std::exception&) { rejected = true; } Require(rejected, message); }
static bool Near(float a, float b) { return std::abs(a - b) < 0.001f; }

static Actor& Rectangle(Scene& scene, Actor& parent, const char* name, OvMaths::FVector2 position,
    OvMaths::FVector2 size, OvMaths::FVector4 color)
{
    auto& actor = scene.CreateActor(name); actor.SetParent(parent);
    actor.AddComponent<CUIImage>().SetColor(color);
    auto& rect = *actor.GetComponent<CRectTransform>();
    rect.SetAnchorMin({0,0}); rect.SetAnchorMax({0,0}); rect.SetPivot({0,0});
    rect.SetAnchoredPosition(position); rect.SetSizeDelta(size);
    return actor;
}

static std::unique_ptr<Scene> MakeScene()
{
    auto scene = std::make_unique<Scene>("", "");
    scene->CreateActor("Camera").AddComponent<CCamera>();
    auto& root = scene->CreateActor("Canvas"); root.AddComponent<CCanvas>();
    Rectangle(*scene, root, "Red", {16,16}, {64,64}, {1,0,0,1});
    auto& mask = scene->CreateActor("Mask"); mask.SetParent(root); mask.AddComponent<CRectMask2D>();
    auto& rect = *mask.GetComponent<CRectTransform>();
    rect.SetAnchorMin({0,0}); rect.SetAnchorMax({0,0}); rect.SetPivot({0,0});
    rect.SetAnchoredPosition({32,32}); rect.SetSizeDelta({32,32});
    mask.AddComponent<CCanvasGroup>().SetAlpha(0.5f);
    Rectangle(*scene, mask, "Green", {-16,-16}, {64,64}, {0,1,0,1});
    Rectangle(*scene, root, "Blue", {72,72}, {16,16}, {0,0,1,1});
    auto& triangle = scene->CreateActor("Triangle"); triangle.SetParent(root);
    auto& renderer = triangle.AddComponent<CCanvasRenderer>();
    auto& triangleRect = *triangle.GetComponent<CRectTransform>();
    triangleRect.SetAnchorMin({0,0}); triangleRect.SetAnchorMax({0,0}); triangleRect.SetPivot({0,0});
    triangleRect.SetAnchoredPosition({0,80}); triangleRect.SetSizeDelta({12,12});
    const std::array<UIVertex,3> vertices{{
        UIVertex{{0,0,0},{0,0},{1,0,1,1}}, UIVertex{{12,0,0},{1,0},{1,0,1,1}}, UIVertex{{0,12,0},{0,1},{1,0,1,1}}}};
    const std::array<uint32_t,3> indices{0,1,2}; renderer.SetMesh(vertices,indices);
    return scene;
}

static void LayoutChecks()
{
    RectLayout layout;
    auto rect = ResolveRect(layout, {{0,0},{800,600}});
    Require(Near(rect.min.x,350) && Near(rect.min.y,250), "center anchors");
    layout.anchorMin = {0,0}; layout.anchorMax = {1,1}; layout.sizeDelta = {-20,-40};
    rect = ResolveRect(layout, {{10,20},{800,600}});
    Require(Near(rect.min.x,20) && Near(rect.min.y,40) && Near(rect.size.x,780) && Near(rect.size.y,560), "stretch anchors and margins");
    layout.pivot = {0,1}; layout.anchoredPosition = {5,-7};
    rect = ResolveRect(layout, {{10,20},{800,600}});
    Require(Near(rect.min.x,15) && Near(rect.min.y,53), "noncenter pivot and anchored offset");
    layout.sizeDelta.x = std::numeric_limits<float>::infinity();
    Reject([&] { ResolveRect(layout, {{0,0},{800,600}}); }, "nonfinite layout rejected");
    std::puts("PASS layout anchors, stretch, pivots, scaling and invalid inputs");
}

static void CanvasModeChecks()
{
    Scene scene("", "");
    auto& cameraActor = scene.CreateActor("RenderCamera");
    auto& cameraComponent = cameraActor.AddComponent<CCamera>();
    auto& canvasActor = scene.CreateActor("Canvas");
    auto& canvas = canvasActor.AddComponent<CCanvas>();
    canvasActor.AddComponent<CUIImage>().SetColor({1, 1, 1, 1});

    Require(canvas.GetRenderMode() == CCanvas::RenderMode::ScreenSpaceOverlay, "overlay is the compatible default");
    canvas.SetRenderMode(CCanvas::RenderMode::ScreenSpaceCamera);
    canvas.SetWorldCamera(&cameraComponent); canvas.SetPlaneDistance(10);
    const auto cameraFrame = ExtractFrame(scene, {200, 100}, 9, &cameraComponent.GetCamera());
    Require(cameraFrame.images.size() == 1 && cameraFrame.images[0].renderMode == CCanvas::RenderMode::ScreenSpaceCamera,
        "screen camera canvas extracted as scene UI");
    Require(cameraFrame.images[0].hasScreenCorners && cameraFrame.HitTest({100, 50}).has_value(),
        "screen camera projection and raycast share the projected quad");
    const auto cameraGeometry = ResolveCanvasGeometry(canvas, {200, 100});
    const auto cameraCenter = cameraGeometry->localToWorld * OvMaths::FVector4{100, 50, 0, 1};
    Require(Near(cameraCenter.x, 0) && Near(cameraCenter.y, 0) && Near(cameraCenter.z, 10),
        "screen camera plane follows camera and plane distance");

    cameraActor.transform.SetWorldPosition({3, 4, 5});
    const auto movedFrame = ExtractFrame(scene, {200, 100}, 10, &cameraComponent.GetCamera());
    const auto movedGeometry = ResolveCanvasGeometry(canvas, {200, 100});
    const auto movedCenter = movedGeometry->localToWorld * OvMaths::FVector4{100, 50, 0, 1};
    Require(Near(movedCenter.x, 3) && Near(movedCenter.y, 4) && Near(movedCenter.z, 15) &&
        Near(canvasActor.transform.GetWorldPosition().x, 3) && Near(canvasActor.transform.GetWorldPosition().y, 4) &&
        Near(canvasActor.transform.GetWorldPosition().z, 15) && movedFrame.images.size() == 1,
        "screen camera canvas and driven transform follow camera movement");
    canvas.SetWorldCamera(nullptr);
    Require(ResolveCanvasCamera(canvas) == &cameraComponent && ResolveCanvasGeometry(canvas, {200, 100}).has_value(),
        "screen camera canvas falls back to the scene main camera");
    canvas.SetWorldCamera(&cameraComponent);

    canvas.SetRenderMode(CCanvas::RenderMode::WorldSpace);
    cameraActor.transform.SetWorldPosition({0, 0, 0});
    auto& rect = *canvasActor.GetComponent<CRectTransform>(); rect.SetSizeDelta({2, 2});
    canvasActor.transform.SetLocalPosition({0, 0, 10}); canvas.SetScaleFactor(2);
    const auto worldFrame = ExtractFrame(scene, {200, 100}, 10, &cameraComponent.GetCamera());
    Require(worldFrame.images.size() == 1 && worldFrame.images[0].renderMode == CCanvas::RenderMode::WorldSpace,
        "world canvas extracted as scene UI");
    Require(Near(worldFrame.images[0].rect.size.x, 2) && Near(worldFrame.images[0].rect.size.y, 2) &&
        worldFrame.HitTest({100, 50}).has_value(), "world canvas uses RectTransform world units and projected hit testing");
    tinyxml2::XMLDocument doc; auto* root = doc.NewElement("root"); doc.InsertEndChild(root); scene.OnSerialize(doc, root);
    Scene restored; restored.OnDeserialize(doc, root->FirstChildElement("scene"));
    const auto* restoredCanvas = restored.FindActorByName("Canvas")->GetComponent<CCanvas>();
    Require(restoredCanvas->GetRenderMode() == CCanvas::RenderMode::WorldSpace && Near(restoredCanvas->GetPlaneDistance(), 10) &&
        restoredCanvas->GetWorldCamera() != nullptr, "canvas mode, plane and camera reference roundtrip");
    Reject([&] { canvas.SetRenderMode(static_cast<CCanvas::RenderMode>(99)); }, "invalid canvas mode rejected");
    Reject([&] { canvas.SetPlaneDistance(0); }, "invalid plane distance rejected");
    std::puts("PASS overlay, screen-camera and world-space canvas geometry, projection and hit testing");
}

static void SceneChecks(const std::filesystem::path& output)
{
    auto scene = MakeScene();
    const auto baseline = ExtractFrame(*scene,{96,96},7);
    auto* green = scene->FindActorByName("Green");
    auto* red = scene->FindActorByName("Red");
    auto* group = scene->FindActorByName("Mask")->GetComponent<CCanvasGroup>();
    Require(baseline.images.size()==3 && baseline.HitTest({48,48})->actorGuid==green->GetGUID(), "ordered masked raycast");
    Require(baseline.meshes.size()==1 && baseline.meshes[0].vertices.size()==3,"CanvasRenderer mesh copied into immutable frame");
    auto* triangle=scene->FindActorByName("Triangle"); auto* renderer=triangle->GetComponent<CCanvasRenderer>();
    const auto sourceVertices=renderer->GetVertices(); const auto sourceIndices=renderer->GetIndices();
    triangle->SetActive(false); Require(ExtractFrame(*scene,{96,96}).meshes.empty(),"inactive Graphic subtree excluded");
    triangle->SetActive(true); renderer->Clear();
    Require(ExtractFrame(*scene,{96,96}).meshes.empty() && baseline.meshes.size()==1,"cleared renderer disappears without mutating captured frame");
    renderer->SetMesh(sourceVertices,sourceIndices);
    Require(baseline.HitTest({20,20})->actorGuid==red->GetGUID() && !baseline.HitTest({90,90}), "outside mask and canvas");
    Require(baseline.HitTestTopLeft({48,48})->actorGuid==green->GetGUID() && !baseline.HitTestTopLeft({-1,48}), "viewport input coordinates");
    group->SetInteractable(false);
    Require(!ExtractFrame(*scene,{96,96}).HitTest({48,48})->interactable, "disabled controls block click through");
    group->SetBlocksRaycasts(false);
    Require(ExtractFrame(*scene,{96,96}).HitTest({48,48})->actorGuid==red->GetGUID(), "raycast group filtering");
    group->SetBlocksRaycasts(true); group->SetInteractable(true); group->SetAlpha(0);
    Require(ExtractFrame(*scene,{96,96}).HitTest({48,48})->actorGuid==green->GetGUID(), "transparent hit target");
    group->SetAlpha(0.5f);
    scene->FindActorByName("Mask")->SetActive(false);
    Require(ExtractFrame(*scene,{96,96}).images.size()==2, "inactive subtree excluded");
    scene->FindActorByName("Mask")->SetActive(true);
    auto* canvas = scene->FindActorByName("Canvas")->GetComponent<CCanvas>();
    canvas->SetScaleFactor(2);
    Require(ExtractFrame(*scene,{192,192}).HitTest({96,96})->actorGuid==green->GetGUID(), "scaled hit and render coordinates agree");
    canvas->SetScaleFactor(1);
    auto& nested=scene->FindActorByName("Mask")->AddComponent<CCanvas>();
    nested.SetSortingOrder(-10); canvas->SetSortingOrder(10);
    Require(ExtractFrame(*scene,{96,96}).HitTest({48,48})->actorGuid==green->GetGUID(), "nested canvas inherits sorting by default");
    nested.SetOverrideSorting(true);
    Require(ExtractFrame(*scene,{96,96}).HitTest({48,48})->actorGuid==red->GetGUID(), "nested canvas explicit sorting override");
    scene->FindActorByName("Mask")->RemoveComponent<CCanvas>(); canvas->SetSortingOrder(0);
    Frame edgeFrame{0,{96,96},{}};
    Image edge; edge.actorGuid=1; edge.rect=edge.visibleRect={{0,0},{96,96}}; edgeFrame.images.push_back(edge);
    Require(edgeFrame.HitTestTopLeft({0,0}).has_value() && !edgeFrame.HitTestTopLeft({0,96}), "top-left viewport boundary");
    tinyxml2::XMLDocument doc; auto* root=doc.NewElement("root"); doc.InsertEndChild(root);
    scene->OnSerialize(doc,root);
    Require(doc.SaveFile((output/"NativeUI.ovscene").string().c_str())==tinyxml2::XML_SUCCESS, "example scene saved");
    auto restored=std::make_unique<Scene>("", ""); restored->OnDeserialize(doc,root->FirstChildElement("scene"));
    const auto roundtrip=ExtractFrame(*restored,{96,96});
    Require(roundtrip.images.size()==baseline.images.size(), "native component factory and scene roundtrip");
    for(size_t i=0;i<baseline.images.size();++i)
        Require(roundtrip.images[i].actorGuid==baseline.images[i].actorGuid &&
            Near(roundtrip.images[i].visibleRect.min.x,baseline.images[i].visibleRect.min.x) &&
            Near(roundtrip.images[i].color.w,baseline.images[i].color.w), "roundtrip layout, order and alpha");
    scene.reset(); restored.reset();
    Require(baseline.HitTest({48,48}).has_value(), "snapshot contains no live actor pointers");
    tinyxml2::XMLDocument example;
    Require(example.LoadFile("Examples/NativeUI/Assets/Scenes/NativeUI.ovscene")==tinyxml2::XML_SUCCESS,"shipped sample parsed");
    Scene sample; sample.OnDeserialize(example,example.RootElement()->FirstChildElement("scene"));
    const auto scaled=ExtractFrame(sample,{192,192});
    Require(scaled.images.size()==3 && Near(scaled.images[1].visibleRect.min.x,64) &&
        Near(scaled.images[1].visibleRect.size.x,64),"shipped sample scales to game viewport");
    std::puts("PASS native scene registration, serialization, clipping, groups, scaling and source lifetime");
}

static std::vector<uint8_t> Pixels(const baregl::Framebuffer::Snapshot& target)
{
    const auto [width,height]=target.GetSize(); std::vector<uint8_t> pixels(size_t(width)*height*4);
    target.Bind(); glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    baregl::Framebuffer::BindDefault(); Require(glGetError()==GL_NO_ERROR,"GL readback"); return pixels;
}
static void CheckPixels(const std::vector<uint8_t>& p)
{
    auto pixel=[&](int x,int y,int r,int g,int b) {
        const size_t index=(size_t(y)*96+x)*4;
        Require(std::abs(int(p[index])-r)<=1 && std::abs(int(p[index+1])-g)<=1 && std::abs(int(p[index+2])-b)<=1,"expected UI pixels");
    };
    pixel(4,4,0,0,0); pixel(20,20,255,0,0); pixel(48,48,128,128,0);
    pixel(68,48,255,0,0); pixel(76,76,0,0,255);
    pixel(3,83,255,0,255);
}

static void TextureChecks(OvRendering::Context::Driver& driver, OvTools::Jobs::JobSystem& jobs)
{
    baregl::Framebuffer target;
    OvCore::Rendering::FramebufferUtil::SetupFramebuffer(target,96,96,true,true,true);
    auto output=target.RecordTarget(); output.Bind(); driver.Clear(true,true,true,{0,0,0,1});
    auto texture=std::make_shared<baregl::Texture>(baregl::types::ETextureType::TEXTURE_2D);
    texture->Allocate({.width=2,.height=1,
        .minFilter=baregl::types::ETextureFilteringMode::NEAREST,.magFilter=baregl::types::ETextureFilteringMode::NEAREST,
        .internalFormat=baregl::types::EInternalFormat::RGBA8,.useMipMaps=false});
    const std::array<uint8_t,8> texels{255,0,0,255,0,255,0,255};
    texture->Upload(texels.data(),baregl::types::EFormat::RGBA,baregl::types::EPixelDataType::UNSIGNED_BYTE);
    const std::weak_ptr<baregl::Texture> watched=texture;
    OvRendering::Data::RenderCommandList commands;
    {
        Frame ui{0,{96,96},{}}; Image image;
        image.rect={{0,0},{96,96}}; image.visibleRect={{48,0},{48,96}}; image.texture=texture;
        ui.images.push_back(image);
        Mesh mesh; mesh.clip={{0,0},{48,96}}; mesh.texture=texture;
        mesh.vertices={
            UIVertex{{0,0,0},{0,0},{1,1,1,1}}, UIVertex{{48,0,0},{1,0},{1,1,1,1}},
            UIVertex{{48,96,0},{1,1},{1,1,1,1}}, UIVertex{{0,96,0},{0,1},{1,1,1,1}}};
        mesh.indices={0,1,2,0,2,3}; ui.meshes.push_back(std::move(mesh));
        OvRendering::Core::MeshCommandRecorder fallback;
        RenderResources resources; Frontend frontend;
        auto scene=resources.Extract(ui,123,fallback.GetFallback2D(),fallback.GetFallbackCube());
        OvRendering::Data::PipelineState poisoned; poisoned.scissorTest=poisoned.stencilTest=true;
        auto prepared=frontend.Prepare(scene,poisoned,&jobs);
        commands.Append(frontend.Record(prepared,output,&jobs));
    }
    texture.reset(); Require(!watched.expired(),"UI texture generation retained by commands");
    commands.Seal(); commands.Execute(driver);
    const auto pixels=Pixels(output);
    for(int x:{50,60,90}) {
        const size_t index=(48*96+size_t(x))*4;
        Require(pixels[index]==0 && pixels[index+1]==255,"clipped texture UVs remain in original rectangle");
    }
    const size_t left=(48*96+10)*4, right=(48*96+40)*4;
    Require(pixels[left]==255 && pixels[left+1]==0 && pixels[right]==0 && pixels[right+1]==255,
        "CanvasRenderer mesh submits UVs and texture sampling");
    Require(watched.expired(),"UI texture retired after command execution");
    std::puts("PASS image clipping, mesh UV/texture submission, inherited state isolation and retained texture lifetime");
}

static void SceneCanvasRenderChecks(OvRendering::Context::Driver& driver, OvTools::Jobs::JobSystem& jobs)
{
    const auto render = [&](CCanvas::RenderMode mode) {
        Scene scene("", "");
        auto& cameraActor = scene.CreateActor("Camera"); auto& camera = cameraActor.AddComponent<CCamera>();
        auto& canvasActor = scene.CreateActor("Canvas"); auto& canvas = canvasActor.AddComponent<CCanvas>();
        canvas.SetRenderMode(mode); canvas.SetWorldCamera(&camera); canvas.SetPlaneDistance(5);
        auto& graphic = scene.CreateActor("Graphic"); graphic.SetParent(canvasActor);
        auto& renderer = graphic.AddComponent<CCanvasRenderer>(); auto& graphicRect = *graphic.GetComponent<CRectTransform>();
        graphicRect.SetAnchorMin({0.5f,0.5f}); graphicRect.SetAnchorMax({0.5f,0.5f}); graphicRect.SetPivot({0.5f,0.5f});
        const float extent = mode == CCanvas::RenderMode::ScreenSpaceCamera ? 25.f : 0.5f;
        graphicRect.SetSizeDelta({extent*2,extent*2});
        const auto color = mode == CCanvas::RenderMode::ScreenSpaceCamera ?
            OvMaths::FVector4{1,0,0,1} : OvMaths::FVector4{0,1,0,1};
        const std::array<UIVertex,4> vertices{{
            UIVertex{{-extent,-extent,0},{0,0},color}, UIVertex{{extent,-extent,0},{1,0},color},
            UIVertex{{extent,extent,0},{1,1},color}, UIVertex{{-extent,extent,0},{0,1},color}}};
        const std::array<uint32_t,6> indices{0,1,2,0,2,3}; renderer.SetMesh(vertices,indices);
        if (mode == CCanvas::RenderMode::WorldSpace)
        {
            canvasActor.GetComponent<CRectTransform>()->SetSizeDelta({2, 2});
            canvasActor.transform.SetLocalPosition({0, 0, 5});
        }
        baregl::Framebuffer target; OvCore::Rendering::FramebufferUtil::SetupFramebuffer(target, 96, 96, true, true, true);
        OvRendering::Data::Material fallback; Frontend frontend;
        OvCore::Rendering::SceneRenderRequest main{{scene, camera.GetCamera(), 96, 96, 1, 11}};
        OvCore::Rendering::SceneFrameRequest request{main, target.RecordTarget(), fallback};
        request.generateShadows = request.captureReflections = false;
        auto extracted = frontend.ExtractFrame(request);
        Require(extracted->GetPlan().passes.back().name == "CanvasUI" &&
            extracted->GetPlan().passes.back().stages == std::vector{OvRendering::Data::RenderStage::SceneUI},
            "camera and world canvases use the SceneUI pass");
        auto commands = frontend.RecordFrame(frontend.PrepareFrame(std::move(extracted), {}, &jobs), &jobs);
        commands.Execute(driver);
        const auto pixels = Pixels(target.RecordTarget()); const size_t center = (48 * 96 + 48) * 4;
        if (mode == CCanvas::RenderMode::ScreenSpaceCamera)
            Require(pixels[center] == 255 && pixels[center + 1] == 0, "screen camera canvas rendered through camera plane");
        else Require(pixels[center] == 0 && pixels[center + 1] == 255, "world canvas rendered with transform and depth");
    };
    render(CCanvas::RenderMode::ScreenSpaceCamera); render(CCanvas::RenderMode::WorldSpace);
    std::puts("PASS screen-camera/world-space SceneUI plan and GPU pixels");
}

static OvRendering::Data::RenderFrame Record(Frontend& frontend, Scene& scene,
    const baregl::Framebuffer::Snapshot& output, OvTools::Jobs::JobSystem& jobs)
{
    OvRendering::Entities::Camera camera; camera.SetClearColor({0,0,0});
    OvRendering::Data::Material fallback;
    OvCore::Rendering::SceneRenderRequest main{{scene,camera,96,96,1,7}};
    OvCore::Rendering::SceneFrameRequest request{main,output,fallback};
    request.generateShadows=request.captureReflections=false;
    auto extracted=frontend.ExtractFrame(request);
    Require(extracted->GetPlan().passes.back().name=="OverlayUI", "UI integrated into production frame plan");
    auto prepared=frontend.PrepareFrame(std::move(extracted),{},&jobs);
    auto commands=frontend.RecordFrame(std::move(prepared),&jobs);
    OvRendering::Data::RenderFrame frame; frame.Append(std::move(commands)); frame.Seal(); return frame;
}

int main(int argc,char** argv) try
{
    std::setvbuf(stdout,nullptr,_IONBF,0);
    _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE); _CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
    Require(argc==2,"output directory supplied"); const std::filesystem::path output=argv[1];
    LayoutChecks(); CanvasModeChecks(); SceneChecks(output);
    OvWindowing::Settings::DeviceSettings ds; ds.contextMajorVersion=4; ds.contextMinorVersion=5;
    OvWindowing::Context::Device device(ds);
    OvWindowing::Settings::WindowSettings ws; ws.visible=false; ws.focused=false; ws.width=96; ws.height=96; ws.samples=1;
    OvWindowing::Window window(device,ws); window.MakeCurrentContext();
    OvTools::Jobs::JobSystem jobs(2);
    std::vector<uint8_t> expected,actual;
    {
        OvRendering::Context::Driver driver(OvRendering::Settings::DriverSettings{true});
        TextureChecks(driver,jobs); SceneCanvasRenderChecks(driver,jobs);
        auto target=std::make_unique<baregl::Framebuffer>();
        OvCore::Rendering::FramebufferUtil::SetupFramebuffer(*target,96,96,true,true,true);
        const auto retained=target->RecordTarget();
        auto frontend=std::make_unique<Frontend>(); auto scene=MakeScene();
        auto frame=Record(*frontend,*scene,retained,jobs);
        scene.reset(); frontend.reset(); target->Resize(48,48); target.reset();
        frame.Submit(driver); frame.Wait(); frame.GetCompletion().RethrowIfFailed();
        expected=Pixels(retained); CheckPixels(expected);
    }
    glfwMakeContextCurrent(nullptr);
    {
        OvRendering::Threading::RenderThread thread([&] {
            window.MakeCurrentContext(); return std::make_unique<OvRendering::Context::Driver>(OvRendering::Settings::DriverSettings{true});
        }, [] { glfwMakeContextCurrent(nullptr); });
        {
            baregl::ResourceExecutor executor([&](std::function<void()> fn){thread.Invoke([&](auto&){fn();});});
            auto target=std::make_unique<baregl::Framebuffer>();
            OvCore::Rendering::FramebufferUtil::SetupFramebuffer(*target,96,96,true,true,true);
            auto retained=target->RecordTarget();
            auto frontend=std::make_unique<Frontend>(); auto scene=MakeScene();
            auto frame=Record(*frontend,*scene,retained,jobs);
            scene.reset(); frontend.reset(); target->Resize(48,48); target.reset();
            Require(glfwGetCurrentContext()==nullptr,"Main never acquired Render context");
            auto done=thread.Submit(std::move(frame)); thread.WaitIdle(); done.RethrowIfFailed();
            actual=thread.Invoke([&](auto&){return Pixels(retained);});
        }
        thread.Shutdown();
    }
    Require(actual==expected,"default and RenderThread pixels match exactly");
    std::ofstream file(output/"native-ui.rgba",std::ios::binary);
    file.write(reinterpret_cast<const char*>(actual.data()),static_cast<std::streamsize>(actual.size()));
    std::puts("PASS production rendering, exact serial/RenderThread pixels, source and target destruction before execution");
    return 0;
}
catch(const std::exception& e) { std::fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }
