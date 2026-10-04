#pragma once
#include <OvCore/ECS/Components/CCamera.h>
#include <OvUI/Plugins/DataDispatcher.h>
#include <OvUI/Widgets/Texts/TextColored.h>
#include <array>
#include <imgui_impl_glfw.h>
#include <GLFW/glfw3.h>

struct _GLFWwindow;
// Feed the same GLFW input entry used by its platform backend; no OS input or
// visible test window is needed, and InputManager still receives real callbacks.
extern "C" void _glfwInputKey(_GLFWwindow*, int, int, int, int);

class ItemRectProbe : public OvUI::Plugins::IPlugin
{
public:
    ImVec2 center{};
    bool disabled = false;
    void Execute(OvUI::Plugins::EPluginExecutionContext) override
    {
        const auto min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        center = {(min.x + max.x) / 2, (min.y + max.y) / 2};
        disabled = (GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
    }
};

template<class T> static OvUI::Plugins::DataDispatcher<T>* Property(
    OvUI::Internal::WidgetContainer& root, const std::string& title)
{
    std::string label;
    for (auto [widget, mode] : root.GetWidgets())
    {
        if (widget->IsDestroyed()) continue;
        if (auto* text = dynamic_cast<OvUI::Widgets::Texts::TextColored*>(widget)) label = text->content;
        if (label == title || label == title + " (Mixed)")
            if (auto* data = widget->GetPlugin<OvUI::Plugins::DataDispatcher<T>>()) return data;
        if (auto* container = dynamic_cast<OvUI::Internal::WidgetContainer*>(widget))
            if (auto* data = Property<T>(*container, title)) return data;
    }
    return nullptr;
}

template<class Draw> static void ValidateEditorInput(OvEditor::Core::Context& context, OvEditor::Core::Editor& editor,
    OvEditor::Core::EditorActions& actions, OvEditor::Panels::Inspector& inspector, Draw draw)
{
    using Light = OvCore::ECS::Components::CPointLight;
    auto* scene = context.sceneManager.GetCurrentScene();
    auto& a = scene->CreateActor("Input A"); auto& b = scene->CreateActor("Input B");
    const auto aID = a.GetGUID(), bID = b.GetGUID();
    a.AddComponent<Light>().SetIntensity(2); b.AddComponent<Light>().SetIntensity(8);
    actions.SetSelection({&a,&b}); actions.ClearHistory(); draw(); draw();
    auto* binding = Property<float>(inspector, "Intensity");
    ItemRectProbe* probe = nullptr;
    for (auto* widget : Widgets<OvUI::Widgets::AWidget>(inspector))
        if (binding && widget->GetPlugin<OvUI::Plugins::DataDispatcher<float>>() == binding)
            probe = &widget->AddPlugin<ItemRectProbe>();
    Require(probe != nullptr, "locate actual scalar drag control");
    draw(); draw();
    auto& io = ImGui::GetIO(); const auto backendFlags = io.BackendFlags;
    io.BackendFlags &= ~ImGuiBackendFlags_HasMouseHoveredViewport;
    ImGui_ImplGlfw_CursorEnterCallback(context.window->GetGlfwWindow(), true);
    io.AddFocusEvent(true);
    const auto pos = probe->center;
    io.AddMousePosEvent(pos.x, pos.y); draw(); draw();
    io.AddMouseButtonEvent(0,true); draw();
    for (int i = 1; i <= 4; ++i) { io.AddMousePosEvent(pos.x + 20 * i,pos.y); draw(); }
    Require(a.GetComponent<Light>()->GetIntensity() != 2 && a.GetComponent<Light>()->GetIntensity() == b.GetComponent<Light>()->GetIntensity(),
        "real ImGui drag edits both selected light intensities");
    Require(actions.UndoCount() == 0, "active drag keeps one transaction open across frames");
    io.AddMouseButtonEvent(0,false); draw(); draw();
    Require(actions.UndoCount() == 1, "releasing the pointer commits exactly one undo step");
    auto key = [&](int code, bool down, int mods) {
        _glfwInputKey(reinterpret_cast<_GLFWwindow*>(context.window->GetGlfwWindow()), code, 0, down ? GLFW_PRESS : GLFW_RELEASE, mods);
    };
    key(GLFW_KEY_LEFT_CONTROL,true,GLFW_MOD_CONTROL); key(GLFW_KEY_Z,true,GLFW_MOD_CONTROL);
    editor.HandleGlobalShortcuts();
    auto current = [&] { return context.sceneManager.GetCurrentScene(); };
    Require(current()->FindActorByGUID(aID)->GetComponent<Light>()->GetIntensity() == 2 &&
        current()->FindActorByGUID(bID)->GetComponent<Light>()->GetIntensity() == 8, "Ctrl+Z through GLFW and editor shortcuts restores both original values");
    key(GLFW_KEY_Z,false,GLFW_MOD_CONTROL); context.inputManager->ClearEvents();
    key(GLFW_KEY_Y,true,GLFW_MOD_CONTROL); editor.HandleGlobalShortcuts();
    Require(current()->FindActorByGUID(aID)->GetComponent<Light>()->GetIntensity() != 2, "Ctrl+Y redoes the pointer gesture");
    key(GLFW_KEY_Y,false,GLFW_MOD_CONTROL); context.inputManager->ClearEvents();
    key(GLFW_KEY_Z,true,GLFW_MOD_CONTROL); editor.HandleGlobalShortcuts();
    key(GLFW_KEY_Z,false,GLFW_MOD_CONTROL); context.inputManager->ClearEvents();
    key(GLFW_KEY_LEFT_SHIFT,true,GLFW_MOD_CONTROL | GLFW_MOD_SHIFT); key(GLFW_KEY_Z,true,GLFW_MOD_CONTROL | GLFW_MOD_SHIFT);
    editor.HandleGlobalShortcuts();
    Require(current()->FindActorByGUID(aID)->GetComponent<Light>()->GetIntensity() != 2, "Ctrl+Shift+Z also redoes");
    key(GLFW_KEY_Z,false,GLFW_MOD_CONTROL | GLFW_MOD_SHIFT); key(GLFW_KEY_LEFT_SHIFT,false,GLFW_MOD_CONTROL);
    key(GLFW_KEY_LEFT_CONTROL,false,0); context.inputManager->ClearEvents();
    io.BackendFlags = backendFlags;
    ImGui_ImplGlfw_CursorEnterCallback(context.window->GetGlfwWindow(),false);
    context.sceneManager.LoadEmptyScene(); draw(); draw();
}

template<class T> static void EditProperty(OvUI::Internal::WidgetContainer& root, const std::string& title, T value)
{
    auto* data = Property<T>(root, title);
    Require(data != nullptr, ("native Inspector binding: " + title).c_str());
    data->Gather(); data->NotifyChange(); data->Provide(value);
}

template<class Draw> static void ValidateHistory(OvEditor::Core::Context& context, OvEditor::Core::EditorActions& actions,
    OvEditor::Panels::Hierarchy& hierarchy, OvEditor::Panels::Inspector& inspector, Draw draw)
{
    using Actor = OvCore::ECS::Actor;
    using Light = OvCore::ECS::Components::CPointLight;
    using Vector = OvMaths::FVector3;
    auto scene = [&] { return context.sceneManager.GetCurrentScene(); };
    context.sceneManager.LoadEmptyScene();
    const auto aID = actions.CreateEmptyActor(true, nullptr, "A").GetGUID();
    actions.CommitSceneEdit(true);
    Require(actions.Undo() && scene()->GetActors().empty(), "undo creation removes the entity");
    Require(actions.Redo() && scene()->FindActorByGUID(aID) && actions.GetSelectedActor().GetGUID() == aID,
        "redo creation restores GUID and selection");
    auto a = [&] { return scene()->FindActorByGUID(aID); };
    auto& second = scene()->CreateActor("B");
    const auto bID = second.GetGUID();
    auto b = [&] { return scene()->FindActorByGUID(bID); };
    a()->transform.SetLocalPosition({1,2,3}); b()->transform.SetLocalPosition({4,5,6});
    a()->AddComponent<Light>().SetIntensity(2); b()->AddComponent<Light>().SetIntensity(8);
    a()->AddComponent<OvCore::ECS::Components::CCamera>();
    actions.SetSelection({a(), b()}); actions.ClearHistory();
    draw(); draw();
    Require(actions.GetSelectedActors().size() == 2, "multi-selection retained across drawing");
    auto headers = Widgets<OvUI::Widgets::Layout::GroupCollapsable>(inspector);
    Require(headers.size() == 2, "multi Inspector displays only common Transform and Point Light components");
    auto* intensity = Property<float>(inspector, "Intensity");
    Require(intensity && intensity->Gather() == 2, "mixed scalar uses primary representative");
    bool mixed = false;
    for (auto* label : Widgets<OvUI::Widgets::Texts::TextColored>(inspector)) mixed |= label->content == "Intensity (Mixed)";
    Require(mixed, "mixed values are explicitly labelled");
    ItemRectProbe* disabledProbe = nullptr;
    for (auto* button : Widgets<OvUI::Widgets::Buttons::Button>(inspector))
        if (button->label == "Constant") disabledProbe = &button->AddPlugin<ItemRectProbe>();
    draw();
    Require(disabledProbe && disabledProbe->disabled, "unsupported batch controls keep plugins inside the disabled scope");
    auto* color = Property<OvUI::Types::Color>(inspector, "Color");
    Require(color != nullptr, "native light color binding exists"); color->Gather();
    bool mixedColor = false;
    for (auto* label : Widgets<OvUI::Widgets::Texts::TextColored>(inspector)) mixedColor |= label->content == "Color (Mixed)";
    Require(!mixedColor, "different intensities do not produce a false mixed RGB color");
    EditProperty<OvUI::Types::Color>(inspector, "Color", {1,0,0,1}); actions.CommitSceneEdit(true);
    Require(a()->GetComponent<Light>()->GetIntensity() == 2 && b()->GetComponent<Light>()->GetIntensity() == 8 &&
        Vector(a()->GetComponent<Light>()->GetColor()) == Vector(1,0,0) && Vector(b()->GetComponent<Light>()->GetColor()) == Vector(1,0,0),
        "batch RGB edit preserves each light's independent intensity");
    Require(actions.Undo(), "undo batch RGB edit");
    EditProperty<float>(inspector, "Intensity", 12);
    EditProperty<float>(inspector, "Intensity", 18);
    actions.CommitSceneEdit(true);
    Require(a()->GetComponent<Light>()->GetIntensity() == 18 && b()->GetComponent<Light>()->GetIntensity() == 18 && actions.UndoCount() == 1,
        "multiple edits in one gesture update both entities and create one history entry");
    auto staleBinding = *Property<float>(inspector, "Intensity");
    Require(actions.Undo(), "undo batch scalar edit");
    Require(a()->GetComponent<Light>()->GetIntensity() == 2 && b()->GetComponent<Light>()->GetIntensity() == 8 && actions.GetSelectedActors().size() == 2,
        "undo restores distinct original values and multi-selection");
    staleBinding.NotifyChange(); staleBinding.Provide(99);
    Require(a()->GetComponent<Light>()->GetIntensity() == 2, "stale property callback after scene restoration is harmless");
    actions.BeginSceneEdit(); actions.CommitSceneEdit(true);
    Require(actions.CanRedo() && actions.Redo(), "no-op gestures preserve redo history");
    EditProperty<std::array<float,3>>(inspector, "Position", {9,2,3});
    actions.CommitSceneEdit(true);
    Require(Vector(a()->transform.GetLocalPosition()) == Vector(9,2,3) && Vector(b()->transform.GetLocalPosition()) == Vector(9,5,6),
        "batch vector edit preserves every target's untouched axes");
    Require(actions.Undo(), "undo batch vector edit");
    EditProperty<std::string>(inspector, "Tag", "Team"); actions.CommitSceneEdit(true);
    Require(a()->GetTag() == "Team" && b()->GetTag() == "Team" && !actions.CanRedo(), "new batch edit clears redo branch");
    // Test the actual common-component close callback, including its queued refresh.
    for (auto* header : Widgets<OvUI::Widgets::Layout::GroupCollapsable>(inspector))
        if (header->name == "Point Light") { header->CloseEvent.Invoke(); break; }
    draw(); actions.CommitSceneEdit(true);
    Require(!a()->GetComponent<Light>() && !b()->GetComponent<Light>(), "closing common component removes it from all selected entities");
    Require(actions.Undo() && a()->GetComponent<Light>() && b()->GetComponent<Light>(), "undo component removal restores both components");

    const auto cID = scene()->CreateActor("C").GetGUID();
    auto c = [&] { return scene()->FindActorByGUID(cID); };
    hierarchy.Synchronize(); hierarchy.SetSearchFilter("");
    hierarchy.SelectActorFromClick(*a(), false, false);
    hierarchy.SelectActorFromClick(*c(), false, true);
    Require(actions.GetSelectedActors().size() == 3, "Shift selects the visible row range");
    hierarchy.SelectActorFromClick(*b(), true, false);
    Require(actions.GetSelectedActors().size() == 2 && !actions.IsActorSelected(*b()), "Ctrl toggles a row without losing other selections");
    actions.SetSelection({a(),b()}); actions.ClearHistory();
    const auto aWorld = a()->transform.GetWorldPosition(), bWorld = b()->transform.GetWorldPosition();
    c()->transform.SetLocalPosition({10,20,30});
    actions.ReparentSelection(*a(),c()); actions.CommitSceneEdit(true);
    Require(a()->GetParent() == c() && b()->GetParent() == c() && Vector(a()->transform.GetWorldPosition()) == aWorld && Vector(b()->transform.GetWorldPosition()) == bWorld,
        "batch reparent preserves world transforms");
    Require(actions.Undo() && !a()->HasParent() && !b()->HasParent(), "undo batch reparent restores roots");
    actions.BeginTransformEdit();
    actions.TransformSelection([&] { a()->transform.SetWorldPosition(a()->transform.GetWorldPosition() + Vector(3,0,0)); });
    actions.EndTransformEdit(); actions.CommitSceneEdit(true);
    Require(Vector(a()->transform.GetWorldPosition()) == aWorld + Vector(3,0,0) && Vector(b()->transform.GetWorldPosition()) == bWorld + Vector(3,0,0),
        "group gizmo applies translation to all selected roots");
    Require(actions.Undo(), "undo group gizmo gesture");
    actions.BeginTransformEdit();
    actions.TransformSelection([&] { a()->transform.SetWorldScale({2,2,2}); });
    actions.EndTransformEdit(); actions.CommitSceneEdit(true);
    Require(Vector(b()->transform.GetWorldScale()) == Vector(2,2,2) &&
        Vector(b()->transform.GetWorldPosition()) == aWorld + (bWorld - aWorld) * 2.f,
        "group scaling also scales separation around the primary pivot");
    Require(actions.Undo(), "undo group scale");
    const auto turn = OvMaths::FQuaternion(Vector(0,90,0));
    actions.BeginTransformEdit();
    actions.TransformSelection([&] { a()->transform.SetWorldRotation(turn); });
    actions.EndTransformEdit(); actions.CommitSceneEdit(true);
    Require(Vector::Distance(b()->transform.GetWorldPosition(), aWorld + turn * (bWorld - aWorld)) < .001f,
        "group rotation orbits other selected roots around the primary pivot");
    Require(actions.Undo(), "undo group rotation");
    b()->SetParent(*a()); actions.SetSelection({b(),a()}); actions.ClearHistory();
    const auto parentBefore = a()->transform.GetWorldPosition(), childBefore = b()->transform.GetWorldPosition();
    actions.BeginTransformEdit();
    actions.TransformSelection([&] { b()->transform.SetWorldPosition(b()->transform.GetWorldPosition() + Vector(2,0,0)); });
    Require(Vector(a()->transform.GetWorldPosition()) == parentBefore + Vector(2,0,0) && Vector(b()->transform.GetWorldPosition()) == childBefore + Vector(2,0,0),
        "selected child and parent move exactly once with child as primary");
    actions.EndTransformEdit(true); actions.CommitSceneEdit(true);
    Require(Vector(a()->transform.GetWorldPosition()) == parentBefore && Vector(b()->transform.GetWorldPosition()) == childBefore && !actions.CanUndo(),
        "cancelled group gesture restores all transforms without history");
    actions.ReparentSelection(*a(),b()); actions.CommitSceneEdit(true);
    Require(!a()->HasParent() && b()->GetParent() == a() && !actions.CanUndo(), "reparent rejects hierarchy cycles atomically");
    actions.DuplicateSelectedActors(); actions.CommitSceneEdit(true);
    Require(scene()->GetActors().size() == 5 && actions.GetSelectedActors().size() == 1,
        "duplicating selected parent and child clones the subtree once");
    Require(actions.Undo() && scene()->GetActors().size() == 3 && actions.GetSelectedActors().size() == 2,
        "undo duplication restores original multi-selection");
    actions.DestroySelectedActors(); actions.CommitSceneEdit(true);
    Require(actions.GetSelectedActors().empty() && !a()->IsAlive() && !b()->IsAlive(), "batch delete marks the whole selected subtree");
    Require(actions.Undo() && a()->IsAlive() && b()->GetParent() == a(), "undo delete restores subtree and parenting");
    EditProperty<std::string>(inspector, "Tag", "Before play"); actions.CommitSceneEdit(true);
    const auto historySize = actions.UndoCount();
    actions.StartPlaying();
    actions.BeginSceneEdit(); a()->SetTag("Runtime"); actions.CommitSceneEdit(true);
    Require(!actions.CanUndo() && !actions.Undo() && actions.UndoCount() == historySize, "runtime edits are excluded from edit history");
    actions.StopPlaying();
    Require(a()->GetTag() == "Before play" && actions.GetSelectedActors().size() == 2 && actions.UndoCount() == historySize,
        "Stop restores edit selection and retains undo history");
    Require(actions.Undo() && a()->GetTag() == "Team", "pre-play edit remains undoable after Stop");
    actions.SetSelection({a()}); actions.BeginSceneEdit("Add script"); a()->AddBehaviour("spawn.lua"); actions.CommitSceneEdit(true);
    Require(actions.Undo() && !a()->GetBehaviour("spawn.lua"), "undo adding a Lua behaviour");
    Require(actions.Redo() && a()->GetBehaviour("spawn.lua"), "redo restores serialized Lua behaviour");
    context.sceneManager.StoreCurrentSceneSourcePath("history-test.ovscene");
    Require(actions.Undo() && context.sceneManager.GetCurrentSceneSourcePath() == "history-test.ovscene", "undo preserves the current scene save destination");
    context.sceneManager.LoadEmptyScene();
    Require(!actions.CanUndo() && !actions.CanRedo(), "switching scenes resets history");
    draw(); draw();
}
