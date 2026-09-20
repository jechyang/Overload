#define wmain HostValidationMain
#include "Tests/Scripting/Integration/native_host_validation.cpp"
#undef wmain
#include <OvCore/Global/ServiceLocator.h>
#include <OvCore/Scripting/ScriptEngine.h>
#include <OvCore/UI/Frame.h>
#include <OvCore/Rendering/RenderFrontend.h>
#include <OvCore/Rendering/FramebufferUtil.h>
#include <baregl/detail/glad/glad.h>
#include <baregl/ResourceExecutor.h>
#include <cmath>
#include <fstream>
using namespace OvCore::ECS::Components;
using OvCore::Scripting::ScriptIdentity;
static auto& Add(OvCore::ECS::Actor& a,const char* type){return a.AddBehaviour(ScriptIdentity::CSharp("GraphicRuntime",type));}
static auto& Rect(OvCore::SceneSystem::Scene& scene,OvCore::ECS::Actor& parent,const char* name,OvMaths::FVector2 pos,OvMaths::FVector2 size){auto& a=scene.CreateActor(name);a.SetParent(parent);auto& r=a.AddComponent<CRectTransform>();r.SetAnchorMin({0,0});r.SetAnchorMax({0,0});r.SetPivot({0,0});r.SetAnchoredPosition(pos);r.SetSizeDelta(size);return a;}
static void Render(Engine& engine,OvCore::SceneSystem::Scene& scene,bool effect,bool masked=true){
 baregl::Framebuffer target;auto setup=[&](auto&){OvCore::Rendering::FramebufferUtil::SetupFramebuffer(target,96,96,true,true,true);};if(engine.render)engine.render->Invoke(setup);else setup(*engine.driver);auto out=target.RecordTarget();
 OvRendering::Entities::Camera camera;camera.SetClearColor({0,0,0});OvRendering::Data::Material fallback;
 OvCore::Rendering::SceneRenderRequest main{{scene,camera,96,96,1,1}};OvCore::Rendering::SceneFrameRequest request{main,out,fallback};request.generateShadows=request.captureReflections=false;
 OvCore::Rendering::RenderFrontend frontend;auto frame=frontend.RecordFrame(frontend.PrepareFrame(frontend.ExtractFrame(request),{}));
 std::vector<uint8_t> pixels(96*96*4);
 auto draw=[&](auto& driver){frame.Execute(driver);out.Bind();uint8_t pixel[4]{};glReadPixels(48,48,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);Require(pixel[0]==(effect?255:0)&&pixel[1]==(effect?0:255),"CLR modifier pixels under stencil");glReadPixels(20,20,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);Require(pixel[0]==0&&pixel[1]==(masked?0:255),"CLR mask diamond corner visibility");glReadPixels(0,0,96,96,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());baregl::Framebuffer::BindDefault();};
 if(engine.render)engine.render->Invoke(draw);else draw(*engine.driver);
 std::ofstream rgba(RuntimeHost::ExecutableDirectory()/(engine.render?"b-render-thread.rgba":"b-default.rgba"),std::ios::binary|std::ios::app);rgba.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());
}
int wmain(int argc,wchar_t** argv)try{
 Require(argc==5,"runtime,host,fixture,mode");std::setvbuf(stdout,nullptr,_IONBF,0);OvReflection::MainThread authority;RuntimeHost host(authority);host.Start(HostPaths::Resolve(RuntimeHost::ExecutableDirectory(),argv[1],argv[2]));
 std::filesystem::path assembly=argv[3],project=assembly.parent_path();OvCore::Scripting::ScriptEngine scripts(project,project);OvCore::Global::ServiceLocator::Provide(scripts);
 Engine engine(std::wstring(argv[4])==L"render-thread");
 std::unique_ptr<baregl::ResourceExecutor> executor;
 if(engine.render)executor=std::make_unique<baregl::ResourceExecutor>([&](std::function<void()> fn){engine.render->Invoke([&](auto&){fn();});});
 OvCore::Global::ServiceLocator::Provide(engine.scenes);Require(scripts.LoadCSharpProject(host,project,assembly,"GraphicRuntime"),"load B fixture");
 auto& scene=*engine.scenes.GetCurrentScene();auto& root=scene.CreateActor("Canvas");root.AddComponent<CCanvas>();Add(root,"GraphicRuntime.RootGroup");Add(root,"GraphicRuntime.RootClip");
 auto& mask=Rect(scene,root,"Mask",{16,16},{64,64});Add(mask,"GraphicRuntime.MaskShape");Add(mask,"GraphicRuntime.HiddenMask");
 auto& child=Rect(scene,mask,"Shape",{-16,-16},{96,96});Add(child,"GraphicRuntime.Shape");auto& modifier=Add(child,"GraphicRuntime.ChannelModifier");
 auto frame=OvCore::UI::ExtractFrame(scene,{96,96});Require(frame.meshes.size()==3,"mask,child,pop production frame");
 auto* renderer=child.GetComponent<CCanvasRenderer>();Require(renderer&&renderer->GetVertices().size()==4,"CLR geometry submitted");
 const auto& v=renderer->GetVertices()[0];Require(v.position.x==3&&v.normal.x==.2f&&v.tangent.w==-1&&v.uv3.y==.6f,"CLR custom channels across ABI");
 Require(frame.RaycastGraphic(child.GetGUID(),{48,48}).has_value()&&!frame.RaycastGraphic(child.GetGUID(),{20,20}),"CLR stencil raycast");Render(engine,scene,true);
 OvCore::Scripting::ManagedFieldValue disabled;disabled.tag=OVCS_FIELD_BOOL;disabled.scalar.boolean=0;Require(modifier.EditManagedField("m_Enabled",disabled),"disable modifier via Inspector");
 OvCore::UI::ExtractFrame(scene,{96,96});Render(engine,scene,false);
 Require(scripts.LoadCSharpProject(host,project,assembly,"GraphicRuntime",true),"hot reload custom modifier and mask");OvCore::UI::ExtractFrame(scene,{96,96});Render(engine,scene,false);
 disabled.scalar.boolean=1;Require(modifier.EditManagedField("m_Enabled",disabled),"reenable modifier before removing it");OvCore::UI::ExtractFrame(scene,{96,96});Render(engine,scene,true);
 Require(child.RemoveBehaviour(ScriptIdentity::CSharp("GraphicRuntime","GraphicRuntime.ChannelModifier")),"destroy modifier");OvCore::UI::ExtractFrame(scene,{96,96});Render(engine,scene,false);
 Require(mask.RemoveBehaviour(ScriptIdentity::CSharp("GraphicRuntime","GraphicRuntime.HiddenMask")),"remove stencil mask");
 frame=OvCore::UI::ExtractFrame(scene,{96,96});Require(frame.meshes.size()==2&&frame.RaycastGraphic(child.GetGUID(),{20,20}).has_value(),"removed mask releases stencil and hit filtering");Render(engine,scene,false,false);
 root.SetActive(false);Require(OvCore::UI::ExtractFrame(scene,{96,96}).meshes.empty(),"disabled parent excludes entire graphic subtree");root.SetActive(true);OvCore::UI::ExtractFrame(scene,{96,96});
 scripts.ClearCSharpProject();Require(renderer->GetVertices().empty(),"closing clears geometry");
 Require(!root.GetComponent<CCanvasGroup>()->IsEnabled()&&!root.GetComponent<CRectMask2D>()->IsEnabled(),"closing removes managed group and rect-mask effects");engine.scenes.UnloadCurrentScene();
 auto collect=reinterpret_cast<OvCsCollectibleStatsFn>(host.Entry(L"Overload.ManagedHost.Bootstrap, Overload.ManagedHost",L"CollectibleStats"));uint64_t alive=0,collected=0;host.Check(collect(1,&alive,&collected),"collect B contexts");Require(alive==0&&collected>=2,"no static graphic/modifier roots");
 host.Shutdown();std::puts("PASS B real CLR modifier, all vertex channels, mask pixels/hits, disable/remove/reload/close");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"B runtime: %s\n",e.what());return 1;}
