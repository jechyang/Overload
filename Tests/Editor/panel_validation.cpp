#include <OvCore/Resources/Material.h>
#include <OvRendering/Context/Driver.h>
#include <OvEditor/Core/Editor.h>
#include <OvEditor/Panels/Hierarchy.h>
#include <OvEditor/Panels/Inspector.h>
#include <OvCore/ECS/Components/CPointLight.h>
#include <OvRendering/Utils/ResourceTracking.h>
#include <OvUI/Widgets/Buttons/Button.h>
#include <OvUI/Widgets/Layout/Columns.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <fstream>
#include <cstdio>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#undef APIENTRY
#include <Windows.h>
#include <crtdbg.h>

static void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
    std::printf("PASS: %s\n", message);
}

class DrawProbe : public OvUI::Widgets::AWidget
{
public:
    explicit DrawProbe(int& count) : m_count(count) {}
private:
    void _Draw_Impl() override { ++m_count; }
    int& m_count;
};

template<class T> static std::vector<T*> Widgets(OvUI::Internal::WidgetContainer& root)
{
    std::vector<T*> found;
    for (auto [widget, mode] : root.GetWidgets())
    {
        if (widget->IsDestroyed()) continue;
        if (auto* typed = dynamic_cast<T*>(widget)) found.push_back(typed);
        if (auto* children = dynamic_cast<OvUI::Internal::WidgetContainer*>(widget))
        {
            auto nested = Widgets<T>(*children);
            found.insert(found.end(), nested.begin(), nested.end());
        }
    }
    return found;
}

#include "history_validation.h"
#include "viewport_validation.h"
#include "game_preview_validation.h"
#include "display_validation.h"
#include "inspector_workflow_validation.h"
#include "numeric_workflow_validation.h"
#include "property_workflow_validation.h"
#include "generated_serialization_validation.h"
#include "resource_serialization_validation.h"

