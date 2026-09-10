#define SNAPSHOT_VALIDATION_NO_MAIN
#include "snapshot_validation.cpp"
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

    for(unsigned cycle=0;cycle<16;++cycle)
    {
        auto target=std::make_unique<baregl::Framebuffer>();
        OvCore::Rendering::FramebufferUtil::SetupFramebuffer(*target,16,8,true,true,true);
        auto texture=target->RetainTextureAttachment(EFramebufferAttachment::COLOR);const auto textureID=texture->GetID();
        glfwMakeContextCurrent(nullptr);const auto cpuTarget=target->RecordTarget();window.MakeCurrentContext();
        Require(cpuTarget.GetSize()==std::pair<uint16_t,uint16_t>{16,8},"CPU target capture size mismatch");
        glfwMakeContextCurrent(nullptr);
        Data::RenderCommandList clear;clear.Append([cpuTarget](auto& consumer){cpuTarget.Bind();consumer.SetViewport(1,2,13,5);consumer.Clear(true,true,true,{.25f,.5f,.75f,1});});clear.Seal();
        window.MakeCurrentContext();
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
        target->Bind();driver.SetViewport(0,0,32,16);driver.Clear(true,true,true,{1,0,0,1});
        const auto red=ReadTarget(resized);
        target.reset();texture.reset();baregl::Framebuffer::BindDefault();driver.SetViewport(0,0,64,32);
        auto poison=driver.CreatePipelineState();poison.scissorTest=true;poison.colorWriting.mask=0;poison.depthWriting=false;poison.stencilWriteMask=0;driver.SetPipelineState(poison);glScissor(0,0,1,1);
        clear.Execute(driver);
        GLint viewport[4]{};glGetIntegerv(GL_VIEWPORT,viewport);
        Require(viewport[0]==1 && viewport[1]==2 && viewport[2]==13 && viewport[3]==5,"captured viewport not restored");
        const auto image=ReadTarget(old);
        for(size_t i=0;i<image.size();i+=4)Require(std::abs(int(image[i])-64)<=1 && std::abs(int(image[i+1])-128)<=1 && std::abs(int(image[i+2])-191)<=1 && image[i+3]==255,"old target clear differs after resize/destruction");
        Require(ReadTarget(resized)==red,"old target replay modified new storage");
        old.Bind();float depth=0;unsigned char stencil=255;glReadPixels(0,0,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&depth);glReadPixels(0,0,1,1,GL_STENCIL_INDEX,GL_UNSIGNED_BYTE,&stencil);
        Require(depth==1.f && stencil==0,"captured depth/stencil clear failed");
        Require(glIsTexture(textureID),"old attachment retired before snapshot");
    }
    // One cubemap attached at six points must keep its layers, selectors and shared identity.
    auto cube=std::make_shared<baregl::Texture>(ETextureType::TEXTURE_CUBE);
    cube->Allocate({.width=4,.height=4,.minFilter=ETextureFilteringMode::NEAREST,.magFilter=ETextureFilteringMode::NEAREST,.internalFormat=EInternalFormat::RGBA8,.useMipMaps=false});
    cube->SetBorderColor({.1f,.2f,.3f,.4f});
    auto layered=std::make_unique<baregl::Framebuffer>();
    for(unsigned face=0;face<6;++face)layered->Attach(cube,EFramebufferAttachment::COLOR,face,face);
    Require(layered->Validate(),"cubemap target invalid");
    std::vector<Data::RenderCommandList> clears;
    std::vector<baregl::Framebuffer::Snapshot> targets;
    for(unsigned face=0;face<6;++face)
    {
        layered->SetTargetDrawBuffer(face);layered->SetTargetReadBuffer(face);targets.push_back(layered->RecordTarget());
        Data::RenderCommandList clear;clear.Append([target=targets.back(),face](auto& consumer){target.Bind();consumer.SetViewport(0,0,4,4);consumer.Clear(true,false,false,{float(face+1)/8,0,0,1});});clear.Seal();clears.push_back(std::move(clear));
    }
    layered->Resize(8,8);auto replacement=layered->RetainTextureAttachment(EFramebufferAttachment::COLOR);
    for(unsigned face=0;face<6;++face)Require(layered->RetainTextureAttachment(EFramebufferAttachment::COLOR,face)==replacement,"resize split cubemap layers into independent textures");
    float border[4]{};glGetTextureParameterfv(replacement->GetID(),GL_TEXTURE_BORDER_COLOR,border);Require(border[2]==.3f,"resize lost border sampling state");
    const auto cubeID=cube->GetID();layered.reset();cube.reset();baregl::Framebuffer::BindDefault();
    for(auto& clear:clears)clear.Execute(driver);
    for(unsigned face=0;face<6;++face)
    {
        auto image=ReadTarget(targets[face]);
        for(size_t i=0;i<image.size();i+=4)Require(std::abs(int(image[i])-int((face+1)*255/8))<=1 && image[i+1]==0,"captured cubemap face/selector differs");
    }
    const auto retainedFBO=targets.front().GetID();clears.clear();targets.clear();
    Require(!glIsTexture(cubeID) && !glIsFramebuffer(retainedFBO),"snapshot resources not retired after last owner");
    Data::RenderCommandList back;back.Append([](auto& consumer){baregl::Framebuffer::BindDefault();consumer.SetViewport(3,4,31,17);});back.Seal();back.Execute(driver);
    GLint bound=-1;glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&bound);Require(bound==0,"default framebuffer replay failed");
    bool rejected=false;try{baregl::Framebuffer::Snapshot{}.Bind();}catch(const std::logic_error&){rejected=true;}Require(rejected,"empty snapshot was accepted");
    Require(glGetError()==GL_NO_ERROR,"target state GL error");
    std::puts("PASS: 16 resize/destruction clear replays; color/depth/stencil/viewport, cubemap layers/selectors, border state, shared attachments, default target and final retirement");
}

#ifndef TARGET_STATE_VALIDATION_NO_MAIN
int main(int argc,char** argv) try { Require(argc==2,"pass output directory");TargetStateValidation();return 0; }
catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
#endif
