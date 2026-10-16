#pragma once
#include <OvCore/Helpers/ReflectionDrawer.h>
#include <OvCore/ECS/Components/CCamera.h>
#include <OvCore/ECS/Components/CPointLight.h>
#include <OvCore/ECS/Components/CPhysicalBox.h>
#include <future>
#include <set>

static void ValidateGeneratedSerialization(OvEditor::Core::Context& context)
{
    using namespace OvCore::Helpers::ReflectionProperties;
    using namespace OvCore::ECS::Components;
    context.sceneManager.LoadEmptyScene();
    auto& scene = *context.sceneManager.GetCurrentScene();
    const auto& registry = OvCore::ECS::GetComponentRegistry();
    const std::vector<std::pair<std::string, std::set<std::string>>> expected{
        {"CTransform", {"position", "rotation", "scale"}},
        {"CCamera", {"fov", "size", "near", "far", "clear_color", "frustum_geometry_culling", "frustum_light_culling", "projection_mode"}},
        {"CPhysicalBox", {"is_trigger", "is_kinematic", "bounciness", "mass", "friction", "linear_factor", "angular_factor", "collision_mode", "size"}},
        {"CPhysicalSphere", {"is_trigger", "is_kinematic", "bounciness", "mass", "friction", "linear_factor", "angular_factor", "collision_mode", "radius"}},
        {"CPhysicalCapsule", {"is_trigger", "is_kinematic", "bounciness", "mass", "friction", "linear_factor", "angular_factor", "collision_mode", "radius", "height"}},
        {"CPointLight", {"color", "intensity", "constant", "linear", "quadratic"}},
        {"CSpotLight", {"color", "intensity", "constant", "linear", "quadratic", "cutoff", "outercutoff"}},
        {"CDirectionalLight", {"color", "intensity", "cast_shadows", "shadow_area_size", "shadow_follow_camera", "shadow_map_resolution"}},
        {"CAmbientBoxLight", {"color", "intensity", "size"}},
        {"CAmbientSphereLight", {"color", "intensity", "radius"}},
        {"CModelRenderer", {"model", "frustum_behaviour", "custom_bounding_sphere_position", "custom_bounding_sphere_radius"}},
        {"CMaterialRenderer", {"materials", "visibility_flags"}},
        {"CAudioSource", {"autoplay", "spatial", "volume", "pan", "looped", "pitch", "attenuation_threshold", "audio_clip"}},
        {"CSkinnedMeshRenderer", {"playing", "looping", "playback_speed", "mesh_bounds_scale", "pose_eval_rate", "time_ticks", "animation_source", "animation"}},
        {"CPostProcessStack", {"bloom_enabled", "bloom_intensity", "bloom_passes", "auto_exposure_enabled", "auto_exposure_center_weight_bias", "auto_exposure_luminance_min", "auto_exposure_luminance_max", "auto_exposure_exposure_compensation", "auto_exposure_progressive", "auto_exposure_speed_up", "auto_exposure_speed_down", "tonemapping_enabled", "tonemapping_exposure", "tonemapping_mode", "tonemapping_gamma_correction", "fxaa_enabled"}},
        {"CRectTransform", {"anchor_min", "anchor_max", "pivot", "anchored_position", "size_delta"}},
        {"CCanvas", {"render_mode", "plane_distance", "world_camera", "scale_factor", "sorting_order", "override_sorting"}},
        {"CUIImage", {"color", "texture", "raycast_target", "image_type", "border", "fill_center", "fill_method", "fill_origin", "fill_amount", "fill_clockwise"}},
        {"CCanvasGroup", {"alpha", "interactable", "blocks_raycasts", "ignore_parent_groups", "enabled"}},
        {"CRectMask2D", {"enabled"}}
    };
    for (const auto& [name, keys] : expected)
    {
        auto* description = registry.Find("class OvCore::ECS::Components::" + name);
        Require(description != nullptr, (name + " generated component registration").c_str());
        auto& owner = scene.CreateActor(name); owner.SetActive(false);
        auto* component = description->DeserializeComponent(owner);
        Require(component && description->Borrow(*component).has_value(), (name + " generated typed component access").c_str());
        tinyxml2::XMLDocument doc; auto* node = doc.NewElement("data"); doc.InsertEndChild(node);
        component->OnSerialize(doc, node);
        std::set<std::string> actual; size_t count = 0;
        for (auto* child = node->FirstChildElement(); child; child = child->NextSiblingElement()) { actual.insert(child->Name()); ++count; }
        Require(actual == keys && count == keys.size(), (name + " inherited codec preserves legacy keys without duplicates").c_str());
        auto& restoredOwner = scene.CreateActor(name + " restored"); restoredOwner.SetActive(false);
        auto* restored = description->DeserializeComponent(restoredOwner); restored->OnDeserialize(doc, node);
        auto source = *description->Borrow(*component), target = *description->Borrow(*restored);
        bool equal = true;
        for (auto* property : Properties(*description->type, "Serialize"))
        {
            std::any first, second;
            equal &= property->ReadValue(registry.Reflection().Main(), source, first) == OvReflection::Error::None;
            equal &= property->ReadValue(registry.Reflection().Main(), target, second) == OvReflection::Error::None;
            equal &= Format(first) == Format(second);
        }
        Require(equal, (name + " generated property values roundtrip").c_str());
    }
    auto& light = *scene.FindActorByName("CPointLight")->GetComponent<CPointLight>();
    tinyxml2::XMLDocument partial; partial.Parse("<data><intensity>3.5</intensity></data>");
    light.SetColor({.2f, .4f, .6f}); light.OnDeserialize(partial, partial.RootElement());
    Require(light.GetIntensity() == 3.5f && light.GetColor().y == .4f, "annotated Keep policy preserves absent legacy light values");
    partial.Clear(); partial.Parse("<data><constant>9</constant><intensity>invalid</intensity></data>");
    const auto constant = light.GetConstant(); bool rejected = false;
    try { light.OnDeserialize(partial, partial.RootElement()); } catch (const std::runtime_error&) { rejected = true; }
    Require(rejected && light.GetConstant() == constant && light.GetIntensity() == 3.5f, "invalid inherited property aborts loading before any setter runs");
    auto& camera = *scene.FindActorByName("CCamera")->GetComponent<CCamera>();
    camera.SetProjectionMode(OvRendering::Settings::EProjectionMode::PERSPECTIVE);
    partial.Clear(); partial.Parse("<data/>"); camera.OnDeserialize(partial, partial.RootElement());
    Require(camera.GetFov() == 0 && camera.GetProjectionMode() == OvRendering::Settings::EProjectionMode::PERSPECTIVE,
        "camera missing-field defaults preserve old projection compatibility");
    auto* boxDescription = registry.Find("class OvCore::ECS::Components::CPhysicalBox");
    Require(!boxDescription->Borrow(light), "generated borrowing rejects another component type");
    Require(std::async(std::launch::async, [&] { return !boxDescription->Borrow(light); }).get(), "component borrowing rejects worker threads");
    Require(std::async(std::launch::async, [&]
    {
        tinyxml2::XMLDocument doc; auto* node = doc.NewElement("data"); doc.InsertEndChild(node);
        bool saveRejected = false, loadRejected = false;
        try { camera.OnSerialize(doc, node); } catch (const std::runtime_error&) { saveRejected = true; }
        try { camera.OnDeserialize(doc, node); } catch (const std::runtime_error&) { loadRejected = true; }
        return saveRejected && loadRejected && !node->FirstChild();
    }).get(), "generated serialization entry points reject worker threads before accessing objects or XML");
    // The new inherited Inspector entry point uses generated Inspect fields and safe tickets.
    auto* transform = &scene.FindActorByName("CTransform")->transform;
    OvUI::Internal::WidgetContainer automaticUI;
    transform->AComponent::OnInspector(automaticUI);
    Require(automaticUI.GetWidgets().size() == 6, "base Inspector automatically draws annotated fields");
    auto controls = Widgets<OvUI::Widgets::Drags::DragMultipleScalars<float, 3>>(automaticUI);
    Require(controls.size() == 3, "base Inspector uses the existing numeric controls");
    auto retained = *controls.front()->GetPlugin<OvUI::Plugins::DataDispatcher<std::array<float, 3>>>();
    const auto previous = retained.Gather();
    context.sceneManager.LoadEmptyScene();
    retained.NotifyChange(); retained.Provide({99, 99, 99});
    Require(retained.Gather() == previous, "generated base Inspector callbacks expire after scene destruction");
}
