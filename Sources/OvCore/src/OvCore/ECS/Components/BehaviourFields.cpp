#include <OvCore/ECS/Components/Behaviour.h>
#include <OvCore/ECS/Actor.h>
#include <OvCore/SceneSystem/Scene.h>
#include <OvCore/Global/ServiceLocator.h>
#include <OvDebug/Logger.h>
#include <OvUI/Widgets/InputFields/InputText.h>
#include <OvUI/Widgets/Selection/CheckBox.h>
#include <OvUI/Widgets/Texts/Text.h>
#include <tinyxml2.h>
#include <algorithm>

using namespace OvCore::Scripting;
namespace {
    // The Inspector may switch to another Actor without redrawing this Behaviour.
    // Widget destruction must invalidate copied/deferred events independently of attachment lifetime.
    class FieldWidgetsLifetime final : public OvUI::Plugins::IPlugin {
    public:
        std::shared_ptr<const bool> token=std::make_shared<const bool>(true);
        void Execute(OvUI::Plugins::EPluginExecutionContext) override {}
    };
    std::string Record(const tinyxml2::XMLNode& node) { tinyxml2::XMLPrinter p(nullptr,true); node.Accept(&p); return p.CStr(); }
    std::string Hex(const std::string& text) {
        const char* digits="0123456789abcdef"; std::string result;
        for(unsigned char c:text) { result+=digits[c>>4]; result+=digits[c&15]; } return result;
    }
    std::optional<std::string> Unhex(const std::string& text) {
        if(text.size()%2) return {}; std::string result;
        for(size_t i=0;i<text.size();i+=2) {
            unsigned int v=0; auto p=std::from_chars(text.data()+i,text.data()+i+2,v,16);
            if(p.ec!=std::errc{} || p.ptr!=text.data()+i+2) return {}; result+=static_cast<char>(v);
        } return result;
    }
    std::optional<ManagedFieldValue> Decode(const tinyxml2::XMLElement& node) {
        uint32_t tag=0,aux=0;
        if(node.QueryUnsignedAttribute("tag",&tag)!=tinyxml2::XML_SUCCESS || node.QueryUnsignedAttribute("auxiliary",&aux)!=tinyxml2::XML_SUCCESS) return {};
        std::string text=node.GetText()?node.GetText():"";
        // tinyxml2's pretty printer inserts whitespace around extension children.
        // Stored scalar/hex lexemes allow XML whitespace; actual string bytes stay hex encoded.
        const auto first=text.find_first_not_of(" \t\r\n");
        text=first==std::string::npos?"":text.substr(first,text.find_last_not_of(" \t\r\n")-first+1);
        if(tag==12 || tag==18) {
            if(!node.Attribute("encoding","utf8-hex")) return {};
            auto decoded=Unhex(text); if(!decoded) return {}; text=*decoded;
        } else if(node.Attribute("encoding")) return {};
        return ManagedFieldValue::Parse(tag,aux,text);
    }
    bool Matches(const ManagedField& f,const char* id) { return id && (f.id==id || std::find(f.aliases.begin(),f.aliases.end(),id)!=f.aliases.end()); }
}
namespace OvCore::ECS::Components {
bool Behaviour::PrepareManagedFields() { return OVSERVICE(Scripting::ScriptEngine).PrepareCSharp(*this); }
std::optional<ManagedFieldValue> Behaviour::ManagedDefault(const std::string& id) const {
    auto it=m_managedDefaults.find(id); return it==m_managedDefaults.end()?std::nullopt:std::optional{it->second};
}
void Behaviour::RebindManagedFieldDefaults(std::map<std::string,ManagedFieldValue> defaults) {
    m_managedDefaults=std::move(defaults); RestoreManagedFields(false);
}
void Behaviour::RestoreManagedFields(bool applyValues) {
    if(!m_script || !m_script->IsValid()) return;
    if(applyValues) for(const auto& f:m_script->ManagedFields()) {
        if(!m_managedDefaults.contains(f.id)) if(auto value=m_script->ReadManagedField(f.token)) m_managedDefaults[f.id]=*value;
        if(auto it=m_managedDefaults.find(f.id);it!=m_managedDefaults.end()) m_script->WriteManagedField(f.token,it->second);
    }
    m_managedOverrides.clear(); m_managedLoaded.clear();
    tinyxml2::XMLDocument doc;
    if(m_rawData.empty() || doc.Parse(m_rawData.c_str())!=tinyxml2::XML_SUCCESS) return;
    auto* fields=doc.RootElement()->FirstChildElement("managed_fields"); if(!fields) return;
    for(const auto& f:m_script->ManagedFields()) {
        std::vector<const tinyxml2::XMLElement*> candidates;
        for(auto* n=fields->FirstChildElement("field");n;n=n->NextSiblingElement("field")) if(Matches(f,n->Attribute("id"))) {
            auto v=Decode(*n);
            if(v && v->tag==f.tag && (f.tag==18 || v->auxiliary==f.auxiliary)) candidates.push_back(n);
            else OVLOG_WARNING("C# field retained with incompatible/invalid value: "+f.id);
        }
        if(candidates.size()!=1) { if(candidates.size()>1) OVLOG_WARNING("C# ambiguous field records retained: "+f.id); continue; }
        auto value=Decode(*candidates.front());
        if(!applyValues || m_script->WriteManagedField(f.token,*value)) {
            // Read back normalization (e.g. asset paths) without invoking any getter.
            auto normalized=applyValues?m_script->ReadManagedField(f.token):value;
            m_managedOverrides[f.id]=normalized?*normalized:*value; m_managedLoaded.insert(Record(*candidates.front()));
        }
    }
}
bool Behaviour::EditManagedField(const std::string& id,const ManagedFieldValue& value) {
    if(!PrepareManagedFields()) return false;
    for(const auto& f:m_script->ManagedFields()) if(f.id==id) {
        if(!m_script->WriteManagedField(f.token,value)) return false;
        // Play writes affect the live object only. Scene snapshots always contain edit values.
        if(!owner.GetScene() || !owner.GetScene()->IsPlaying()) {
            auto actual=m_script->ReadManagedField(f.token); m_managedOverrides[id]=actual?*actual:value;
        }
        return true;
    }
    return false;
}
bool Behaviour::SetManagedFieldOverride(const std::string& id,bool enabled) {
    if(!PrepareManagedFields()) return false;
    for(const auto& f:m_script->ManagedFields()) if(f.id==id) {
        if(enabled) { auto live=m_script->ReadManagedField(f.token); return live && EditManagedField(id,*live); }
        auto value=ManagedDefault(id); if(!value || !m_script->WriteManagedField(f.token,*value)) return false;
        if(!owner.GetScene() || !owner.GetScene()->IsPlaying()) m_managedOverrides.erase(id);
        return true;
    }
    return false;
}
void Behaviour::SerializeManagedFields(tinyxml2::XMLDocument& doc,tinyxml2::XMLNode* node) {
    auto* fields=node->FirstChildElement("managed_fields");
    if(!fields && !m_managedOverrides.empty()) { fields=doc.NewElement("managed_fields"); node->InsertEndChild(fields); }
    if(!fields) return;
    for(auto* n=fields->FirstChild();n;) { auto* next=n->NextSibling(); if(m_managedLoaded.contains(Record(*n))) fields->DeleteChild(n); n=next; }
    for(const auto& [id,v]:m_managedOverrides) {
        // Preserve attributes/comments/extension children of successfully decoded records too.
        tinyxml2::XMLElement* n=nullptr; tinyxml2::XMLDocument source;
        for(const auto& record:m_managedLoaded) {
            source.Clear(); if(source.Parse(record.c_str())!=tinyxml2::XML_SUCCESS) continue;
            const auto* original=source.RootElement();
            const auto& metadata=m_script->ManagedFields();
            auto f=std::find_if(metadata.begin(),metadata.end(),[&](const auto& field){return field.id==id;});
            if(f!=metadata.end() && Matches(*f,original->Attribute("id"))) { n=original->DeepClone(&doc)->ToElement(); break; }
        }
        if(!n) n=doc.NewElement("field");
        n->SetAttribute("id",id.c_str()); n->SetAttribute("tag",v.tag); n->SetAttribute("auxiliary",v.auxiliary);
        if(v.tag==12 || v.tag==18) { n->SetAttribute("encoding","utf8-hex"); n->SetText(Hex(v.text).c_str()); }
        else { n->DeleteAttribute("encoding"); n->SetText(v.Display().c_str()); }
        fields->InsertEndChild(n);
    }
}
void Behaviour::RemapManagedActorReferences(const std::unordered_map<uint64_t,uint64_t>& mapping) {
    if(m_identity.backend!="csharp") return;
    tinyxml2::XMLDocument doc; auto* data=doc.NewElement("data");doc.InsertEndChild(data); OnSerialize(doc,data);
    if(auto* fields=data->FirstChildElement("managed_fields")) for(auto* n=fields->FirstChildElement("field");n;n=n->NextSiblingElement("field")) {
        auto v=Decode(*n); if(!v || v->tag!=OVCS_FIELD_ACTOR_REF || v->auxiliary!=0) continue;
        auto it=mapping.find(v->scalar.actor_guid); if(it!=mapping.end()) n->SetText(std::to_string(it->second).c_str());
    }
    // Also works without a project assembly; only explicitly tagged GUID references are rewritten.
    m_rawData=Record(*data); m_managedLoaded.clear(); m_managedOverrides.clear();
    if(m_script && m_script->IsValid()) RestoreManagedFields();
}
void Behaviour::InspectManagedFields(OvUI::Internal::WidgetContainer& root) {
    using OvUI::Widgets::Texts::Text;
    using OvUI::Widgets::Selection::CheckBox;
    using OvUI::Widgets::InputFields::InputText;
    m_managedUi=std::make_shared<const bool>(true);
    if(!PrepareManagedFields()) { root.CreateWidget<Text>("C# unavailable or faulted; serialized data preserved. See console."); return; }
    root.CreateWidget<Text>("C# fields: enable override to edit; disable to reset to constructor default.");
    std::weak_ptr<const bool> lifetime=m_managedUi;
    for(const auto& f:m_script->ManagedFields()) {
        const auto live=m_script->ReadManagedField(f.token);
        if(!live) { root.CreateWidget<Text>(f.name+": unsupported value; see console"); continue; }
        auto& toggle=root.CreateWidget<CheckBox>(HasManagedFieldOverride(f.id),f.name);
        std::weak_ptr<const bool> widgets=toggle.AddPlugin<FieldWidgetsLifetime>().token;
        auto& input=root.CreateWidget<InputText>(live->Display(),"##"+f.id);
        input.disabled=!HasManagedFieldOverride(f.id);
        // Text parsing supports the complete integer domain, including UInt64.MaxValue.
        input.ContentChangedEvent += [this,lifetime,widgets,f,aux=live->auxiliary](std::string text) {
            if(lifetime.expired() || widgets.expired()) return;
            auto current=f.tag==OVCS_FIELD_ASSET_REF?m_script->ReadManagedField(f.token):std::nullopt;
            auto value=ManagedFieldValue::Parse(f.tag,current?current->auxiliary:aux,text);
            if(!value || !EditManagedField(f.id,*value)) OVLOG_WARNING("Invalid C# field value: "+f.id);
        };
        toggle.ValueChangedEvent += [this,lifetime,widgets,id=f.id,&input](bool enabled) {
            if(lifetime.expired() || widgets.expired()) return;
            if(SetManagedFieldOverride(id,enabled)) {
                input.disabled=!enabled;
                if(!enabled) if(auto value=ManagedDefault(id)) input.content=value->Display();
            }
        };
        if(f.tag==OVCS_FIELD_ACTOR_REF) {
            auto* scene=owner.GetScene(); auto* actor=scene?scene->FindActorByGUID(live->scalar.actor_guid):nullptr;
            root.CreateWidget<Text>(live->scalar.actor_guid==0?"Actor: none":actor?"Actor: "+actor->GetName():"Actor: missing (GUID retained)");
        }
        if(f.tag==OVCS_FIELD_ASSET_REF) {
            auto& kind=root.CreateWidget<InputText>(std::to_string(live->auxiliary),"Asset kind##"+f.id);
            kind.disabled=input.disabled;
            toggle.ValueChangedEvent += [this,lifetime,widgets,id=f.id,&kind](bool enabled) {
                if(lifetime.expired() || widgets.expired()) return; kind.disabled=!enabled;
                if(!enabled) if(auto value=ManagedDefault(id)) kind.content=std::to_string(value->auxiliary);
            };
            kind.ContentChangedEvent += [this,lifetime,widgets,f](std::string text) {
                if(lifetime.expired() || widgets.expired()) return;
                uint32_t auxiliary=0; if(!ManagedFieldValue::ParseNumber(text,auxiliary)) return;
                auto current=m_script->ReadManagedField(f.token); if(!current) return;
                current->auxiliary=auxiliary; EditManagedField(f.id,*current);
            };
            root.CreateWidget<Text>("Asset kind: 0 any, 1 model, 2 texture, 3 shader, 4 material, 5 sound, 6 scene, 7 script, 8 font, 9 prefab");
            const auto* scene=owner.GetScene();
            root.CreateWidget<Text>(live->text.empty()?"Asset: none":scene && scene->ResolveScriptAsset(live->auxiliary,live->text)?"Asset: resolved":"Asset: missing (identity retained)");
        }
        if(f.tag==OVCS_FIELD_ENUM_INT64 || f.tag==OVCS_FIELD_ENUM_UINT64) root.CreateWidget<Text>("Enum underlying tag="+std::to_string(f.auxiliary));
    }
}
}
