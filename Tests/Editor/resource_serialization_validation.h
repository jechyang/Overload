#pragma once
#include <OvCore/ECS/Components/CAudioSource.h>
#include <OvCore/ECS/Components/CMaterialRenderer.h>
#include <OvCore/ECS/Components/CModelRenderer.h>
#include <OvCore/ECS/Components/CSkinnedMeshRenderer.h>
#include <OvCore/Helpers/ReflectionResourceCodecs.h>

static void ValidateResourceSerialization(OvEditor::Core::Context& context)
{
    using namespace OvCore::ECS::Components;
    context.sceneManager.LoadEmptyScene();
    auto& scene = *context.sceneManager.GetCurrentScene();
    auto& actor = scene.CreateActor("Resources"); actor.SetActive(false);
    auto& model = actor.AddComponent<CModelRenderer>();
    auto& materials = actor.AddComponent<CMaterialRenderer>();
    auto& audio = actor.AddComponent<CAudioSource>();
    auto& skin = actor.AddComponent<CSkinnedMeshRenderer>();
    auto* cube = context.modelManager.GetResource(":Models/Cube.fbx");
    auto* material = context.materialManager.GetResource(":Materials/Default.ovmat");
    auto* tone = context.soundManager.GetResource("ReflectionTone.wav");
    Require(cube && material, "resource codecs load actual engine assets through production managers");
    Require(tone, "resource codec fixture loads valid PCM audio");
    auto save = [](AComponent& component, tinyxml2::XMLDocument& doc)
    { doc.Clear(); auto* data = doc.NewElement("data"); doc.InsertEndChild(data); component.OnSerialize(doc, data); };
    auto load = [](AComponent& component, tinyxml2::XMLDocument& doc, const std::string& xml)
    { doc.Clear(); Require(doc.Parse(xml.c_str()) == tinyxml2::XML_SUCCESS, "resource test XML parsed"); component.OnDeserialize(doc, doc.RootElement()); };
    tinyxml2::XMLDocument doc;
    load(model, doc, "<data><model>:Models/Cube.fbx</model><frustum_behaviour>3</frustum_behaviour><custom_bounding_sphere_radius>2.75</custom_bounding_sphere_radius></data>");
    Require(model.GetModel() == cube && model.GetFrustumBehaviour() == CModelRenderer::EFrustumBehaviour::CUSTOM_BOUNDS && model.GetCustomBoundingSphere().radius == 2.75f,
        "typed resource getter/setter codec restores model and legacy bounds");
    load(model, doc, "<data><model>__missing__/robot.fbx</model></data>"); save(model, doc);
    Require(!model.GetModel() && std::string(doc.RootElement()->FirstChildElement("model")->GetText()) == "__missing__/robot.fbx", "missing model identity survives saving");
    model.SetModel(nullptr); save(model, doc);
    Require(std::string(doc.RootElement()->FirstChildElement("model")->GetText()) == "?", "explicit model clear removes retained missing identity");
    model.SetModel(cube);
    doc.Clear(); doc.Parse("<data><model>:Models/Pyramid.fbx</model><custom_bounding_sphere_radius>NaN</custom_bounding_sphere_radius></data>");
    const bool wasLoaded = context.modelManager.IsResourceRegistered(":Models/Pyramid.fbx");
    bool rejected = false; try { model.OnDeserialize(doc, doc.RootElement()); } catch (const std::runtime_error&) { rejected = true; }
    Require(rejected && model.GetModel() == cube && context.modelManager.IsResourceRegistered(":Models/Pyramid.fbx") == wasLoaded,
        "invalid adjacent scalar aborts before resource resolution or model replacement");
    load(materials, doc, "<data><materials><material>:Materials/Default.ovmat</material><material>?</material><material>__missing__/surface.ovmat</material></materials><visibility_flags>5</visibility_flags></data>");
    save(materials, doc); auto* list = doc.RootElement()->FirstChildElement("materials");
    auto* first = list->FirstChildElement("material"); auto* hole = first->NextSiblingElement(); auto* missing = hole->NextSiblingElement();
    Require(materials.GetMaterialAtIndex(0) == material && !materials.GetMaterialAtIndex(1) && !materials.GetMaterialAtIndex(2) &&
        std::string(hole->GetText()) == "?" && std::string(missing->GetText()) == "__missing__/surface.ovmat" && !missing->NextSiblingElement(),
        "bounded material codec preserves gaps, unresolved identity and trims only trailing empty slots");
    model.SetModel(nullptr); save(materials, doc);
    Require(doc.RootElement()->FirstChildElement("materials")->FirstChildElement()->NextSiblingElement()->NextSiblingElement(),
        "authored material slots survive when no model is assigned");
    load(materials, doc, "<data/>"); save(materials, doc);
    Require(doc.RootElement()->FirstChildElement("materials")->FirstChildElement() && static_cast<uint32_t>(materials.GetVisibilityFlags()) == 5,
        "missing material collection and flags keep previous values");
    std::string overflow = "<data><materials>";
    for (size_t i = 0; i < 256; ++i) overflow += "<material>?</material>";
    overflow += "</materials><visibility_flags>0</visibility_flags></data>";
    doc.Clear(); doc.Parse(overflow.c_str()); rejected = false;
    try { materials.OnDeserialize(doc, doc.RootElement()); } catch (const std::runtime_error&) { rejected = true; }
    Require(rejected && materials.GetMaterialAtIndex(0) == material && static_cast<uint32_t>(materials.GetVisibilityFlags()) == 5,
        "overflowing material collection rejects entire component before setters");
    doc.Clear(); doc.Parse("<data><materials><unexpected>?</unexpected></materials></data>"); rejected = false;
    try { materials.OnDeserialize(doc, doc.RootElement()); } catch (const std::runtime_error&) { rejected = true; }
    Require(rejected && materials.GetMaterialAtIndex(0) == material, "material collection rejects incorrect element schema");
    materials.RemoveMaterialAtIndex(2); save(materials, doc);
    Require(!doc.RootElement()->FirstChildElement("materials")->FirstChildElement()->NextSiblingElement(), "clearing unresolved material removes its path and trailing holes");
    load(materials, doc, "<data><materials/></data>");
    Require(!materials.GetMaterialAtIndex(0), "explicit empty material list clears all slots");
    overflow = "<data><materials>";
    for (size_t i = 0; i < 254; ++i) overflow += "<material>?</material>";
    overflow += "<material>:Materials/Default.ovmat</material></materials></data>";
    load(materials, doc, overflow); save(materials, doc);
    size_t count = 0; for (auto* item = doc.RootElement()->FirstChildElement("materials")->FirstChildElement(); item; item = item->NextSiblingElement()) ++count;
    Require(count == 255 && materials.GetMaterialAtIndex(254) == material, "material collection supports its exact 255-slot boundary");
    load(audio, doc, "<data><audio_clip>ReflectionTone.wav</audio_clip></data>");
    Require(audio.GetSound() == tone, "generated sound reference resolves the existing manager-owned audio clip");
    load(audio, doc, "<data><autoplay>true</autoplay><spatial>true</spatial><volume>0.35</volume><pan>-0.2</pan><looped>true</looped><pitch>1.25</pitch><attenuation_threshold>7</attenuation_threshold><audio_clip>__missing__/tone.wav</audio_clip></data>");
    save(audio, doc);
    Require(audio.IsAutoplayed() && audio.IsSpatial() && audio.GetVolume() == .35f && audio.GetPan() == -.2f && audio.IsLooped() && audio.GetPitch() == 1.25f &&
        audio.GetAttenuationThreshold() == 7 && !audio.GetSound() && std::string(doc.RootElement()->FirstChildElement("audio_clip")->GetText()) == "__missing__/tone.wav",
        "audio source generated codec restores settings and retains missing clip");
    audio.SetSound(nullptr); save(audio, doc);
    Require(std::string(doc.RootElement()->FirstChildElement("audio_clip")->GetText()) == "?", "explicit audio clip clear drops unresolved identity");
    load(skin, doc, "<data><playing>false</playing><looping>false</looping><playback_speed>1.75</playback_speed><mesh_bounds_scale>0.5</mesh_bounds_scale><pose_eval_rate>-1</pose_eval_rate><time_ticks>4</time_ticks><animation_source>__missing__/animation.fbx</animation_source><animation>Walk</animation></data>");
    save(skin, doc);
    Require(!skin.IsPlaying() && !skin.IsLooping() && skin.GetPlaybackSpeed() == 1.75f && skin.GetMeshBoundsScale() == 1 &&
        std::string(doc.RootElement()->FirstChildElement("pose_eval_rate")->GetText()) == "0" && std::string(doc.RootElement()->FirstChildElement("animation")->GetText()) == "Walk" &&
        std::string(doc.RootElement()->FirstChildElement("animation_source")->GetText()) == "__missing__/animation.fbx",
        "animation post-load hook normalizes values once and preserves pending clip identity");
    auto* mannequin = context.modelManager.GetResource(":Models/Mannequin.fbx");
    Require(mannequin && !mannequin->GetAnimations().empty(), "animation roundtrip fixture has actual clips");
    model.SetModel(mannequin); skin.SetAnimationSourceModel(nullptr); skin.SetAnimation(std::optional<uint32_t>{0}); skin.SetTime(.2f); skin.Pause();
    save(skin, doc);
    auto& restoredActor = scene.CreateActor("Restored resources"); restoredActor.SetActive(false);
    auto& restoredModel = restoredActor.AddComponent<CModelRenderer>(); restoredModel.SetModel(mannequin);
    auto& restoredSkin = restoredActor.AddComponent<CSkinnedMeshRenderer>(); restoredSkin.OnDeserialize(doc, doc.RootElement());
    Require(restoredSkin.GetActiveAnimationName() == skin.GetActiveAnimationName() && !restoredSkin.IsPlaying() && std::abs(restoredSkin.GetTime() - skin.GetTime()) < .0001f && restoredSkin.HasSkinningData(),
        "generated animation codec restores clip, playback position and rebuilt palette");
    audio.SetSound(tone);
    doc.Clear(); auto* sceneNode = doc.NewElement("scene"); doc.InsertEndChild(sceneNode); scene.OnSerialize(doc, sceneNode);
    OvCore::SceneSystem::Scene restoredScene; restoredScene.OnDeserialize(doc, sceneNode->FirstChildElement("scene"));
    auto* cloned = restoredScene.FindActorByName("Resources");
    Require(cloned && cloned->GetComponent<CModelRenderer>()->GetModel() == mannequin && cloned->GetComponent<CAudioSource>()->GetSound() == tone &&
        cloned->GetComponent<CMaterialRenderer>()->GetMaterialAtIndex(254) == material && cloned->GetComponent<CSkinnedMeshRenderer>()->HasSkinningData(),
        "production scene save/load restores annotated resources, material slots and animation together");
    OvUI::Internal::WidgetContainer inspector;
    model.OnInspector(inspector);
    auto fields = Widgets<OvUI::Widgets::InputFields::AssetField>(inspector);
    Require(fields.size() == 1 && fields[0]->content == mannequin->path, "typed model Inspector retains asset picker");
    auto retained = *fields[0]->GetPlugin<OvUI::Plugins::DataDispatcher<std::string>>();
    Require(retained.IsWritable(), "typed asset field exposes the guarded property provider");
    retained.NotifyChange(); retained.Provide("__missing__/picker.fbx");
    Require(!model.GetModel() && retained.Gather() == "__missing__/picker.fbx", "asset field preserves selected unresolved identity");
    retained.NotifyChange(); retained.Provide(""); save(model, doc);
    Require(!model.GetModel() && std::string(doc.RootElement()->FirstChildElement("model")->GetText()) == "?", "asset field clear removes unresolved identity");
    retained.NotifyChange(); retained.Provide(mannequin->path);
    Require(model.GetModel() == mannequin, "asset field provider restores a valid manager-owned model");
    Require(std::async(std::launch::async, [&] { retained.NotifyChange(); retained.Provide(":Models/Cube.fbx"); return retained.Gather().empty(); }).get() && model.GetModel() == mannequin,
        "typed asset picker binding rejects worker access");
    context.sceneManager.LoadEmptyScene(); retained.NotifyChange(); retained.Provide(":Models/Cube.fbx");
    Require(retained.Gather().empty(), "typed asset picker binding expires after scene destruction");
}
