#define main BaselineMain
#include "native_ui_validation.cpp"
#undef main
#include <OvCore/UI/Geometry.h>
#include <OvCore/UI/Fonts/Font.h>
#include <cstring>

static Mesh Quad(float x,float y,float w,float h,OvMaths::FVector4 color,uint64_t sequence)
{
    Mesh mesh;mesh.actorGuid=sequence+1;mesh.sequence=sequence;mesh.clip={{0,0},{96,96}};
    mesh.vertices={{{x,y,0},{0,0},color},{{x+w,y,0},{1,0},color},{{x+w,y+h,0},{1,1},color},{{x,y+h,0},{0,1},color}};
    mesh.indices={0,1,2,0,2,3};mesh.screenCorners={OvMaths::FVector2{x,y},{x,y+h},{x+w,y+h},{x+w,y}};mesh.hasScreenCorners=true;
    return mesh;
}
static Frame MaskFrame(CCanvas::RenderMode mode)
{
    Frame frame{0,{96,96},{}};
    auto mask=Quad(16,16,64,64,{1,1,1,1},0);mask.stencil={true,true,false,1,0,1};
    // A diamond proves stencil follows generated mesh instead of the mask's bounding rectangle.
    mask.vertices[0].position={48,16,0};mask.vertices[1].position={80,48,0};mask.vertices[2].position={48,80,0};mask.vertices[3].position={16,48,0};
    frame.meshes.push_back(mask);
    auto nested=Quad(0,0,96,56,{1,1,1,1},1);nested.stencil={true,true,false,3,1,2};frame.meshes.push_back(nested);
    auto child=Quad(0,0,96,96,{0,1,0,1},2);child.stencil={true,false,true,3,3,0};frame.meshes.push_back(child);
    auto popNested=nested;popNested.sequence=3;popNested.pop=true;popNested.raycastTarget=false;popNested.stencil={true,true,false,1,1,2};frame.meshes.push_back(popNested);
    auto pop=mask;pop.sequence=4;pop.pop=true;pop.raycastTarget=false;pop.stencil={true,true,false,0,0,1};frame.meshes.push_back(pop);
    frame.meshes.push_back(Quad(2,2,8,8,{0,0,1,1},5));
    for(auto& mesh:frame.meshes) {
        mesh.renderMode=mode;
        mesh.canvasToWorld=OvMaths::FMatrix4::Translation({-1,-1,0})*OvMaths::FMatrix4::Scaling({2.f/96,2.f/96,1});
        mesh.screenToCanvas=OvMaths::FMatrix4::Inverse(mesh.canvasToWorld);
    }
    return frame;
}
static void Pixel(const std::vector<uint8_t>& p,int x,int y,int r,int g,int b)
{
    const size_t i=(size_t(y)*96+x)*4;
    if(std::abs(int(p[i])-r)>1||std::abs(int(p[i+1])-g)>1||std::abs(int(p[i+2])-b)>1) {
        std::fprintf(stderr,"Pixel %d,%d actual %d,%d,%d expected %d,%d,%d\n",x,y,p[i],p[i+1],p[i+2],r,g,b);
        throw std::runtime_error("B GPU pixel mismatch");
    }
}
static std::vector<uint8_t> DrawFrame(const Frame& frame,OvRendering::Context::Driver& driver,OvTools::Jobs::JobSystem& jobs)
{
    baregl::Framebuffer target;OvCore::Rendering::FramebufferUtil::SetupFramebuffer(target,96,96,true,true,true);
    auto output=target.RecordTarget();output.Bind();driver.Clear(true,true,true,{0,0,0,1});
    RenderResources resources;OvRendering::Core::MeshCommandRecorder fallback;Frontend frontend;
    const bool overlay=frame.meshes.empty()||frame.meshes[0].renderMode==CCanvas::RenderMode::ScreenSpaceOverlay;
    OvRendering::Data::RenderViewInput view;view.matrices=OvRendering::Data::ViewMatrices{OvMaths::FMatrix4::Identity,OvMaths::FMatrix4::Identity};
    auto scene=resources.Extract(frame,3,fallback.GetFallback2D(),fallback.GetFallbackCube(),overlay?OvRendering::Data::RenderStage::OverlayUI:OvRendering::Data::RenderStage::SceneUI,overlay?nullptr:&view);
    OvRendering::Data::PipelineState poison;poison.scissorTest=true;poison.stencilTest=true;poison.stencilWriteMask=0;
    auto prepared=frontend.Prepare(scene,poison,&jobs);auto commands=frontend.Record(prepared,output,&jobs);commands.Execute(driver);
    return Pixels(output);
}
static void NativeChecks()
{
    Scene scene("","");auto& actor=scene.CreateActor("sink");auto& renderer=actor.AddComponent<CCanvasRenderer>();
    auto mesh=Quad(0,0,16,16,{1,1,1,1},0);renderer.SetMesh(mesh.vertices,mesh.indices);
    const auto generation=renderer.GetMeshGeneration();auto bad=mesh.vertices;bad[0].uv3.x=std::numeric_limits<float>::quiet_NaN();
    Reject([&]{renderer.SetMesh(bad,mesh.indices);},"NaN extra channel rejected");Require(renderer.GetMeshGeneration()==generation,"bad upload is atomic");
    Reject([&]{renderer.SetMesh(mesh.vertices,std::array<uint32_t,3>{0,1,999});},"bad native index");
    Reject([&]{renderer.SetMaterialCount(65);},"slot limit");renderer.SetMaterialCount(2);renderer.SetIndexRange(1,3,3);
    Reject([&]{renderer.SetIndexRange(1,1,3);},"triangle aligned ranges");
    const auto frame=MaskFrame(CCanvas::RenderMode::ScreenSpaceOverlay);
    Require(frame.RaycastGraphic(3,{48,48}).has_value(),"nested stencil hit inside");
    Require(!frame.RaycastGraphic(3,{20,20}).has_value(),"diamond geometry rejects bounding box corner");
    Require(!frame.RaycastGraphic(3,{48,70}).has_value(),"nested rect rejects hit");
    Require(frame.RaycastGraphic(6,{4,4}).has_value(),"pop restores later sibling hit");
    Scene clipped("","");auto& root=clipped.CreateActor("root");root.AddComponent<CCanvas>();
    auto& mask=clipped.CreateActor("native rect mask");mask.SetParent(root);mask.AddComponent<CRectMask2D>();
    auto* rect=mask.GetComponent<CRectTransform>();rect->SetAnchorMin({0,0});rect->SetAnchorMax({0,0});rect->SetPivot({0,0});rect->SetSizeDelta({20,20});
    auto& graphic=clipped.CreateActor("graphic");graphic.SetParent(mask);auto& sink=graphic.AddComponent<CCanvasRenderer>();
    rect=graphic.GetComponent<CRectTransform>();rect->SetAnchorMin({0,0});rect->SetAnchorMax({0,0});rect->SetPivot({0,0});rect->SetSizeDelta({96,96});
    auto full=Quad(0,0,96,96,{1,1,1,1},0);sink.SetMesh(full.vertices,full.indices);
    const auto before=ExtractFrame(clipped,{96,96});Require(before.meshes.size()==1&&Near(before.meshes[0].clip.size.x,20),"authored native RectMask preserved");
    sink.SetIgnoreParentRectMasks(true);auto after=ExtractFrame(clipped,{96,96});Require(Near(after.meshes[0].clip.size.x,96)&&Near(before.meshes[0].clip.size.x,20),"maskable false bypass and immutable old clip");
    sink.SetMaterialCount(1,true);Require(ExtractFrame(clipped,{96,96}).meshes.size()==2,"pop material without stencil submits after subtree");sink.SetMaterialCount(0,true);
    auto& parentGroup=root.AddComponent<CCanvasGroup>();parentGroup.SetAlpha(.2f);parentGroup.SetBlocksRaycasts(false);
    auto& ownGroup=graphic.AddComponent<CCanvasGroup>();ownGroup.SetIgnoreParentGroups(true);
    after=ExtractFrame(clipped,{96,96});Require(after.meshes[0].raycastTarget&&Near(after.meshes[0].vertices[0].color.w,1),"CanvasGroup ignores parent alpha/raycast state");
    sink.SetIgnoreParentRectMasks(false);auto& independent=graphic.AddComponent<CCanvas>();independent.SetOverrideSorting(true);independent.SetSortingOrder(2);
    after=ExtractFrame(clipped,{96,96});Require(Near(after.meshes[0].clip.size.x,96),"override sorting canvas resets ancestor clipping");
    std::puts("PASS B invalid streams, material ranges, stencil hit geometry, maskable and CanvasGroup boundaries");
}
static void OffCenterCameraCheck(OvRendering::Context::Driver& driver,OvTools::Jobs::JobSystem& jobs)
{
    Scene scene("","");auto& camera=scene.CreateActor("Camera").AddComponent<CCamera>();camera.GetCamera().SetClearColor({0,0,0});auto& root=scene.CreateActor("Canvas");
    auto& canvas=root.AddComponent<CCanvas>();canvas.SetRenderMode(CCanvas::RenderMode::ScreenSpaceCamera);canvas.SetWorldCamera(&camera);canvas.SetPlaneDistance(5);
    Rectangle(scene,root,"OffCenter",{10,20},{20,20},{1,0,0,1});
    const auto ui=ExtractFrame(scene,{96,96},0,&camera.GetCamera());Require(ui.HitTest({16,28}).has_value()&&!ui.HitTest({80,28}),"camera canvas screen-right matches off-center input");
    baregl::Framebuffer target;OvCore::Rendering::FramebufferUtil::SetupFramebuffer(target,96,96,true,true,true);auto out=target.RecordTarget();
    OvRendering::Data::Material fallback;Frontend frontend;OvCore::Rendering::SceneRenderRequest main{{scene,camera.GetCamera(),96,96,1,1}};
    OvCore::Rendering::SceneFrameRequest request{main,out,fallback};request.generateShadows=request.captureReflections=false;
    auto commands=frontend.RecordFrame(frontend.PrepareFrame(frontend.ExtractFrame(request),{},&jobs),&jobs);commands.Execute(driver);
    const auto p=Pixels(out);Pixel(p,16,28,255,0,0);Pixel(p,80,28,0,0,0);
}
static std::vector<uint8_t> capturedPixels;
static void GPUChecks(OvRendering::Context::Driver& driver,OvTools::Jobs::JobSystem& jobs)
{
    OffCenterCameraCheck(driver,jobs);
    {
        Frame frame{0,{96,96},{}};frame.meshes.push_back(Quad(0,0,96,96,{1,1,1,1},0));
        RenderResources resources;OvRendering::Core::MeshCommandRecorder fallback;Frontend frontend;
        auto scene=resources.Extract(frame,9,fallback.GetFallback2D(),fallback.GetFallbackCube());
        // Deliberately malformed producer packet exercises the UI-only guard.
        auto& input=const_cast<OvRendering::Data::MeshInput&>(scene->GetMeshes()[0]);input.stage=OvRendering::Data::RenderStage::Opaque;
        auto& pipeline=const_cast<OvRendering::Data::RenderPipeline&>(scene->GetPipeline());pipeline.stages={OvRendering::Data::RenderStage::Opaque};
        Reject([&]{frontend.Prepare(scene,{},&jobs);},"non-UI stage rejects stencil state override");
    }
    auto mesh=Quad(0,0,96,96,{1,1,1,1},0);
    for(auto& v:mesh.vertices){v.normal={.2f,.3f,.4f};v.tangent={1,2,3,4};v.uv1={5,6};v.uv2={7,8};v.uv3={9,10};}
    {
        OvCore::UI::Geometry geometry(mesh.vertices,mesh.indices);geometry.Bind();
        GLint buffer=0;glGetVertexAttribiv(7,GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,&buffer);Require(buffer!=0,"UV3 GPU binding");
        glBindBuffer(GL_ARRAY_BUFFER,buffer);std::array<UIVertex,4> actual;glGetBufferSubData(GL_ARRAY_BUFFER,0,sizeof(actual),actual.data());
        Require(std::memcmp(actual.data(),mesh.vertices.data(),sizeof(actual))==0,"all GPU channels exact");geometry.Unbind();
    }
    for(auto mode:{CCanvas::RenderMode::ScreenSpaceOverlay,CCanvas::RenderMode::ScreenSpaceCamera,CCanvas::RenderMode::WorldSpace}) {
        const auto frame=MaskFrame(mode);auto pixels=DrawFrame(frame,driver,jobs);capturedPixels.insert(capturedPixels.end(),pixels.begin(),pixels.end());
        Pixel(pixels,48,48,0,255,0);Pixel(pixels,20,20,0,0,0);Pixel(pixels,48,70,0,0,0);Pixel(pixels,4,4,0,0,255);
    }
    auto texture=[](std::array<uint8_t,4> rgba){auto t=std::make_shared<baregl::Texture>(baregl::types::ETextureType::TEXTURE_2D);t->Allocate({.width=1,.height=1,.internalFormat=baregl::types::EInternalFormat::RGBA8,.useMipMaps=false});t->Upload(rgba.data(),baregl::types::EFormat::RGBA,baregl::types::EPixelDataType::UNSIGNED_BYTE);return t;};
    Frame slots{0,{96,96},{}};mesh=Quad(0,0,96,96,{1,1,1,1},0);mesh.materialSlots={{"",texture({255,0,0,255}),nullptr,0,3},{"",texture({0,0,255,255}),nullptr,3,3}};slots.meshes.push_back(mesh);
    auto pixels=DrawFrame(slots,driver,jobs);capturedPixels.insert(capturedPixels.end(),pixels.begin(),pixels.end());Pixel(pixels,70,20,255,0,0);Pixel(pixels,20,70,0,0,255);
    // Synthetic immutable SDF page tests the production font slot/shader without FreeType or text policy.
    auto page=std::make_shared<Fonts::AtlasPage>();page->generation=1;page->revision=1;page->size=4;page->pixels.assign(16,255);
    slots.meshes[0].materialSlots={{"",nullptr,page,0,6}};for(auto& v:slots.meshes[0].vertices)v.uv1={16,-8};
    pixels=DrawFrame(slots,driver,jobs);capturedPixels.insert(capturedPixels.end(),pixels.begin(),pixels.end());Pixel(pixels,48,48,255,255,255);
    std::puts("PASS B GPU full vertex stream, multi-material slots, nested stencil/clip in all Canvas modes and SDF page");
}
int main(int argc,char** argv) try
{
    if(BaselineMain(argc,argv)!=0)return 1;
    NativeChecks();
    OvWindowing::Settings::DeviceSettings ds;ds.contextMajorVersion=4;ds.contextMinorVersion=5;OvWindowing::Context::Device device(ds);
    OvWindowing::Settings::WindowSettings ws;ws.visible=false;ws.focused=false;ws.width=96;ws.height=96;ws.samples=1;OvWindowing::Window window(device,ws);window.MakeCurrentContext();
    OvTools::Jobs::JobSystem jobs(2);
    {OvRendering::Context::Driver driver(OvRendering::Settings::DriverSettings{true});GPUChecks(driver,jobs);}
    const auto expected=capturedPixels;capturedPixels.clear();
    glfwMakeContextCurrent(nullptr);
    {OvRendering::Threading::RenderThread thread([&]{window.MakeCurrentContext();return std::make_unique<OvRendering::Context::Driver>(OvRendering::Settings::DriverSettings{true});},[]{glfwMakeContextCurrent(nullptr);});
     thread.Invoke([&](auto& driver){OvTools::Jobs::JobSystem threadJobs(2);GPUChecks(driver,threadJobs);});thread.Shutdown();}
    Require(expected==capturedPixels,"B complete images default/RenderThread exact match");
    std::puts("PASS B default and RenderThread GPU validation");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL B: %s\n",e.what());return 1;}
