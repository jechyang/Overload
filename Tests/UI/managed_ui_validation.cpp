#define wmain HostValidationMain
#include "Tests/Scripting/Integration/native_host_validation.cpp"
#undef wmain
#include <OvCore/Global/ServiceLocator.h>
#include <OvCore/Scripting/ScriptEngine.h>
#include <OvCore/ECS/Components/CUI.h>
#include <OvCore/UI/Frame.h>
#include <OvCore/Rendering/RenderFrontend.h>
#include <OvCore/Rendering/FramebufferUtil.h>
#include <baregl/ResourceExecutor.h>
#include <baregl/detail/glad/glad.h>
#include <tinyxml2.h>
#include <cmath>
#include <array>

using namespace OvCore::ECS::Components;
using Behaviour = OvCore::ECS::Components::Behaviour;
using OvCore::Scripting::ScriptIdentity;
static bool Near(float a, float b) { return std::abs(a-b)<0.002f; }
static Behaviour& Add(OvCore::ECS::Actor& actor, const char* name)
{ return actor.AddBehaviour(ScriptIdentity::CSharp("ManagedUI",name)); }
static int Field(Behaviour& behaviour, const char* name)
{
    for (const auto& field : behaviour.GetScript()->ManagedFields())
        if (field.name == name) return behaviour.GetScript()->ReadManagedField(field.token)->scalar.int32_value;
    throw std::runtime_error("Missing managed field");
}
static void Render(OvCore::SceneSystem::Scene& scene, OvRendering::Context::Driver& driver, uint16_t width=300, uint16_t height=200,
    bool expectTriangle=false)
{
    baregl::Framebuffer target; OvCore::Rendering::FramebufferUtil::SetupFramebuffer(target,width,height,true,true,true);
    auto output=target.RecordTarget();
    OvRendering::Entities::Camera camera; camera.SetClearColor({0,0,0});
    OvRendering::Data::Material fallback;
    OvCore::Rendering::SceneRenderRequest main{{scene,camera,width,height,1,1}};
    OvCore::Rendering::SceneFrameRequest request{main,output,fallback};
    request.generateShadows=request.captureReflections=false;
    OvCore::Rendering::RenderFrontend frontend;
    auto extracted=frontend.ExtractFrame(request);
    auto prepared=frontend.PrepareFrame(std::move(extracted),{});
    auto commands=frontend.RecordFrame(std::move(prepared));
    OvRendering::Data::RenderFrame frame; frame.Append(std::move(commands)); frame.Seal();
    frame.Submit(driver); frame.Wait(); frame.GetCompletion().RethrowIfFailed();
    output.Bind();
    for (const auto pixel : {std::array<int,5>{55,180,255,0,0}, {150,180,0,255,0}, {108,180,0,0,0}})
    {
        uint8_t color[4]{}; glReadPixels(pixel[0]*width/300,pixel[1]*height/200,1,1,GL_RGBA,GL_UNSIGNED_BYTE,color);
        Require(color[0]==pixel[2] && color[1]==pixel[3] && color[2]==pixel[4],"managed layout production pixels");
    }
    if (expectTriangle) {
        uint8_t triangle[4]{}; glReadPixels(230*width/300,35*height/200,1,1,GL_RGBA,GL_UNSIGNED_BYTE,triangle);
        Require(triangle[0]+triangle[1]+triangle[2]>100 && triangle[3]>200,"managed custom Graphic production pixel");
        for (const auto point : {std::array<int,2>{280,20}, {280,50}}) {
            uint8_t white[4]{}; glReadPixels(point[0]*width/300,point[1]*height/200,1,1,GL_RGBA,GL_UNSIGNED_BYTE,white);
            Require(white[0]>240 && white[1]>240 && white[2]>240 && white[3]>240,"managed Image/RawImage production pixel");
        }
    }
    baregl::Framebuffer::BindDefault();
}
int wmain(int argc, wchar_t** argv) try
{
    Require(argc==4 || argc==5,"managed UI arguments: runtime, managed host, fixture assembly, optional demo");
    std::setvbuf(stdout,nullptr,_IONBF,0);
    OvReflection::MainThread main; RuntimeHost host(main);
    host.Start(HostPaths::Resolve(RuntimeHost::ExecutableDirectory(),argv[1],argv[2]));
    const std::filesystem::path assembly=argv[3], project=assembly.parent_path();
    OvCore::Scripting::ScriptEngine scripts(project,project); OvCore::Global::ServiceLocator::Provide(scripts);
    Engine engine(false); OvCore::Global::ServiceLocator::Provide(engine.scenes);
    const bool demo=argc==5;
    Require(scripts.LoadCSharpProject(host,project,assembly,demo?"GameScripts":"ManagedUI"),"load real project C# UI classes");
    auto& scene=*engine.scenes.GetCurrentScene();
    if(demo)
    {
        tinyxml2::XMLDocument doc;
        Require(doc.LoadFile("Examples/ManagedUI/Assets/Scenes/ManagedUI.ovscene")==tinyxml2::XML_SUCCESS,"load shipped C# UI example");
        scene.OnDeserialize(doc,doc.RootElement()->FirstChildElement("scene"));
        auto& gameplay=scene.CreateActor("Gameplay").AddBehaviour(ScriptIdentity::CSharp("GameScripts","UIFirstFrame.GameplayOnly"));
        auto& layout=scene.CreateActor("LayoutProbe").AddBehaviour(ScriptIdentity::CSharp("GameScripts","UIFirstFrame.LayoutProbe"));
        auto& interfaceLayout=scene.FindActorByName("A")->AddBehaviour(ScriptIdentity::CSharp("GameScripts","UIFirstFrame.InterfaceProbe"));
        OvCore::SceneSystem::Scene dormant;
        auto& dormantLayout=dormant.CreateActor("Other scene").AddBehaviour(ScriptIdentity::CSharp("GameScripts","UIFirstFrame.LayoutProbe"));
        Require(!gameplay.GetScript()->IsValid() && !layout.GetScript()->IsValid(),"scene loading is metadata-only");
        // No Inspector preparation, selection or Play before the first frame.
        // Serialized rectangles match 300x200; a different viewport proves the
        // first render actually ran C# scaler and layout rather than cached data.
        const auto first=OvCore::UI::ExtractFrame(scene,{600,400});
        Require(first.images.empty() && first.meshes.size()==2 &&
            Near(first.meshes[0].vertices[1].position.x-first.meshes[0].vertices[0].position.x,180) &&
            Near(first.meshes[1].vertices[0].position.x,230),
            "first edit frame initializes C# UI without selection or Play");
        Render(scene,*engine.driver,600,400);
        Require(layout.GetScript()->IsValid() && Field(layout,"awakes")==0 && !gameplay.GetScript()->IsValid(),
            "UI preparation does not awaken components or construct gameplay scripts");
        Require(!dormantLayout.GetScript()->IsValid(),"UI preparation only touches the rendered scene");
        Require(interfaceLayout.GetScript()->IsValid() && Field(interfaceLayout,"measurements")>0,
            "first edit frame discovers ordinary Behaviour layout interfaces");
        const auto ui=OvCore::UI::ExtractFrame(scene,{300,200});
        Require(ui.images.empty() && ui.meshes.size()==2 &&
            Near(ui.meshes[0].vertices[1].position.x-ui.meshes[0].vertices[0].position.x,90) &&
            Near(ui.meshes[1].vertices[0].position.x,115),"shipped scene uses project C# graphics and layout");
        Render(scene,*engine.driver);
        Require(scripts.LoadCSharpProject(host,project,assembly,"GameScripts",true),"hot reload edit-mode UI project");
        const auto reloaded=OvCore::UI::ExtractFrame(scene,{600,400});
        Require(reloaded.meshes.size()==2 &&
            Near(reloaded.meshes[0].vertices[1].position.x-reloaded.meshes[0].vertices[0].position.x,180) &&
            Field(layout,"awakes")==0 && !gameplay.GetScript()->IsValid(),
            "edit-mode reload preserves UI classification and does not start gameplay");
        scene.Play();
        Require(Field(layout,"awakes")==1 && Field(gameplay,"awakes")==1,"Play awakens prepared UI and ordinary gameplay once");
        const auto resized=OvCore::UI::ExtractFrame(scene,{600,400});
        Require(Near(resized.meshes[0].vertices[1].position.x-resized.meshes[0].vertices[0].position.x,180) &&
            Near(resized.meshes[1].vertices[0].position.x,230),"C# CanvasScaler responds to viewport resize");
        engine.scenes.UnloadCurrentScene(); scripts.ClearCSharpProject(); host.Shutdown();
        std::puts("PASS shipped managed UI example"); return 0;
    }
    auto& root=scene.CreateActor("Canvas"); root.AddComponent<CCanvas>();
    auto& group=Add(root,"ScriptUI.CustomHorizontalLayout"); Add(root,"UIFixture.Scaler");
    auto& a=scene.CreateActor("A"); a.SetParent(root); a.AddComponent<CUIImage>().SetColor({1,0,0,1});
    auto& b=scene.CreateActor("B"); b.SetParent(root); b.AddComponent<CUIImage>().SetColor({0,1,0,1});
    a.GetComponent<CRectTransform>()->SetPivot({0,0}); b.GetComponent<CRectTransform>()->SetPivot({0,0});
    Add(a,"UIFixture.Left"); Add(a,"UIFixture.ExtraPreferred"); Add(b,"UIFixture.Right");
    auto& panel=scene.CreateActor("Panel"); Add(panel,"UIFixture.Column"); Add(panel,"UIFixture.FittedPanel");
    auto& small=scene.CreateActor("Small"); small.SetParent(panel); small.AddComponent<CRectTransform>().SetPivot({0,0}); Add(small,"UIFixture.Small");
    auto& large=scene.CreateActor("Large"); large.SetParent(panel); large.AddComponent<CRectTransform>().SetPivot({0,0}); Add(large,"UIFixture.Large");
    auto& observer=scene.CreateActor("Observer"); observer.AddComponent<CRectTransform>(); Add(observer,"UIFixture.RebuildObserver");
    auto& graphicCanvas=scene.CreateActor("GraphicCanvas"); graphicCanvas.AddComponent<CCanvas>();
    auto& triangle=scene.CreateActor("Triangle"); triangle.SetParent(graphicCanvas);
    auto& triangleRect=triangle.AddComponent<CRectTransform>(); triangleRect.SetAnchorMin({0,0}); triangleRect.SetAnchorMax({0,0});
    triangleRect.SetPivot({0,0}); triangleRect.SetAnchoredPosition({200,10}); triangleRect.SetSizeDelta({60,60});
    auto& triangleGraphic=Add(triangle,"UIFixture.TriangleGraphic");
    auto& image=scene.CreateActor("Image"); image.SetParent(graphicCanvas);
    auto& imageRect=image.AddComponent<CRectTransform>(); imageRect.SetAnchorMin({0,0}); imageRect.SetAnchorMax({0,0});
    imageRect.SetPivot({0,0}); imageRect.SetAnchoredPosition({270,10}); imageRect.SetSizeDelta({20,20}); Add(image,"UIFixture.TexturedImage");
    auto& rawImage=scene.CreateActor("RawImage"); rawImage.SetParent(graphicCanvas);
    auto& rawRect=rawImage.AddComponent<CRectTransform>(); rawRect.SetAnchorMin({0,0}); rawRect.SetAnchorMax({0,0});
    rawRect.SetPivot({0,0}); rawRect.SetAnchoredPosition({270,40}); rawRect.SetSizeDelta({20,20}); Add(rawImage,"UIFixture.TexturedRawImage");
    auto& probe=Add(scene.CreateActor("Probe"),"UIFixture.Probe");
    for(auto* actor:scene.GetActors()) for(auto& [key, behaviour]:actor->GetBehaviours()) Require(scripts.PrepareCSharp(behaviour),"prepare managed component");
    OvCore::Scripting::ManagedFieldValue authoredGap; authoredGap.tag=OVCS_FIELD_FLOAT32; authoredGap.scalar.float32_value=7;
    Require(group.EditManagedField("extraGap",authoredGap),"author project C# layout field through production field editor API");
    scene.Play();
    auto ui=OvCore::UI::ExtractFrame(scene,{300,200});
    Require(ui.images.size()==2 && Near(ui.images[0].rect.min.x,10) && Near(ui.images[0].rect.size.x,90)
        && Near(ui.images[1].rect.min.x,117) && Near(ui.images[1].rect.size.x,180),"C# override drives native layout and custom gap");
    Require(ui.meshes.size()==3 && ui.meshes[0].vertices.size()==3 && ui.meshes[0].indices.size()==3 &&
        Field(triangleGraphic,"populations")==1,"project Graphic override uploads a triangle through CanvasRenderer");
    Require(image.GetComponent<CCanvasRenderer>()->GetTexturePath()=="Textures/Checker.png" &&
        image.GetComponent<CCanvasRenderer>()->GetMaterialPath()=="Materials/UI.mat" &&
        rawImage.GetComponent<CCanvasRenderer>()->GetTexturePath()=="Textures/Checker.png" &&
        Near(ui.meshes[2].vertices[0].uv.x,0.25f) && Near(ui.meshes[2].vertices[0].uv.y,0.5f),
        "Image/RawImage submit texture, material and uvRect through the managed bridge");
    scene.Update(1);
    Require(Field(probe,"passed")==1,"managed API, layout, scaler, interface lookup and wrong-thread contracts");
    Render(scene,*engine.driver,300,200,true);
    const int before=Field(group,"horizontalPasses");
    Require(before>1,"managed layout runs before production rendering");
    a.RemoveComponent<CRectTransform>(); a.AddComponent<CRectTransform>().SetPivot({0,0});
    scene.Update(2); Require(Field(probe,"passed")==2,"native UI handles retain component epochs");
    Require(scripts.LoadCSharpProject(host,project,assembly,"ManagedUI",true),"hot reload managed UI project");
    ui=OvCore::UI::ExtractFrame(scene,{300,200});
    Require(Near(ui.images[0].rect.size.x,90) && Near(ui.images[1].rect.min.x,117),"layout fields and overridden behavior survive reload");
    Require(Field(group,"horizontalPasses")==before+1,"custom managed state preserved and old rebuild queue retired");
    Require(ui.meshes.size()==3 && Field(triangleGraphic,"populations")==3,"Graphic/Image/RawImage dirty rebuild and hot reload use the new session");
    // Detached scene serialization contains managed identities/fields, no C++ layout components.
    tinyxml2::XMLDocument doc; auto* xml=doc.NewElement("root"); doc.InsertEndChild(xml); scene.OnSerialize(doc,xml);
    const auto sample=project/"ManagedUI.ovscene"; Require(doc.SaveFile(sample.string().c_str())==tinyxml2::XML_SUCCESS,"save managed UI scene");
    tinyxml2::XMLPrinter printed; doc.Print(&printed);
    Require(std::string(printed.CStr()).find("CHorizontalLayoutGroup")==std::string::npos,"no native layout component is serialized");
    {
        OvCore::SceneSystem::Scene restored; restored.OnDeserialize(doc,xml->FirstChildElement("scene"));
        auto* restoredGroup=restored.FindActorByName("Canvas")->GetBehaviour(ScriptIdentity::CSharp("ManagedUI","ScriptUI.CustomHorizontalLayout"));
        Require(restoredGroup && scripts.PrepareCSharp(*restoredGroup),"restore managed layout component from scene");
        bool found=false;
        for(const auto& field:restoredGroup->GetScript()->ManagedFields()) if(field.name=="extraGap")
            found=Near(restoredGroup->GetScript()->ReadManagedField(field.token)->scalar.float32_value,7);
        Require(found,"authored layout field survives XML roundtrip");
    }
    engine.scenes.UnloadCurrentScene(); scripts.ClearCSharpProject(); host.Shutdown();
    std::puts("PASS managed UI integration and lifetime"); return 0;
}
catch(const std::exception& error) { std::fprintf(stderr,"FAIL: %s\n",error.what()); return 1; }