int wmain(int argc, wchar_t** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try
    {
        Require(argc == 2, "isolated editor project supplied");
        OvRendering::Utils::ResourceTracking::TrackResources();
        OvEditor::Core::Context context(argv[1]);
        context.window->Hide();
        OvEditor::Core::Editor editor(context);
        auto& hierarchy = EDITOR_PANEL(OvEditor::Panels::Hierarchy, "Hierarchy");
        auto& inspector = EDITOR_PANEL(OvEditor::Panels::Inspector, "Inspector");
        auto& actions = OvCore::Global::ServiceLocator::Get<OvEditor::Core::EditorActions>();
        using Node = OvUI::Widgets::Layout::TreeNode;
        auto nodes = [&] { return Widgets<Node>(hierarchy); };
        auto find = [&](const std::string& name) -> Node*
        {
            for (auto* node : nodes()) if (node->name == name) return node;
            return nullptr;
        };
        hierarchy.Synchronize();
        Require(nodes().size() == context.sceneManager.GetCurrentScene()->GetActors().size(), "initial scene actors appear without creation events");
        context.sceneManager.LoadEmptyScene();
        auto& scene = *context.sceneManager.GetCurrentScene();
        auto& parent = scene.CreateActor("World");
        auto& child = scene.CreateActor("Player");
        child.SetParent(parent);
        hierarchy.Synchronize();
        auto* parentNode = find("World");
        auto* childNode = find("Player");
        Require(parentNode && childNode && childNode->GetParent() == parentNode && !parentNode->leaf, "dynamic creation restores complete parent and child hierarchy");
        parentNode->Close();
        hierarchy.SetSearchFilter("PLAYER");
        Require(childNode->enabled && parentNode->enabled && parentNode->IsOpened(), "case-insensitive search reveals matching descendants and ancestors");
        auto& spawned = scene.CreateActor("Player clone");
        hierarchy.Synchronize();
        Require(find("Player clone")->enabled, "runtime additions participate in an existing search");
        child.SetName("Camera rig");
        hierarchy.Synchronize();
        Require(!childNode->enabled && !parentNode->enabled, "rename updates filtered nodes even when they are not drawn");
        child.SetTag("player");
        hierarchy.Synchronize();
        Require(childNode->enabled && parentNode->enabled, "tag changes update search results live");
        hierarchy.SetSearchFilter("");
        Require(!parentNode->IsOpened(), "clearing search restores collapsed ancestors");
        child.DetachFromParent();
        hierarchy.Synchronize();
        Require(childNode->GetParent() == parentNode->GetParent() && parentNode->leaf, "detached entities return to the tree container and update leaf flags");
        parent.SetParent(child);
        hierarchy.Synchronize();
        Require(parentNode->GetParent() == childNode, "reversed parent child relation stays valid");
        parent.DetachFromParent();
        child.SetParent(parent);
        hierarchy.Synchronize();
        scene.DestroyActor(parent);
        hierarchy.Synchronize();
        Require(!find("World") && find("Camera rig") == childNode && !child.HasParent(), "deleting a parent preserves surviving child widgets");
        {
            OvCore::SceneSystem::Scene temporary(context.projectAssetsPath, context.engineAssetsPath);
            temporary.CreateActor("Foreign scene actor");
            hierarchy.Synchronize();
            Require(!find("Foreign scene actor") && nodes().size() == 2, "temporary scene actors never pollute the active scene tree");
        }
        hierarchy.SetSearchFilter("does not match");
        auto& created = actions.CreateEmptyActor(true, nullptr, "Selected new entity");
        hierarchy.Synchronize();
        Require(find(created.GetName())->selected && find(created.GetName())->enabled && inspector.GetTargetActor().has_value(), "creation reveals the selected entity through a stale filter");
        auto* contentBefore = inspector.GetWidgets().back().first;
        auto controlsBefore = Widgets<OvUI::Widgets::Layout::Columns<2>>(inspector);
        inspector.FocusActor(created);
        Require(contentBefore == inspector.GetWidgets().back().first && controlsBefore == Widgets<OvUI::Widgets::Layout::Columns<2>>(inspector), "reselecting an entity does not rebuild property controls");

        OvUI::Modules::Canvas canvas;
        canvas.AddPanel(hierarchy); canvas.AddPanel(inspector);
        auto& ui = *context.uiManager;
        ui.SetCanvas(canvas); ui.EnableEditorLayoutSave(false);
        hierarchy.dockable = inspector.dockable = false;
        hierarchy.autoSize = inspector.autoSize = false;
        hierarchy.SetPosition({0, 0}); hierarchy.SetSize({360, 650});
        inspector.SetPosition({370, 0}); inspector.SetSize({480, 650});
        auto draw = [&]
        {
            actions.ExecuteDelayedActions();
            auto clear = [] { glBindFramebuffer(GL_FRAMEBUFFER, 0); glDisable(GL_SCISSOR_TEST); glClearColor(.08f, .08f, .08f, 1); glClear(GL_COLOR_BUFFER_BIT); };
            if (context.renderThread)
            {
                auto commands = ui.RecordRenderCommands();
                context.renderThread->Invoke([&](auto& driver) { clear(); commands.Execute(driver); });
            }
            else { clear(); ui.Render(); }
        };
        draw(); draw();
        auto* inspectorWindow = ImGui::FindWindowByName((inspector.name + inspector.GetPanelID()).c_str());
        const ImGuiID transformID = ImHashStr("Transform#####inspector_component_Transform", 0, inspectorWindow->ID);
        inspectorWindow->StateStorage.SetInt(transformID, 0);
        auto& light = created.AddComponent<OvCore::ECS::Components::CPointLight>();
        draw(); draw();
        Require(Widgets<OvUI::Widgets::Layout::GroupCollapsable>(inspector).size() == 3, "adding a component refreshes the live Inspector");
        int transformDraws = 0;
        for (auto* header : Widgets<OvUI::Widgets::Layout::GroupCollapsable>(inspector))
            if (header->name == "Transform") header->CreateWidget<DrawProbe>(transformDraws);
        draw();
        Require(transformDraws == 0, "component refresh preserves the Transform collapsed state");
        inspectorWindow->StateStorage.SetInt(transformID, 1);
        draw();
        Require(transformDraws == 1, "expanding the same Transform section renders its controls");
        created.RemoveComponent(light);
        draw(); draw();
        Require(Widgets<OvUI::Widgets::Layout::GroupCollapsable>(inspector).size() == 2, "removing a component removes its stale controls");
        created.AddComponent<OvCore::ECS::Components::CPointLight>();
        scene.DestroyActor(created);
        draw(); draw();
        Require(!inspector.GetTargetActor() && nodes().size() == 2, "deleting the selection before its queued refresh clears Inspector safely");
        Require(inspector.GetWidgets().front().first->enabled, "empty Inspector explains how to select an entity");

        spawned.AddBehaviour("spawn.lua");
        const auto editCount = scene.GetActors().size();
        actions.StartPlaying();
        editor.UpdateCurrentEditorMode(0.016f);
        auto* runtimeEntity = context.sceneManager.GetCurrentScene()->FindActorByName("Runtime Entity");
        Require(runtimeEntity != nullptr, "real Lua OnUpdate dynamically creates an entity");
        actions.SelectActor(*runtimeEntity);
        draw(); draw();
        Require(find("Runtime Entity") && find("Runtime Entity")->selected && nodes().size() == editCount + 1, "Play mode creation is visible and selectable on the next UI frame");
        actions.StopPlaying();
        draw(); draw();
        Require(!find("Runtime Entity") && nodes().size() == editCount, "Stop restores the edit scene without stale runtime nodes");
        context.sceneManager.LoadEmptyScene();
        draw(); draw();
        Require(nodes().empty(), "scene replacement clears all root and detached nodes");
        auto& stressScene = *context.sceneManager.GetCurrentScene();
        std::vector<OvCore::ECS::Actor*> batch;
        for (int i = 0; i < 64; ++i) batch.push_back(&stressScene.CreateActor("Batch " + std::to_string(i)));
        for (size_t i = 1; i < batch.size(); ++i) batch[i]->SetParent(*batch[0]);
        draw(); draw();
        Require(nodes().size() == batch.size(), "a batch of runtime entities produces exactly one node per entity");
        auto* batchRoot = find("Batch 0");
        for (int i = 0; i < 10; ++i) draw();
        Require(find("Batch 0") == batchRoot && batchRoot->GetWidgets().front().first == find("Batch 1"), "unchanged frames retain widgets and scene insertion order");
        for (auto* actor : batch) stressScene.DestroyActor(*actor);
        draw(); draw();
        Require(nodes().empty(), "batch destruction leaves no orphaned tree nodes");
        ValidateHistory(context, actions, hierarchy, inspector, draw);
        ValidateEditorInput(context, editor, actions, inspector, draw);
        ValidateViewport(context, actions);
        ValidateGamePreview(context, editor, actions);
        ValidateSceneDisplay(context, editor, actions);
        ui.SetCanvas(canvas);
        ValidateInspectorWorkflow(context, actions, hierarchy, inspector, draw);
        ValidateNumericWorkflow(context, actions, inspector, draw);
        ValidatePropertyWorkflow(context, actions, inspector, draw);
        ValidateGeneratedSerialization(context);
        ValidateResourceSerialization(context);
        auto& demo = actions.CreateEmptyActor(true, nullptr, "Player");
        demo.SetTag("Gameplay"); demo.AddComponent<OvCore::ECS::Components::CPointLight>();
        auto& cameraRig = context.sceneManager.GetCurrentScene()->CreateActor("Camera Rig");
        cameraRig.SetParent(demo);
        context.sceneManager.GetCurrentScene()->CreateActor("Environment");
        auto& fillLight = context.sceneManager.GetCurrentScene()->CreateActor("Fill Light");
        fillLight.AddComponent<OvCore::ECS::Components::CPointLight>().SetIntensity(4);
        fillLight.transform.SetLocalPosition({3,2,0});
        actions.SetSelection({&demo, &fillLight});
        ImGui::GetIO().AddMousePosEvent(-10000,-10000);
        draw(); draw();
        auto capture = [&]
        {
            const auto [width, height] = context.window->GetFramebufferSize();
            std::vector<uint8_t> pixels(size_t(width) * height * 4);
            glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            std::ofstream output(context.renderThread ? "panels-thread.ppm" : "panels.ppm", std::ios::binary);
            output << "P6\n" << width << " " << height << "\n255\n";
            for (int y = height - 1; y >= 0; --y) for (int x = 0; x < width; ++x)
                output.write(reinterpret_cast<const char*>(pixels.data() + (size_t(y) * width + x) * 4), 3);
            Require(output.good(), "real panel rendering captured");
        };
        if (context.renderThread) context.renderThread->Invoke([&](auto&) { capture(); }); else capture();
        ui.RemoveCanvas();
        editor.DrainRenderFrames();
        std::puts("PASS: editor panel validation complete");
        return 0;
    }
    catch (const std::exception& error) { std::fprintf(stderr, "FAIL: %s\n", error.what()); return 1; }
}
