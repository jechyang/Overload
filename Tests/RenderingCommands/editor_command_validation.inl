#include <OvCore/Rendering/RenderFrontend.h>
#include <OvEditor/Core/EditorActions.h>
#include <OvEditor/Rendering/EditorOverlayCollector.h>
#include <OvEditor/Rendering/PickingCollector.h>
#include <OvCore/ECS/Components/CPhysicalBox.h>

size_t editorReadCalls=0;
PFNGLREADPIXELSPROC editorOriginalReadPixels=nullptr;
void APIENTRY CountEditorReadPixels(GLint x,GLint y,GLsizei w,GLsizei h,GLenum format,GLenum type,void* data)
{++editorReadCalls;editorOriginalReadPixels(x,y,w,h,format,type,data);}
struct EditorReadHook
{
    EditorReadHook(){editorReadCalls=0;editorOriginalReadPixels=glad_glReadPixels;glad_glReadPixels=CountEditorReadPixels;}
    ~EditorReadHook(){glad_glReadPixels=editorOriginalReadPixels;}
};

void CheckReadbackPacking(const baregl::Framebuffer::Snapshot& target,const std::vector<uint8_t>& reference)
{
    GLuint pack;glCreateBuffers(1,&pack);glNamedBufferData(pack,1024,nullptr,GL_STREAM_READ);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,pack);glPixelStorei(GL_PACK_ALIGNMENT,8);glPixelStorei(GL_PACK_ROW_LENGTH,181);
    glPixelStorei(GL_PACK_SKIP_ROWS,3);glPixelStorei(GL_PACK_SKIP_PIXELS,5);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,0);GLint draw;glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);
    std::vector<uint8_t> pixels(reference.size());target.ReadRGBA8(pixels);Require(pixels==reference,"readback inherited foreign pack state");
    GLint value;glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&value);Require(value==0,"readback changed read target");
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&value);Require(value==draw,"readback changed draw target");
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&value);Require(static_cast<GLuint>(value)==pack,"readback changed pack buffer");
    glGetIntegerv(GL_PACK_ALIGNMENT,&value);Require(value==8,"readback changed pack alignment");
    glGetIntegerv(GL_PACK_ROW_LENGTH,&value);Require(value==181,"readback changed pack row length");
    glGetIntegerv(GL_PACK_SKIP_ROWS,&value);Require(value==3,"readback changed pack rows");
    glGetIntegerv(GL_PACK_SKIP_PIXELS,&value);Require(value==5,"readback changed pack pixels");
    glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glDeleteBuffers(1,&pack);glPixelStorei(GL_PACK_ALIGNMENT,4);glPixelStorei(GL_PACK_ROW_LENGTH,0);
    glPixelStorei(GL_PACK_SKIP_ROWS,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);
    bool rejected=false;try{target.ReadRGBA8(std::span<uint8_t>{});}catch(const std::invalid_argument&){rejected=true;}
    Require(rejected && glGetError()==GL_NO_ERROR,"readback invalid size/GL state regression");
}

