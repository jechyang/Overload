#define FRAME_INPUT_VALIDATION_NO_MAIN
#include "frame_input_validation.cpp"
#include <baregl/Renderbuffer.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

std::vector<unsigned char> ReadTarget(const baregl::Framebuffer::Snapshot& target)
{
    target.Bind();const auto [w,h]=target.GetSize();std::vector<unsigned char> image(size_t(w)*h*4);
    glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,image.data());return image;
}

void TargetStateValidation()
{
    OvWindowing::Settings::DeviceSettings ds;ds.contextMajorVersion=4;ds.contextMinorVersion=5;
    OvWindowing::Context::Device device(ds);
    OvWindowing::Settings::WindowSettings ws;ws.visible=false;ws.focused=false;ws.width=64;ws.height=32;ws.samples=1;
    OvWindowing::Window window(device,ws);window.MakeCurrentContext();Context::Driver driver(Settings::DriverSettings{true});
    Core::CompositeRenderer renderer(driver);
    for(unsigned cycle=0;cycle<16;++cycle)
    {
        auto target=std::make_unique<baregl::Framebuffer>();
        OvCore::Rendering::FramebufferUtil::SetupFramebuffer(*target,16,8,true,true,true);
        auto texture=target->RetainTextureAttachment(EFramebufferAttachment::COLOR);const auto textureID=texture->GetID();
        glfwMakeContextCurrent(nullptr);const auto cpuTarget=target->RecordTarget();window.MakeCurrentContext();
        Require(cpuTarget.GetSize()==std::pair<uint16_t,uint16_t>{16,8},"CPU target capture size mismatch");
        renderer.BindTarget(target.get());renderer.SetViewport(1,2,13,5);
        glfwMakeContextCurrent(nullptr);auto clear=renderer.PrepareClear(true,true,true,{.25f,.5f,.75f,1});window.MakeCurrentContext();
        const auto old=target->RecordTarget();const auto oldID=old.GetID();
        Require(target->RecordTarget().GetID()==oldID,"unchanged target snapshot was not reused");
        const auto sourceID=target->GetID();
        bool invalid=false;try{target->Resize(0,16);}catch(const std::invalid_argument&){invalid=true;}
        Require(invalid && target->RecordTarget().GetID()==oldID,"invalid resize changed target generation");
        target->Resize(32,16);
        Require(target->GetID()==sourceID && target->GetSize()==std::pair<uint16_t,uint16_t>{32,16},"resize changed source identity/size");
        Require(texture->GetDesc().width==16 && target->RetainTextureAttachment(EFramebufferAttachment::COLOR)->GetID()!=textureID,"resize mutated retained storage");
        const auto resized=target->RecordTarget();Require(resized.GetID()!=oldID,"resize reused old snapshot");
        target->Resize(32,16);Require(target->RecordTarget().GetID()==resized.GetID(),"same-size resize invalidated snapshot");
        renderer.BindTarget(target.get());renderer.SetViewport(0,0,32,16);renderer.Clear(true,true,true,{1,0,0,1});
        const auto red=ReadTarget(resized);
        target.reset();texture.reset();renderer.BindTarget(nullptr);renderer.SetViewport(0,0,64,32);
        auto poison=driver.CreatePipelineState();poison.scissorTest=true;poison.colorWriting.mask=0;poison.depthWriting=false;poison.stencilWriteMask=0;driver.SetPipelineState(poison);glScissor(0,0,1,1);
        renderer.ExecuteClear(clear);
        GLint viewport[4]{};glGetIntegerv(GL_VIEWPORT,viewport);
        Require(viewport[0]==1 && viewport[1]==2 && viewport[2]==13 && viewport[3]==5,"captured viewport not restored");
        const auto image=ReadTarget(old);
        for(size_t i=0;i<image.size();i+=4)Require(std::abs(int(image[i])-64)<=1 && std::abs(int(image[i+1])-128)<=1 && std::abs(int(image[i+2])-191)<=1 && image[i+3]==255,"old target clear differs after resize/destruction");
        Require(ReadTarget(resized)==red,"old target replay modified new storage");
        old.Bind();float depth=0;unsigned char stencil=255;glReadPixels(0,0,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&depth);glReadPixels(0,0,1,1,GL_STENCIL_INDEX,GL_UNSIGNED_BYTE,&stencil);
        Require(depth==1.f && stencil==0,"captured depth/stencil clear failed");
        const auto selected=renderer.RecordPassState();Require(!selected.target && selected.viewport.width==64,"replay changed CPU pass selection");
        Require(glIsTexture(textureID),"old attachment retired before snapshot");
    }
    // One cubemap attached at six points must keep its layers, selectors and shared identity.
    auto cube=std::make_shared<baregl::Texture>(ETextureType::TEXTURE_CUBE);
    cube->Allocate({.width=4,.height=4,.minFilter=ETextureFilteringMode::NEAREST,.magFilter=ETextureFilteringMode::NEAREST,.internalFormat=EInternalFormat::RGBA8,.useMipMaps=false});
    cube->SetBorderColor({.1f,.2f,.3f,.4f});
    auto layered=std::make_unique<baregl::Framebuffer>();
    for(unsigned face=0;face<6;++face)layered->Attach(cube,EFramebufferAttachment::COLOR,face,face);
    Require(layered->Validate(),"cubemap target invalid");
    std::vector<Core::ABaseRenderer::ClearCommand> clears;
    for(unsigned face=0;face<6;++face)
    {
        layered->SetTargetDrawBuffer(face);layered->SetTargetReadBuffer(face);renderer.BindTarget(layered.get());renderer.SetViewport(0,0,4,4);
        clears.push_back(renderer.PrepareClear(true,false,false,{float(face+1)/8,0,0,1}));
    }
    layered->Resize(8,8);auto replacement=layered->RetainTextureAttachment(EFramebufferAttachment::COLOR);
    for(unsigned face=0;face<6;++face)Require(layered->RetainTextureAttachment(EFramebufferAttachment::COLOR,face)==replacement,"resize split cubemap layers into independent textures");
    float border[4]{};glGetTextureParameterfv(replacement->GetID(),GL_TEXTURE_BORDER_COLOR,border);Require(border[2]==.3f,"resize lost border sampling state");
    const auto cubeID=cube->GetID();layered.reset();cube.reset();renderer.BindTarget(nullptr);
    for(const auto& clear:clears)renderer.ExecuteClear(clear);
    for(unsigned face=0;face<6;++face)
    {
        auto image=ReadTarget(*clears[face].state.target);
        for(size_t i=0;i<image.size();i+=4)Require(std::abs(int(image[i])-int((face+1)*255/8))<=1 && image[i+1]==0,"captured cubemap face/selector differs");
    }
    const auto retainedFBO=clears.front().state.target->GetID();clears.clear();
    Require(!glIsTexture(cubeID) && !glIsFramebuffer(retainedFBO),"snapshot resources not retired after last owner");
    renderer.BindTarget(nullptr);renderer.SetViewport(3,4,31,17);auto back=renderer.RecordPassState();
    renderer.SetViewport(0,0,1,1);renderer.ExecutePassState(back);GLint bound=-1;glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&bound);Require(bound==0,"default framebuffer replay failed");
    bool rejected=false;try{baregl::Framebuffer::Snapshot{}.Bind();}catch(const std::logic_error&){rejected=true;}Require(rejected,"empty snapshot was accepted");
    Require(glGetError()==GL_NO_ERROR,"target state GL error");
    std::puts("PASS: 16 resize/destruction clear replays; color/depth/stencil/viewport, cubemap layers/selectors, border state, shared attachments, default target and final retirement");
}

