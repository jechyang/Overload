#pragma once
#include <OvUI/Widgets/Drags/DragSingleScalar.h>

template<class Draw> static void ValidateNumericWorkflow(OvEditor::Core::Context& context,
    OvEditor::Core::EditorActions& actions, OvEditor::Panels::Inspector& inspector, Draw draw)
{
    using Vector = OvMaths::FVector3;
    using Field = OvUI::Widgets::Drags::DragMultipleScalars<float,3>;
    using Light = OvCore::ECS::Components::CPointLight;
    context.sceneManager.LoadEmptyScene(); inspector.SetSearchFilter(""); inspector.SetLocked(false);
    inspector.SetSize({560,760});
    auto scene = [&] { return context.sceneManager.GetCurrentScene(); };
    const auto aID = scene()->CreateActor("Numeric A").GetGUID(), bID = scene()->CreateActor("Numeric B").GetGUID();
    auto a = [&] { return scene()->FindActorByGUID(aID); }; auto b = [&] { return scene()->FindActorByGUID(bID); };
    a()->transform.SetLocalPosition({0,2,3}); b()->transform.SetLocalPosition({9,2,7});
    a()->AddComponent<Light>().SetIntensity(2); b()->AddComponent<Light>().SetIntensity(8);
    actions.SetSelection({a(),b()}); actions.ClearHistory(); draw(); draw();
    auto field = [&](const char* name) -> Field& {
        const auto* binding = Property<std::array<float,3>>(inspector,name);
        for (auto* widget : Widgets<Field>(inspector))
            if (widget->GetPlugin<OvUI::Plugins::DataDispatcher<std::array<float,3>>>() == binding) return *widget;
        throw std::runtime_error(std::string("Missing vector field: ")+name);
    };
    auto scalar = [&]() -> OvUI::Widgets::AWidget& {
        auto* binding = Property<float>(inspector,"Intensity");
        for (auto* widget : Widgets<OvUI::Widgets::AWidget>(inspector))
            if (widget->GetPlugin<OvUI::Plugins::DataDispatcher<float>>() == binding) return *widget;
        throw std::runtime_error("Missing Intensity control");
    };
    Require(field("Position").axisLabels && field("Position").mixedAxes == std::array<bool,3>{true,false,true},
        "Transform shows axis labels and marks only the axes with mixed values");
    auto& io = ImGui::GetIO(); const auto flags = io.BackendFlags;
    io.BackendFlags &= ~ImGuiBackendFlags_HasMouseHoveredViewport;
    ImGui_ImplGlfw_CursorEnterCallback(context.window->GetGlfwWindow(),true); io.AddFocusEvent(true);
    auto click = [&](ImVec2 p) {
        io.AddMousePosEvent(p.x,p.y); draw(); draw(); io.AddMouseButtonEvent(0,true); draw();
        io.AddMouseButtonEvent(0,false); draw(); draw(); draw();
    };
    auto key = [&](ImGuiKey k) { io.AddKeyEvent(k,true); draw(); io.AddKeyEvent(k,false); draw(); draw(); };
    auto rect = [&](OvUI::Widgets::AWidget& widget) {
        auto* probe = widget.GetPlugin<InspectorHeaderProbe>();
        if (!probe) probe = &widget.AddPlugin<InspectorHeaderProbe>();
        draw(); draw(); return std::pair{probe->min,probe->max};
    };
    auto axisPoint = [&](const char* name, size_t axis, bool number) {
        auto& value = field(name); const auto [min,max] = rect(value);
        const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
        const float linkedSpace = value.showProportionalLock ? 26*OVUI_SCALE+gap : 0;
        const float width = (max.x-min.x-linkedSpace-2*gap)/3;
        return ImVec2{min.x+axis*(width+gap)+(number ? 22*OVUI_SCALE+(width-22*OVUI_SCALE)*.5f : 10*OVUI_SCALE),
            min.y+ImGui::GetFrameHeight()*.5f};
    };
    click(axisPoint("Position",0,false));
    Require(Vector(a()->transform.GetLocalPosition()) == Vector(0,2,3) && Vector(b()->transform.GetLocalPosition()) == Vector(0,2,7),
        "axis reset applies to secondary mixed values even when the primary is already at default");
    Require(actions.UndoCount() == 1 && actions.Undo(),"one undo restores an axis reset across the selection");
    draw(); draw(); actions.ClearHistory();
    auto input = rect(scalar()); click({(input.first.x+input.second.x)*.5f,(input.first.y+input.second.y)*.5f});
    Require(GImGui->TempInputId && GImGui->ActiveId == GImGui->TempInputId,"single click opens precise numeric text editing");
    io.AddInputCharactersUTF8("23"); draw(); draw();
    Require(a()->GetComponent<Light>()->GetIntensity() == 2 && b()->GetComponent<Light>()->GetIntensity() == 8 && actions.UndoCount() == 0,
        "typed numeric draft leaves authored values and undo history untouched before confirmation");
    key(ImGuiKey_Escape);
    Require(a()->GetComponent<Light>()->GetIntensity() == 2 && b()->GetComponent<Light>()->GetIntensity() == 8 && actions.UndoCount() == 0,
        "Escape cancels numeric input while retaining distinct multi-selection values");
    input = rect(scalar()); click({(input.first.x+input.second.x)*.5f,(input.first.y+input.second.y)*.5f});
    io.AddInputCharactersUTF8("."); draw(); key(ImGuiKey_Enter);
    Require(a()->GetComponent<Light>()->GetIntensity() == 2 && b()->GetComponent<Light>()->GetIntensity() == 8 && actions.UndoCount() == 0,
        "invalid numeric text leaves mixed values and undo history untouched");
    input = rect(scalar()); click({(input.first.x+input.second.x)*.5f,(input.first.y+input.second.y)*.5f});
    io.AddInputCharactersUTF8("23"); draw(); key(ImGuiKey_Enter);
    Require(a()->GetComponent<Light>()->GetIntensity() == 23 && b()->GetComponent<Light>()->GetIntensity() == 23 && actions.UndoCount() == 1,
        "Enter confirms numeric input for all selected entities with one undo entry");
    Require(actions.Undo(),"undo confirmed numeric input"); draw(); draw(); actions.ClearHistory();
    // Axis labels use the same fine adjustment as native numeric-box dragging.
    const auto axisOrigin = axisPoint("Position",0,false);
    io.AddMousePosEvent(axisOrigin.x,axisOrigin.y); draw(); draw();
    io.AddKeyEvent(ImGuiMod_Alt,true); draw(); io.AddMouseButtonEvent(0,true); draw();
    io.AddMousePosEvent(axisOrigin.x+40,axisOrigin.y); draw();
    const float fineX = a()->transform.GetLocalPosition().x;
    Require(fineX > 0 && fineX < 1 && b()->transform.GetLocalPosition().x == fineX &&
        a()->transform.GetLocalPosition().y == 2 && b()->transform.GetLocalPosition().z == 7 && actions.UndoCount() == 0,
        "Alt dragging an axis label adjusts precisely while preserving unrelated mixed axes");
    io.AddMouseButtonEvent(0,false); draw(); io.AddKeyEvent(ImGuiMod_Alt,false); draw(); draw();
    Require(a()->transform.GetLocalPosition().x == fineX && actions.UndoCount() == 1 && actions.Undo(),
        "releasing an axis drag retains its value without resetting and creates one undo entry");
    draw(); draw(); actions.ClearHistory();
    click(axisPoint("Position",0,true)); io.AddInputCharactersUTF8("0"); draw(); key(ImGuiKey_Enter);
    Require(Vector(a()->transform.GetLocalPosition()) == Vector(0,2,3) && Vector(b()->transform.GetLocalPosition()) == Vector(0,2,7),
        "typing the primary axis value explicitly resolves only that mixed axis");
    actions.Undo(); draw(); draw(); actions.ClearHistory();
    click(axisPoint("Position",0,true)); io.AddInputCharactersUTF8("5"); draw(); key(ImGuiKey_Tab);
    Require(a()->transform.GetLocalPosition().x == 5 && b()->transform.GetLocalPosition().x == 5 &&
        GImGui->TempInputId && GImGui->ActiveId == GImGui->TempInputId,"Tab commits X and continues into another numeric input");
    io.AddInputCharactersUTF8("6"); draw(); key(ImGuiKey_Enter);
    Require(Vector(a()->transform.GetLocalPosition()) == Vector(5,6,3) && Vector(b()->transform.GetLocalPosition()) == Vector(5,6,7),
        "keyboard traversal continues from X to Y without stopping on axis reset buttons");
    actions.CommitSceneEdit(true);
    // Drag the actual property title, rather than the value box.
    OvUI::Widgets::Texts::TextColored* title = nullptr;
    for (auto* item : Widgets<OvUI::Widgets::Texts::TextColored>(inspector))
        if (item->content == "Intensity (Mixed)" || item->content == "Intensity") title = item;
    Require(title && title->GetPlugin<OvUI::Plugins::NumericLabelDrag>(),"scalar property titles expose a scrub interaction");
    const auto labelRect = rect(*title); const ImVec2 origin{(labelRect.first.x+labelRect.second.x)*.5f,(labelRect.first.y+labelRect.second.y)*.5f};
    actions.ClearHistory(); io.AddMousePosEvent(origin.x,origin.y); draw(); draw(); io.AddMouseButtonEvent(0,true); draw();
    io.AddMousePosEvent(origin.x+40,origin.y); draw();
    Require(a()->GetComponent<Light>()->GetIntensity() > 2 && a()->GetComponent<Light>()->GetIntensity() == b()->GetComponent<Light>()->GetIntensity(),
        "dragging a scalar property name scrubs both selected entities");
    Require(actions.UndoCount() == 0,"label scrub keeps one transaction open while held");
    io.AddMouseButtonEvent(0,false); draw(); draw();
    Require(actions.UndoCount() == 1 && actions.Undo(),"releasing a label scrub creates one reversible gesture"); draw(); draw();
    a()->transform.SetLocalScale({2,4,6}); b()->transform.SetLocalScale({3,9,12}); actions.ClearHistory();
    auto [scaleMin,scaleMax] = rect(field("Scale"));
    click({scaleMax.x-13*OVUI_SCALE,scaleMin.y+ImGui::GetFrameHeight()*.5f});
    Require(field("Scale").proportionalLink && *field("Scale").proportionalLink && actions.UndoCount() == 0,
        "scale proportion toggle changes only Inspector interaction state");
    click(axisPoint("Scale",0,true)); io.AddInputCharactersUTF8("8"); draw(); key(ImGuiKey_Enter);
    Require(Vector::Distance(a()->transform.GetLocalScale(),{8,16,24}) < .001f && Vector::Distance(b()->transform.GetLocalScale(),{8,24,32}) < .001f,
        "linked scale editing preserves each selected entity's independent proportions");
    Require(actions.UndoCount() == 1 && actions.Undo(),"undo linked scale edit restores all original proportions"); draw(); draw();
    Require(*field("Scale").proportionalLink,"proportion preference survives undo and Inspector reconstruction");
    actions.ClearHistory(); click(axisPoint("Scale",1,false));
    Require(Vector(a()->transform.GetLocalScale()) == Vector(2,1,6) && Vector(b()->transform.GetLocalScale()) == Vector(3,1,12),
        "single-axis scale reset restores one while preserving the other axes even with proportions locked");
    actions.Undo(); draw(); draw(); actions.SelectActor(*a()); draw(); draw();
    Require(*field("Scale").proportionalLink,"proportion preference survives selecting another entity");
    inspector.SetSize({300,760}); draw(); draw();
    const auto narrow = rect(field("Position"));
    auto* window = ImGui::FindWindowByName((inspector.name+inspector.GetPanelID()).c_str());
    Require(narrow.second.y-narrow.first.y > ImGui::GetFrameHeight()*2 && narrow.second.x <= window->InnerRect.Max.x,
        "narrow Inspector stacks vector axes vertically without overflowing the panel");
    inspector.SetSize({560,760}); actions.SetSelection({a(),b()}); draw(); draw();
    io.AddMousePosEvent(-10000,-10000); draw(); draw();
    auto capture = [&] {
        const auto [w,h] = context.window->GetFramebufferSize(); std::vector<uint8_t> pixels(size_t(w)*h*4);
        glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        std::ofstream out(context.renderThread ? "numeric-workflow-thread.ppm" : "numeric-workflow.ppm",std::ios::binary);
        out << "P6\n" << w << " " << h << "\n255\n";
        for (int y=h-1;y>=0;--y) for (int x=0;x<w;++x) out.write(reinterpret_cast<const char*>(pixels.data()+(size_t(y)*w+x)*4),3);
        Require(out.good(),"Transform axes and mixed numeric controls captured from real editor rendering");
    };
    if (context.renderThread) context.renderThread->Invoke([&](auto&){capture();}); else capture();
    io.BackendFlags = flags; ImGui_ImplGlfw_CursorEnterCallback(context.window->GetGlfwWindow(),false);
    context.sceneManager.LoadEmptyScene(); inspector.SetSize({480,650}); draw(); draw();
}
