#include <OvCore/Helpers/ReflectionCollectionField.h>
#include <OvUI/Widgets/Layout/GroupCollapsable.h>
#include <OvUI/Widgets/Layout/Columns.h>
#include <OvUI/Styling/Style.h>
#include <imgui_internal.h>
#include <imgui.h>
#include <algorithm>
#include <cstdio>

namespace OvCore::Helpers::ReflectionProperties
{
    CollectionValue CollectionValue::From(const MainThread& main,const PropertyDesc& property,std::any data)
    {
        auto* codec = FindValueCodec(property);
        if (!main.IsCurrent() || !codec || !codec->collection) return {};
        tinyxml2::XMLDocument doc; auto* node=doc.NewElement("value"); doc.InsertEndChild(node);
        if (!WriteValueXML(main,property,doc,*node,data)) return {};
        tinyxml2::XMLPrinter printer(nullptr,true); doc.Print(&printer);
        return {std::move(data),&property,codec->collection,printer.CStr(),{}};
    }
    CollectionValue CollectionValue::Merge(const MainThread& main,const CollectionValue& current) const
    {
        if (!patch) return *this;
        if (!property || !current.property || property->Info().type.key != current.property->Info().type.key) return current;
        auto copy=current.data; if (!patch(copy)) return current;
        return From(main,*current.property,std::move(copy));
    }
    // A record element edit changes only its edited leaves on secondary targets.
    // Math axes use the same rule, preserving unrelated coordinates.
    static bool MergeElement(const MainThread& main,const PropertyDesc& property,const std::any& before,const std::any& edited,std::any& current)
    {
        if (auto* codec=FindValueCodec(property); codec && codec->recordType && codec->borrowRecord)
        {
            auto b=before,e=edited; auto bo=codec->borrowRecord(b),eo=codec->borrowRecord(e),co=codec->borrowRecord(current);
            if (!bo || !eo || !co) return false;
            for (auto* child:Properties(*codec->recordType,"Inspect"))
            {
                std::any bv,ev,cv;
                if (child->ReadValue(main,*bo,bv)!=Error::None || child->ReadValue(main,*eo,ev)!=Error::None || child->ReadValue(main,*co,cv)!=Error::None ||
                    !MergeElement(main,*child,bv,ev,cv) || child->WriteValue(main,*co,cv)!=Error::None) return false;
            }
            return true;
        }
        bool handled=false;
        Dispatch(property,[&]<class T>()
        {
            if constexpr (std::is_same_v<T,OvMaths::FVector2> || std::is_same_v<T,OvMaths::FVector3> || std::is_same_v<T,OvMaths::FVector4> || std::is_same_v<T,OvMaths::FQuaternion>)
            {
                auto* b=std::any_cast<T>(&before); auto* e=std::any_cast<T>(&edited); auto* c=std::any_cast<T>(&current);
                if (!b || !e || !c) return;
                if (b->x!=e->x) c->x=e->x; if (b->y!=e->y) c->y=e->y;
                if constexpr (!std::is_same_v<T,OvMaths::FVector2>) if (b->z!=e->z) c->z=e->z;
                if constexpr (std::is_same_v<T,OvMaths::FVector4> || std::is_same_v<T,OvMaths::FQuaternion>) if (b->w!=e->w) c->w=e->w;
                handled=true;
            }
        });
        if (handled) return true;
        tinyxml2::XMLDocument bd,ed; auto* bn=bd.NewElement("value"); auto* en=ed.NewElement("value"); bd.InsertEndChild(bn); ed.InsertEndChild(en);
        if (!WriteValueXML(main,property,bd,*bn,before) || !WriteValueXML(main,property,ed,*en,edited)) return false;
        tinyxml2::XMLPrinter bp(nullptr,true),ep(nullptr,true); bd.Print(&bp); ed.Print(&ep);
        if (std::string_view(bp.CStr())!=ep.CStr()) current=edited;
        return true;
    }
    CollectionField::CollectionField(Binding binding,const Registry& registry,std::span<const DrawerHook> hooks)
        : DataWidget(m_value),m_binding(std::move(binding)),m_registry(registry),m_hooks(hooks.begin(),hooks.end())
    {
        fullWidth=true; m_autoExecutePlugins=false;
        auto name=Meta(m_binding.property->Info().metadata,"DisplayName"); m_name=std::string(name.empty()?m_binding.property->Info().name:name);
        SetID("reflection_collection_"+(m_binding.identity.empty()?std::string(m_binding.property->Info().id):m_binding.identity));
        disabled=m_binding.readOnly || m_binding.property->Info().readOnly;
        auto ticket=m_scope.Ticket(); auto& dispatcher=AddPlugin<OvUI::Plugins::DataDispatcher<CollectionValue>>();
        dispatcher.RegisterGatherer([this,ticket,main=&registry.Main()] {
            if (!main->IsCurrent() || ticket.expired()) return CollectionValue{};
            std::any value; if (!m_binding.read(value).Ok()) { disabled=true; return CollectionValue{}; }
            return CollectionValue::From(*main,*m_binding.property,std::move(value)); });
        dispatcher.RegisterProvider([this,ticket,main=&registry.Main()](CollectionValue value) {
            if (!main->IsCurrent() || ticket.expired() || disabled || !value.property || value.property->Info().type.key!=m_binding.property->Info().type.key) return;
            auto checked=CollectionValue::From(*main,*m_binding.property,value.data);
            if (checked.property) m_binding.write(checked.data); });
        m_value=dispatcher.Gather(); Rebuild();
    }
    bool CollectionField::Edit(std::function<bool(std::any&)> operation,bool structural)
    {
        if (!m_registry.Main().IsCurrent() || disabled) return false;
        auto* dispatcher=GetPlugin<OvUI::Plugins::DataDispatcher<CollectionValue>>();
        auto value=dispatcher->Gather(); if (!value.property) return false;
        auto copy=value.data; if (!operation(copy)) return false;
        auto edited=CollectionValue::From(m_registry.Main(),*m_binding.property,std::move(copy)); if (!edited.property) return false;
        edited.patch=std::move(operation); dispatcher->NotifyChange(); dispatcher->Provide(edited);
        m_value=dispatcher->Gather(); m_seen=m_value.canonical;
        if (structural) { m_items.Invalidate(); m_rebuild=true; }
        return true;
    }
    bool CollectionField::Resize(size_t size)
    {
        auto* codec=FindValueCodec(*m_binding.property); if (!codec || !codec->collection || !codec->collection->dynamic || size>CollectionLimit(*m_binding.property)) return false;
        return Edit([ops=codec->collection,size](std::any& data) { return ops->resize(data,size); },true);
    }
    bool CollectionField::Erase(size_t index)
    { if (m_mixedShape) return false; auto* ops=FindValueCodec(*m_binding.property)->collection; return Edit([ops,index](std::any& data) { return ops->erase(data,index); },true); }
    bool CollectionField::Move(size_t from,size_t to)
    { if (m_mixedShape) return false; auto* ops=FindValueCodec(*m_binding.property)->collection; return Edit([ops,from,to](std::any& data) { return ops->move(data,from,to); },true); }
    bool CollectionField::Duplicate(size_t index)
    { if (m_mixedShape) return false; auto* ops=FindValueCodec(*m_binding.property)->collection; auto limit=CollectionLimit(*m_binding.property);
        return Edit([ops,index,limit](std::any& data) { std::any item; auto size=ops->size(data); return size<limit && ops->get(data,index,item) && ops->resize(data,size+1) && ops->set(data,size,item) && ops->move(data,size,index+1); },true); }
    void CollectionField::Rebuild()
    {
        m_items.Invalidate(); RemoveAllWidgets(); m_rebuild=false; m_seen=m_value.canonical;
        if (!m_value.ops) return;
        auto size=m_value.ops->size(m_value.data); m_page=std::min(m_page,size? (size-1)/32:size_t{});
        auto ticket=m_items.Ticket(); auto* main=&m_registry.Main(); const auto* ops=m_value.ops;
        for (size_t i=m_page*32;i<std::min(size,(m_page+1)*32);++i)
        {
            auto& group=CreateWidget<OvUI::Widgets::Layout::Group>();
            group.SetID("collection_item_"+m_binding.identity+std::string(m_binding.property->Info().id)+"/"+std::to_string(i));
            auto& columns=group.CreateWidget<OvUI::Widgets::Layout::Columns<2>>(); columns.widths[0]=180*OVUI_SCALE;
            Binding item{ops->element,
                [this,ticket,main,ops,i](std::any& value)->Result {
                    if (!main->IsCurrent()) return {"item",Status::AccessDenied,Error::WrongThread};
                    if (ticket.expired()) return {"item",Status::Expired};
                    std::any data;
                    // Child reads need a fresh owned value, but not a complete XML
                    // clipboard snapshot for every visible scalar on every frame.
                    if (!m_binding.read(data).Ok() || ops->size(data)!=ops->size(m_value.data)) return {"item",Status::Invalid};
                    return {"item",ops->get(data,i,value)?Status::Applied:Status::Invalid}; },
                [this,ticket,main,ops,i](const std::any& value)->Result {
                    if (!main->IsCurrent()) return {"item",Status::AccessDenied,Error::WrongThread};
                    if (ticket.expired()) return {"item",Status::Expired};
                    if (m_mixedShape || disabled) return {"item",Status::ReadOnly};
                    std::any before; auto data=GetPlugin<OvUI::Plugins::DataDispatcher<CollectionValue>>()->Gather();
                    if (!data.ops || ops->size(data.data)!=ops->size(m_value.data) || !ops->get(data.data,i,before)) return {"item",Status::Invalid};
                    return {"item",Edit([ops,i,before,value,main](std::any& data) { std::any current;
                        return ops->get(data,i,current) && MergeElement(*main,*ops->element,before,value,current) && ops->set(data,i,current); },false)?Status::Applied:Status::Invalid}; },
                m_binding.identity+"/"+std::string(m_binding.property->Info().id)+"/"+std::to_string(i),disabled,"Element "+std::to_string(i)};
            ReflectionProperties::Draw(columns,item,m_registry,m_hooks).Ok();
        }
    }
    void CollectionField::_Draw_Impl()
    {
        if (!m_registry.Main().IsCurrent()) return;
        if (m_value.canonical!=m_seen) { m_items.Invalidate(); m_rebuild=true; }
        if (m_rebuild) Rebuild();
        if (!m_value.ops) { ImGui::TextDisabled("Collection unavailable"); return; }
        const auto size=m_value.ops->size(m_value.data);
        auto label=m_name+" ("+std::to_string(size)+")"+(m_mixedValues?" (Mixed)":"")+"###"+m_widgetID;
        auto* storage=ImGui::GetStateStorage(); auto id=ImGui::GetID(label.c_str()); auto previous=storage->GetInt(id,1);
        if (expandForSearch) ImGui::SetNextItemOpen(true,ImGuiCond_Always);
        bool open=ImGui::CollapsingHeader(label.c_str(),ImGuiTreeNodeFlags_DefaultOpen);
        if (expandForSearch) storage->SetInt(id,previous);
        ExecutePlugins(OvUI::Plugins::EPluginExecutionContext::WIDGET);
        if (!open) return;
        ImGui::PushID(m_widgetID.c_str());
        if (m_value.ops->dynamic)
        {
            if (m_lastSize!=size) { std::snprintf(m_sizeText.data(),m_sizeText.size(),"%zu",size); m_lastSize=size; m_sizeInvalid=false; }
            ImGui::SetNextItemWidth(100*OVUI_SCALE);
            if (ImGui::InputText("Size",m_sizeText.data(),m_sizeText.size(),ImGuiInputTextFlags_EnterReturnsTrue|ImGuiInputTextFlags_CharsDecimal))
            { size_t count{}; auto* end=m_sizeText.data()+std::char_traits<char>::length(m_sizeText.data()); auto result=std::from_chars(m_sizeText.data(),end,count);
                m_sizeInvalid=result.ec!=std::errc{} || result.ptr!=end || !Resize(count); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Press Enter to apply Size (0..%zu)",CollectionLimit(*m_binding.property));
            ImGui::SameLine(); ImGui::BeginDisabled(m_mixedShape || size>=CollectionLimit(*m_binding.property));
            if (ImGui::Button("Add")) Resize(size+1); ImGui::EndDisabled(); ImGui::SameLine(); if (ImGui::Button("Clear")) Resize(0);
            if (m_sizeInvalid) ImGui::TextColored({1,.45f,.35f,1},"Size must be an integer from 0 to %zu",CollectionLimit(*m_binding.property));
        }
        if (m_mixedShape) ImGui::TextDisabled("Mixed lengths. Set Size to edit elements.");
        if (size>32) { if (ImGui::Button("Previous") && m_page) { --m_page; m_rebuild=true; } ImGui::SameLine();
            ImGui::Text("Page %zu / %zu",m_page+1,(size+31)/32); ImGui::SameLine(); if (ImGui::Button("Next") && (m_page+1)*32<size) { ++m_page; m_rebuild=true; } }
        // Structural edits invalidate the previous index epoch immediately; rebuild
        // only outside the child draw iteration, so copied callbacks cannot hit a shifted item.
        if (!m_rebuild) for (size_t row=0;row<GetWidgets().size();++row)
        {
            const size_t i=m_page*32+row; ImGui::PushID(static_cast<int>(i));
            ImGui::BeginDisabled(m_mixedShape);
            ImGui::BeginDisabled(i==0); if (ImGui::SmallButton("Up")) Move(i,i-1); ImGui::EndDisabled(); ImGui::SameLine();
            ImGui::BeginDisabled(i+1>=size); if (ImGui::SmallButton("Down")) Move(i,i+1); ImGui::EndDisabled();
            if (m_value.ops->dynamic) { ImGui::SameLine(); ImGui::BeginDisabled(size>=CollectionLimit(*m_binding.property)); if (ImGui::SmallButton("Duplicate")) Duplicate(i); ImGui::EndDisabled();
                ImGui::SameLine(); if (ImGui::SmallButton("Remove")) Erase(i); }
            if (!m_rebuild) GetWidgets()[row].first->Draw();
            ImGui::EndDisabled(); ImGui::PopID(); if (m_rebuild) break;
        }
        ImGui::PopID();
    }
}
