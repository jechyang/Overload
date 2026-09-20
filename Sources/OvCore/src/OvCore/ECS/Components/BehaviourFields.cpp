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
#include <cfloat>
#include <cstring>
#include <imgui.h>
#include <OvCore/Helpers/ManagedFieldInput.h>
#include <OvCore/Helpers/InspectorLabel.h>
#include <OvCore/Helpers/GUIHelpers.h>
#include <OvUI/Widgets/Layout/Dummy.h>
#include <OvUI/Widgets/Layout/TreeNode.h>
#include <OvUI/Plugins/DDTarget.h>

using namespace OvCore::Scripting;
namespace {
    class ManagedFieldLabel final : public OvUI::Widgets::Texts::Text {
    public:
        using Text::Text;
    protected:
        void _Draw_Impl() override { ImGui::AlignTextToFramePadding(); Text::_Draw_Impl(); }
    };
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
        if(tag==12 || tag==18 || tag==OVCS_FIELD_COMPONENT_REF) {
            if(!node.Attribute("encoding","utf8-hex")) return {};
            auto decoded=Unhex(text); if(!decoded) return {}; text=*decoded;
        } else if(node.Attribute("encoding")) return {};
        auto result=ManagedFieldValue::Parse(tag,aux,text);
        if(result && tag==OVCS_FIELD_COMPONENT_REF) {
            const auto* guid=node.Attribute("actor_guid"); if(!guid) return {};
            const auto* end=guid+std::strlen(guid);
            const auto parsed=std::from_chars(guid,end,result->scalar.actor_guid);
            if(parsed.ec!=std::errc{} || parsed.ptr!=end) return {};
        }
        return result;
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
        if(v.tag==OVCS_FIELD_COMPONENT_REF) n->SetAttribute("actor_guid",std::to_string(v.scalar.actor_guid).c_str());
        if(v.tag==12 || v.tag==18 || v.tag==OVCS_FIELD_COMPONENT_REF) { n->SetAttribute("encoding","utf8-hex"); n->SetText(Hex(v.text).c_str()); }
        else { n->DeleteAttribute("encoding"); n->SetText(v.Display().c_str()); }
        fields->InsertEndChild(n);
    }
}
void Behaviour::RemapManagedActorReferences(const std::unordered_map<uint64_t,uint64_t>& mapping) {
    if(m_identity.backend!="csharp") return;
    tinyxml2::XMLDocument doc; auto* data=doc.NewElement("data");doc.InsertEndChild(data); OnSerialize(doc,data);
    if(auto* fields=data->FirstChildElement("managed_fields")) for(auto* n=fields->FirstChildElement("field");n;n=n->NextSiblingElement("field")) {
        auto v=Decode(*n); if(!v || (v->tag!=OVCS_FIELD_ACTOR_REF && v->tag!=OVCS_FIELD_COMPONENT_REF) || v->auxiliary!=0) continue;
        auto it=mapping.find(v->scalar.actor_guid); if(it!=mapping.end()) {
            if(v->tag==OVCS_FIELD_COMPONENT_REF) n->SetAttribute("actor_guid",std::to_string(it->second).c_str());
            else n->SetText(std::to_string(it->second).c_str());
        }
    }
    // Also works without a project assembly; only explicitly tagged GUID references are rewritten.
    m_rawData=Record(*data); m_managedLoaded.clear(); m_managedOverrides.clear();
    if(m_script && m_script->IsValid()) RestoreManagedFields();
}
void Behaviour::InspectManagedFields(OvUI::Internal::WidgetContainer& root)
{
    using namespace OvUI::Widgets;
    using Helpers::GUIHelpers;
    m_managedUi = std::make_shared<const bool>(true);
    if (!PrepareManagedFields()) {
        root.CreateWidget<Texts::Text>("Script unavailable");
        root.CreateWidget<Texts::Text>("See Console. Saved fields are preserved.");
        return;
    }
    std::weak_ptr<const bool> lifetime = m_managedUi;
    for (const auto& field : m_script->ManagedFields()) {
        const auto label = !field.displayName.empty() && field.displayName != field.name ? field.displayName : Helpers::InspectorLabel(field.name);
        auto& title = root.CreateWidget<ManagedFieldLabel>(label);
        title.tooltip = field.name + "\nRight-click the value to reset to its script default.";
        if (!m_script->ReadManagedField(field.token)) {
            auto& unavailable = root.CreateWidget<Texts::Text>("Unavailable");
            unavailable.tooltip = "This value cannot be edited. Saved data is preserved; see Console for details.";
            continue;
        }
        std::weak_ptr<const bool> widgets = title.AddPlugin<FieldWidgetsLifetime>().token;
        auto valid = [lifetime, widgets] { return !lifetime.expired() && !widgets.expired(); };
        auto& input = root.CreateWidget<Helpers::ManagedFieldInput>();
        input.fieldId = field.id;
        input.read = [this,valid,token=field.token]() -> std::optional<ManagedFieldValue> {
            return valid() ? m_script->ReadManagedField(token) : std::nullopt;
        };
        auto commit = [this,valid,id=field.id](ManagedFieldValue value) {
            if (valid() && !EditManagedField(id,value)) OVLOG_WARNING("Invalid C# field value: " + id);
        };
        input.ValueChangedEvent += commit;
        input.reset = [this,valid,id=field.id] { if (valid()) SetManagedFieldOverride(id,false); };
        title.AddPlugin<OvUI::Plugins::DataDispatcher<std::string>>().RegisterGatherer([this,valid,label,id=field.id] {
            return valid() && HasManagedFieldOverride(id) ? label + " *" : label;
        });
        if (field.tag == OVCS_FIELD_ACTOR_REF || field.tag == OVCS_FIELD_COMPONENT_REF) {
            input.referenceLabel = [this,valid](const ManagedFieldValue& value) {
                if (!valid() || !value.scalar.actor_guid) return std::string{value.tag==OVCS_FIELD_COMPONENT_REF?"None (Component)":"None (Actor)"};
                auto* scene = owner.GetScene();
                auto* actor = scene ? scene->FindActorByGUID(value.scalar.actor_guid) : nullptr;
                return actor ? actor->GetName() : std::string{"Missing (Actor)"};
            };
            input.pick = [this,valid,commit,field,read=input.read] {
                if (!valid()) return;
                auto* scene = owner.GetScene(); if (!scene) return;
                GUIHelpers::PickerItemList items;
                auto select = [commit,read](uint64_t guid) {
                    auto value=read(); if(!value) return; value->scalar.actor_guid=guid; commit(*value);
                };
                items.Add({"__none__","None","Clear reference",0u,[select] { select(0); }});
                for (auto* actor : scene->GetActors()) {
                    const auto guid = actor->GetGUID();
                    items.Add({std::to_string(guid),actor->GetName(),"Actor reference",GUIHelpers::GetActorIconID(),[select,guid] { select(guid); }});
                }
                GUIHelpers::OpenPicker(std::move(items),"Select Actor");
            };
            using Payload = std::pair<OvCore::ECS::Actor*,Layout::TreeNode*>;
            input.AddPlugin<OvUI::Plugins::DDTarget<Payload>>("Actor").DataReceivedEvent += [this,valid,commit,read=input.read](Payload payload) {
                if (!valid() || !payload.first || payload.first->GetScene() != owner.GetScene()) return;
                auto value=read(); if(!value) return; value->scalar.actor_guid=payload.first->GetGUID(); commit(*value);
            };
        }
        if (field.tag == OVCS_FIELD_ASSET_REF) {
            input.pick = [valid,read=input.read,commit] {
                if (!valid()) return;
                const auto current = read(); if (!current) return;
                using Type = OvTools::Utils::PathParser::EFileType;
                static const Type kinds[] = {Type::UNKNOWN,Type::MODEL,Type::TEXTURE,Type::SHADER,Type::MATERIAL,
                    Type::SOUND,Type::SCENE,Type::SCRIPT,Type::FONT,Type::PREFAB};
                const auto kind = current->auxiliary < std::size(kinds) ? kinds[current->auxiliary] : Type::UNKNOWN;
                GUIHelpers::OpenAssetPicker(kind,[valid,read,commit](std::string path) {
                    if (!valid()) return;
                    auto value = read(); if (!value) return;
                    value->text=std::move(path); commit(*value);
                },true,false);
            };
        }
    }
    if (m_script->ManagedFields().empty()) {
        root.CreateWidget<Texts::Text>("No serialized fields");
        root.CreateWidget<Layout::Dummy>();
    }
}
}

