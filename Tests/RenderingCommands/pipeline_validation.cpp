#define main SkinningValidationMain
#include "../Skinning/serial_validation.cpp"
#undef main
#include "inspection_pipeline.h"
#include <OvRendering/Data/RenderFrame.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

void PipelineValidation(const std::filesystem::path& dir)
{
    OvWindowing::Settings::DeviceSettings ds;ds.contextMajorVersion=4;ds.contextMinorVersion=5;
    OvWindowing::Context::Device device(ds);
    OvWindowing::Settings::WindowSettings ws;ws.visible=false;ws.focused=false;ws.width=96;ws.height=64;ws.samples=1;
    OvWindowing::Window window(device,ws);window.MakeCurrentContext();
    OvRendering::Context::Driver driver(OvRendering::Settings::DriverSettings{true});
    OvCore::ResourceManagement::ShaderManager shaders;OvCore::ResourceManagement::ModelManager models;
    const auto assets=std::filesystem::current_path()/"Data/Engine";
    shaders.ProvideAssetPaths(assets,assets);models.ProvideAssetPaths(assets,assets);
    ServiceLocator::Provide(shaders);ServiceLocator::Provide(models);
    OvTools::Jobs::JobSystem one(1),two(2);
    auto* model=models.GetResource(":Models/Mannequin.fbx");auto* shader=shaders.GetResource(":Shaders/Standard.ovfx");
    Require(model&&shader,"pipeline fixture resources missing");
    const auto bounds=model->GetBoundingSphere();
    std::ofstream images(dir/"pipeline-frames.bin",std::ios::binary);
    std::array<std::vector<uint8_t>,4> configurationImages;
    for(unsigned settings=0;settings<4;++settings)
    {
        std::vector<uint8_t> reference;
        for(unsigned mode=0;mode<3;++mode)
        {
            auto scene=std::make_unique<Scene>(assets,assets);auto& skin=Add(*scene,model);
            skin.SetAnimation(std::optional<uint32_t>{0});skin.SetTime(.2f);
            auto material=std::make_unique<OvCore::Resources::Material>();material->SetShader(shader);
            material->SetReceiveShadows(false);material->SetReceiveReflections(false);
            material->SetProperty("u_EmissiveIntensity",.5f);
            skin.owner.AddComponent<CMaterialRenderer>().FillWithMaterial(*material);
            auto transparent=std::make_unique<OvCore::Resources::Material>();transparent->SetShader(shader);
            transparent->SetReceiveShadows(false);transparent->SetReceiveReflections(false);transparent->SetBlendable(true);
            transparent->SetDepthWriting(false);transparent->SetProperty("u_Albedo",FVector4{.2f,.5f,.8f,.4f});
            auto& ghost=Add(*scene,model);ghost.owner.AddComponent<CMaterialRenderer>().FillWithMaterial(*transparent);
            ghost.owner.transform.SetLocalPosition({bounds.radius*.2f,0,0});
            auto uiMaterial=std::make_unique<OvCore::Resources::Material>();uiMaterial->SetShader(shaders.GetResource(":Shaders/Unlit.ovfx"));
            uiMaterial->SetUserInterface(true);uiMaterial->SetBlendable(true);uiMaterial->SetDepthWriting(false);uiMaterial->SetDepthTest(false);
            uiMaterial->SetProperty("u_Diffuse",FVector4{.9f,.15f,.4f,.6f});
            auto& ui=scene->CreateActor("pipeline scene UI");ui.AddComponent<CModelRenderer>().SetModel(model);
            ui.AddComponent<CMaterialRenderer>().FillWithMaterial(*uiMaterial);ui.transform.SetLocalScale({.5f,.5f,.5f});
            OvRendering::Entities::Camera camera;camera.SetPosition(bounds.position+FVector3{0,0,std::max(bounds.radius*2.8f,2.f)});
            camera.SetRotation(FQuaternion(FVector3{0,180,0}));camera.SetFar(1000);camera.SetClearColor({.1f,.2f,.3f});
            auto inspection=std::make_unique<baregl::Framebuffer>(),main=std::make_unique<baregl::Framebuffer>();
            OvCore::Rendering::FramebufferUtil::SetupFramebuffer(*inspection,48,32,true,true,true);
            OvCore::Rendering::FramebufferUtil::SetupFramebuffer(*main,96,64,true,true,true);
            const auto a=inspection->RecordTarget(),b=main->RecordTarget();
            OvCore::Rendering::RenderFrontend frontend;RenderingExamples::InspectionPipeline pipeline;
            pipeline.includeTransparent=(settings&1)!=0;pipeline.includeSceneUI=(settings&2)!=0;
            OvCore::Rendering::SceneRenderRequest request{{*scene,camera,96,64,100,42,.125f}};
            auto frame=pipeline.Extract(frontend,request,camera,a,b);
            Require(frame->GetPlan().passes.size()==2&&frame->GetViews().size()==2,"custom pipeline lost view/pass requirements");
            const auto frozenViews=std::vector(frame->GetViews().begin(),frame->GetViews().end());
            const std::array<std::string_view,7> diagnostics={"missing view","Missing producer","Wrong View/Stage","dimensions differ","Unknown pass resource","Independent pass needs","temporal resources require"};
            for(unsigned fault=0;fault<diagnostics.size();++fault)
            {
                auto invalid=frame->GetPlan();
                if(fault==0)invalid.passes[0].view=99;
                if(fault==1)invalid.passes[0].reads={{invalid.passes[1].writes.front().resource,1}};
                if(fault==2)invalid.passes[0].stages={OvRendering::Data::RenderStage::Picking};
                if(fault==3)invalid.passes[0].target=b;
                if(fault==4)invalid.passes[0].writes={{invalid.resources.size(),1}};
                if(fault==5)
                {
                    invalid.passes[0].view=OvRendering::Data::RenderPlanPass::NoView;
                    invalid.passes[0].stages.clear();invalid.passes[0].reads.clear();invalid.passes[0].writes.clear();
                    invalid.passes[0].operation={};
                }
                if(fault==6)invalid.resources[0].origin=OvRendering::Data::ResourceOrigin::History;
                bool rejected=false;try{frontend.ComposeFrame(frozenViews,std::move(invalid));}catch(const std::invalid_argument& e){const std::string_view message=e.what();rejected=message.find(diagnostics[fault])!=message.npos && message.find(fault<6?"RenderPlan pass":"ComposeFrame")!=message.npos;}
                Require(rejected,"invalid custom pipeline was accepted without a diagnostic");
            }
            pipeline.includeTransparent=!pipeline.includeTransparent;pipeline.includeSceneUI=!pipeline.includeSceneUI;
            request.pipeline.stages.clear();scene.reset();material.reset();transparent.reset();uiMaterial.reset();
            const auto state=driver.CreatePipelineState();glfwMakeContextCurrent(nullptr);
            auto* jobs=mode==0?nullptr:mode==1?&one:&two;
            frame=frontend.PrepareFrame(std::move(frame),state,jobs);
            const auto& stages=frame->GetPreparedViews()[1]->GetLists();
            Require(stages.size()==1+size_t(bool(settings&1))+size_t(bool(settings&2)),"configuration changed after publication");
            if(mode)for(const auto& view:frame->GetPreparedViews())Require(view->GetPreparationThread()!=std::this_thread::get_id(),"pipeline preparation did not run on Worker");
            auto commands=frontend.RecordFrame(std::move(frame),jobs);window.MakeCurrentContext();
            inspection->Resize(8,8);main->Resize(8,8);inspection.reset();main.reset();
            OvRendering::Data::RenderFrame product;product.Append(std::move(commands));product.Seal();product.Submit(driver);product.Wait();product.GetCompletion().RethrowIfFailed();
            std::vector<uint8_t> pixels(48*32*4),mainPixels(96*64*4);a.ReadRGBA8(pixels);b.ReadRGBA8(mainPixels);pixels.insert(pixels.end(),mainPixels.begin(),mainPixels.end());
            if(mode==0){reference=pixels;configurationImages[settings]=pixels;}else Require(pixels==reference,"custom pipeline serial/one/two Worker image mismatch");
            images.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());
            Require(glGetError()==GL_NO_ERROR,"pipeline GL failure");
        }
    }
    Require(configurationImages[0]!=configurationImages[1] && configurationImages[0]!=configurationImages[2],"pipeline feature selection did not affect the image");
    Require(images.good(),"pipeline output failed");
    std::puts("PASS: 4 custom pipeline configurations x serial/one/two Worker, exact two-target images, owned configuration/source/target lifetime and 7 invalid plan/owner diagnostics");
}
int main(int argc,char** argv) try
{
    std::setvbuf(stdout,nullptr,_IONBF,0);_CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
    Require(argc==2,"pass output directory");PipelineValidation(argv[1]);return 0;
}
catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
