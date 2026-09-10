#pragma push_macro("main")
#undef main
#define main SerialValidationMain
#include "../Skinning/serial_validation.cpp"
#undef main
#pragma pop_macro("main")
#include <OvGame/Core/Game.h>
#include <OvCore/ECS/Components/CCamera.h>
#include <tinyxml2.h>
#include <imgui.h>
#include <sstream>
#include "product_loop_validation.inl"
int main(int argc,char** argv) try
{
    std::setvbuf(stdout,nullptr,_IONBF,0);_CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
    Require(argc>1,"output directory missing");const auto dir=std::filesystem::path(argv[1]);const auto project=dir/"GameProject";
    std::filesystem::create_directories(project/"Assets");
    std::ofstream(project/"Game.ini")<<"gravity=-9.81\nx_resolution=320\ny_resolution=240\nfullscreen=false\nexecutable_name=FrameValidation\nstart_scene=frame.ovscene\nvsync=false\nmultisampling=false\nsamples=1\n";
    OvGame::Core::Context context;context.sceneManager.LoadEmptyScene();
    auto* model=context.modelManager.GetResource(":Models/Mannequin.fbx");auto* material=context.materialManager.GetResource(":Materials/Default.ovmat");Require(model&&material,"Game assets missing");
    {
        auto& scene=*context.sceneManager.GetCurrentScene();const auto bounds=model->GetBoundingSphere();
        auto& camera=scene.CreateActor("Camera");camera.AddComponent<CCamera>();camera.transform.SetLocalPosition(bounds.position+FVector3{0,bounds.radius*3,std::max(bounds.radius*7,2.f)});camera.transform.SetLocalRotation(FQuaternion(FVector3{20,180,0}));scene.AddDefaultLights();
        for(int i=0;i<8;++i){auto& skin=Add(scene,model);skin.SetAnimation(std::optional<uint32_t>{0});skin.SetTime(float(i)*.03f);skin.owner.AddComponent<CMaterialRenderer>().FillWithMaterial(*material);skin.owner.transform.SetLocalPosition({(float(i%4)-1.5f)*bounds.radius*1.5f,0,float(i/4)*bounds.radius*1.5f});}
        tinyxml2::XMLDocument doc;auto* node=doc.NewElement("root");doc.InsertFirstChild(node);scene.OnSerialize(doc,node);Require(doc.SaveFile((project/"Assets/frame.ovscene").string().c_str())==tinyxml2::XML_SUCCESS,"save Game frame scene");
    }
    OvGame::Core::Game game(context);game.Update(1.f/60);ImGui::GetIO().ConfigInputTextCursorBlink=false;
    auto clear=[&]{context.driver->OnFrameCompleted();glBindFramebuffer(GL_FRAMEBUFFER,0);glDisable(GL_SCISSOR_TEST);glClearColor(.08f,.12f,.18f,1);glClear(GL_COLOR_BUFFER_BIT);};
    auto pixels=[&]{glBindFramebuffer(GL_READ_FRAMEBUFFER,0);const auto [w,h]=context.window->GetFramebufferSize();std::vector<uint8_t> data(size_t(w)*h*4);glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,data.data());return data;};
    auto immediate=[&]{clear();game.Update(0.0f);};
    std::ofstream evidence(dir/"game-render-frame.bin",std::ios::binary);int pairs=0;
    for(bool debugUI:{false,true})
    {
        game.m_showDebugInformation=debugUI;for(int i=0;i<4;++i)immediate();
        for(int i=0;i<3;++i)
        {
            immediate();const auto expected=pixels();clear();auto frame=game.RecordRenderFrame();auto done=frame.GetCompletion();Require(frame.Submit(*context.driver),"Game frame submit");
            const auto actual=pixels();Require(actual==expected,"Game scene/blit/debug UI aggregate differs");
            size_t varied=0;for(size_t p=4;p<actual.size();p+=4)if(std::memcmp(actual.data(),actual.data()+p,3))++varied;Require(varied>100,"empty Game frame");
            Require(!done.Succeeded(),"Game CPU submission reported GPU completion");frame.Wait();Require(done.Succeeded(),"Game frame did not retire");
            evidence.write(reinterpret_cast<const char*>(actual.data()),static_cast<std::streamsize>(actual.size()));++pairs;
        }
    }
    std::vector<std::string> states;std::vector<std::vector<uint8_t>> loopImages;
    game.m_showDebugInformation=false;
    for(bool owned:{false,true})
    {
        game.SetFrameSubmissionEnabled(owned);Require(context.sceneManager.LoadScene("frame.ovscene"),"Game loop scene reload");context.sceneManager.GetCurrentScene()->Play();ProductFenceHook hook;
        for(int i=0;i<8;++i)
        {
            game.PreUpdate();game.Update(.02f);std::ostringstream pose(std::ios::binary);for(auto* actor:context.sceneManager.GetCurrentScene()->GetActors())if(auto skin=actor->GetComponent<Skin>())Snapshot(pose,*skin);
            if(owned){Require(pose.str()==states[i],"Game product loop advanced pose differently");RejectProductCall([&]{game.Update(.02f);},"double Game update accepted");RejectProductCall([&]{game.SetFrameSubmissionEnabled(false);},"Game switched mode mid-frame");}else states.push_back(pose.str());
            context.window->KeyPressedEvent.Invoke(int(OvWindowing::Inputs::EKey::KEY_SPACE));auto done=owned?game.m_pendingFrame->GetCompletion():OvRendering::Data::RenderFrame::Completion{};
            game.PostUpdate();Require(!context.inputManager->IsKeyPressed(OvWindowing::Inputs::EKey::KEY_SPACE),"Game events survived PostUpdate");
            if(owned)Require(done.Succeeded()&&!game.m_pendingFrame,"Game frame did not retire once");
            const auto [w,h]=context.framebuffer->GetSize();std::vector<uint8_t> image(size_t(w)*h*4);context.framebuffer->RecordTarget().ReadRGBA8(image);
            if(owned)Require(image==loopImages[i],"Game product loop image differs");else loopImages.push_back(std::move(image));
        }
        Require(ProductFenceHook::count==(owned?8:0),"Game fence count differs from owned frame count");
    }
    Require(states.front()!=states.back() && loopImages.front()!=loopImages.back(),"Game product sequence did not animate");
    game.SetFrameSubmissionEnabled(false);
    std::puts("PASS: real Game Update/PostUpdate modes: 8 exact moving pose/view pairs, one fence/frame, event clearing, duplicate/mid-frame switch rejection");
    {auto cancelled=game.RecordRenderFrame();auto done=cancelled.GetCompletion();cancelled.Cancel();Require(done.IsRetired()&&!done.Succeeded(),"Game cancelled frame success");}
    {auto recovered=game.RecordRenderFrame();Require(recovered.Submit(*context.driver,[&]{context.window->SwapBuffers();}),"Game presentation after cancellation");recovered.Wait();Require(recovered.GetCompletion().Succeeded()&&recovered.GetCompletion().PresentationRequested(),"Game presentation completion");}
    // Destroy the source framebuffer/scene and resize its replacement before consuming the captured blit.
    game.m_showDebugInformation=false;immediate();const auto expected=pixels();clear();auto retained=game.RecordRenderFrame();
    context.sceneManager.UnloadCurrentScene();context.framebuffer=std::make_unique<baregl::Framebuffer>("replacement");OvCore::Rendering::FramebufferUtil::SetupFramebuffer(*context.framebuffer,96,80,true,false,false);
    Require(retained.Submit(*context.driver),"detached Game frame submit");Require(pixels()==expected,"source scene/framebuffer destruction changed Game frame");retained.Wait();
    Require(glGetError()==GL_NO_ERROR,"Game aggregate GL error");
    std::printf("PASS: %d exact actual Game scene/blit/debug UI aggregate images; cancellation/presentation and captured scene/target destruction\n",pairs);return 0;
}
catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
