#pragma once
#include <OvEditor/Panels/GameView.h>
#include <OvCore/ECS/Components/CCamera.h>
#include <bit>
extern "C" void _glfwInputCursorPos(_GLFWwindow*, double, double);

static void ValidateGamePreview(OvEditor::Core::Context& context, OvEditor::Core::Editor& editor,
    OvEditor::Core::EditorActions& actions)
{
    using namespace OvMaths;
    using namespace OvCore::ECS::Components;
    using namespace OvWindowing::Inputs;
    using OvEditor::Panels::GameView;
    using OvEditor::Settings::EditorSettings;
    const int oldMode = EditorSettings::GamePreviewMode.Get(), oldWidth = EditorSettings::GamePreviewWidth.Get(),
        oldHeight = EditorSettings::GamePreviewHeight.Get();
    const bool oldPreview = EditorSettings::CameraPreviewEnabled.Get();
    EditorSettings::CameraPreviewEnabled = true;
    context.sceneManager.LoadEmptyScene();
    auto& scene = *context.sceneManager.GetCurrentScene();
    auto& mainActor = scene.CreateActor("Main Camera"); auto& main = mainActor.AddComponent<CCamera>();
    auto& previewActor = scene.CreateActor("Shot Camera"); auto& selected = previewActor.AddComponent<CCamera>();
    main.SetClearColor({.06f,.1f,.16f}); selected.SetClearColor({.18f,.06f,.13f});
    for (auto* actor : {&mainActor,&previewActor}) actor->transform.SetWorldRotation(FQuaternion::LookAt({0,0,1},FVector3::Up));
    auto& cube = scene.CreateActor("Framing target"); cube.transform.SetWorldPosition({0,0,5});
    cube.AddComponent<CModelRenderer>().SetModel(context.editorResources->GetModel("Cube"));
    OvCore::Resources::Material material;
    material.SetShader(context.shaderManager[":Shaders\\Unlit.ovfx"]);
    material.SetProperty("u_Diffuse",FVector4{.95f,.62f,.16f,1});
    material.SetProperty("u_DiffuseMap",static_cast<OvRendering::Resources::Texture*>(nullptr));
    cube.AddComponent<CMaterialRenderer>().FillWithMaterial(material);
    actions.SelectActor(previewActor);
    auto& game = EDITOR_PANEL(GameView,"Game View");
    auto& view = EDITOR_PANEL(OvEditor::Panels::SceneView,"Scene View");
    game.Open(); game.dockable = false; game.autoSize = false; game.SetPosition({0,0}); game.SetSize({700,650});
    game.SetPreviewMode(GameView::PreviewMode::HD);
    OvUI::Modules::Canvas canvas; canvas.AddPanel(game); context.uiManager->SetCanvas(canvas);
    auto execute = [&](auto& commands, bool clear = false) {
        auto run = [&](auto& driver) { if (clear) { glBindFramebuffer(GL_FRAMEBUFFER,0); glDisable(GL_SCISSOR_TEST); glClearColor(.08f,.08f,.08f,1); glClear(GL_COLOR_BUFFER_BIT); } commands.Execute(driver); };
        if (context.renderThread) context.renderThread->Invoke(run); else run(*context.driver);
    };
    auto draw = [&] {
        game.Update(.016f);
        auto render = game.RecordRenderCommands(); execute(render);
        auto ui = context.uiManager->RecordRenderCommands(); execute(ui,true);
    };
    for (int i=0;i<3;++i) draw();
    Require(game.GetOutputTarget().GetSize() == std::pair<uint16_t,uint16_t>{1280,720},"Game View renders HD pixels independently of its panel size");
    auto size = game.GetImageSize();
    Require(std::abs(size.x/size.y-16.f/9) < .001f && size.y < 500,"HD display preserves aspect and leaves centered letterboxing");
    auto checkInput = [&] {
        game.Update(.016f);
        const auto viewport = game.GetInputViewport();
        const auto [w,h] = game.GetOutputSize();
        const UIPoint center{viewport.offset.x+viewport.extent.x*.5,viewport.offset.y+viewport.extent.y*.5};
        const auto pixel = viewport.Convert(center);
        Require(std::abs(pixel.x-w*.5) < .01 && std::abs(pixel.y-h*.5) < .01,"display center maps to framebuffer center after scaling");
        const auto edge = viewport.Convert({viewport.offset.x,viewport.offset.y});
        Require(std::abs(edge.x) < .01 && std::abs(edge.y-h) < .01,"Game image top-left maps to the bottom-origin UI coordinate system");
        const auto outside = viewport.Convert({viewport.offset.x-4,viewport.offset.y-4});
        Require(outside.x < 0 && outside.y > h,"toolbar and black bars stay outside the game input viewport");
        auto& input = *context.inputManager; input.ClearEvents();
        input.UIInput().Push({UIInputKind::Focus,1}); input.UIInput().Push({UIInputKind::PointerMove,0,center});
        const auto& words = input.CaptureUIWords();
        const auto unpack = [](uint64_t word) { return UIPoint{std::bit_cast<float>(uint32_t(word)),std::bit_cast<float>(uint32_t(word >> 32))}; };
        const auto actual = unpack(words[5]), extent = unpack(words[6]);
        Require(std::abs(actual.x-w*.5) < .01 && std::abs(actual.y-h*.5) < .01 && extent.x == w && extent.y == h,
            "InputManager receives the displayed Game image offset and render scale");
        input.ClearEvents();
    };
    checkInput();
    game.SetPreviewMode(GameView::PreviewMode::PHONE); draw(); draw();
    size = game.GetImageSize();
    Require(game.GetOutputSize() == std::pair<uint16_t,uint16_t>{1080,1920} && std::abs(size.x/size.y-9.f/16) < .001f,
        "portrait resolution produces pillarboxing without stretching the camera");
    checkInput();
    game.SetPreviewMode(GameView::PreviewMode::CUSTOM); game.SetCustomResolution(800,600); draw(); draw();
    Require(game.GetOutputSize() == std::pair<uint16_t,uint16_t>{800,600},"custom Game resolution controls the real render target");
    game.SetCustomResolution(-1,100000);
    Require(game.GetSafeSize() == std::pair<uint16_t,uint16_t>{64,4096},"custom dimensions are bounded before framebuffer allocation");
    game.SetPreviewMode(GameView::PreviewMode::WIDE); draw(); draw(); draw();
    size = game.GetImageSize();
    Require(std::abs(size.x/size.y-16.f/9) < .01f && game.GetOutputSize().first < 1280,"aspect-only mode adapts its rendering resolution to available content");
    game.SetPreviewMode(GameView::PreviewMode::FREE); draw(); draw(); draw();
    size = game.GetImageSize();
    Require(size.x > 680 && size.y > 580,"Free aspect uses the available content below the toolbar");

    // Real ImGui events select a preset from the production combo.
    auto& io = ImGui::GetIO(); const auto flags = io.BackendFlags;
    io.BackendFlags &= ~ImGuiBackendFlags_HasMouseHoveredViewport;
    ImGui_ImplGlfw_CursorEnterCallback(context.window->GetGlfwWindow(),true); io.AddFocusEvent(true);
    auto click = [&](ImVec2 p) { io.AddMousePosEvent(p.x,p.y); draw(); draw(); io.AddMouseButtonEvent(0,true); draw(); io.AddMouseButtonEvent(0,false); draw(); };
    click({game.GetPosition().x+100,game.GetPosition().y+ImGui::GetFrameHeight()+ImGui::GetFrameHeight()*.5f});
    auto* popup = ImGui::GetCurrentContext()->OpenPopupStack.empty() ? nullptr : ImGui::GetCurrentContext()->OpenPopupStack.back().Window;
    Require(popup != nullptr,"Game View resolution selector opens through actual ImGui input");
    click({popup->Pos.x+80,popup->Pos.y+ImGui::GetStyle().WindowPadding.y+ImGui::GetTextLineHeightWithSpacing()*5.5f});
    Require(game.GetPreviewMode() == GameView::PreviewMode::HD,"Game View preset selection changes the production render mode");
    game.SetSize({230,650}); for (int i=0;i<3;++i) draw();
    checkInput();
    Require(game.GetImageSize().x <= 230 && game.GetOutputSize() == std::pair<uint16_t,uint16_t>{1280,720},"narrow Game panel fits the image while keeping the chosen render resolution");

    view.Open(); view.SetPosition({0,0}); view.SetSize({600,700});
    game.SetPosition({610,0}); game.SetSize({340,700}); canvas.AddPanel(view);
    auto drawBoth = [&] {
        game.Update(.016f);
        auto gameCommands = game.RecordRenderCommands(); execute(gameCommands);
        auto sceneCommands = view.RecordRenderCommands(); execute(sceneCommands);
        auto ui = context.uiManager->RecordRenderCommands(); execute(ui,true);
    };
    for (int i=0;i<4;++i) drawBoth();
    auto* preview = view.GetCameraPreviewView();
    Require(preview && preview->GetCamera() == &selected.GetCamera() && game.GetCamera() == &main.GetCamera(),
        "selected camera preview resolves its own camera while Game View keeps the main camera");
    Require(preview->GetOutputSize() == game.GetOutputSize(),"selected camera preview uses Game View resolution for matching composition and UI layout");
    const auto read = [&](auto target) {
        const auto [w,h] = target.GetSize(); std::vector<uint8_t> pixels(size_t(w)*h*4);
        target.ReadRGBA8(pixels); return pixels;
    };
    auto gamePixels = read(game.GetOutputTarget()), previewPixels = read(preview->GetOutputTarget());
    const auto [w,h] = game.GetOutputSize(); const size_t middle = (size_t(h/2)*w+w/2)*4;
    Require(previewPixels[middle] > 200 && previewPixels[middle+1] > 100 &&
        previewPixels[0] != gamePixels[0],"preview draws real scene geometry and the selected camera clear color");
    previewActor.transform.SetWorldPosition({10,0,0}); drawBoth();
    previewPixels = read(preview->GetOutputTarget());
    Require(previewPixels[middle] < 100 && previewPixels[middle+1] < 100,"moving the selected camera updates its preview on the next rendered frame");
    previewActor.transform.SetWorldPosition({0,0,0}); selected.SetProjectionMode(OvRendering::Settings::EProjectionMode::ORTHOGRAPHIC); selected.SetSize(3); drawBoth();
    previewPixels = read(preview->GetOutputTarget());
    Require(previewPixels[middle] > 200,"camera preview supports orthographic cameras");
    const auto [rectPos,rectSize] = view.GetCameraPreviewRect();
    Require(rectSize.x > 100 && rectPos.y+rectSize.y <= 700,"camera preview card fits inside the Scene viewport");
    // Replay GLFW's cached cursor path on the hidden test window, without an OS
    // cursor warp. Both overlay hit tests and native picking use InputManager.
    io.AddMousePosEvent(rectPos.x+rectSize.x*.5f,rectPos.y+rectSize.y*.5f); drawBoth(); drawBoth();
    auto* window = context.window->GetGlfwWindow(); const int cursorMode = glfwGetInputMode(window,GLFW_CURSOR);
    glfwSetInputMode(window,GLFW_CURSOR,GLFW_CURSOR_DISABLED);
    const auto origin = ImGui::GetMainViewport()->Pos;
    _glfwInputCursorPos(reinterpret_cast<_GLFWwindow*>(window),rectPos.x+rectSize.x*.5-origin.x,rectPos.y+rectSize.y*.5-origin.y);
    Require(view.IsCameraPreviewHovered() && view.IsNavigationHovered(),"camera preview participates in the production overlay input hit test");
    context.window->MouseButtonPressedEvent.Invoke(GLFW_MOUSE_BUTTON_RIGHT); view.Update(.016f);
    Require(!view.GetCameraController().IsOperating(),"right-clicking the camera preview does not capture the Scene camera");
    context.window->MouseButtonReleasedEvent.Invoke(GLFW_MOUSE_BUTTON_RIGHT); context.inputManager->ClearEvents();
    context.window->MouseButtonPressedEvent.Invoke(GLFW_MOUSE_BUTTON_LEFT);
    auto picking = view.RecordRenderCommands(); const auto interaction = view.GetRecordedInteraction();
    execute(picking); view.CompleteRender();
    Require(interaction && !interaction->hovered && actions.IsActorSelected(previewActor),"camera preview blocks scene picking behind its image");
    context.window->MouseButtonReleasedEvent.Invoke(GLFW_MOUSE_BUTTON_LEFT); context.inputManager->ClearEvents();
    glfwSetInputMode(window,GLFW_CURSOR,cursorMode);
    auto capture = [&] {
        const auto [width,height] = context.window->GetFramebufferSize(); std::vector<uint8_t> pixels(size_t(width)*height*4);
        glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        std::ofstream output(context.renderThread ? "game-preview-thread.ppm" : "game-preview.ppm",std::ios::binary);
        output << "P6\n" << width << " " << height << "\n255\n";
        for (int y=height-1;y>=0;--y) for (int x=0;x<width;++x) output.write(reinterpret_cast<const char*>(pixels.data()+(size_t(y)*width+x)*4),3);
        Require(output.good(),"Game View and selected camera preview captured from real editor rendering");
    };
    if (context.renderThread) context.renderThread->Invoke([&](auto&) {capture();}); else capture();
    click({rectPos.x+rectSize.x-17,rectPos.y+10});
    Require(!EditorSettings::CameraPreviewEnabled.Get() && !view.GetCameraPreviewView() && actions.IsActorSelected(previewActor),
        "closing the preview through real ImGui input preserves the camera selection");
    EditorSettings::CameraPreviewEnabled = true;
    game.SetPreviewMode(GameView::PreviewMode::CUSTOM); game.SetCustomResolution(1024,768);
    EditorSettings::Save(); game.SetPreviewMode(GameView::PreviewMode::FREE); game.SetCustomResolution(64,64);
    EditorSettings::CameraPreviewEnabled = false; EditorSettings::Load();
    Require(game.GetPreviewMode() == GameView::PreviewMode::CUSTOM && game.GetSafeSize() == std::pair<uint16_t,uint16_t>{1024,768} &&
        EditorSettings::CameraPreviewEnabled.Get(),"preview mode, custom dimensions, and camera preview visibility survive settings reload");
    game.SetPreviewMode(GameView::PreviewMode::HD); drawBoth();
    // Exercise the product's Extract-all / Prepare-all / Record-all path as well.
    auto frame = editor.RecordRenderFrame(.016f);
    auto submit = [&](auto& driver) { frame.Submit(driver); frame.Wait(); };
    if (context.renderThread) context.renderThread->Invoke(submit); else submit(*context.driver);
    Require(editor.CompleteRenderFrame(frame.GetCompletion()),"batched Editor frame submission includes the selected camera preview");
    previewActor.SetActive(false);
    Require(view.GetCameraPreviewView() == nullptr,"inactive selected camera clears the preview safely");
    previewActor.SetActive(true); preview = view.GetCameraPreviewView();
    previewActor.RemoveComponent(selected);
    Require(preview->GetCamera() == nullptr && view.GetCameraPreviewView() == nullptr,"removed camera component invalidates a retained preview view");
    mainActor.SetActive(false); drawBoth();
    Require(!game.GetCamera() && game.GetImageSize().x == 0,"Game View shows an empty state instead of the previous camera image");
    game.Close(); game.Update(.016f); context.inputManager->ClearEvents();
    Require(context.inputManager->CaptureUIWords()[4] == 0,"closing Game View releases runtime UI focus");
    context.inputManager->ClearEvents(); game.Open();
    context.sceneManager.LoadEmptyScene();
    Require(!preview->GetScene() && !preview->GetCamera() && !view.GetCameraPreviewView(),"scene replacement rejects previous camera GUIDs and lifetime tokens");
    auto& undoCamera = context.sceneManager.GetCurrentScene()->CreateActor("Undo Camera");
    undoCamera.AddComponent<CCamera>(); const auto undoGUID = undoCamera.GetGUID();
    actions.SelectActor(undoCamera); actions.ClearHistory(); preview = view.GetCameraPreviewView();
    Require(preview != nullptr,"new scene selection establishes a new camera preview binding");
    auto pendingPreview = preview->RecordRenderCommands();
    actions.DestroySelectedActors(); actions.CommitSceneEdit(true);
    Require(!preview->GetCamera() && actions.Undo(),"camera deletion can be undone without keeping a stale component reference");
    execute(pendingPreview);
    Require(!preview->GetScene() && view.GetCameraPreviewView() &&
        actions.GetSelectedActor().GetGUID() == undoGUID && view.GetCameraPreviewView()->GetCamera(),
        "undo rebinds the restored camera while previously recorded preview commands remain safe");
    EditorSettings::GamePreviewMode = oldMode; EditorSettings::GamePreviewWidth = oldWidth;
    EditorSettings::GamePreviewHeight = oldHeight; EditorSettings::CameraPreviewEnabled = oldPreview;
    io.BackendFlags = flags; ImGui_ImplGlfw_CursorEnterCallback(context.window->GetGlfwWindow(),false);
    io.AddMousePosEvent(-10000,-10000); context.uiManager->RemoveCanvas();
}
