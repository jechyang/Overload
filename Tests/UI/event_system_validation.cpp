#define wmain HostValidationMain
#include "Tests/Scripting/Integration/native_host_validation.cpp"
#undef wmain
#include <OvCore/Global/ServiceLocator.h>
#include <OvCore/Scripting/ScriptEngine.h>
#include <OvWindowing/Inputs/InputManager.h>
#include <OvCore/ECS/Components/CUI.h>
#include <OvCore/ECS/Components/CCamera.h>
#include <OvCore/UI/Frame.h>
#include <OvCore/Rendering/RenderFrontend.h>
#include <OvCore/Rendering/FramebufferUtil.h>
#include <baregl/ResourceExecutor.h>
#include <baregl/detail/glad/glad.h>

using ManagedBehaviour = OvCore::ECS::Components::Behaviour;
static ManagedBehaviour& AddInput(OvCore::ECS::Actor& actor,const char* type)
{ return actor.AddBehaviour(OvCore::Scripting::ScriptIdentity::CSharp("FInput",type)); }
static int InputField(ManagedBehaviour& behaviour,const char* name)
{
    for(const auto& field:behaviour.GetScript()->ManagedFields()) if(field.name == name)
        return behaviour.GetScript()->ReadManagedField(field.token)->scalar.int32_value;
    throw std::runtime_error("Missing F fixture field");
}
int wmain(int argc,wchar_t** argv) try
{
    Require(argc==4 || argc==5,"input test arguments: runtime host fixture [render-thread]");
    OvReflection::MainThread main; RuntimeHost host(main);
    host.Start(HostPaths::Resolve(RuntimeHost::ExecutableDirectory(),argv[1],argv[2]));
    std::filesystem::path assembly=argv[3], project=assembly.parent_path();
    OvCore::Scripting::ScriptEngine scripts(project,project); OvCore::Global::ServiceLocator::Provide(scripts);
    Engine engine(argc==5); OvCore::Global::ServiceLocator::Provide(engine.scenes);
    OvCore::Global::ServiceLocator::Provide(engine.physics);
    OvWindowing::Inputs::InputManager input(engine.window); OvCore::Global::ServiceLocator::Provide(input);
    input.SetUIViewport({{10,20},{100,100},{1,1}});
    Require(scripts.LoadCSharpProject(host,project,assembly,"FInput"),"load F project input classes");
    auto& scene=*engine.scenes.GetCurrentScene(); auto& root=scene.CreateActor("Events");
    AddInput(root,"FInputFixture.SystemComponent"); AddInput(root,"FInputFixture.InputComponent");
    AddInput(root,"FInputFixture.ModuleComponent"); AddInput(root,"FInputFixture.RaycasterComponent");
    auto& target=scene.CreateActor("Target"); auto& probe=AddInput(target,"FInputFixture.TargetComponent");
    using namespace OvCore::ECS::Components;
    auto& camera=scene.CreateActor("Camera").AddComponent<CCamera>(); camera.GetCamera().SetClearColor({0,0,0});
    auto& canvasActor=scene.CreateActor("GraphicCanvas"); auto& canvas=canvasActor.AddComponent<CCanvas>();
    AddInput(canvasActor,"FInputFixture.GraphicRaycasterComponent"); canvas.SetWorldCamera(&camera);
    auto& graphicActor=scene.CreateActor("GraphicTarget"); graphicActor.SetParent(canvasActor);
    AddInput(graphicActor,"FInputFixture.ImageComponent"); auto& graphicProbe=AddInput(graphicActor,"FInputFixture.TargetComponent");
    canvasActor.SetActive(false);
    auto& frontCanvasActor=scene.CreateActor("FrontCanvas"); auto& frontCanvas=frontCanvasActor.AddComponent<CCanvas>();frontCanvas.SetSortingOrder(10);
    AddInput(frontCanvasActor,"FInputFixture.GraphicRaycasterComponent");frontCanvasActor.SetActive(false);
    auto& frontGraphic=scene.CreateActor("FrontGraphic");frontGraphic.SetParent(frontCanvasActor);
    AddInput(frontGraphic,"FInputFixture.GreenImageComponent");auto& frontProbe=AddInput(frontGraphic,"FInputFixture.TargetComponent");
    auto& stencilActor=scene.CreateActor("StencilMask");stencilActor.SetParent(canvasActor);stencilActor.SetActive(false);
    AddInput(stencilActor,"FInputFixture.MaskImageComponent");AddInput(stencilActor,"FInputFixture.MaskComponent");
    auto& physical3DRoot=scene.CreateActor("Physics3D"); AddInput(physical3DRoot,"FInputFixture.PhysicsRaycasterComponent");physical3DRoot.SetActive(false);
    auto& physical2DRoot=scene.CreateActor("Physics2D"); AddInput(physical2DRoot,"FInputFixture.Physics2DRaycasterComponent");physical2DRoot.SetActive(false);
    auto& body=scene.CreateActor("3D target");body.transform.SetLocalPosition({0,0,5});body.AddComponent<CPhysicalBox>().SetMass(0);
    auto& bodyProbe=AddInput(body,"FInputFixture.TargetComponent");
    auto& shape=scene.CreateActor("2D target");shape.transform.SetLocalPosition({0,0,3});AddInput(shape,"FInputFixture.Collider2DComponent");
    auto& shapeProbe=AddInput(shape,"FInputFixture.TargetComponent");
    for(auto* actor:scene.GetActors()) for(auto& [key,behaviour]:actor->GetBehaviours()) Require(scripts.PrepareCSharp(behaviour),"prepare input fixture");
    scene.Play();
    using namespace OvWindowing::Inputs;
    auto push=[&](UIInputEvent event) { engine.window.UIInputEvent.Invoke(std::move(event)); };
    auto tick=[&] { scene.Update(0.016f); input.ClearEvents(); };
    push({UIInputKind::Focus,1}); push({UIInputKind::PointerDown,0,{30,40}}); push({UIInputKind::PointerUp,0,{30,40}}); tick();
    Require(InputField(probe,"downs")==1 && InputField(probe,"ups")==1 && InputField(probe,"clicks")==1,"Window -> InputManager -> real CLR same-frame click");
    push({UIInputKind::PointerDown,0,{30,40}}); tick();
    push({UIInputKind::PointerMove,0,{60,40}}); tick();
    push({UIInputKind::PointerMove,0,{160,40}}); push({UIInputKind::PointerUp,0,{160,40}}); tick();
    Require(InputField(probe,"begins")==1 && InputField(probe,"ends")==1 && InputField(probe,"clicks")==1,"real CLR drag capture across viewport edge");
    push({UIInputKind::PointerDown,0,{30,40}}); tick(); push({UIInputKind::Focus,0}); tick();
    Require(InputField(probe,"ups")==3 && InputField(probe,"clicks")==1,"focus loss cancels native capture without click");
    Require(scripts.LoadCSharpProject(host,project,assembly,"FInput",true),"hot reload custom input/raycaster/handler");
    push({UIInputKind::Focus,1}); push({UIInputKind::Touch,7,{30,40},UITouchPhase::Began}); push({UIInputKind::Touch,11,{30,40},UITouchPhase::Began}); tick();
    push({UIInputKind::Touch,7,{30,40},UITouchPhase::Ended}); push({UIInputKind::Touch,11,{30,40},UITouchPhase::Canceled}); tick();
    Require(InputField(probe,"clicks")==2,"two fingers after real project reload");
    target.SetActive(false); push({UIInputKind::PointerDown,0,{30,40}}); push({UIInputKind::PointerUp,0,{30,40}}); tick();
    Require(InputField(probe,"clicks")==2,"inactive native actor is skipped");
    Require(root.RemoveBehaviour(OvCore::Scripting::ScriptIdentity::CSharp("FInput","FInputFixture.RaycasterComponent")),"remove custom raycaster");
    engine.physics.Update(0.016f);
    physical3DRoot.SetActive(true);
    push({UIInputKind::PointerDown,0,{60,70}});push({UIInputKind::PointerUp,0,{60,70}});tick();
    Require(InputField(bodyProbe,"clicks")==1,"PhysicsRaycaster native Bullet query dispatches to real 3D Actor");physical3DRoot.SetActive(false);
    physical2DRoot.SetActive(true);
    push({UIInputKind::PointerDown,0,{60,70}});push({UIInputKind::PointerUp,0,{60,70}});tick();
    Require(InputField(shapeProbe,"clicks")==1,"Physics2DRaycaster native minimal backend dispatches to real 2D Actor");physical2DRoot.SetActive(false);
    canvasActor.SetActive(true);
    auto& rect=*graphicActor.GetComponent<CRectTransform>();
    rect.SetAnchorMin({0,0}); rect.SetAnchorMax({0,0}); rect.SetPivot({0,0});
    std::unique_ptr<baregl::ResourceExecutor> executor;
    if(engine.render) executor=std::make_unique<baregl::ResourceExecutor>([&](std::function<void()> fn){engine.render->Invoke([&](auto&){fn();});});
    {
        baregl::Framebuffer framebuffer; OvCore::Rendering::FramebufferUtil::SetupFramebuffer(framebuffer,100,100,true,true,true);
        auto output=framebuffer.RecordTarget();
        for(auto mode:{CCanvas::RenderMode::ScreenSpaceOverlay,CCanvas::RenderMode::ScreenSpaceCamera,CCanvas::RenderMode::WorldSpace})
        {
            canvas.SetRenderMode(mode); canvas.SetPlaneDistance(10);
            if(mode==CCanvas::RenderMode::WorldSpace) {
                canvasActor.GetComponent<CRectTransform>()->SetSizeDelta({2,2}); canvasActor.transform.SetLocalPosition({0,0,10});
                rect.SetAnchoredPosition({0.2f,0.2f}); rect.SetSizeDelta({0.8f,0.8f});
            } else { rect.SetAnchoredPosition({10,10}); rect.SetSizeDelta({40,40}); }
            OvCore::Rendering::RenderFrontend frontend;
            OvRendering::Data::Material fallback;
            OvCore::Rendering::SceneRenderRequest mainRequest{{scene,camera.GetCamera(),100,100,1,1}};
            OvCore::Rendering::SceneFrameRequest request{mainRequest,output,fallback}; request.generateShadows=request.captureReflections=false;
            auto extracted=frontend.ExtractFrame(request);
            const auto ui=OvCore::UI::ExtractFrame(scene,{100,100},0,&camera.GetCamera());
            const OvCore::UI::Mesh* mesh=nullptr;
            for(const auto& item:ui.meshes) if(item.actorGuid==graphicActor.GetGUID() && !item.pop) { mesh=&item; break; }
            Require(mesh && mesh->hasScreenCorners,"production managed graphic has projected hit geometry");
            OvMaths::FVector2 screen{}; for(const auto& corner:mesh->screenCorners) screen+=corner*0.25f;
            auto commands=frontend.RecordFrame(frontend.PrepareFrame(std::move(extracted),{}));
            OvRendering::Data::RenderFrame frame; frame.Append(std::move(commands)); frame.Seal();
            auto checkPixel=[&] {
                output.Bind(); uint8_t rgba[4]{}; glReadPixels(int(screen.x),int(screen.y),1,1,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
                Require(rgba[0]>240 && rgba[1]<10 && rgba[2]<10,"raycast point matches actual red GPU pixel"); baregl::Framebuffer::BindDefault();
            };
            if(engine.render) { auto done=engine.render->Submit(std::move(frame));engine.render->WaitIdle();done.RethrowIfFailed();engine.render->Invoke([&](auto&){checkPixel();}); }
            else { frame.Submit(*engine.driver);frame.Wait();frame.GetCompletion().RethrowIfFailed();checkPixel(); }
            const int before=InputField(graphicProbe,"clicks");
            UIPoint client{screen.x+10,120-screen.y};
            push({UIInputKind::PointerDown,0,client});push({UIInputKind::PointerUp,0,client});tick();
            Require(InputField(graphicProbe,"clicks")==before+1,"production GraphicRaycaster hits Overlay/Camera/World visible pixel");
        }
        auto renderPixel=[&](int x,int y,int red,int green) {
            OvCore::Rendering::RenderFrontend frontend; OvRendering::Data::Material fallback;
            OvCore::Rendering::SceneRenderRequest mainRequest{{scene,camera.GetCamera(),100,100,1,1}};
            OvCore::Rendering::SceneFrameRequest request{mainRequest,output,fallback};request.generateShadows=request.captureReflections=false;
            auto commands=frontend.RecordFrame(frontend.PrepareFrame(frontend.ExtractFrame(request),{}));
            OvRendering::Data::RenderFrame frame;frame.Append(std::move(commands));frame.Seal();
            auto check=[&] { output.Bind();uint8_t rgba[4]{};glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
                Require(std::abs(int(rgba[0])-red)<10 && std::abs(int(rgba[1])-green)<10,"mask/sort reference GPU pixel");baregl::Framebuffer::BindDefault(); };
            if(engine.render) { auto done=engine.render->Submit(std::move(frame));engine.render->WaitIdle();done.RethrowIfFailed();engine.render->Invoke([&](auto&){check();}); }
            else {frame.Submit(*engine.driver);frame.Wait();frame.GetCompletion().RethrowIfFailed();check();}
        };
        auto click=[&](double x,double y) { UIPoint client{x+10,120-y};push({UIInputKind::PointerDown,0,client});push({UIInputKind::PointerUp,0,client});tick(); };
        canvas.SetRenderMode(CCanvas::RenderMode::ScreenSpaceOverlay);
        auto& maskActor=scene.CreateActor("RectMask");maskActor.SetParent(canvasActor);maskActor.AddComponent<CRectMask2D>();
        auto& maskRect=*maskActor.GetComponent<CRectTransform>();maskRect.SetAnchorMin({0,0});maskRect.SetAnchorMax({0,0});maskRect.SetPivot({0,0});maskRect.SetAnchoredPosition({20,20});maskRect.SetSizeDelta({20,20});
        graphicActor.SetParent(maskActor);rect.SetAnchoredPosition({-10,-10});rect.SetSizeDelta({40,40});
        int before=InputField(graphicProbe,"clicks");renderPixel(15,30,0,0);click(15,30);
        Require(InputField(graphicProbe,"clicks")==before,"RectMask excludes invisible raycast area");
        renderPixel(30,30,255,0);click(30,30);Require(InputField(graphicProbe,"clicks")==before+1,"RectMask visible region receives click");
        maskActor.AddComponent<CCanvasGroup>().SetBlocksRaycasts(false);renderPixel(30,30,255,0);click(30,30);
        Require(InputField(graphicProbe,"clicks")==before+1,"CanvasGroup blocksRaycasts=false preserves pixels and rejects input");
        graphicActor.SetParent(stencilActor);stencilActor.SetActive(true);
        auto& stencilRect=*stencilActor.GetComponent<CRectTransform>();stencilRect.SetAnchorMin({0,0});stencilRect.SetAnchorMax({0,0});stencilRect.SetPivot({0,0});stencilRect.SetAnchoredPosition({20,20});stencilRect.SetSizeDelta({20,20});
        renderPixel(15,30,0,0);click(15,30);Require(InputField(graphicProbe,"clicks")==before+1,"stencil rejects invisible raycast area");
        renderPixel(30,30,255,0);click(30,30);Require(InputField(graphicProbe,"clicks")==before+2,"stencil visible region receives click");
        frontCanvasActor.SetActive(true);auto& frontRect=*frontGraphic.GetComponent<CRectTransform>();
        frontRect.SetAnchorMin({0,0});frontRect.SetAnchorMax({0,0});frontRect.SetPivot({0,0});frontRect.SetAnchoredPosition({20,20});frontRect.SetSizeDelta({20,20});
        renderPixel(30,30,0,255);click(30,30);
        Require(InputField(frontProbe,"clicks")==1 && InputField(graphicProbe,"clicks")==before+2,"multiple Canvas sorting agrees with foreground pixel");
        frontCanvasActor.SetActive(false);graphicActor.SetParent(canvasActor);stencilActor.SetActive(false);
        canvas.SetRenderMode(CCanvas::RenderMode::ScreenSpaceCamera);rect.SetAnchoredPosition({30,30});rect.SetSizeDelta({40,40});
        scene.Update(2);input.ClearEvents();renderPixel(50,50,255,0);click(50,50);
        Require(InputField(graphicProbe,"clicks")==before+2,"native 2D/3D blocking objects occlude Graphic raycast");
        scene.Update(3);input.ClearEvents();renderPixel(50,50,255,0);click(50,50);
        Require(InputField(graphicProbe,"clicks")==before+3,"disabling physics blocking restores visible Graphic input");
    }
    executor.reset();
    engine.scenes.UnloadCurrentScene(); scripts.ClearCSharpProject();
    auto collect=reinterpret_cast<OvCsCollectibleStatsFn>(host.Entry(L"Overload.ManagedHost.Bootstrap, Overload.ManagedHost",L"CollectibleStats"));
    uint64_t alive=0,collected=0; host.Check(collect(1,&alive,&collected),"collect F sessions");
    Require(alive==0 && collected>=2,"input registries do not retain retired project sessions");
    host.Shutdown(); std::puts("PASS F native input, real CLR dispatch, reload, focus and scene lifetime"); return 0;
}
catch(const std::exception& e) { std::fprintf(stderr,"FAIL %s\n",e.what()); return 1; }
