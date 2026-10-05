#pragma once
#include <OvCore/ECS/Components/CMaterialRenderer.h>
#include <OvUI/Widgets/Texts/TextDisabled.h>

class InspectorHeaderProbe : public OvUI::Plugins::IPlugin
{
public:
    ImVec2 min{}, max{};
    void Execute(OvUI::Plugins::EPluginExecutionContext) override
    { min = ImGui::GetItemRectMin(); max = ImGui::GetItemRectMax(); }
};

template<class Draw> static void ValidateInspectorWorkflow(OvEditor::Core::Context& context,
    OvEditor::Core::EditorActions& actions, OvEditor::Panels::Hierarchy& hierarchy,
    OvEditor::Panels::Inspector& inspector, Draw draw)
{
    using namespace OvCore::ECS::Components;
    using Header = OvUI::Widgets::Layout::GroupCollapsable;
    using Vector = OvMaths::FVector3;
    context.sceneManager.LoadEmptyScene(); inspector.SetSearchFilter("");
    auto scene = [&] { return context.sceneManager.GetCurrentScene(); };
    const auto parentID = scene()->CreateActor("Rig").GetGUID();
    const auto aID = scene()->CreateActor("Inspector A").GetGUID();
    const auto bID = scene()->CreateActor("Scene B").GetGUID();
    const auto cID = scene()->CreateActor("Scene C").GetGUID();
    auto a = [&] { return scene()->FindActorByGUID(aID); };
    auto b = [&] { return scene()->FindActorByGUID(bID); };
    auto c = [&] { return scene()->FindActorByGUID(cID); };
    a()->AddComponent<CPointLight>().SetIntensity(7);
    a()->AddComponent<CCamera>(); b()->AddComponent<CPointLight>().SetIntensity(2);
    c()->AddComponent<CPointLight>().SetIntensity(3);
    b()->SetParent(*scene()->FindActorByGUID(parentID));
    b()->transform.SetLocalPosition({6,1,2});
    const auto lightType = a()->GetComponent<CPointLight>()->GetTypeName();
    const auto cameraType = a()->GetComponent<CCamera>()->GetTypeName();
    const auto transformType = a()->transform.GetTypeName();
    auto header = [&](const char* name) -> Header& {
        for (auto* item : Widgets<Header>(inspector)) if (item->name == name) return *item;
        throw std::runtime_error(std::string("Missing Inspector section: ")+name);
    };
    auto propertyWidget = [&](const char* label) -> OvUI::Widgets::AWidget* {
        auto* binding = Property<float>(inspector,label);
        for (auto* item : Widgets<OvUI::Widgets::AWidget>(inspector))
            if (binding && item->GetPlugin<OvUI::Plugins::DataDispatcher<float>>() == binding) return item;
        throw std::runtime_error(std::string("Missing Inspector control: ")+label);
    };
    actions.SelectActor(*a()); draw(); draw();
    auto* farControl = propertyWidget("Far"); auto* nearControl = propertyWidget("Near");
    auto* sizeControl = propertyWidget("Size");
    auto* window = ImGui::FindWindowByName((inspector.name+inspector.GetPanelID()).c_str());
    const auto cameraID = ImHashStr("Camera#####inspector_component_Camera",0,window->ID);
    window->StateStorage.SetInt(cameraID,0);
    int cameraDraws = 0; header("Camera").CreateWidget<DrawProbe>(cameraDraws);
    draw(); Require(cameraDraws == 0,"collapsed camera properties stay closed before search");
    inspector.SetSearchFilter("CaMeRa FaR"); draw();
    Require(header("Camera").visible && !header("Point Light").visible && farControl->visible && !nearControl->visible,
        "Inspector search combines case-insensitive component and property names");
    Require(cameraDraws == 1 && window->StateStorage.GetInt(cameraID) == 0 && header("Camera").expandForSearch,
        "search opens matching properties without changing saved expansion");
    Require(propertyWidget("Far") == farControl,"filtering preserves property widget and binding identity");
    inspector.SetSearchFilter("camera"); draw();
    Require(cameraDraws == 2 && sizeControl->visible && !sizeControl->enabled,
        "component search reveals all properties while retaining projection-dependent controls");
    inspector.SetSearchFilter("no such property"); draw();
    bool emptyMessage = false;
    for (auto* text : Widgets<OvUI::Widgets::Texts::TextDisabled>(inspector))
        emptyMessage |= text->enabled && text->content == "No matching components or properties";
    Require(emptyMessage && !header("Camera").visible && !header("Transform").visible,
        "unmatched Inspector search explains the empty result");
    inspector.SetSearchFilter(""); draw();
    Require(farControl->visible && nearControl->visible && !header("Camera").expandForSearch && cameraDraws == 2,
        "clearing search restores fields and the original collapsed camera");
    inspector.SetLocked(true); actions.SelectActor(*b()); draw(); draw();
    Require(inspector.GetTargetActor()->GetGUID() == aID && actions.GetSelectedActor().GetGUID() == bID,
        "locked Inspector retains its target independently of scene selection");
    bool selectedB = false, selectedA = false;
    for (auto* node : Widgets<OvUI::Widgets::Layout::TreeNode>(hierarchy))
    { if (node->name == "Scene B") selectedB = node->selected; if (node->name == "Inspector A") selectedA = node->selected; }
    Require(selectedB && !selectedA,"Hierarchy selection follows the scene while Inspector is locked");
    Require(Vector::Distance(EDITOR_PANEL(OvEditor::Panels::SceneView,"Scene View").GetGizmoFrame().position,
        b()->transform.GetWorldPosition()) < .001f,"gizmo follows the scene selection rather than the locked target");
    actions.ClearHistory(); EditProperty<float>(inspector,"Intensity",11); actions.CommitSceneEdit(true);
    Require(a()->GetComponent<CPointLight>()->GetIntensity() == 11 && b()->GetComponent<CPointLight>()->GetIntensity() == 2,
        "locked property editing changes only the inspected actor");
    Require(actions.Undo() && inspector.IsLocked() && inspector.GetTargetActor()->GetGUID() == aID &&
        actions.GetSelectedActor().GetGUID() == bID && a()->GetComponent<CPointLight>()->GetIntensity() == 7,
        "undo rebinds locked Inspector by GUID without changing scene selection");
    Require(actions.Redo() && inspector.IsLocked() && a()->GetComponent<CPointLight>()->GetIntensity() == 11,
        "redo retains Inspector lock after scene reconstruction");
    inspector.SetLocked(false);
    Require(inspector.GetTargetActor()->GetGUID() == bID,"unlock immediately follows the current scene selection");
    actions.SelectActor(*a()); a()->GetComponent<CPointLight>()->SetColor({.2f,.4f,.6f});
    a()->GetComponent<CPointLight>()->SetLinear(.25f);
    Require(inspector.CopyComponentValues(lightType),"copy captures primary component values");
    actions.SetSelection({b(),c()}); actions.ClearHistory();
    auto* originalLight = b()->GetComponent<CPointLight>();
    Require(!inspector.CanPasteComponentValues(cameraType) && !inspector.PasteComponentValues(cameraType),
        "component clipboard rejects mismatched component types");
    Require(inspector.PasteComponentValues(lightType) && actions.UndoCount() == 1 &&
        b()->GetComponent<CPointLight>() == originalLight && b()->GetParent()->GetGUID() == parentID && b()->GetName() == "Scene B",
        "batch paste creates one undo step and preserves native instance, actor identity and parent");
    Require(b()->GetComponent<CPointLight>()->GetIntensity() == 11 && c()->GetComponent<CPointLight>()->GetLinear() == .25f &&
        Vector(c()->GetComponent<CPointLight>()->GetColor()) == Vector(.2f,.4f,.6f),
        "batch paste applies all serialized light properties to common components");
    Require(actions.Undo() && b()->GetComponent<CPointLight>()->GetIntensity() == 2 &&
        c()->GetComponent<CPointLight>()->GetIntensity() == 3,"undo paste restores each target's distinct original values");
    Require(actions.Redo(),"redo batch component paste"); draw(); draw();
    inspector.SetLocked(true); actions.SelectActor(*a());
    Require(inspector.ResetComponentValues(lightType) && b()->GetComponent<CPointLight>()->GetIntensity() == 1 &&
        c()->GetComponent<CPointLight>()->GetIntensity() == 1 && a()->GetComponent<CPointLight>()->GetIntensity() == 11,
        "reset applies native defaults to every locked target and leaves scene selection untouched");
    Require(actions.Undo() && inspector.IsLocked() && inspector.GetTargetActors().size() == 2 &&
        actions.GetSelectedActor().GetGUID() == aID && b()->GetComponent<CPointLight>()->GetIntensity() == 11,
        "undo component reset restores both locked targets");
    inspector.SetLocked(false); actions.SetSelection({b(),c()});
    scene()->DestroyActor(*a()); draw(); draw();
    Require(inspector.CanPasteComponentValues(lightType) && inspector.PasteComponentValues(lightType),
        "owned component clipboard remains usable after the source actor is destroyed");
    draw(); draw(); actions.SelectActor(*b()); actions.ClearHistory();
    Require(inspector.ResetComponentValues(transformType) && Vector(b()->transform.GetLocalPosition()) == Vector::Zero &&
        Vector(b()->transform.GetLocalScale()) == Vector::One && b()->GetParent()->GetGUID() == parentID,
        "Transform reset changes local values while retaining parenting");
    Require(actions.Undo() && Vector(b()->transform.GetLocalPosition()) == Vector(6,1,2),"undo Transform reset");
    inspector.ResetComponentValues(transformType); draw(); draw(); actions.ClearHistory();
    Require(inspector.ResetComponentValues(transformType) && actions.UndoCount() == 0,
        "resetting an already-default component creates no history entry"); draw(); draw();

    auto& materialSource = scene()->CreateActor("Material source");
    auto& materialTarget = scene()->CreateActor("Material target");
    const auto materialSourceID = materialSource.GetGUID();
    const auto materialTargetID = materialTarget.GetGUID();
    auto target = [&] { return scene()->FindActorByGUID(materialTargetID)->GetComponent<CMaterialRenderer>(); };
    auto& source = materialSource.AddComponent<CMaterialRenderer>(); materialTarget.AddComponent<CMaterialRenderer>();
    auto* material = context.materialManager[":Materials\\Default.ovmat"];
    Require(material != nullptr,"managed material resource available for component clipboard");
    source.SetMaterialAtIndex(254,*material); source.SetVisibilityFlags(OvCore::Rendering::EVisibilityFlags::SHADOW);
    actions.SelectActor(materialSource); const auto materialType = source.GetTypeName();
    Require(inspector.CopyComponentValues(materialType),"copy Material Renderer without a Model Renderer");
    actions.SelectActor(materialTarget); actions.ClearHistory();
    Require(inspector.PasteComponentValues(materialType) && target()->GetMaterialAtIndex(254) == material &&
        target()->GetMaterialAtIndex(0) == nullptr && target()->GetVisibilityFlags() == OvCore::Rendering::EVisibilityFlags::SHADOW,
        "material paste retains high authored slots, empty gaps and visibility flags without a model"); draw(); draw();
    Require(inspector.ResetComponentValues(materialType) && target()->GetMaterialAtIndex(254) == nullptr &&
        target()->GetVisibilityFlags() == OvCore::Rendering::EVisibilityFlags::ALL,"material reset clears slots and restores visibility defaults");
    Require(actions.Undo() && target()->GetMaterialAtIndex(254) == material && target()->GetMaterialAtIndex(0) == nullptr,
        "undo material reset restores resource references and empty slots");

    actions.SelectActor(*b()); inspector.SetLocked(true); actions.SelectActor(*c());
    scene()->DestroyActor(*c()); draw(); draw();
    Require(inspector.IsLocked() && inspector.GetTargetActor()->GetGUID() == bID && !actions.IsAnyActorSelected(),
        "deleting scene selection leaves the independently locked actor intact");
    actions.SelectActor(*scene()->FindActorByGUID(materialSourceID)); scene()->DestroyActor(*b()); draw(); draw();
    Require(!inspector.IsLocked() && inspector.GetTargetActor()->GetGUID() == materialSourceID,
        "deleting the locked actor unlocks and follows surviving scene selection");
    inspector.SetLocked(true); actions.UnselectActor(); context.sceneManager.LoadEmptyScene(); draw(); draw();
    Require(!inspector.IsLocked() && !inspector.GetTargetActor() && inspector.GetSelectedActors().empty(),
        "scene unload clears pinned targets even when scene selection is already empty");

    const auto pinID = scene()->CreateActor("Pinned light").GetGUID();
    const auto selectionID = scene()->CreateActor("Selected camera").GetGUID();
    scene()->FindActorByGUID(pinID)->AddComponent<CPointLight>().SetIntensity(9);
    scene()->FindActorByGUID(selectionID)->AddComponent<CCamera>();
    actions.SelectActor(*scene()->FindActorByGUID(pinID)); inspector.SetLocked(true);
    actions.SelectActor(*scene()->FindActorByGUID(selectionID)); actions.StartPlaying();
    Require(inspector.IsLocked() && actions.GetSelectedActor().GetGUID() == selectionID,"Play retains independent Inspector and scene targets");
    actions.StopPlaying(); draw(); draw();
    Require(inspector.IsLocked() && inspector.GetTargetActor()->GetGUID() == pinID && actions.GetSelectedActor().GetGUID() == selectionID,
        "Stop restores the authored locked target and pre-Play scene selection");
    inspector.SetLocked(false); actions.SelectActor(*scene()->FindActorByGUID(pinID));
    inspector.SetSearchFilter("point light"); draw(); draw();
    auto& probe = header("Point Light").AddPlugin<InspectorHeaderProbe>(); draw();
    ImVec2 resetMin{}, resetMax{};
    auto menu = header("Point Light").contextMenu;
    header("Point Light").contextMenu = [&,menu] {
        menu(); resetMin = ImGui::GetItemRectMin(); resetMax = ImGui::GetItemRectMax();
    };
    auto& io = ImGui::GetIO(); const auto flags = io.BackendFlags;
    io.BackendFlags &= ~ImGuiBackendFlags_HasMouseHoveredViewport;
    ImGui_ImplGlfw_CursorEnterCallback(context.window->GetGlfwWindow(),true); io.AddFocusEvent(true);
    auto click = [&](ImVec2 position, int button = 0) {
        io.AddMousePosEvent(position.x,position.y); draw(); draw();
        io.AddMouseButtonEvent(button,true); draw(); io.AddMouseButtonEvent(button,false); draw();
        // Editor commands and their Inspector rebuilds each use a deferred turn.
        for (int i=0;i<4;++i) draw();
    };
    click({probe.min.x+70,(probe.min.y+probe.max.y)*.5f},1);
    Require(!GImGui->OpenPopupStack.empty(),"right-clicking a component header opens its context menu");
    draw(); draw(); // Let popup auto-sizing and screen-edge placement settle.
    auto* popup = GImGui->OpenPopupStack.back().Window;
    click({popup->DC.CursorStartPos.x+70,popup->DC.CursorStartPos.y+ImGui::GetFontSize()*.5f});
    Require(inspector.CanPasteComponentValues(lightType),"native context-menu copy captures component values");
    const auto headerHeight = probe.max.y-probe.min.y;
    click({probe.max.x-headerHeight*1.5f,(probe.min.y+probe.max.y)*.5f});
    Require(!GImGui->OpenPopupStack.empty(),"component ellipsis button opens the same action menu");
    draw(); draw();
    popup = GImGui->OpenPopupStack.back().Window;
    actions.ClearHistory();
    click({resetMin.x+70,(resetMin.y+resetMax.y)*.5f});
    Require(scene()->FindActorByGUID(pinID)->GetComponent<CPointLight>()->GetIntensity() == 1 && actions.UndoCount() == 1,
        "native component reset menu safely defers mutation and commits one undo step");
    actions.Undo(); draw(); draw();
    auto& staleProbe = header("Point Light").AddPlugin<InspectorHeaderProbe>();
    menu = header("Point Light").contextMenu;
    header("Point Light").contextMenu = [&,menu] {
        menu(); resetMin = ImGui::GetItemRectMin(); resetMax = ImGui::GetItemRectMax();
    };
    actions.ClearHistory(); draw();
    click({staleProbe.min.x+70,(staleProbe.min.y+staleProbe.max.y)*.5f},1);
    io.AddMousePosEvent(resetMin.x+70,(resetMin.y+resetMax.y)*.5f); draw(); draw();
    io.AddMouseButtonEvent(0,true); draw(); io.AddMouseButtonEvent(0,false); draw();
    actions.SelectActor(*scene()->FindActorByGUID(selectionID));
    for (int i=0;i<5;++i) draw();
    Require(scene()->FindActorByGUID(pinID)->GetComponent<CPointLight>()->GetIntensity() == 9 && actions.UndoCount() == 0 &&
        actions.GetSelectedActor().GetGUID() == selectionID,"changing selection invalidates a queued component-menu action safely");
    actions.SelectActor(*scene()->FindActorByGUID(pinID)); draw(); draw();
    window = ImGui::FindWindowByName((inspector.name+inspector.GetPanelID()).c_str());
    const ImVec2 toolbar = window->DC.CursorStartPos;
    click({toolbar.x+32*OVUI_SCALE,toolbar.y+ImGui::GetFrameHeight()*.5f});
    Require(inspector.IsLocked(),"native toolbar button locks the Inspector");
    inspector.SetSearchFilter(""); draw(); draw();
    click({toolbar.x+160*OVUI_SCALE,toolbar.y+ImGui::GetFrameHeight()*.5f});
    io.AddInputCharactersUTF8("intensity"); draw(); draw();
    Require(inspector.GetSearchFilter() == "intensity" && header("Point Light").visible && !header("Transform").visible,
        "native search field filters properties through real text input");
    click({window->WorkRect.Max.x-12*OVUI_SCALE,toolbar.y+ImGui::GetFrameHeight()*.5f});
    Require(inspector.GetSearchFilter().empty(),"native clear button restores the Inspector");
    actions.SelectActor(*scene()->FindActorByGUID(selectionID)); inspector.SetSearchFilter("light");
    io.AddMousePosEvent(-10000,-10000); draw(); draw();
    auto capture = [&] {
        const auto [width,height] = context.window->GetFramebufferSize(); std::vector<uint8_t> pixels(size_t(width)*height*4);
        glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        std::ofstream output(context.renderThread ? "inspector-workflow-thread.ppm" : "inspector-workflow.ppm",std::ios::binary);
        output << "P6\n" << width << " " << height << "\n255\n";
        for (int y=height-1;y>=0;--y) for (int x=0;x<width;++x)
            output.write(reinterpret_cast<const char*>(pixels.data()+(size_t(y)*width+x)*4),3);
        Require(output.good(),"locked and filtered Inspector native UI captured");
    };
    if (context.renderThread) context.renderThread->Invoke([&](auto&) { capture(); }); else capture();
    io.BackendFlags = flags; ImGui_ImplGlfw_CursorEnterCallback(context.window->GetGlfwWindow(),false);
    inspector.SetSearchFilter(""); inspector.SetLocked(false); context.sceneManager.LoadEmptyScene(); draw(); draw();
}
