#pragma once
#include <OvCore/ECS/Components/CReflectionProbe.h>

template<class Draw> static void ValidateProbe(OvEditor::Core::Context& context,OvEditor::Core::EditorActions& actions,OvEditor::Panels::Inspector& inspector,Draw draw)
{
    using P=OvCore::ECS::Components::CReflectionProbe;
    context.sceneManager.LoadEmptyScene(); inspector.SetLocked(false); inspector.SetSearchFilter("");
    auto id=context.sceneManager.GetCurrentScene()->CreateActor("Reflection settings").GetGUID();
    auto actor=[&] { return context.sceneManager.GetCurrentScene()->FindActorByGUID(id); };
    actor()->SetActive(false); actor()->AddComponent<P>(); auto probe=[&] { return actor()->GetComponent<P>(); };
    auto gpuWidth=[&](std::shared_ptr<baregl::Texture> texture) {
        int width=0; auto query=[&] { glGetTextureLevelParameteriv(texture->GetID(),0,GL_TEXTURE_WIDTH,&width); };
        if (context.renderThread) context.renderThread->Invoke([&](auto&) { query(); }); else query(); return width;
    };
    tinyxml2::XMLDocument doc;
    auto load=[&](const char* xml) { doc.Clear(); doc.Parse(xml); probe()->OnDeserialize(doc,doc.RootElement()); };
    load("<data><refresh_mode>2</refresh_mode><capture_speed>6</capture_speed><resolution>16</resolution><capture_position><x>2</x><y>3</y><z>4</z></capture_position><brightness>2.5</brightness><influence_policy>0</influence_policy><influence_size><x>7</x><y>8</y><z>9</z></influence_size><box_projection>true</box_projection></data>");
    Require(probe()->GetRefreshMode()==P::ERefreshMode::ON_DEMAND && probe()->GetCaptureSpeed()==P::ECaptureSpeed::SIX_FACES && probe()->GetCubemap()->GetDesc().width==16 && probe()->GetBrightness()==2.5f && probe()->IsBoxProjectionEnabled(),"generated probe settings restore legacy keys and rebuild the actual cubemap allocation");
    Require(gpuWidth(probe()->GetCubemap())==16,"OpenGL confirms loaded probe resolution in actual GPU storage");
    auto texture=probe()->GetCubemap(); load("<data><brightness>3</brightness></data>");
    Require(probe()->GetCubemap()==texture && OvMaths::FVector3(probe()->GetCapturePosition())==OvMaths::FVector3(2,3,4),"partial probe load preserves settings and does not reallocate unchanged resources");
    for (auto xml:{"<data><brightness>9</brightness><resolution>0</resolution></data>","<data><brightness>9</brightness><resolution>17</resolution></data>",
        "<data><brightness>9</brightness><resolution>8192</resolution></data>","<data><brightness>9</brightness><capture_speed>5</capture_speed></data>","<data><brightness>9</brightness><capture_position><x>NaN</x></capture_position></data>"})
    { bool rejected=false; try { load(xml); } catch (const std::exception&) { rejected=true; }
        Require(rejected && probe()->GetBrightness()==3 && probe()->GetCubemap()==texture,"invalid probe settings reject the complete record before publishing fields or GPU resources"); }
    load("<data><capture_speed>1</capture_speed><resolution>32</resolution></data>");
    Require(probe()->GetCubemap()!=texture && probe()->GetCubemap()->GetDesc().width==32 && probe()->GetCaptureSpeed()==P::ECaptureSpeed::ONE_FACE,"loading progressive capture and resolution rebuilds the required buffers together"); texture.reset();
    Require(gpuWidth(probe()->GetCubemap())==32,"OpenGL confirms progressive probe buffer resolution on its owner thread");
    doc.Clear(); auto* root=doc.NewElement("data"); doc.InsertEndChild(root); probe()->OnSerialize(doc,root);
    std::set<std::string> keys; for (auto* e=root->FirstChildElement();e;e=e->NextSiblingElement()) keys.insert(e->Name());
    Require(keys==std::set<std::string>{"refresh_mode","capture_speed","resolution","capture_position","brightness","influence_policy","influence_size","box_projection"},"probe serialization keeps its eight legacy keys and excludes capture state and native handles");
    actions.SelectActor(*actor()); draw(); draw();
    auto* resolution=Property<int>(inspector,"Cubemap Resolution"); Require(resolution && resolution->Gather()==32,"probe Inspector uses metadata power-of-two choices"); actions.ClearHistory();
    resolution->NotifyChange(); resolution->Provide(64); draw(); draw(); actions.CommitSceneEdit(true);
    Require(probe()->GetCubemap()->GetDesc().width==64 && actions.UndoCount()==1 && actions.Undo() && probe()->GetCubemap()->GetDesc().width==32,"probe resolution edits and undo keep GPU allocation consistent"); draw(); draw();
    auto* brightness=Property<float>(inspector,"Brightness"); Require(brightness && brightness->Gather()==3,"probe Inspector presents the correctly named brightness control");
    auto* box=Property<bool>(inspector,"Box Projection"); probe()->SetInfluencePolicy(P::EInfluencePolicy::GLOBAL); box->Gather();
    box->NotifyChange(); box->Provide(false); Require(probe()->IsBoxProjectionEnabled(),"global influence disables and guards local box projection editing");
    probe()->SetInfluencePolicy(P::EInfluencePolicy::LOCAL); box->Gather(); box->NotifyChange(); box->Provide(false);
    Require(!probe()->IsBoxProjectionEnabled(),"changing influence policy re-enables local Inspector controls"); draw(); draw();
    OvUI::Internal::WidgetContainer controls; probe()->OnInspector(controls); auto buttons=Widgets<OvUI::Widgets::Buttons::Button>(controls);
    Require(buttons.size()==1 && buttons[0]->label=="Capture Now","generated probe Inspector retains its capture action"); auto event=buttons[0]->ClickedEvent;
    context.sceneManager.LoadEmptyScene(); event.Invoke(); Require(true,"probe capture action expires safely after scene destruction");
}
