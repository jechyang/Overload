#pragma once
#include <OvCore/Helpers/ManagedFieldInput.h>
#include <OvUI/Widgets/Layout/Group.h>
#include <imgui.h>
#include <limits>

static void FoundationFieldChecks(OvCore::SceneSystem::Scene& scene, OvCore::Scripting::ScriptEngine& scripts,
    RuntimeHost& host, const std::filesystem::path& project, const std::filesystem::path& assembly)
{
    using Value = OvCore::Scripting::ManagedFieldValue;
    auto& actor=scene.CreateActor("UI value fields");
    auto& behaviour=Add(actor,"UIFixture.Foundation.ValueFields");
    Require(scripts.PrepareCSharp(behaviour),"prepare UI value fields before Play");
    auto& target=scene.CreateActor("Reference target"); target.AddComponent<CRectTransform>();
    auto& targetScript=Add(target,"UIFixture.Foundation.ValueFields");
    Require(scripts.PrepareCSharp(targetScript),"prepare component reference target");
    auto metadata=[&](const char* name) -> const OvCore::Scripting::ManagedField& {
        for(const auto& f:behaviour.GetScript()->ManagedFields()) if(f.name==name) return f;
        throw std::runtime_error(std::string("Missing UI value field: ")+name);
    };
    auto read=[&](const char* name) {
        auto v=behaviour.GetScript()->ReadManagedField(metadata(name).token);
        if(!v) throw std::runtime_error("Cannot read UI value field"); return *v;
    };
    struct Case { const char* name; uint32_t tag; const char* value; };
    const Case cases[]={
        {"point",OVCS_FIELD_VECTOR2,"-0.25,1024.5"},
        {"vector",OVCS_FIELD_VECTOR4,"1.5,-2.25,3.125,-4.5"},
        {"rect",OVCS_FIELD_RECT,"-20,-30,120,80"},
        {"padding",OVCS_FIELD_RECT_OFFSET,"-2147483648,2147483647,-8,12"},
        {"tint",OVCS_FIELD_COLOR,"2,0.125,-0.25,0.75"}
    };
    // Create and use the production Inspector controls; no alternative field setter.
    OvUI::Widgets::Layout::Group inspector; behaviour.OnInspector(inspector);
    for(const auto& test:cases) {
        Require(metadata(test.name).tag==test.tag,"UI scalar has a distinct ABI tag");
        OvCore::Helpers::ManagedFieldInput* input=nullptr;
        for(auto [widget,mode]:inspector.GetWidgets()) {
            (void)mode;
            if(auto* field=dynamic_cast<OvCore::Helpers::ManagedFieldInput*>(widget);field && field->fieldId==test.name) input=field;
        }
        Require(input && !input->disabled,"UI value type has editable production Inspector control");
        auto value=Value::Parse(test.tag,0,test.value);
        Require(value.has_value(),"parse finite UI value"); input->ValueChangedEvent.Invoke(*value);
        Require(read(test.name).Display()==test.value && behaviour.HasManagedFieldOverride(test.name),"Inspector edit reaches CLR and creates XML override");
    }
    // Draw all new typed branches using a real ImGui context, including HDR color and integer edges.
    auto* previous=ImGui::GetCurrentContext(); auto* context=ImGui::CreateContext();
    auto& io=ImGui::GetIO(); io.IniFilename=nullptr; io.LogFilename=nullptr;
    io.DisplaySize={900,1200}; io.DeltaTime=1.0f/60;
    unsigned char* pixels=nullptr; int width=0,height=0; io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    for(int frame=0;frame<2;++frame) {
        ImGui::NewFrame(); ImGui::SetNextWindowSize({850,1150}); ImGui::Begin("U1 field controls");
        inspector.Draw(); ImGui::End(); ImGui::Render();
    }
    Require(ImGui::GetDrawData()->TotalVtxCount>0,"new Inspector controls generate real ImGui geometry");
    ImGui::DestroyContext(context); ImGui::SetCurrentContext(previous);
    for(const auto& test:cases) Require(read(test.name).Display()==test.value,"drawing Inspector preserves authored values");
    for(const char* invalid:{"nan,1","inf,1","1","1,2,3","1,"})
        Require(!Value::Parse(OVCS_FIELD_VECTOR2,0,invalid),"native field parser rejects nonfinite and malformed vectors");
    Require(!Value::Parse(OVCS_FIELD_RECT_OFFSET,0,"2147483648,0,0,0"),"integer edge overflow is rejected");
    // Bypass the text parser to exercise the managed ABI validation boundary too.
    for(const auto& test:cases) if(test.tag!=OVCS_FIELD_RECT_OFFSET) {
        auto value=read(test.name); value.scalar.float_values[0]=std::numeric_limits<float>::quiet_NaN();
        Require(!behaviour.EditManagedField(test.name,value),"managed UI scalar boundary rejects NaN");
        Require(read(test.name).Display()==test.value,"failed UI field write is atomic");
    }
    for(const auto* name:{"position","rotation","scalar","precise"}) {
        auto value=read(name); const auto before=value.Display();
        if(value.tag==OVCS_FIELD_FLOAT64) value.scalar.float64_value=std::numeric_limits<double>::infinity();
        else if(value.tag==OVCS_FIELD_VECTOR3) value.scalar.vector3.y=std::numeric_limits<float>::infinity();
        else if(value.tag==OVCS_FIELD_QUATERNION) value.scalar.quaternion.w=std::numeric_limits<float>::infinity();
        else value.scalar.float32_value=std::numeric_limits<float>::infinity();
        Require(!behaviour.EditManagedField(name,value) && read(name).Display()==before,"existing floating field tags reject infinity atomically");
    }
    auto mode=read("mode"); mode.scalar.int64_value=3;
    Require(!behaviour.EditManagedField("mode",mode),"undefined nonflags enum value is rejected");
    mode.scalar.int64_value=4; Require(behaviour.EditManagedField("mode",mode),"defined enum value is accepted");
    auto flags=read("flags"); flags.scalar.uint64_value=9;
    Require(behaviour.EditManagedField("flags",flags),"declared flags combination is accepted");
    flags.scalar.uint64_value=2; Require(!behaviour.EditManagedField("flags",flags) && read("flags").scalar.uint64_value==9,"undefined flag bits are rejected atomically");
    auto text=read("text"); text.text.assign(Value::MaxTextBytes,'x');
    Require(behaviour.EditManagedField("text",text),"field byte limit includes its boundary");
    text.text.push_back('x'); Require(!behaviour.EditManagedField("text",text),"managed field byte limit rejects overflow");
    Require(!Value::Parse(OVCS_FIELD_STRING,0,text.text),"native field byte limit rejects overflow");
    text.text="retained"; Require(behaviour.EditManagedField("text",text),"field is still editable after rejected writes");
    for(const auto* name:{"rectTarget","scriptTarget"}) {
        auto value=read(name); Require(value.tag==OVCS_FIELD_COMPONENT_REF && !value.text.empty(),"component reference carries stable type identity");
        value.scalar.actor_guid=target.GetGUID();
        Require(behaviour.EditManagedField(name,value),"author component reference identity");
        auto invalid=value; invalid.text="native:Unknown.Type";
        Require(!behaviour.EditManagedField(name,invalid) && read(name).text==value.text,"component reference type mismatch is rejected atomically");
    }
    tinyxml2::XMLDocument doc; auto* xml=doc.NewElement("root"); doc.InsertEndChild(xml); scene.OnSerialize(doc,xml);
    {
        OvCore::SceneSystem::Scene restored; restored.OnDeserialize(doc,xml->FirstChildElement("scene"));
        auto* copy=restored.FindActorByName("UI value fields")->GetBehaviour(ScriptIdentity::CSharp("ManagedUI","UIFixture.Foundation.ValueFields"));
        Require(copy && scripts.PrepareCSharp(*copy),"UI field XML creates prepared managed instance in another scene");
        for(const auto& test:cases) {
            bool same=false;
            for(const auto& f:copy->GetScript()->ManagedFields()) if(f.name==test.name) same=copy->GetScript()->ReadManagedField(f.token)->Display()==test.value;
            Require(same,"UI value survives native XML roundtrip");
        }
        for(const auto* name:{"rectTarget","scriptTarget"}) {
            bool same=false;
            for(const auto& f:copy->GetScript()->ManagedFields()) if(f.name==name) {
                auto value=copy->GetScript()->ReadManagedField(f.token);
                same=value && value->scalar.actor_guid==target.GetGUID() && value->text==read(name).text;
            }
            Require(same,"component identity and type survive XML roundtrip without live handles");
        }
    }
    inspector.RemoveAllWidgets();
    scene.Play();
    scene.Update(1); Require(read("referencesPassed").scalar.int32_value==1,"native and managed component references resolve in caller scene");
    Require(read("hierarchyPassed").scalar.int32_value==1,"runtime sibling ordering and component snapshots passed");
    {
        tinyxml2::XMLDocument hierarchy; auto* document=hierarchy.NewElement("root"); hierarchy.InsertEndChild(document);
        scene.OnSerialize(hierarchy,document);
        OvCore::SceneSystem::Scene copy; copy.OnDeserialize(hierarchy,document->FirstChildElement("scene"));
        auto* parent=copy.FindActorByName("U1 hierarchy parent");
        Require(parent && parent->GetChildren().size()==3,"hierarchy XML restores all reordered siblings");
        Require(parent->GetChildren()[0]->GetName()=="U1 child C" && parent->GetChildren()[1]->GetName()=="U1 child A" &&
            parent->GetChildren()[2]->GetName()=="U1 child B","sibling order survives Scene XML");
        OvCore::ECS::Actor* lastRoot=nullptr;
        for(auto* actor:copy.GetActors()) if(!actor->GetParent()) lastRoot=actor;
        Require(lastRoot==parent,"root sibling order survives Scene XML");
        auto* leaf=copy.FindActorByName("U1 grandchild");
        Require(leaf && !leaf->IsActive() && leaf->GetParent()==parent->GetChildren()[2],"hierarchy activation and parent links survive Scene XML");
    }
    for(int generation=1;generation<=3;++generation) {
        Require(scripts.LoadCSharpProject(host,project,assembly,"ManagedUI",true),"reload UI field generation");
        for(const auto& test:cases) Require(read(test.name).Display()==test.value,"UI value survives managed generation migration");
        Require(read("restored").scalar.int32_value==generation,"restored field instance receives reload callback once");
        scene.Update(1); Require(read("referencesPassed").scalar.int32_value==generation+1,"component references rebind to current managed generation");
    }
    Require(read("hierarchyPassed").scalar.int32_value==4,"hierarchy lifecycle phases survive reload and complete destruction");
    Require(target.RemoveComponent<CRectTransform>(),"remove referenced native component");
    Require(target.RemoveBehaviour(ScriptIdentity::CSharp("ManagedUI","UIFixture.Foundation.ValueFields")),"remove referenced managed component");
    scene.Update(2); Require(read("referencesPassed").scalar.int32_value==5,"invalid component identities resolve null after removal");
}
