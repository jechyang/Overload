#pragma once
#include <OvCore/ECS/Components/CPostProcessStack.h>
#include <OvCore/ECS/Components/CUI.h>
#include <OvCore/Reflection.generated.h>
#include <OvUI/Widgets/Selection/ColorEdit.h>

template<class Draw> static void ValidateRecordSerialization(OvEditor::Core::Context& context,
    OvEditor::Core::EditorActions& actions, OvEditor::Panels::Inspector& inspector, Draw draw)
{
    using namespace OvCore::ECS::Components;
    using namespace OvCore::Rendering::PostProcess;
    using namespace OvCore::Helpers::ReflectionProperties;
    using Group = OvUI::Widgets::Layout::GroupCollapsable;
    context.sceneManager.LoadEmptyScene(); inspector.SetLocked(false); inspector.SetSearchFilter("");
    auto scene = [&] { return context.sceneManager.GetCurrentScene(); };
    const auto id = scene()->CreateActor("Record settings").GetGUID();
    auto actor = [&] { return scene()->FindActorByGUID(id); };
    actor()->SetActive(false);
    actor()->AddComponent<CPostProcessStack>();
    auto post = [&] { return actor()->GetComponent<CPostProcessStack>(); };
    tinyxml2::XMLDocument doc;
    auto load = [&](AComponent& component, const std::string& xml) {
        doc.Clear(); if (doc.Parse(xml.c_str()) != tinyxml2::XML_SUCCESS) throw std::runtime_error("Invalid record fixture XML");
        component.OnDeserialize(doc, doc.RootElement());
    };
    auto save = [&](AComponent& component) {
        doc.Clear(); auto* data = doc.NewElement("data"); doc.InsertEndChild(data); component.OnSerialize(doc, data);
    };
    auto rejects = [&](AComponent& component, const std::string& xml) {
        try { load(component, xml); } catch (const std::runtime_error&) { return true; } return false;
    };
    load(*post(), "<data><auto_exposure_luminance_min>-6.5</auto_exposure_luminance_min><auto_exposure_luminance_max>12.5</auto_exposure_luminance_max><bloom_enabled>false</bloom_enabled><bloom_intensity>3</bloom_intensity><bloom_passes>9</bloom_passes><tonemapping_mode>5</tonemapping_mode><fxaa_enabled>false</fxaa_enabled></data>");
    Require(post()->GetAutoExposureSettings().minLuminanceEV == -6.5f && post()->GetAutoExposureSettings().maxLuminanceEV == 12.5f &&
        !post()->GetBloomSettings().enabled && post()->GetBloomSettings().passes == 9 && post()->GetTonemappingSettings().mode == ETonemappingMode::ACES && !post()->GetFXAASettings().enabled,
        "flattened inherited records read actual legacy postprocessing keys including previously lost exposure limits");
    load(*post(), "<data><auto_exposure_min_luminance>-8</auto_exposure_min_luminance><auto_exposure_max_luminance>14</auto_exposure_max_luminance></data>");
    Require(post()->GetAutoExposureSettings().minLuminanceEV == -8 && post()->GetAutoExposureSettings().maxLuminanceEV == 14 && post()->GetBloomSettings().intensity == 3,
        "prefixed aliases restore alternate exposure names while absent record groups keep authored values");
    load(*post(), "<data><auto_exposure_luminance_min>-4</auto_exposure_luminance_min><auto_exposure_min_luminance>-9</auto_exposure_min_luminance></data>");
    Require(post()->GetAutoExposureSettings().minLuminanceEV == -4, "canonical flattened key wins over its legacy alias");
    Require(rejects(*post(), "<data><auto_exposure_luminance_min>-20</auto_exposure_luminance_min><bloom_passes>overflow</bloom_passes><fxaa_enabled>true</fxaa_enabled></data>") &&
        post()->GetAutoExposureSettings().minLuminanceEV == -4 && post()->GetBloomSettings().passes == 9 && !post()->GetFXAASettings().enabled,
        "invalid child in one record rejects the complete component before any group setter");
    save(*post()); auto& clone = scene()->CreateActor("Record clone").AddComponent<CPostProcessStack>(); clone.OnDeserialize(doc, doc.RootElement());
    Require(clone.GetAutoExposureSettings().minLuminanceEV == -4 && clone.GetAutoExposureSettings().maxLuminanceEV == 14 && clone.GetBloomSettings().passes == 9,
        "postprocessing saves and reloads non-default aggregate fields");
    load(clone, "<data><tonemapping_mode>99</tonemapping_mode></data>"); save(clone);
    Require(std::string(doc.RootElement()->FirstChildElement("tonemapping_mode")->GetText()) == "99", "record enum preserves representable unknown legacy values");
    auto& reflection = OvCore::ECS::GetComponentRegistry().Reflection();
    auto* type = OvCore::ECS::GetComponentRegistry().Find(post()->GetTypeName());
    const auto* bloomProperty = type->type->FindProperty("BloomSettings");
    Require(bloomProperty != nullptr, "record property descriptor uses generated stable identity");
    const auto* bloomCodec = FindValueCodec(*bloomProperty);
    std::any record = post()->GetBloomSettings(); doc.Clear(); auto* nested = doc.NewElement("settings"); doc.InsertEndChild(nested);
    Require(bloomCodec && bloomCodec->write(reflection.Main(), *bloomProperty, doc, *nested, record, "") && nested->FirstChildElement("enabled") && nested->FirstChildElement("passes"),
        "generic record codec also supports nested XML without a legacy flatten prefix");
    nested->FirstChildElement("intensity")->SetText(4.5f);
    Require(bloomCodec->read(reflection.Main(), *bloomProperty, *nested, record, "") && std::any_cast<BloomSettings>(record).intensity == 4.5f && post()->GetBloomSettings().intensity == 3,
        "record parsing writes owned aggregate copies without publishing component state");
    auto invalid = post()->GetBloomSettings(); invalid.intensity = std::numeric_limits<float>::quiet_NaN(); post()->SetBloomSettings(invalid);
    bool saveRejected = false; try { save(*post()); } catch (const std::runtime_error&) { saveRejected = true; }
    Require(saveRejected && !doc.RootElement()->FirstChild(), "invalid nested value rejects saving without publishing partial component XML");
    invalid.intensity = 3; post()->SetBloomSettings(invalid);
    {
        OvUI::Internal::WidgetContainer disabled;
        Binding binding = Bind(*post(), *bloomProperty, reflection.Main(), post()->InspectorTicket()); binding.readOnly = true;
        Require(OvCore::Helpers::ReflectionProperties::Draw(disabled, binding, reflection).Ok(), "read-only record binding draws its nested fields");
        const auto fields = Widgets<OvUI::Widgets::Drags::DragSingleScalar<float>>(disabled);
        auto* dispatcher = fields.front()->GetPlugin<OvUI::Plugins::DataDispatcher<float>>(); dispatcher->NotifyChange(); dispatcher->Provide(8);
        Require(fields.front()->disabled && post()->GetBloomSettings().intensity == 3, "parent read-only state disables and guards nested property providers");
    }

    auto& rect = actor()->AddComponent<CRectTransform>(); rect.SetSizeDelta({120, 80}); rect.SetAnchoredPosition({7, 9});
    auto& driven = rect.GetDrivenProperties(); driven.Acquire(rect.GetLayout(), 64, 1, 100); rect.SetSizeDelta({300, 80});
    save(rect);
    Require(doc.RootElement()->FirstChildElement("size_delta")->FirstChildElement("x")->FloatText() == 120, "generated RectLayout saves authored axes rather than driven runtime values");
    load(rect, "<data><size_delta><x>140</x></size_delta><anchored_position><y>11</y></anchored_position></data>");
    Require(rect.GetSizeDelta() == OvMaths::FVector2(300, 80) && rect.GetAnchoredPosition() == OvMaths::FVector2(7, 11) && driven.Authored(rect.GetLayout()).sizeDelta.x == 140,
        "partial Vec2 load updates saved authored axes while preserving active driven values and missing coordinates");
    Require(rejects(rect, "<data><size_delta><x>170</x></size_delta><pivot><y>NaN</y></pivot></data>") && driven.Authored(rect.GetLayout()).sizeDelta.x == 140,
        "invalid layout coordinate leaves authored claims and runtime layout unchanged");
    driven.Release(const_cast<OvCore::UI::RectLayout&>(rect.GetLayout()), 100, 64, 1);
    Require(rect.GetSizeDelta().x == 140 && driven.Mask() == 0, "releasing a controller restores newly loaded authored layout");
    auto& canvas = actor()->AddComponent<CCanvas>(); canvas.SetSortingOrder(8);
    for (auto xml : {"<data><sorting_order>99</sorting_order><scale_factor>0</scale_factor></data>",
        "<data><sorting_order>99</sorting_order><plane_distance>-1</plane_distance></data>",
        "<data><sorting_order>99</sorting_order><render_mode>99</render_mode></data>"})
        Require(rejects(canvas, xml) && canvas.GetSortingOrder() == 8 && canvas.GetScaleFactor() == 1 && canvas.GetPlaneDistance() == 100,
            "business validation rejects invalid Canvas data before adjacent setters");
    load(canvas, "<data><world_camera>18446744073709551615</world_camera><render_mode>2</render_mode><scale_factor>2</scale_factor><sorting_order>-4</sorting_order></data>"); save(canvas);
    Require(canvas.GetWorldCameraGuid() == UINT64_MAX && canvas.GetRenderMode() == CCanvas::RenderMode::WorldSpace &&
        std::string(doc.RootElement()->FirstChildElement("world_camera")->GetText()) == "18446744073709551615", "generated Canvas fields preserve exact 64-bit camera identities and enum state");
    auto& image = actor()->AddComponent<CUIImage>(); image.SetColor({.2f, .3f, .4f, .5f});
    load(image, "<data><color><x>2</x><w>3</w></color><texture>__missing__/ui.png</texture><raycast_target>false</raycast_target></data>"); save(image);
    Require(image.GetColor() == OvMaths::FVector4(2, .3f, .4f, 1) && !image.IsRaycastTarget() && !image.GetTexture() &&
        std::string(doc.RootElement()->FirstChildElement("texture")->GetText()) == "__missing__/ui.png", "generated Vec4 preserves partial color data, alpha normalization and unresolved UI texture identity");
    Require(rejects(image, "<data><color><y>NaN</y></color><raycast_target>true</raycast_target></data>") && !image.IsRaycastTarget(), "invalid UI color aborts before neighboring fields");
    image.SetTexture(nullptr); save(image);
    Require(std::string(doc.RootElement()->FirstChildElement("texture")->GetText()) == "?", "explicit UI texture clear drops the missing asset identity");
    auto& sink = actor()->AddComponent<CCanvasRenderer>(); const std::array<UIVertex, 3> vertices{}; const std::array<uint32_t, 3> indices{0,1,2};
    sink.SetMesh(vertices, indices); save(sink);
    Require(!doc.RootElement()->FirstChild(), "managed Graphic output remains transient under generated scene serialization");
    load(sink, "<data/>"); Require(sink.GetVertices().empty() && sink.GetIndices().empty(), "loading CanvasRenderer still clears transient geometry");

    actions.SelectActor(*actor()); inspector.SetSize({540,850}); draw(); draw();
    auto group = [&](const char* name) -> Group& { for (auto* g : Widgets<Group>(inspector)) if (g->name == name) return *g; throw std::runtime_error(std::string("Missing record group: ") + name); };
    Require(Widgets<OvUI::Widgets::Drags::DragSingleScalar<int>>(group("Bloom")).size() == 1 && Property<int>(group("Bloom"), "Passes") &&
        Widgets<OvUI::Widgets::Selection::ColorEdit>(inspector).size() == 1, "metadata draws grouped integer drags and retains the UI color picker");
    auto* textureField = Property<std::string>(group("UI Image"), "Texture");
    Require(textureField != nullptr, "UI texture picker participates in property bindings");
    load(image, "<data><texture>__missing__/changed.png</texture></data>");
    Require(textureField->Gather() == "__missing__/changed.png", "live UI texture field observes external reference replacement");
    textureField->NotifyChange(); textureField->Provide(""); save(image);
    Require(std::string(doc.RootElement()->FirstChildElement("texture")->GetText()) == "?", "typed UI texture picker clears retained missing identity"); draw(); draw();
    auto* intensity = Property<float>(group("Bloom"), "Intensity");
    auto replacement = post()->GetBloomSettings(); replacement.intensity = 5; replacement.passes = 7; post()->SetBloomSettings(replacement);
    Require(intensity->Gather() == 5, "nested Inspector reads newly replaced settings without retaining stale member references");
    intensity->NotifyChange(); intensity->Provide(6);
    Require(post()->GetBloomSettings().intensity == 6 && post()->GetBloomSettings().passes == 7, "nested editing preserves other current aggregate fields"); draw(); draw();
    inspector.SetSearchFilter("bloom intensity"); draw();
    Require(group("Bloom").visible && group("Bloom").expandForSearch && !group("Auto Exposure").visible, "property search reveals and expands only matching nested settings groups");
    inspector.SetSearchFilter(""); draw(); Require(!group("Bloom").expandForSearch, "clearing nested search restores normal group expansion");
    auto* inspectorWindow = ImGui::FindWindowByName((inspector.name + inspector.GetPanelID()).c_str());
    const auto bloomID = ImHashStr(("Bloom#####reflection_record_" + std::string(bloomProperty->Info().id)).c_str(), 0, inspectorWindow->ID);
    inspectorWindow->StateStorage.SetInt(bloomID, 0); inspector.Refresh(); draw(); draw();
    int nestedDraws = 0; group("Bloom").CreateWidget<DrawProbe>(nestedDraws); draw();
    Require(nestedDraws == 0 && inspectorWindow->StateStorage.GetInt(bloomID, -1) == 0,
        "stable record group identity preserves collapsed state through Inspector rebuilds");
    inspectorWindow->StateStorage.SetInt(bloomID, 1); draw(); Require(nestedDraws == 1, "reopening a retained record group renders its controls");
    actions.ClearHistory();
    auto menu = [&] { for (auto* w : Widgets<OvUI::Widgets::AWidget>(group("Bloom"))) if (w->GetPlugin<OvUI::Plugins::DataDispatcher<float>>() == Property<float>(group("Bloom"), "Intensity"))
        return w->GetPlugin<OvEditor::Panels::InspectorBindings::PropertyMenu>()->actions; throw std::runtime_error("Missing nested property menu"); };
    Require(menu()->hasDefault && menu()->reset() && post()->GetBloomSettings().intensity == 1 && post()->GetBloomSettings().passes == 7 && actions.UndoCount() == 1,
        "nested property reset uses generated constructor defaults without resetting other settings");
    Require(actions.Undo() && post()->GetBloomSettings().intensity == 6 && post()->GetBloomSettings().passes == 7, "undo nested reset restores authored aggregate state"); draw(); draw();
    const auto otherID = scene()->CreateActor("Other settings").GetGUID();
    auto other = [&] { return scene()->FindActorByGUID(otherID)->GetComponent<CPostProcessStack>(); };
    scene()->FindActorByGUID(otherID)->AddComponent<CPostProcessStack>(); auto second = other()->GetBloomSettings(); second.intensity = 2; second.passes = 3; other()->SetBloomSettings(second);
    Require(menu()->copy(), "nested property copy owns the scalar value");
    actions.SetSelection({actor(), scene()->FindActorByGUID(otherID)}); draw(); draw(); actions.ClearHistory();
    Require(menu()->paste() && post()->GetBloomSettings().intensity == 6 && other()->GetBloomSettings().intensity == 6 && other()->GetBloomSettings().passes == 3 && actions.UndoCount() == 1,
        "nested property paste edits all targets while preserving each record's other fields");
    Require(actions.Undo() && other()->GetBloomSettings().intensity == 2, "nested multi-target undo restores distinct values"); draw(); draw();
    actions.SelectActor(*scene()->FindActorByGUID(otherID)); inspector.SetSearchFilter("post process"); draw(); draw();
    auto capture = [&] {
        const auto [width, height] = context.window->GetFramebufferSize(); std::vector<uint8_t> pixels(size_t(width) * height * 4);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        std::ofstream output(context.renderThread ? "records-thread.ppm" : "records.ppm", std::ios::binary);
        output << "P6\n" << width << " " << height << "\n255\n";
        for (int y = height - 1; y >= 0; --y) for (int x = 0; x < width; ++x) output.write(reinterpret_cast<const char*>(pixels.data() + (size_t(y) * width + x) * 4), 3);
        Require(output.good(), "actual metadata-driven record Inspector captured");
    };
    if (context.renderThread) context.renderThread->Invoke([&](auto&) { capture(); }); else capture();
    inspector.SetSearchFilter("");
    // Retain a production component binding independently of Editor view lifetime.
    OvUI::Internal::WidgetContainer controls; post()->OnInspector(controls);
    auto* first = Widgets<OvUI::Widgets::Drags::DragSingleScalar<float>>(controls).front();
    auto retained = *first->GetPlugin<OvUI::Plugins::DataDispatcher<float>>(); const auto previous = retained.Gather();
    Require(std::async(std::launch::async, [&] { retained.NotifyChange(); retained.Provide(99); return retained.Gather() == previous; }).get(), "nested bindings reject worker access before borrowing aggregate objects");
    context.sceneManager.LoadEmptyScene(); retained.NotifyChange(); retained.Provide(99);
    Require(retained.Gather() == previous, "nested callbacks expire safely after component and scene destruction");
}