void TargetPacketValidation(const std::filesystem::path& dir)
{
    OvWindowing::Settings::DeviceSettings ds;ds.contextMajorVersion=4;ds.contextMinorVersion=5;OvWindowing::Context::Device device(ds);
    OvWindowing::Settings::WindowSettings ws;ws.visible=false;ws.focused=false;ws.width=64;ws.height=32;ws.samples=1;
    OvWindowing::Window window(device,ws);window.MakeCurrentContext();Context::Driver driver(Settings::DriverSettings{true});Core::CompositeRenderer renderer(driver);
    const auto destroy=[](Resources::Shader* p){Resources::Loaders::ShaderLoader::Destroy(p);};
    std::unique_ptr<Resources::Shader,decltype(destroy)> shader(Resources::Loaders::ShaderLoader::CreateFromSource(
        "#version 450 core\nlayout(location=0)in vec3 pos;void main(){gl_Position=vec4(pos,1);}",
        "#version 450 core\nout vec4 color;void main(){color=vec4(.25,.5,.75,1);}"),destroy);
    Mat material(shader.get());const std::array<Geometry::Vertex,3> vertices{{{{-1,-1,0}},{{1,-1,0}},{{0,1,0}}}};const std::array<uint32_t,3> indices{0,1,2};Resources::Mesh mesh(vertices,indices);
    Entities::Drawable drawable;drawable.mesh=mesh;drawable.material=material;drawable.stateMask=Mask(2);
    baregl::Framebuffer output,a,b,noise;for(auto* t:{&output,&a,&b,&noise})OvCore::Rendering::FramebufferUtil::SetupFramebuffer(*t,64,32,true,true,false);
    Entities::Camera camera;Data::FrameDescriptor frame{.renderWidth=64,.renderHeight=32,.camera=camera,.outputBuffer=output};
    bool recorded=false;unsigned iteration=0;std::vector<std::vector<unsigned char>> reference;std::ofstream images(dir/"target-packet-frames.bin",std::ios::binary);
    renderer.AddPass<PacketPass>("target-state",0,std::function<void(Data::PipelineState)>([&](auto pso){
        unsigned view=0;for(auto* target:{&a,&b})
        {
            const uint16_t w=uint16_t(32+iteration*2),h=uint16_t(16+iteration);target->Resize(w,h);
            renderer.BindTarget(target);renderer.SetViewport(view+1,view+2,w/2,h/2);renderer.Clear(true,true,true,{0,0,0,1});
            if(recorded){noise.Bind();driver.SetViewport(0,0,1,1);}
            renderer.DrawEntity(pso,drawable);auto image=ReadTarget(target->RecordTarget());
            Require(std::count_if(image.begin(),image.end(),[](auto c){return c>0 && c<255;})>10,"target draw blank");
            if(!recorded)reference.push_back(image);else Require(image==reference[view],"target/viewport packet image differs");
            images.write(reinterpret_cast<const char*>(image.data()),image.size());++view;
        }
    }));
    for(iteration=0;iteration<8;++iteration){reference.clear();for(bool mode:{false,true}){recorded=mode;renderer.SetDrawRecordingEnabled(mode);renderer.BeginFrame(frame);renderer.DrawFrame();renderer.EndFrame();TracyGpuCollect;}}
    Require(glGetError()==GL_NO_ERROR && images.good(),"target packet GL/output failure");std::puts("PASS: 16 target/viewport immediate-packet image pairs with resize and GPU binding contamination");
}

int main(int argc,char** argv) try
{
    std::setvbuf(stdout,nullptr,_IONBF,0);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);Require(argc==2,"pass output directory");
    TargetStateValidation();TargetPacketValidation(argv[1]);FrameInputValidation(argv[1]);FramePacketValidation(argv[1]);PacketValidation(argv[1]);
    const auto immediate=std::filesystem::path(argv[1])/"immediate",recorded=std::filesystem::path(argv[1])/"recorded";
    std::filesystem::create_directories(immediate);std::filesystem::create_directories(recorded);ImageValidation(immediate,false,false);ImageValidation(recorded,false,true);
    const auto read=[](const auto& p){std::ifstream f(p,std::ios::binary);return std::vector<char>(std::istreambuf_iterator<char>(f),{});};
    Require(read(immediate/"frames.bin")==read(recorded/"frames.bin"),"SceneRenderer target state images differ");std::puts("PASS: 24 real Mannequin immediate/packet frames exactly match");return 0;
}
catch(const std::exception& e){allocationFailureCountdown=-1;std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
