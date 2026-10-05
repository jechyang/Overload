#pragma once
#include <OvEditor/Panels/InspectorBindings.h>
#include <OvCore/ECS/Components/CPhysicalBox.h>

template<class Draw> static void ValidatePropertyWorkflow(OvEditor::Core::Context& context,
    OvEditor::Core::EditorActions& actions, OvEditor::Panels::Inspector& inspector, Draw draw)
{
    using namespace OvEditor::Panels::InspectorBindings;
    using Vector = OvMaths::FVector3;
    using Light = OvCore::ECS::Components::CPointLight;
    using Body = OvCore::ECS::Components::CPhysicalBox;
    context.sceneManager.LoadEmptyScene(); inspector.SetLocked(false); inspector.SetSearchFilter("");
    auto scene = [&] { return context.sceneManager.GetCurrentScene(); };
    const auto aID = scene()->CreateActor("Property source").GetGUID();
    const auto bID = scene()->CreateActor("Property B").GetGUID();
    const auto cID = scene()->CreateActor("Property C").GetGUID();
    const auto parentID = scene()->CreateActor("Property parent").GetGUID();
    auto a = [&] { return scene()->FindActorByGUID(aID); };
    auto b = [&] { return scene()->FindActorByGUID(bID); };
    auto c = [&] { return scene()->FindActorByGUID(cID); };
    a()->AddComponent<Light>().SetIntensity(7); b()->AddComponent<Light>().SetIntensity(2); c()->AddComponent<Light>().SetIntensity(8);
    a()->transform.SetLocalPosition({7,8,9}); b()->transform.SetLocalPosition({2,3,4}); c()->transform.SetLocalPosition({-2,5,6});
    b()->transform.SetLocalScale({2,3,4}); c()->transform.SetLocalScale({3,5,7}); b()->SetParent(*scene()->FindActorByGUID(parentID));
    b()->GetComponent<Light>()->SetColor({.2f,.4f,.6f}); c()->GetComponent<Light>()->SetColor({.4f,.6f,.8f});
    auto widget = [&]<class T>(const char* name) -> OvUI::Widgets::AWidget& {
        auto* binding = Property<T>(inspector,name);
        for (auto* item : Widgets<OvUI::Widgets::AWidget>(inspector))
            if (binding && item->GetPlugin<OvUI::Plugins::DataDispatcher<T>>() == binding) return *item;
        throw std::runtime_error(std::string("Missing property menu control: ")+name);
    };
    auto state = [&]<class T>(const char* name) {
        auto* menu = widget.template operator()<T>(name).template GetPlugin<PropertyMenu>();
        if (!menu) throw std::runtime_error(std::string("Missing property actions: ")+name);
        return menu->actions;
    };
    auto vectorState = [&](const char* name) { return state.template operator()<std::array<float,3>>(name); };
    auto scalarState = [&](const char* name) { return state.template operator()<float>(name); };
    actions.SelectActor(*a()); draw(); draw(); actions.ClearHistory();
    Require(vectorState("Position")->hasDefault && vectorState("Position")->modified() && !vectorState("Rotation")->modified(),
        "property indicators compare against constructor defaults rather than the initial inspected values");
    Require(vectorState("Position")->copy() && actions.UndoCount() == 0,"copying a property creates no scene history");
    Require(!scalarState("Intensity")->canPaste(),"property clipboard rejects an incompatible vector-to-scalar paste");
    actions.SetSelection({b(),c()}); draw(); draw();
    Require(vectorState("Position")->paste() && actions.UndoCount() == 1 && Vector(b()->transform.GetLocalPosition()) == Vector(7,8,9) &&
        Vector(c()->transform.GetLocalPosition()) == Vector(7,8,9) && Vector(b()->transform.GetLocalScale()) == Vector(2,3,4) &&
        b()->GetParent()->GetGUID() == parentID,"property paste replaces the whole vector on all targets while preserving other properties and parenting");
    Require(actions.Undo() && Vector(b()->transform.GetLocalPosition()) == Vector(2,3,4) &&
        Vector(c()->transform.GetLocalPosition()) == Vector(-2,5,6),"undo property paste restores distinct original vectors"); draw(); draw();
    actions.ClearHistory();
    Require(vectorState("Position")->reset() && actions.UndoCount() == 1 && Vector(b()->transform.GetLocalPosition()) == Vector::Zero &&
        Vector(c()->transform.GetLocalPosition()) == Vector::Zero && Vector(c()->transform.GetLocalScale()) == Vector(3,5,7),
        "property reset changes only the chosen vector on all selected entities");
    Require(actions.Undo(),"undo property reset"); draw(); draw(); actions.ClearHistory();
    Require(vectorState("Scale")->reset() && Vector(b()->transform.GetLocalScale()) == Vector::One &&
        Vector(c()->transform.GetLocalScale()) == Vector::One && Vector(b()->transform.GetLocalPosition()) == Vector(2,3,4),
        "scale property reset uses native one defaults and ignores proportional editing state");
    Require(actions.Undo(),"undo scale property reset"); draw(); draw();
    actions.SelectActor(*a()); draw(); draw(); scalarState("Intensity")->copy(); a()->GetComponent<Light>()->SetIntensity(9);
    actions.SetSelection({b(),c()}); inspector.SetLocked(true); actions.SelectActor(*scene()->FindActorByGUID(parentID));
    actions.ClearHistory(); draw(); draw();
    Require(scalarState("Intensity")->paste() && actions.UndoCount() == 1 && b()->GetComponent<Light>()->GetIntensity() == 7 &&
        c()->GetComponent<Light>()->GetIntensity() == 7 && a()->GetComponent<Light>()->GetIntensity() == 9 &&
        actions.GetSelectedActor().GetGUID() == parentID,"property paste uses an owned clipboard and independent locked Inspector targets");
    Require(actions.Undo() && b()->GetComponent<Light>()->GetIntensity() == 2 && c()->GetComponent<Light>()->GetIntensity() == 8,
        "undo a locked multi-target property paste restores each target"); draw(); draw();
    b()->GetComponent<Light>()->SetIntensity(1); c()->GetComponent<Light>()->SetIntensity(4); actions.ClearHistory();
    Require(scalarState("Intensity")->modified() && scalarState("Intensity")->canReset(),
        "default indicator includes secondary targets even when the primary is already at default");
    Require(scalarState("Intensity")->reset() && b()->GetComponent<Light>()->GetIntensity() == 1 && c()->GetComponent<Light>()->GetIntensity() == 1 &&
        Vector(b()->GetComponent<Light>()->GetColor()) == Vector(.2f,.4f,.6f) && Vector(c()->GetComponent<Light>()->GetColor()) == Vector(.4f,.6f,.8f),
        "scalar property reset preserves neighboring color properties"); draw(); draw();
    Require(!scalarState("Intensity")->modified() && !scalarState("Intensity")->canReset(),"restored defaults clear the indicator and disable redundant reset");
    actions.ClearHistory(); Require(scalarState("Intensity")->reset() && actions.UndoCount() == 0,"resetting an already-default property does not create history"); draw(); draw();
    scene()->DestroyActor(*a()); draw(); draw();
    Require(scalarState("Intensity")->paste() && b()->GetComponent<Light>()->GetIntensity() == 7,"property clipboard survives destruction of its source actor"); draw(); draw();
    auto stale = scalarState("Intensity"); actions.ClearHistory();
    actions.DelayAction([stale] { stale->reset(); }); inspector.SetLocked(false); actions.SelectActor(*c());
    for (int i=0;i<4;++i) draw();
    Require(c()->GetComponent<Light>()->GetIntensity() == 7 && actions.UndoCount() == 0,"selection changes safely invalidate deferred property commands");
    const auto bodyID = scene()->CreateActor("Bound body").GetGUID();
    auto body = [&] { return scene()->FindActorByGUID(bodyID)->GetComponent<Body>(); };
    scene()->FindActorByGUID(bodyID)->AddComponent<Body>().SetMass(4);
    const auto actorCount = scene()->GetActors().size(); actions.SelectActor(*scene()->FindActorByGUID(bodyID)); draw(); draw(); actions.ClearHistory();
    Require(scene()->GetActors().size() == actorCount,"sampling component defaults leaves scene entity membership unchanged");
    Require(scalarState("Bounciness")->paste() && body()->GetBounciness() == 1 && body()->GetMass() == 4,
        "numeric property paste respects the destination range and native setter"); draw(); draw();
    auto& lockedField = widget.template operator()<float>("Bounciness"); lockedField.disabled = true; actions.ClearHistory();
    Require(!scalarState("Bounciness")->canPaste() && !scalarState("Bounciness")->canReset() &&
        !scalarState("Bounciness")->reset() && body()->GetBounciness() == 1 && actions.UndoCount() == 0,
        "property commands respect disabled and read-only controls"); lockedField.disabled = false;
    state.template operator()<std::string>("Name")->copy(); actions.ClearHistory();
    Require(state.template operator()<std::string>("Tag")->paste() && scene()->FindActorByGUID(bodyID)->GetTag() == "Bound body",
        "string property clipboard works across compatible actor fields"); draw(); draw();
    Require(state.template operator()<std::string>("Tag")->reset() && scene()->FindActorByGUID(bodyID)->GetTag().empty() &&
        scene()->FindActorByGUID(bodyID)->GetName() == "Bound body","Tag reset preserves actor name"); draw(); draw();
    Require(!state.template operator()<std::string>("Name")->hasDefault,"properties without a defined default keep reset unavailable");
    // Custom layouts can insert/reorder fields. A coincident index and value
    // type must never allow one property's defaults to reset another property.
    {
        OvUI::Internal::WidgetContainer prototype, changed;
        float source = 3, target = 8; PropertyDefaults defaults; PropertyClipboard clipboard;
        OvCore::Helpers::GUIDrawer::DrawScalar<float>(prototype,"Original property",source);
        CapturePropertyDefaults(prototype,defaults);
        OvCore::Helpers::GUIDrawer::DrawScalar<float>(changed,"Inserted property",target);
        OvTools::Utils::CallbackScope lifetime;
        Connect({&changed},lifetime.Ticket(),{&clipboard,&defaults,[](auto command) { command(); },{}});
        auto* menu = changed.GetWidgets().back().first->GetPlugin<PropertyMenu>();
        Require(menu && !menu->actions->hasDefault && !menu->actions->reset() && target == 8,
            "default snapshots require matching property identities when custom layouts change");
        OvUI::Internal::WidgetContainer groupedPrototype, groupedChanged;
        auto& firstGroup = groupedPrototype.CreateWidget<OvUI::Widgets::Layout::GroupCollapsable>("First group");
        auto& secondGroup = groupedChanged.CreateWidget<OvUI::Widgets::Layout::GroupCollapsable>("Second group");
        OvCore::Helpers::GUIDrawer::DrawScalar<float>(firstGroup,"Amount",source);
        OvCore::Helpers::GUIDrawer::DrawScalar<float>(secondGroup,"Amount",target);
        PropertyDefaults groupedDefaults; CapturePropertyDefaults(groupedPrototype,groupedDefaults);
        Connect({&groupedChanged},lifetime.Ticket(),{&clipboard,&groupedDefaults,[](auto command) { command(); },{}});
        auto* groupedMenu = secondGroup.GetWidgets().back().first->GetPlugin<PropertyMenu>();
        Require(groupedMenu && !groupedMenu->actions->hasDefault && !groupedMenu->actions->reset() && target == 8,
            "default identity includes ancestor groups with repeated property titles");
    }

    inspector.SetSize({540,850}); draw(); draw();
    auto& io = ImGui::GetIO(); const auto flags = io.BackendFlags;
    io.BackendFlags &= ~ImGuiBackendFlags_HasMouseHoveredViewport;
    ImGui_ImplGlfw_CursorEnterCallback(context.window->GetGlfwWindow(),true); io.AddFocusEvent(true);
    auto click = [&](ImVec2 p, int button = 0) {
        io.AddMousePosEvent(p.x,p.y); draw(); draw(); io.AddMouseButtonEvent(button,true); draw();
        io.AddMouseButtonEvent(button,false); draw(); for (int i=0;i<4;++i) draw();
    };
    auto rect = [&](OvUI::Widgets::AWidget& value) {
        auto& probe = value.AddPlugin<InspectorHeaderProbe>(); draw(); draw(); return std::pair{probe.min,probe.max};
    };
    const auto fieldRect = rect(widget.template operator()<float>("Bounciness"));
    click({(fieldRect.first.x+fieldRect.second.x)*.5f,(fieldRect.first.y+fieldRect.second.y)*.5f},1);
    Require(!GImGui->OpenPopupStack.empty(),"native right-click on a property value opens its menu");
    auto capture = [&] {
        const auto [w,h] = context.window->GetFramebufferSize(); std::vector<uint8_t> pixels(size_t(w)*h*4);
        glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        std::ofstream out(context.renderThread ? "property-menu-thread.ppm" : "property-menu.ppm",std::ios::binary);
        out << "P6\n" << w << " " << h << "\n255\n";
        for (int y=h-1;y>=0;--y) for (int x=0;x<w;++x) out.write(reinterpret_cast<const char*>(pixels.data()+(size_t(y)*w+x)*4),3);
        Require(out.good(),"property context menu and default indicators captured from real editor rendering");
    };
    if (context.renderThread) context.renderThread->Invoke([&](auto&){capture();}); else capture();
    auto menuItem = [&](int row) {
        auto* popup = GImGui->OpenPopupStack.back().Window;
        const auto start = popup->DC.CursorStartPos; const auto& style = ImGui::GetStyle();
        return ImVec2{start.x+65,start.y+ImGui::GetTextLineHeightWithSpacing()+style.ItemSpacing.y+1+
            row*ImGui::GetTextLineHeightWithSpacing()+(row == 2 ? style.ItemSpacing.y+1 : 0)+ImGui::GetFontSize()*.5f};
    };
    click(menuItem(0));
    const auto massRect = rect(widget.template operator()<float>("Mass"));
    click({(massRect.first.x+massRect.second.x)*.5f,(massRect.first.y+massRect.second.y)*.5f},1); actions.ClearHistory();
    click(menuItem(1));
    Require(body()->GetMass() == 1 && actions.UndoCount() == 1,"native property menu copies and pastes between numeric fields with one undo entry");
    Require(actions.Undo() && body()->GetMass() == 4,"undo native property menu paste"); draw(); draw(); actions.ClearHistory();
    OvUI::Widgets::Texts::TextColored* title = nullptr;
    for (auto* value : Widgets<OvUI::Widgets::Texts::TextColored>(inspector)) if (value->content == "Bounciness") title = value;
    const auto titleRect = rect(*title); click({(titleRect.first.x+titleRect.second.x)*.5f,(titleRect.first.y+titleRect.second.y)*.5f},1);
    Require(!GImGui->OpenPopupStack.empty(),"native right-click on a property title opens the same actions");
    click(menuItem(2));
    Require(body()->GetBounciness() == 0 && body()->GetMass() == 4 && actions.UndoCount() == 1,"native property menu resets one property without changing neighboring values");
    io.BackendFlags = flags; ImGui_ImplGlfw_CursorEnterCallback(context.window->GetGlfwWindow(),false);
    context.sceneManager.LoadEmptyScene(); inspector.SetSize({480,650}); draw(); draw();
}
