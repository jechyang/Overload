#include "editor_product_validation.inl"
void EditorFrameValidation(OvEditor::Core::Editor& editor,OvEditor::Core::Context& context,const std::filesystem::path& dir)
{
    auto& ui=*context.uiManager;ui.EnableEditorLayoutSave(false);ImGui::GetIO().ConfigInputTextCursorBlink=false;
    auto& asset=editor.m_panelsManager.GetPanelAs<OvEditor::Panels::AssetView>("Asset View");
    auto& scene=editor.m_panelsManager.GetPanelAs<OvEditor::Panels::SceneView>("Scene View");
    auto& game=editor.m_panelsManager.GetPanelAs<OvEditor::Panels::GameView>("Game View");
    std::vector<std::pair<OvUI::Panels::PanelWindow*,bool>> opened;
    for(auto& [name,panel]:editor.m_panelsManager.m_panels)if(auto* window=dynamic_cast<OvUI::Panels::PanelWindow*>(panel.get()))
    {opened.emplace_back(window,window->IsOpened());window->Close();}
    OvUI::Modules::Canvas canvas;int index=0;
    for(auto* view:std::array<OvEditor::Panels::AView*,3>{&asset,&game,&scene})
    {view->Open();view->dockable=false;view->autoSize=false;view->SetPosition({float(index++*320),20});view->SetSize({320,260});canvas.AddPanel(*view);}
    ui.SetCanvas(canvas);
    // Finish the queued asset-camera focus motion before freezing update time.
    for(int i=0;i<240;++i)asset.Update(1.f/60);
    auto clear=[&]{context.driver->OnFrameCompleted();glBindFramebuffer(GL_FRAMEBUFFER,0);glDisable(GL_SCISSOR_TEST);glClearColor(.08f,.12f,.18f,1);glClear(GL_COLOR_BUFFER_BIT);};
    auto pixels=[&]{glBindFramebuffer(GL_READ_FRAMEBUFFER,0);const auto [w,h]=context.window->GetFramebufferSize();std::vector<uint8_t> data(size_t(w)*h*4);glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,data.data());return data;};
    auto immediate=[&]{clear();editor.RenderViews(0);editor.UpdateEditorPanels(0);ui.Render();};
    for(int i=0;i<4;++i){clear();ui.Render();}
    for(int i=0;i<4;++i)immediate();
    Require(asset.IsVisible()&&game.IsVisible()&&scene.IsVisible(),"aggregate fixture did not show all three views");
    std::ofstream evidence(dir/"editor-render-frame.bin",std::ios::binary);
    for(int i=0;i<3;++i)
    {
        immediate();const auto expected=pixels();
        std::vector<std::vector<uint8_t>> viewExpected;
        for(auto* view:std::array<OvEditor::Panels::AView*,3>{&asset,&game,&scene}){const auto [w,h]=view->GetSafeSize();viewExpected.emplace_back(size_t(w)*h*4);view->GetOutputTarget().ReadRGBA8(viewExpected.back());}
        clear();auto frame=editor.RecordRenderFrame(0);const auto done=frame.GetCompletion();
        Require(!editor.CompleteRenderFrame(done),"Editor accepted unsubmitted frame");
        Require(frame.Submit(*context.driver),"Editor aggregate submit failed");
        Require(!editor.CompleteRenderFrame(done),"Editor accepted frame before GPU retirement");
        const auto actual=pixels();
        size_t viewIndex=0;
        for(auto* view:std::array<OvEditor::Panels::AView*,3>{&asset,&game,&scene})
        {
            const auto [w,h]=view->GetSafeSize();std::vector<uint8_t> data(size_t(w)*h*4);view->GetOutputTarget().ReadRGBA8(data);
            Require(data==viewExpected[viewIndex++],"Editor aggregate source view differs from immediate frame");
        }
        Require(actual==expected,"Editor three-view/UI aggregate differs from immediate frame");
        frame.Wait();Require(editor.CompleteRenderFrame(done),"Editor did not consume successful latest frame");
        Require(!editor.CompleteRenderFrame(done),"Editor consumed completion twice");
        evidence.write(reinterpret_cast<const char*>(actual.data()),static_cast<std::streamsize>(actual.size()));
    }
    {auto frame=editor.RecordRenderFrame(0);auto done=frame.GetCompletion();frame.Cancel();Require(!editor.CompleteRenderFrame(done),"Editor accepted cancelled frame");}
    {auto frame=editor.RecordRenderFrame(0);auto done=frame.GetCompletion();Require(!frame.Submit(*context.driver,[]{throw std::runtime_error("presentation failure");}),"Editor failed presentation accepted");frame.Wait();Require(!editor.CompleteRenderFrame(done),"Editor applied failed frame picking");}
    {
        auto old=editor.RecordRenderFrame(0);auto oldDone=old.GetCompletion();Require(old.Submit(*context.driver),"old Editor frame submit");old.Wait();
        auto latest=editor.RecordRenderFrame(0);Require(!editor.CompleteRenderFrame(oldDone),"Editor accepted stale completion against latest picking");
        Require(latest.Submit(*context.driver,[&]{context.window->SwapBuffers();}),"latest Editor frame submit");latest.Wait();
        Require(latest.GetCompletion().PresentationRequested()&&editor.CompleteRenderFrame(latest.GetCompletion()),"latest presented frame completion");
    }
    EditorProductValidation(editor,context);
    EditorInteractionValidation(editor,context);
    ui.SetCanvas(editor.m_canvas);ui.SetCommandRecordingEnabled(false);
    for(auto [window,wasOpen]:opened)if(wasOpen)window->Open();else window->Close();
    Require(glGetError()==GL_NO_ERROR,"Editor aggregate GL error");
    std::puts("PASS: 3 exact actual Editor Asset/Game/Scene+UI aggregate images; GPU completion, cancellation, failed presentation, stale/double picking rejection");
}