int OvCore::Helpers::ManagedFieldInput::ResizeText(ImGuiInputTextCallbackData* data)
{
    auto& text = *static_cast<std::string*>(data->UserData);
    text.resize(static_cast<size_t>(data->BufTextLen));
    data->Buf = text.data();
    return 0;
}

void OvCore::Helpers::ManagedFieldInput::_Draw_Impl()
{
    auto current = read ? read() : std::nullopt;
    if (!current) { ImGui::TextDisabled("Unavailable"); return; }
    auto value = *current;
    ImGui::PushID(m_widgetID.c_str());
    ImGui::SetNextItemWidth(-FLT_MIN);
    bool changed = false;
    switch (value.tag) {
    case OVCS_FIELD_BOOL: {
        bool enabled = value.scalar.boolean != 0;
        changed = ImGui::Checkbox("##value",&enabled); value.scalar.boolean = enabled ? 1 : 0; break;
    }
    case OVCS_FIELD_INT8: changed=ImGui::InputScalar("##value",ImGuiDataType_S8,&value.scalar.int8_value); break;
    case OVCS_FIELD_UINT8: changed=ImGui::InputScalar("##value",ImGuiDataType_U8,&value.scalar.uint8_value); break;
    case OVCS_FIELD_INT16: changed=ImGui::InputScalar("##value",ImGuiDataType_S16,&value.scalar.int16_value); break;
    case OVCS_FIELD_UINT16: changed=ImGui::InputScalar("##value",ImGuiDataType_U16,&value.scalar.uint16_value); break;
    case OVCS_FIELD_INT32: changed=ImGui::DragScalar("##value",ImGuiDataType_S32,&value.scalar.int32_value,1); break;
    case OVCS_FIELD_UINT32: changed=ImGui::DragScalar("##value",ImGuiDataType_U32,&value.scalar.uint32_value,1); break;
    case OVCS_FIELD_INT64: case OVCS_FIELD_ENUM_INT64: changed=ImGui::InputScalar("##value",ImGuiDataType_S64,&value.scalar.int64_value); break;
    case OVCS_FIELD_UINT64: case OVCS_FIELD_ENUM_UINT64: changed=ImGui::InputScalar("##value",ImGuiDataType_U64,&value.scalar.uint64_value); break;
    case OVCS_FIELD_FLOAT32: changed=ImGui::DragScalar("##value",ImGuiDataType_Float,&value.scalar.float32_value,0.05f,nullptr,nullptr,"%.4g"); break;
    case OVCS_FIELD_FLOAT64: changed=ImGui::DragScalar("##value",ImGuiDataType_Double,&value.scalar.float64_value,0.05f,nullptr,nullptr,"%.6g"); break;
    case OVCS_FIELD_VECTOR2: case OVCS_FIELD_VECTOR3: case OVCS_FIELD_VECTOR4: case OVCS_FIELD_QUATERNION: case OVCS_FIELD_RECT: {
        const int count = value.tag == OVCS_FIELD_VECTOR2 ? 2 : value.tag == OVCS_FIELD_VECTOR3 ? 3 : 4;
        float parts[4]{};
        if (value.tag == OVCS_FIELD_VECTOR3) { parts[0]=value.scalar.vector3.x; parts[1]=value.scalar.vector3.y; parts[2]=value.scalar.vector3.z; }
        else if (value.tag == OVCS_FIELD_QUATERNION) { parts[0]=value.scalar.quaternion.x; parts[1]=value.scalar.quaternion.y; parts[2]=value.scalar.quaternion.z; parts[3]=value.scalar.quaternion.w; }
        else std::copy_n(value.scalar.float_values,count,parts);
        const float width = std::max(1.0f,(ImGui::GetContentRegionAvail().x-ImGui::GetStyle().ItemInnerSpacing.x*(count-1))/count);
        const char* formats[] = {"X: %.3f","Y: %.3f","Z: %.3f","W: %.3f"};
        if (value.tag == OVCS_FIELD_RECT) { formats[2]="W: %.3f"; formats[3]="H: %.3f"; }
        ImGui::BeginGroup();
        for (int i=0;i<count;++i) {
            if (i) ImGui::SameLine(0,ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::PushID(i); ImGui::SetNextItemWidth(width);
            changed |= ImGui::DragFloat("##axis",&parts[i],0.025f,0,0,formats[i]); ImGui::PopID();
        }
        ImGui::EndGroup();
        if (value.tag == OVCS_FIELD_VECTOR3) value.scalar.vector3={parts[0],parts[1],parts[2]};
        else if (value.tag == OVCS_FIELD_QUATERNION) value.scalar.quaternion={parts[0],parts[1],parts[2],parts[3]};
        else std::copy_n(parts,count,value.scalar.float_values);
        break;
    }
    case OVCS_FIELD_COLOR:
        changed=ImGui::ColorEdit4("##value",value.scalar.float_values,ImGuiColorEditFlags_Float|ImGuiColorEditFlags_HDR);
        break;
    case OVCS_FIELD_RECT_OFFSET: {
        const float width=std::max(1.0f,(ImGui::GetContentRegionAvail().x-ImGui::GetStyle().ItemInnerSpacing.x*3)/4);
        const char* formats[]={"L: %d","R: %d","T: %d","B: %d"};
        ImGui::BeginGroup();
        for(int i=0;i<4;++i) {
            if(i) ImGui::SameLine(0,ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::PushID(i); ImGui::SetNextItemWidth(width);
            changed|=ImGui::InputScalar("##edge",ImGuiDataType_S32,&value.scalar.int32_values[i],nullptr,nullptr,formats[i]);
            ImGui::PopID();
        }
        ImGui::EndGroup();
        break;
    }
    case OVCS_FIELD_ACTOR_REF: case OVCS_FIELD_COMPONENT_REF: {
        const auto label = referenceLabel ? referenceLabel(value) : value.Display();
        if (ImGui::Button((label+"###reference").c_str(),ImVec2(-FLT_MIN,0)) && pick) pick();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nClick to choose; drag an actor from the Hierarchy.",value.Display().c_str());
        break;
    }
    default: {
        if (value.tag == OVCS_FIELD_ASSET_REF) {
            static const char* kinds[] = {"Any asset","Model","Texture","Shader","Material","Sound","Scene","Script","Font","Prefab"};
            int kind = static_cast<int>(value.auxiliary);
            if (ImGui::Combo("##kind",&kind,kinds,static_cast<int>(std::size(kinds)))) { value.auxiliary=static_cast<uint32_t>(kind); changed=true; }
            ImGui::SetNextItemWidth(std::max(1.0f,ImGui::GetContentRegionAvail().x-ImGui::GetFrameHeight()-ImGui::GetStyle().ItemSpacing.x));
        }
        if (!m_textActive) m_text = value.text;
        const bool edited = ImGui::InputText("##value",m_text.data(),m_text.capacity()+1,ImGuiInputTextFlags_CallbackResize,ResizeText,&m_text);
        m_textActive = ImGui::IsItemActive();
        if (edited) { m_text.resize(std::strlen(m_text.c_str())); value.text=m_text; changed=true; }
        if (value.tag == OVCS_FIELD_ASSET_REF) {
            ImGui::SameLine(); if (ImGui::Button("...",ImVec2(ImGui::GetFrameHeight(),0)) && pick) pick();
        }
        break;
    }
    }
    if (changed) ValueChangedEvent.Invoke(value);
    if (ImGui::BeginPopupContextItem("field_actions")) {
        if (ImGui::MenuItem("Reset to Script Default") && reset) { reset(); m_textActive=false; }
        if (value.tag == OVCS_FIELD_ACTOR_REF || value.tag == OVCS_FIELD_ASSET_REF || value.tag == OVCS_FIELD_COMPONENT_REF) {
            if (ImGui::MenuItem("Clear Reference")) {
                value.scalar.actor_guid=0; if(value.tag!=OVCS_FIELD_COMPONENT_REF) value.text.clear();
                ValueChangedEvent.Invoke(value); m_textActive=false;
            }
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
}
