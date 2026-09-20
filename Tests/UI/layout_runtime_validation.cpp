#define wmain HostValidationMain
#include "Tests/Scripting/Integration/native_host_validation.cpp"
#undef wmain
#include <OvCore/Global/ServiceLocator.h>
#include <OvCore/Scripting/ScriptEngine.h>
#include <OvCore/Scripting/CSharp/DrivenBridge.h>
#include <OvCore/ECS/Components/CUI.h>
#include <OvCore/UI/Frame.h>
#include <OvCore/Rendering/RenderFrontend.h>
#include <OvCore/Rendering/FramebufferUtil.h>
#include <baregl/ResourceExecutor.h>
#include <baregl/detail/glad/glad.h>
#include <tinyxml2.h>
#include <array>
#include <cmath>
using namespace OvCore::ECS::Components;
using OvCore::Scripting::ScriptIdentity;
static bool Near(float a,float b){return std::abs(a-b)<.003f;}
static auto& Add(OvCore::ECS::Actor& actor,const char* name){return actor.AddBehaviour(ScriptIdentity::CSharp("LayoutRuntime",name));}
static void SetInt(OvCore::ECS::Components::Behaviour& b,const char* field,int value){
    OvCore::Scripting::ManagedFieldValue v;v.tag=OVCS_FIELD_INT32;v.scalar.int32_value=value;
    Require(b.EditManagedField(field,v),"edit production layout field");
}
static int Int(OvCore::ECS::Components::Behaviour& b,const char* name){
    for(const auto& f:b.GetScript()->ManagedFields())if(f.name==name)return b.GetScript()->ReadManagedField(f.token)->scalar.int32_value;
    throw std::runtime_error("missing field");
}
static void Render(Engine& engine,OvCore::SceneSystem::Scene& scene,const std::vector<OvMaths::FVector2>& centers){
    baregl::Framebuffer target;
    auto setup=[&](auto&){OvCore::Rendering::FramebufferUtil::SetupFramebuffer(target,300,200,true,true,true);};
    if(engine.render)engine.render->Invoke(setup);else setup(*engine.driver);
    auto output=target.RecordTarget();
    OvRendering::Entities::Camera camera;camera.SetClearColor({0,0,0});OvRendering::Data::Material fallback;
    OvCore::Rendering::SceneRenderRequest main{{scene,camera,300,200,1,1}};
    OvCore::Rendering::SceneFrameRequest request{main,output,fallback};request.generateShadows=request.captureReflections=false;
    OvCore::Rendering::RenderFrontend frontend;
    auto extracted=frontend.ExtractFrame(request);auto prepared=frontend.PrepareFrame(std::move(extracted),{});auto commands=frontend.RecordFrame(std::move(prepared));
    OvRendering::Data::RenderFrame frame;frame.Append(std::move(commands));frame.Seal();
    auto draw=[&](auto& driver){frame.Submit(driver);frame.Wait();frame.GetCompletion().RethrowIfFailed();output.Bind();
        for(auto center:centers){uint8_t pixel[4]{};glReadPixels(int(center.x),int(center.y),1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);Require(pixel[0]>240&&pixel[1]<5&&pixel[2]<5,"production layout center pixel");}
        uint8_t gap[4]{};glReadPixels(2,2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,gap);Require(gap[0]<5&&gap[1]<5&&gap[2]<5,"production layout padding pixel");
        baregl::Framebuffer::BindDefault();};
    if(engine.render)engine.render->Invoke(draw);else draw(*engine.driver);
    // Resources retain their command leases until completion; destructor queues release.
}
int wmain(int argc,wchar_t** argv)try{
    Require(argc==5,"layout runtime arguments: runtime,host,fixture,default/render-thread");
    std::setvbuf(stdout,nullptr,_IONBF,0);OvReflection::MainThread authority;RuntimeHost host(authority);
    host.Start(HostPaths::Resolve(RuntimeHost::ExecutableDirectory(),argv[1],argv[2]));
    std::filesystem::path assembly=argv[3],project=assembly.parent_path();
    OvCore::Scripting::ScriptEngine scripts(project,project);OvCore::Global::ServiceLocator::Provide(scripts);
    Engine engine(std::wstring(argv[4])==L"render-thread");OvCore::Global::ServiceLocator::Provide(engine.scenes);
    Require(scripts.LoadCSharpProject(host,project,assembly,"LayoutRuntime"),"load layout runtime fixture");
    auto& scene=*engine.scenes.GetCurrentScene();auto& root=scene.CreateActor("Grid");root.AddComponent<CCanvas>();auto& grid=Add(root,"LayoutRuntime.Grid");
    std::vector<OvCore::ECS::Actor*> children;
    for(int i=0;i<7;++i){auto& c=scene.CreateActor("Cell"+std::to_string(i));c.SetParent(root);c.AddComponent<CUIImage>().SetColor({1,0,0,1});auto* r=c.GetComponent<CRectTransform>();r->SetPivot({.2f,.8f});r->SetAnchorMin({0,0});r->SetAnchorMax({1,1});r->SetSizeDelta({19,23});children.push_back(&c);}
    Require(scripts.PrepareCSharp(grid),"prepare layout class without Play");
    for(int corner=0;corner<4;++corner)for(int axis=0;axis<2;++axis)for(int constraint=0;constraint<3;++constraint){
        SetInt(grid,"corner",corner);SetInt(grid,"axis",axis);SetInt(grid,"constraintMode",constraint);
        auto ui=OvCore::UI::ExtractFrame(scene,{300,200});Require(ui.images.size()==7,"all grid cells reach production frame");
        const int cols=constraint==1?2:constraint==2?4:6,rows=constraint==1?4:constraint==2?2:5;
        const int stride=axis==0?cols:rows;
        const int usedX=axis==0?std::min(cols,7):std::min(cols,(7+stride-1)/stride),usedY=axis==1?std::min(rows,7):std::min(rows,(7+stride-1)/stride);
        std::vector<OvMaths::FVector2> centers;
        for(int i=0;i<7;++i){int x=axis==0?i%stride:i/stride,y=axis==0?i/stride:i%stride;if(corner&1)x=usedX-1-x;if(corner&2)y=usedY-1-y;
            auto* r=children[i]->GetComponent<CRectTransform>();Require(Near(r->GetSizeDelta().x,40)&&Near(r->GetSizeDelta().y,30)&&Near(r->GetAnchoredPosition().x,18+x*45)&&Near(r->GetAnchoredPosition().y,-16-y*37),"native Grid corner/axis/constraint geometry");
            centers.push_back({30.f+x*45,175.f-y*37});}
        Render(engine,scene,centers);
    }
    // At rest neither full-root traversal nor setters should be necessary.
    OvCore::UI::ExtractFrame(scene,{300,200});int passes=Int(grid,"passes");OvCore::UI::ExtractFrame(scene,{300,200});Require(Int(grid,"passes")==passes,"idle frame skips layout measurement");
    tinyxml2::XMLDocument doc;auto* xml=doc.NewElement("rect");doc.InsertEndChild(xml);children[0]->GetComponent<CRectTransform>()->OnSerialize(doc,xml);
    OvCore::SceneSystem::Scene scratch;auto& restored=scratch.CreateActor("restored").AddComponent<CRectTransform>();restored.OnDeserialize(doc,xml);Require(Near(restored.GetSizeDelta().x,19)&&Near(restored.GetAnchorMax().x,1),"XML stores authored driven values");
    OvCore::Scripting::ManagedFieldValue enabled;enabled.tag=OVCS_FIELD_BOOL;enabled.scalar.boolean=0;
    Require(grid.EditManagedField("m_Enabled",enabled),"Inspector disables layout controller");OvCore::UI::ExtractFrame(scene,{300,200});
    Require(children[0]->GetComponent<CRectTransform>()->GetDrivenProperties().Mask()==0&&Near(children[0]->GetComponent<CRectTransform>()->GetSizeDelta().x,19),"Inspector disable restores driven properties");
    enabled.scalar.boolean=1;Require(grid.EditManagedField("m_Enabled",enabled),"Inspector enables layout controller");OvCore::UI::ExtractFrame(scene,{300,200});
    Require(scripts.LoadCSharpProject(host,project,assembly,"LayoutRuntime",true),"hot reload layout session");OvCore::UI::ExtractFrame(scene,{300,200});
    Require(children[0]->GetComponent<CRectTransform>()->GetDrivenProperties().Mask()!=0,"new session reacquires driven properties");
    Require(root.RemoveBehaviour(ScriptIdentity::CSharp("LayoutRuntime","LayoutRuntime.Grid")),"remove edit-mode layout controller");
    for(auto* c:children){auto* r=c->GetComponent<CRectTransform>();Require(r->GetDrivenProperties().Mask()==0&&Near(r->GetSizeDelta().x,19)&&Near(r->GetSizeDelta().y,23)&&Near(r->GetAnchorMax().x,1),"controller removal restores native properties");}
    // Sessions share the native R6 context, but tokens remain isolated.
    auto& rect=*children[0]->GetComponent<CRectTransform>();auto handle=OvCore::Scripting::PublishComponent(host.NativeContext(),rect);auto state=OvReflection::Script::Acquire(host.NativeContext().Id());uint64_t token=0,result=0;
    QueryDriven(*state,700,handle,60,64,"",token);rect.SetSizeDelta({200,23});
    bool denied=false;try{QueryDriven(*state,701,handle,60,64,std::to_string(token),result);}catch(const OvReflection::Script::Failure&){denied=true;}Require(denied,"shared-context cross-session extension rejected");
    denied=false;try{QueryDriven(*state,701,handle,61,token,"",result);}catch(const OvReflection::Script::Failure&){denied=true;}Require(denied&&Near(rect.GetSizeDelta().x,200),"shared-context cross-session release rejected");
    QueryDriven(*state,700,handle,61,token,"",result);Require(Near(rect.GetSizeDelta().x,19),"legal native session restores authored property");
    for(auto* c:children)c->SetActive(false);
    auto& parent=scene.CreateActor("AspectParent");parent.SetParent(root);auto& pr=parent.AddComponent<CRectTransform>();pr.SetAnchorMin({0,0});pr.SetAnchorMax({0,0});pr.SetPivot({0,0});pr.SetSizeDelta({300,200});auto& child=scene.CreateActor("AspectChild");child.SetParent(parent);auto& aspectRect=child.AddComponent<CRectTransform>();aspectRect.SetAnchorMin({.1f,.2f});aspectRect.SetAnchorMax({.9f,.8f});aspectRect.SetPivot({.2f,.8f});aspectRect.SetSizeDelta({20,30});Add(child,"LayoutRuntime.Aspect");child.AddComponent<CUIImage>().SetColor({1,0,0,1});
    OvCore::UI::ExtractFrame(scene,{300,200});Require(Near(aspectRect.GetRect().size.x,300)&&Near(aspectRect.GetRect().size.y,150),"native stretch aspect fit");Render(engine,scene,{{150,115}});
    parent.GetComponent<CRectTransform>()->SetSizeDelta({600,100});OvCore::UI::ExtractFrame(scene,{300,200});Require(Near(aspectRect.GetRect().size.x,200)&&Near(aspectRect.GetRect().size.y,100),"native parent resize precise dirty");Render(engine,scene,{{180,50}});
    auto& content=scene.CreateActor("Content");content.SetParent(root);auto& contentRect=content.AddComponent<CRectTransform>();contentRect.SetAnchorMin({.1f,.2f});contentRect.SetAnchorMax({.9f,.8f});contentRect.SetSizeDelta({11,13});Add(content,"LayoutRuntime.Content");Add(content,"LayoutRuntime.Sizes");
    OvCore::UI::ExtractFrame(scene,{300,200});Require(Near(contentRect.GetRect().size.x,70)&&Near(contentRect.GetRect().size.y,25),"native content fitter min/preferred");
    scripts.ClearCSharpProject();Require(aspectRect.GetDrivenProperties().Mask()==0&&Near(aspectRect.GetSizeDelta().x,20)&&Near(aspectRect.GetSizeDelta().y,30),"session closing restores native leases");
    Require(contentRect.GetDrivenProperties().Mask()==0&&Near(contentRect.GetSizeDelta().x,11)&&Near(contentRect.GetSizeDelta().y,13),"closing restores content fitter");
    engine.scenes.UnloadCurrentScene();
    const auto collect=reinterpret_cast<OvCsCollectibleStatsFn>(host.Entry(L"Overload.ManagedHost.Bootstrap, Overload.ManagedHost",L"CollectibleStats"));
    uint64_t alive=0,collected=0;host.Check(collect(1,&alive,&collected),"collect retired layout sessions");Require(alive==0&&collected>=2,"layout trackers and observations do not retain retired project contexts");
    host.Shutdown();std::puts("PASS U4 real engine geometry, pixels, driven XML, reload and closing");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"U4 runtime failed: %s\n",error.what());return 1;}