void EditorCommandValidation(OvEditor::Core::Context& context, OvRendering::Resources::Model* model,
    OvCore::Resources::Material* material, const std::filesystem::path& dir)
{
    using Readback=OvEditor::Rendering::PickingReadback;
    using List=OvRendering::Data::RenderCommandList;
    const auto bounds=model->GetBoundingSphere();
    std::ofstream images(dir/"editor-command-frames.bin",std::ios::binary);
    size_t actorPixels=0,gizmoPixels=0;
    for(int iteration=0;iteration<6;++iteration)
    {
        std::vector<uint8_t> reference,referencePicking;
        for(bool delayed:{false,true})
        {
            std::optional<List> commands;
            std::shared_ptr<const Readback> readback;
            baregl::Framebuffer::Snapshot output;
            tinyxml2::XMLDocument serialized;
            {
                Scene scene;auto& skin=Add(scene,model);skin.SetAnimation(std::optional<uint32_t>{0});skin.SetTime(float(iteration)*.1f);
                skin.owner.AddComponent<CMaterialRenderer>().FillWithMaterial(*material);
                skin.owner.AddComponent<OvCore::ECS::Components::CPhysicalBox>();
                auto& cameraActor=scene.CreateActor("debug camera");cameraActor.AddComponent<CCamera>();cameraActor.transform.SetLocalPosition({bounds.radius*2,0,0});
                auto& probe=scene.CreateActor("debug probe").AddComponent<OvCore::ECS::Components::CReflectionProbe>();
                probe.SetCubemapResolution(16);probe.SetCaptureSpeed(OvCore::ECS::Components::CReflectionProbe::ECaptureSpeed::SIX_FACES);
                probe.owner.transform.SetLocalPosition({-bounds.radius*2,0,0});
                scene.AddDefaultLights();
                baregl::Framebuffer target;OvCore::Rendering::FramebufferUtil::SetupFramebuffer(target,160,120,true,true,false);output=target.RecordTarget();
                OvRendering::Entities::Camera camera;camera.SetPosition(bounds.position+FVector3{0,bounds.radius*.5f,bounds.radius*7});
                camera.SetRotation(FQuaternion(FVector3{0,180,0}));camera.SetFar(1000);camera.CacheMatrices(160,120);
                OvCore::Rendering::RenderFrontend frontend;
                OvEditor::Rendering::EditorOverlayCollector overlays;
                OvEditor::Rendering::PickingCollector newPicking;
                OvRendering::Data::Material shadow(context.shaderManager[":Shaders\\ShadowFallback.ovfx"]);
                const auto recordFrontend=[&] {
                    OvCore::Rendering::SceneRenderRequest request{{scene,camera,160,120}};request.fallbackMaterial=material;
                    OvCore::Rendering::SceneFrameRequest frameRequest{request,target.RecordTarget(),shadow};
                    const auto state=context.driver->CreatePipelineState();
                    auto prepared=frontend.PrepareFrame(frontend.ExtractFrame(frameRequest),state,delayed?context.jobSystem.get():nullptr);
                    const auto view=prepared->GetPreparedViews().back()->GetView();
                    List result;result.Append(frontend.RecordFrame(std::move(prepared),context.jobSystem.get()));
                    auto& selected=iteration%3==0?skin.owner:iteration%3==1?cameraActor:probe.owner;
                    OvEditor::Rendering::EditorOverlayRequest overlay;overlay.grid=true;overlay.debugObjects=true;overlay.picking=true;
                    overlay.scene=scene.GetLifetimeToken();overlay.selected=selected.GetGUID();overlay.operation=static_cast<OvEditor::Core::EGizmoOperation>(iteration%3);
                    overlay.gridColor={.17f,.17f,.17f};overlay.gameViewSize=EDITOR_PANEL(OvEditor::Panels::GameView,"Game View").GetSafeSize();
                    result.AppendRecording(overlays.Extract(scene,overlay,view,state,target.RecordTarget()));
                    result.AppendRecording(newPicking.Extract(request,overlay,state,context.jobSystem.get()));
                    return result;
                };
                if(delayed && iteration==0)
                {
                    for(int mode=0;mode<3;++mode)
                    {
                        auto failed=recordFrontend();auto failedReadback=newPicking.GetReadback();
                        if(mode==1)failed.Append([](auto&){throw std::runtime_error("failure after picking readback");});
                        failed.Seal(delayed?context.jobSystem.get():nullptr);
                        if(mode==0)failed.Cancel();
                        if(mode==1){bool caught=false;try{failed.Execute(*context.driver);}catch(const std::runtime_error&){caught=true;}Require(caught,"readback failure injection missing");}
                        if(mode==2){auto abandoned=std::move(failed);}
                        Require(!failedReadback->IsReady() && !failedReadback->Resolve(scene,80,60),"failed/abandoned frame published picking results");
                    }
                }
                {EditorReadHook hook;
                 if(delayed) { commands.emplace(recordFrontend());commands->Seal(delayed?context.jobSystem.get():nullptr);Require(editorReadCalls==0,"recording performed picking GPU readback"); }
                 else { auto referenceCommands=recordFrontend();referenceCommands.Seal();referenceCommands.Execute(*context.driver); }}
                if(delayed)
                {
                    readback=newPicking.GetReadback();Require(readback && !readback->IsReady(),"picking published before command completion");
                    if(iteration%2==0)
                    {
                        commands->Execute(*context.driver);
                        Require(readback->IsReady(),"picking did not publish on success");
                        Require(readback->pixels==referencePicking,"delayed picking image differs");
                        Require(!readback->Resolve(scene,160,0) && !readback->Resolve(scene,0,120),"picking accepted outside coordinates");
                        OvCore::ECS::Actor* pickedActor=nullptr;uint32_t pickedX=0,pickedY=0;
                        for(uint32_t y=0;y<120;++y)for(uint32_t x=0;x<160;++x)
                        {
                            auto result=readback->Resolve(scene,x,y);if(!result)continue;
                            if(auto actor=std::get_if<OvTools::Utils::OptRef<OvCore::ECS::Actor>>(&*result))
                            {++actorPixels;pickedActor=&actor->value();pickedX=x;pickedY=y;}
                            else ++gizmoPixels;
                        }
                        Require(pickedActor!=nullptr,"recorded picking found no actor");
                        scene.DestroyActor(*pickedActor);
                        Require(!readback->Resolve(scene,pickedX,pickedY),"deleted actor survived readback resolution");
                    }
                }
                else
                {
                    reference.resize(160*120*4);output.ReadRGBA8(reference);
                    referencePicking=newPicking.GetReadback()->pixels;
                }
                auto* rootNode=serialized.NewElement("root");serialized.InsertFirstChild(rootNode);scene.OnSerialize(serialized,rootNode);
            }
            if(delayed)
            {
                if(commands->GetState()==List::State::READY)commands->Execute(*context.driver);
                std::vector<uint8_t> pixels(160*120*4);output.ReadRGBA8(pixels);
                Require(pixels==reference && readback->pixels==referencePicking,"destroyed editor renderer/scene command replay differs");
                if(iteration==0)CheckReadbackPacking(output,reference);
                Scene replacement;replacement.OnDeserialize(serialized,serialized.FirstChildElement("root")->FirstChildElement("scene"));
                for(uint32_t y=0;y<120;y+=5)for(uint32_t x=0;x<160;x+=5)
                    Require(!readback->Resolve(replacement,x,y),"old readback selected actor/gizmo in replacement scene");
                images.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());
            }
        }
    }
    std::puts("PASS: picking cancellation/failure/abandonment publication guards, zero recording readbacks, pack buffer/layout/target restoration");
    Require(actorPixels>0 && gizmoPixels>0 && images.good(),"editor picking coverage/output missing");
    std::printf("PASS: 6 exact editor debug/picking command pairs, source destruction, %zu actor and %zu gizmo resolutions, scene replacement/deletion/bounds rejection\n",actorPixels,gizmoPixels);
}
