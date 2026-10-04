#pragma once
#include <OvCore/ECS/Components/CPhysicalBox.h>
#include <OvCore/ECS/Components/CPostProcessStack.h>
#include <OvCore/ECS/Components/CReflectionProbe.h>
#include <OvRendering/Core/ViewPreparer.h>
#include <OvRendering/Resources/Loaders/TextureLoader.h>

static void ValidateSceneDisplay(OvEditor::Core::Context& context, OvEditor::Core::Editor& editor,
    OvEditor::Core::EditorActions& actions)
{
    using namespace OvMaths;
    using namespace OvCore::ECS::Components;
    using OvEditor::Settings::EditorSettings;
    using DisplayMode = OvEditor::Panels::SceneView::DisplayMode;
    const auto oldMode = EditorSettings::SceneDisplayMode.Get();
    const auto oldGameMode = EditorSettings::GamePreviewMode.Get();
    const bool oldGrid = EditorSettings::SceneShowGrid.Get(), oldLights = EditorSettings::SceneShowLightIcons.Get(),
        oldCameras = EditorSettings::SceneShowCameraIcons.Get(), oldProbes = EditorSettings::SceneShowReflectionIcons.Get(),
        oldColliders = EditorSettings::SceneShowColliders.Get(), oldFrustums = EditorSettings::SceneShowCameraFrustums.Get(),
        oldGeometryBounds = EditorSettings::ShowGeometryBounds.Get(), oldLightBounds = EditorSettings::ShowLightBounds.Get(),
        oldPreview = EditorSettings::CameraPreviewEnabled.Get();
    EditorSettings::SceneDisplayMode = 0; EditorSettings::SceneShowGrid = false;
    EditorSettings::ShowGeometryBounds = EditorSettings::ShowLightBounds = false;
    EditorSettings::CameraPreviewEnabled = false;
    context.sceneManager.LoadEmptyScene();
    auto& scene = *context.sceneManager.GetCurrentScene();
    auto& cube = scene.CreateActor("Material preview");
    cube.AddComponent<CModelRenderer>().SetModel(context.editorResources->GetModel("Cube"));
    OvCore::Resources::Material material;
    material.SetShader(context.shaderManager[":Shaders\\Standard.ovfx"]);
    const FVector4 color{.85f,.24f,.08f,1};
    material.SetProperty("u_Albedo",color); material.SetProperty("u_EmissiveIntensity",0.f);
    cube.AddComponent<CMaterialRenderer>().FillWithMaterial(material);
    actions.UnselectActor();
    auto& view = EDITOR_PANEL(OvEditor::Panels::SceneView,"Scene View");
    view.Open(); view.dockable = false; view.autoSize = false; view.SetPosition({0,0}); view.SetSize({960,700});
    view.GetCamera()->SetProjectionMode(OvRendering::Settings::EProjectionMode::PERSPECTIVE);
    view.GetCameraController().SetPosition({0,0,8});
    view.GetCameraController().SetRotation(FQuaternion::LookAt({0,0,-1},FVector3::Up));
    OvUI::Modules::Canvas canvas; canvas.AddPanel(view); context.uiManager->SetCanvas(canvas);
    auto execute = [&](auto& commands, bool clear = false) {
        auto run = [&](auto& driver) { if (clear) { glBindFramebuffer(GL_FRAMEBUFFER,0); glDisable(GL_SCISSOR_TEST); glClearColor(.08f,.08f,.08f,1); glClear(GL_COLOR_BUFFER_BIT); } commands.Execute(driver); };
        if (context.renderThread) context.renderThread->Invoke(run); else run(*context.driver);
    };
    auto draw = [&] {
        auto commands = view.RecordRenderCommands(); execute(commands);
        auto ui = context.uiManager->RecordRenderCommands(); execute(ui,true);
    };
    auto read = [&](auto target) {
        const auto [w,h] = target.GetSize(); std::vector<uint8_t> pixels(size_t(w)*h*4);
        target.ReadRGBA8(pixels); return pixels;
    };
    for (int i=0;i<3;++i) draw();
    const auto [w,h] = view.GetOutputSize(); const size_t middle = (size_t(h/2)*w+w/2)*4;
    auto lit = read(view.GetOutputTarget());
    view.SetDisplayMode(DisplayMode::UNLIT); draw();
    const auto unlit = read(view.GetOutputTarget());
    Require(lit[middle] < 20 && unlit[middle] > 200 && unlit[middle+1] > 45 && unlit[middle+2] < 35,
        "Unlit displays Standard base color even without scene lighting");
    auto stale = view.GetRecordedInteraction();
    view.SetDisplayMode(DisplayMode::WIREFRAME); draw();
    Require(!view.CompleteInteraction(stale),"changing display mode invalidates previously recorded picking");
    const auto wire = read(view.GetOutputTarget());
    size_t filledPixels = 0, wirePixels = 0;
    for (size_t p=0;p<wire.size();p+=4)
    {
        if (unlit[p] > 200 && unlit[p+1] < 100) ++filledPixels;
        if (wire[p] > 160 && wire[p+1] > 180 && wire[p+2] > 200) ++wirePixels;
    }
    Require(filledPixels > 1000 && wirePixels > 100 && wirePixels < filledPixels/5,
        "Wireframe renders sparse triangle edges instead of filled surfaces");
    const auto unchangedColor = std::get<FVector4>(material.GetProperty("u_Albedo")->value);
    Require(unchangedColor.x == color.x && unchangedColor.y == color.y && unchangedColor.z == color.z && unchangedColor.w == color.w && material.GetFeatures().empty(),
        "display switching leaves authored material properties and feature flags unchanged");
    auto extracted = view.ExtractView();
    Require(extracted.request->wireframe && !extracted.request->applyPostProcess,
        "Scene captures wireframe and post-process bypass in its owned render request");
    view.PrepareView(extracted);
    bool lineState = false;
    for (auto& prepared : extracted.frame->GetPreparedViews()) for (auto& draw : prepared->GetDraws())
        lineState |= draw.pipelineState.rasterizationMode == baregl::types::ERasterizationMode::LINE;
    Require(lineState,"wireframe is encoded in per-draw pipeline state before render-thread execution");
    auto commands = view.RecordView(extracted); execute(commands);

    OvEditor::Rendering::PickingCollector picking;
    OvEditor::Rendering::EditorOverlayRequest overlay; overlay.scene = scene.GetLifetimeToken();
    OvCore::Rendering::SceneRenderRequest pickRequest{{scene,*view.GetCamera(),w,h}};
    pickRequest.meshTechnique = "WIREFRAME_PASS"; pickRequest.wireframe = true;
    auto pick = picking.Extract(pickRequest,overlay,context.driver->CreatePipelineState(),nullptr); pick.Seal(); execute(pick);
    const auto picked = picking.GetReadback()->ResolveRegion(scene,w/2-15,h/2-15,w/2+15,h/2+15);
    Require(picked.size() == 1 && picked.front() == &cube,"Wireframe picking keeps filled geometry selectable between edges");

    // Use a real two-texel cutout to verify texture sampling and alpha clipping.
    using TextureLoader = OvRendering::Resources::Loaders::TextureLoader;
    const uint8_t texels[]{0,255,0,255,255,0,0,0};
    auto release = [](OvRendering::Resources::Texture* asset) { TextureLoader::Destroy(asset); };
    std::unique_ptr<OvRendering::Resources::Texture,decltype(release)> texture(TextureLoader::CreateFromMemory(texels,2,1,
        baregl::types::ETextureFilteringMode::NEAREST,baregl::types::ETextureFilteringMode::NEAREST,
        baregl::types::ETextureWrapMode::REPEAT,baregl::types::ETextureWrapMode::REPEAT,false),release);
    material.SetProperty("u_Albedo",FVector4{1,1,1,1}); material.SetProperty("u_AlbedoMap",texture.get());
    material.SetProperty("u_AlphaClippingThreshold",.5f);
    view.SetDisplayMode(DisplayMode::UNLIT); draw();
    const auto cutout = read(view.GetOutputTarget()); size_t green = 0, red = 0;
    for (size_t p=0;p<cutout.size();p+=4) { if (cutout[p+1] > 220 && cutout[p] < 20) ++green; if (cutout[p] > 220 && cutout[p+1] < 20) ++red; }
    Require(green > filledPixels/3 && green < filledPixels*2/3 && red == 0,
        "Unlit retains authored texture sampling and transparent cutouts");
    view.SetDisplayMode(DisplayMode::WIREFRAME); draw();
    const auto wireCutout = read(view.GetOutputTarget()); size_t cutoutEdges = 0;
    for (size_t p=0;p<wireCutout.size();p+=4)
        if (wireCutout[p] > 160 && wireCutout[p+1] > 180 && wireCutout[p+2] > 200) ++cutoutEdges;
    Require(cutoutEdges > 50 && cutoutEdges < wirePixels*3/4,"Wireframe retains material alpha cutouts on its edges");
    // Custom shaders without preview passes use common diffuse semantics.
    std::ifstream shaderInput(context.engineAssetsPath/"Shaders"/"Unlit.ovfx");
    std::string shaderSource{std::istreambuf_iterator<char>(shaderInput),std::istreambuf_iterator<char>()};
    for (const std::string pass : {"#pass UNLIT_PASS\n", "#pass WIREFRAME_PASS\n"})
        if (auto i = shaderSource.find(pass); i != std::string::npos) shaderSource.erase(i,pass.size());
    // Windows text streams normalize CRLF, so the fixture matches both checkouts.
    { std::ofstream file(context.projectAssetsPath/"CustomPreview.ovfx"); file << shaderSource; }
    OvCore::Resources::Material custom;
    custom.SetShader(context.shaderManager["CustomPreview.ovfx"]); custom.SetFeatures({"ALPHA_CLIPPING"});
    custom.SetProperty("u_Diffuse",FVector4{1,1,1,1}); custom.SetProperty("u_DiffuseMap",texture.get());
    custom.SetProperty("u_AlphaClippingThreshold",.5f); custom.SetBlendable(true); custom.SetDepthWriting(false);
    cube.GetComponent<CMaterialRenderer>()->FillWithMaterial(custom);
    view.SetDisplayMode(DisplayMode::UNLIT); draw();
    const auto fallbackPixels = read(view.GetOutputTarget()); size_t fallbackGreen = 0, fallbackRed = 0;
    for (size_t p=0;p<fallbackPixels.size();p+=4) { if (fallbackPixels[p+1] > 220 && fallbackPixels[p] < 20) ++fallbackGreen; if (fallbackPixels[p] > 220 && fallbackPixels[p+1] < 20) ++fallbackRed; }
    Require(!custom.HasPass("UNLIT_PASS") && fallbackGreen == green && fallbackRed == 0,
        "custom shader preview fallback preserves diffuse textures and alpha clipping");
    auto fallback = view.ExtractView(); const auto& fallbackMesh = fallback.frame->GetViews().back()->GetMeshes().front();
    Require(fallbackMesh.transparent && fallbackMesh.state.blendable && !fallbackMesh.state.depthWriting && custom.HasFeature("ALPHA_CLIPPING"),
        "custom preview fallback preserves source render state without modifying its features");
    cube.GetComponent<CMaterialRenderer>()->FillWithMaterial(material);
    material.SetProperty("u_Albedo",color); material.SetProperty("u_AlbedoMap",static_cast<baregl::Texture*>(nullptr));
    material.SetProperty("u_AlphaClippingThreshold",0.f);
    view.SetDisplayMode(DisplayMode::WIREFRAME); material.SetUserInterface(true);
    auto uiCapture = view.ExtractView(); view.PrepareView(uiCapture); bool filledUI = false;
    for (auto& prepared : uiCapture.frame->GetPreparedViews()) for (auto& draw : prepared->GetDraws())
        if (prepared->GetScene().GetMeshes()[draw.sourceIndex].stage == OvRendering::Data::RenderStage::SceneUI)
            filledUI |= draw.pipelineState.rasterizationMode == baregl::types::ERasterizationMode::FILL;
    Require(filledUI,"Scene UI materials keep filled rendering while geometry uses Wireframe");
    material.SetUserInterface(false); view.SetDisplayMode(DisplayMode::UNLIT);
    for (auto name : {":Shaders\\Standard.ovfx", ":Shaders\\Unlit.ovfx"})
        for (auto pass : {"UNLIT_PASS", "WIREFRAME_PASS"})
        {
            auto program = context.shaderManager[name]->RetainVariant(pass,{"SKINNING"});
            Require(program && OvRendering::Core::MeshCommandRecorder::ValidateProgram(*program),"display shader passes retain skeletal animation support");
        }
    auto& effects = scene.CreateActor("Post processing").AddComponent<CPostProcessStack>();
    auto tone = effects.GetTonemappingSettings(); tone.enabled = true; tone.exposure = .05f; effects.SetTonemappingSettings(tone);
    draw(); Require(read(view.GetOutputTarget())[middle] > 200,"Unlit bypasses scene post-processing exposure");

    cube.AddComponent<CPhysicalBox>().SetSize({3,3,3});
    auto& secondary = scene.CreateActor("Secondary collider"); secondary.transform.SetWorldPosition({3,0,0});
    secondary.AddComponent<CPhysicalBox>().SetSize({2,2,2});
    auto& shot = scene.CreateActor("Shot Camera"); auto& camera = shot.AddComponent<CCamera>();
    shot.transform.SetWorldPosition({3,1,0}); camera.SetFar(4);
    auto& lamp = scene.CreateActor("Point Light"); lamp.AddComponent<CPointLight>(); lamp.transform.SetWorldPosition({-3,1,0});
    auto& probeActor = scene.CreateActor("Reflection probe"); probeActor.AddComponent<CReflectionProbe>(); probeActor.transform.SetWorldPosition({0,2,0});
    OvEditor::Rendering::EditorOverlayCollector overlays;
    const auto state = context.driver->CreatePipelineState();
    const auto renderView = OvRendering::Core::ViewPreparer::BuildView(view.ExtractView().frame->GetViews().back()->GetView());
    auto batches = [&] { auto c = overlays.Extract(scene,overlay,renderView,state,view.GetOutputTarget()); c.Seal(); execute(c); return overlays.GetFrameInfo().batchCount; };
    overlay.debugObjects = true; overlay.lightIcons = overlay.cameraIcons = overlay.reflectionIcons = false;
    overlay.colliders = overlay.cameraFrustums = false;
    Require(batches() == 0,"disabling all unselected Scene helpers produces no overlay draws");
    overlay.lightIcons = true; Require(batches() > 0,"light icons can be enabled independently"); overlay.lightIcons = false;
    overlay.cameraIcons = true; Require(batches() > 0,"camera icons can be enabled independently"); overlay.cameraIcons = false;
    overlay.reflectionIcons = true; Require(batches() > 0,"reflection icons can be enabled independently"); overlay.reflectionIcons = false;
    overlay.selected = cube.GetGUID(); overlay.selection = {cube.GetGUID()};
    const auto noColliders = batches(); overlay.colliders = true; const auto oneCollider = batches();
    overlay.selection = {cube.GetGUID(),secondary.GetGUID()}; overlay.colliders = false;
    const auto twoWithoutColliders = batches(); overlay.colliders = true; const auto twoColliders = batches();
    Require(noColliders > 0 && oneCollider > noColliders && twoColliders-twoWithoutColliders > oneCollider-noColliders,
        "collider helpers cover secondary selections while outlines and transform gizmos remain visible");
    secondary.SetParent(cube); const auto overlapping = batches(); overlay.selection = {cube.GetGUID()};
    Require(batches() == overlapping,"selected parent and child draw collider helpers once per entity"); secondary.DetachFromParent();
    overlay.selected = shot.GetGUID(); overlay.selection = {shot.GetGUID()}; overlay.colliders = false;
    const auto noFrustum = batches(); overlay.cameraFrustums = true;
    Require(batches() > noFrustum,"selected camera frustums have an independent visibility switch");
    overlay.selected = 0; overlay.selection.clear(); overlay.cameraFrustums = false;
    auto pickVisible = [&](bool icons) {
        overlay.lightIcons = overlay.cameraIcons = overlay.reflectionIcons = icons;
        auto c = picking.Extract(pickRequest,overlay,state,nullptr); c.Seal(); execute(c);
        return picking.GetReadback()->ResolveRegion(scene,0,0,w,h);
    };
    const auto withIcons = pickVisible(true), withoutIcons = pickVisible(false);
    Require(withIcons.size() > withoutIcons.size() && withoutIcons.size() == 1 && withoutIcons.front() == &cube,
        "hidden camera, light and reflection icons are removed from scene picking");
    scene.DestroyActor(probeActor); secondary.SetActive(false); lamp.SetActive(false);

    // Scene display overrides must not propagate to the two game-camera views.
    EditorSettings::CameraPreviewEnabled = true; actions.SelectActor(shot);
    shot.transform.SetWorldPosition({0,0,8}); shot.transform.SetWorldRotation(FQuaternion::LookAt({0,0,-1},FVector3::Up)); camera.SetFar(100);
    auto& game = EDITOR_PANEL(OvEditor::Panels::GameView,"Game View"); game.Open();
    game.SetPreviewMode(OvEditor::Panels::GameView::PreviewMode::HD);
    view.SetDisplayMode(DisplayMode::WIREFRAME);
    auto frame = editor.RecordRenderFrame(.016f);
    auto submit = [&](auto& driver) { frame.Submit(driver); frame.Wait(); };
    if (context.renderThread) context.renderThread->Invoke(submit); else submit(*context.driver);
    Require(editor.CompleteRenderFrame(frame.GetCompletion()),"batched Editor frame supports Scene wireframe with camera preview");
    auto gameCapture = game.ExtractView(); auto* preview = view.GetCameraPreviewView(); auto previewCapture = preview->ExtractView();
    Require(!gameCapture.request->wireframe && gameCapture.request->meshTechnique.empty() && gameCapture.request->applyPostProcess &&
        !previewCapture.request->wireframe && previewCapture.request->meshTechnique.empty() && previewCapture.request->applyPostProcess,
        "Game View and camera preview retain normal rendering and post-processing");
    Require(read(game.GetOutputTarget()) == read(preview->GetOutputTarget()),"Game View and camera preview pixels match while Scene uses Wireframe");
    actions.UnselectActor(); EditorSettings::CameraPreviewEnabled = false;

    auto& io = ImGui::GetIO(); const auto flags = io.BackendFlags;
    io.BackendFlags &= ~ImGuiBackendFlags_HasMouseHoveredViewport;
    ImGui_ImplGlfw_CursorEnterCallback(context.window->GetGlfwWindow(),true); io.AddFocusEvent(true);
    auto click = [&](ImVec2 p) { io.AddMousePosEvent(p.x,p.y); draw(); draw(); io.AddMouseButtonEvent(0,true); draw(); io.AddMouseButtonEvent(0,false); draw(); };
    const auto pos = view.GetPosition(); const float row = pos.y+3*ImGui::GetFrameHeight()+26;
    click({pos.x+70,row+ImGui::GetFrameHeight()*.5f});
    auto popup = [&] { return ImGui::GetCurrentContext()->OpenPopupStack.empty() ? nullptr : ImGui::GetCurrentContext()->OpenPopupStack.back().Window; };
    Require(popup() != nullptr,"Scene display combo opens through real ImGui input");
    click({popup()->Pos.x+55,popup()->Pos.y+ImGui::GetStyle().WindowPadding.y+ImGui::GetTextLineHeightWithSpacing()*1.5f});
    Require(view.GetDisplayMode() == DisplayMode::UNLIT,"Scene display combo changes the actual rendering mode");
    const bool gridBefore = EditorSettings::SceneShowGrid.Get();
    click({pos.x+175,row+ImGui::GetFrameHeight()*.5f});
    Require(popup() != nullptr,"Scene Overlays menu opens through real ImGui input");
    click({popup()->DC.CursorStartPos.x+50,popup()->DC.CursorStartPos.y+ImGui::GetFontSize()*.5f});
    Require(EditorSettings::SceneShowGrid.Get() != gridBefore,"Scene Grid toggle changes the production overlay setting");
    view.SetDisplayMode(DisplayMode::WIREFRAME); EditorSettings::SceneShowLightIcons = false; EditorSettings::SceneShowColliders = false;
    EditorSettings::Save(); EditorSettings::SceneDisplayMode = 0; EditorSettings::SceneShowGrid = gridBefore;
    EditorSettings::SceneShowLightIcons = EditorSettings::SceneShowColliders = true; EditorSettings::Load();
    Require(view.GetDisplayMode() == DisplayMode::WIREFRAME && EditorSettings::SceneShowGrid.Get() != gridBefore &&
        !EditorSettings::SceneShowLightIcons.Get() && !EditorSettings::SceneShowColliders.Get(),"Scene display and overlay choices survive settings reload");
    secondary.SetActive(true); secondary.AddComponent<CModelRenderer>().SetModel(context.editorResources->GetModel("Cube"));
    secondary.AddComponent<CMaterialRenderer>().FillWithMaterial(material);
    cube.transform.SetWorldPosition({1,1,0}); cube.transform.SetWorldScale({2,2,2});
    secondary.transform.SetWorldPosition({-2,1,-1}); secondary.transform.SetWorldScale({2,2,2});
    shot.SetActive(false);
    view.GetCameraController().SetPosition({5,4,9});
    view.GetCameraController().SetRotation(FQuaternion::LookAt({-5,-3,-9},FVector3::Up));
    actions.SelectActor(cube); io.AddMousePosEvent(-10000,-10000); for (int i=0;i<3;++i) draw();
    auto capture = [&] {
        const auto [width,height] = context.window->GetFramebufferSize(); std::vector<uint8_t> pixels(size_t(width)*height*4);
        glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        std::ofstream output(context.renderThread ? "scene-display-thread.ppm" : "scene-display.ppm",std::ios::binary);
        output << "P6\n" << width << " " << height << "\n255\n";
        for (int y=height-1;y>=0;--y) for (int x=0;x<width;++x) output.write(reinterpret_cast<const char*>(pixels.data()+(size_t(y)*width+x)*4),3);
        Require(output.good(),"Scene display toolbar and wireframe captured from real editor rendering");
    };
    if (context.renderThread) context.renderThread->Invoke([&](auto&) {capture();}); else capture();
    io.BackendFlags = flags; ImGui_ImplGlfw_CursorEnterCallback(context.window->GetGlfwWindow(),false);
    EditorSettings::SceneDisplayMode = oldMode; EditorSettings::SceneShowGrid = oldGrid;
    EditorSettings::SceneShowLightIcons = oldLights; EditorSettings::SceneShowCameraIcons = oldCameras;
    EditorSettings::SceneShowReflectionIcons = oldProbes; EditorSettings::SceneShowColliders = oldColliders;
    EditorSettings::SceneShowCameraFrustums = oldFrustums; EditorSettings::ShowGeometryBounds = oldGeometryBounds;
    EditorSettings::ShowLightBounds = oldLightBounds; EditorSettings::CameraPreviewEnabled = oldPreview;
    EditorSettings::GamePreviewMode = oldGameMode;
    EditorSettings::Save(); context.uiManager->RemoveCanvas(); context.sceneManager.LoadEmptyScene();
}
