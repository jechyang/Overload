#include <OvCore/Helpers/ReflectionCollectionField.h>
#include <OvUI/Widgets/Layout/GroupCollapsable.h>
#include <OvUI/Widgets/Layout/Columns.h>
#include <OvUI/Widgets/Texts/TextColored.h>
#include <OvUI/Styling/Style.h>
#include <imgui_internal.h>
#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <atomic>

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
        static std::atomic<uint64_t> nextOwner{1}; m_dragOwner=nextOwner++;
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
        if (edited==value && !m_mixedValues) return false;
        edited.patch=std::move(operation); dispatcher->NotifyChange(); dispatcher->Provide(edited);
        m_value=dispatcher->Gather(); m_seen=m_value.canonical;
        if (structural) { m_items.Invalidate(); ++m_itemEpoch; m_rebuild=true; }
        return true;
    }
    bool CollectionField::Resize(size_t size)
    {
        auto* codec=FindValueCodec(*m_binding.property); if (!codec || !codec->collection || !codec->collection->dynamic || size>CollectionLimit(*m_binding.property)) return false;
        return Edit([ops=codec->collection,size,mixed=m_mixedShape](std::any& data) { return (mixed || ops->size(data)!=size) && ops->resize(data,size); },true);
    }
    bool CollectionField::Erase(size_t index)
    {
        if (m_mixedShape) return false; auto* ops=FindValueCodec(*m_binding.property)->collection;
        if (!Edit([ops,index](std::any& data) { return ops->erase(data,index); },true)) return false;
        if (m_selected!=NoSelection && m_selected>index) --m_selected;
        return true;
    }
    bool CollectionField::Move(size_t from,size_t to)
    {
        if (m_mixedShape || from==to) return false; auto* ops=FindValueCodec(*m_binding.property)->collection;
        if (!Edit([ops,from,to](std::any& data) { return ops->move(data,from,to); },true)) return false;
        m_selected=to; m_page=to/32; return true;
    }
    bool CollectionField::Duplicate(size_t index)
    { if (m_mixedShape) return false; auto* ops=FindValueCodec(*m_binding.property)->collection; auto limit=CollectionLimit(*m_binding.property);
        if (!Edit([ops,index,limit](std::any& data) { std::any item; auto size=ops->size(data); return size<limit && ops->get(data,index,item) && ops->resize(data,size+1) && ops->set(data,size,item) && ops->move(data,size,index+1); },true)) return false;
        m_selected=index+1; m_page=m_selected/32; return true; }
    void CollectionField::Rebuild()
    {
        m_items.Invalidate(); ++m_itemEpoch; RemoveAllWidgets(); m_rebuild=false; m_seen=m_value.canonical;
        if (!m_value.ops) return;
        auto size=m_value.ops->size(m_value.data); m_page=std::min(m_page,size? (size-1)/32:size_t{});
        if (!size) m_selected=NoSelection;
        else if (m_selected!=NoSelection) m_selected=std::min(m_selected,size-1);
        auto ticket=m_items.Ticket(); auto* main=&m_registry.Main(); const auto* ops=m_value.ops;
        for (size_t i=m_page*32;i<std::min(size,(m_page+1)*32);++i)
        {
            auto& group=CreateWidget<OvUI::Widgets::Layout::Group>();
            group.SetID("collection_item_"+m_binding.identity+std::string(m_binding.property->Info().id)+"/"+std::to_string(i));
            auto& columns=group.CreateWidget<OvUI::Widgets::Layout::Columns<2>>(); columns.widths[0]=90*OVUI_SCALE;
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
            for (auto [widget,mode]:columns.GetWidgets())
            {
                if (auto* title=dynamic_cast<OvUI::Widgets::Texts::TextColored*>(widget)) title->color=OVUI_STYLE(TextDisabled);
                if (auto* record=dynamic_cast<OvUI::Widgets::Layout::GroupCollapsable*>(widget)) record->defaultExpanded=false;
            }
        }
    }
    namespace
    {
        constexpr const char* ItemPayload = "OV_REFLECTION_LIST_ITEM";
        struct DragItem { uint64_t owner, epoch; size_t index; };
        CollectionField::Rect ItemRect() { return {ImGui::GetItemRectMin(),ImGui::GetItemRectMax()}; }
    }
    void CollectionField::DrawItemMenu(size_t index,size_t size)
    {
        if (!ImGui::BeginPopup("item_menu")) return;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::TextDisabled("Element %zu",index); ImGui::Separator();
        const bool editable=!disabled && !m_mixedShape;
        if (m_value.ops->dynamic)
        {
            if (ImGui::MenuItem("Duplicate",nullptr,false,editable && size<CollectionLimit(*m_binding.property))) Duplicate(index);
            if (ImGui::MenuItem("Delete",nullptr,false,editable)) Erase(index);
            ImGui::Separator();
        }
        if (ImGui::MenuItem("Move Up",nullptr,false,editable && index>0)) Move(index,index-1);
        if (ImGui::MenuItem("Move Down",nullptr,false,editable && index+1<size)) Move(index,index+1);
        if (m_value.ops->dynamic)
        {
            ImGui::Separator();
            if (ImGui::MenuItem("Clear List",nullptr,false,editable && size>0)) Resize(0);
        }
        ImGui::EndPopup();
    }
    void CollectionField::DrawRows(size_t size)
    {
        const float scale=OVUI_SCALE;
        const bool editable=!disabled && !m_mixedShape;
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,{4*scale,2*scale});
        // Keep the list footer reachable even when many record elements are expanded.
        ImGui::SetNextWindowSizeConstraints({0,0},{FLT_MAX,ImGui::GetFrameHeightWithSpacing()*9});
        ImGui::PushStyleColor(ImGuiCol_ChildBg,ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        if (ImGui::BeginChild("items",{0,0},ImGuiChildFlags_Borders|ImGuiChildFlags_AutoResizeY,ImGuiWindowFlags_NoSavedSettings))
        {
            if (!size)
            {
                ImGui::Dummy({0,8*scale}); ImGui::TextDisabled("List is empty"); ImGui::Dummy({0,8*scale});
            }
            else if (!m_rebuild && ImGui::BeginTable("rows",2,ImGuiTableFlags_NoSavedSettings|ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("handle",ImGuiTableColumnFlags_WidthFixed,18*scale);
                ImGui::TableSetupColumn("value",ImGuiTableColumnFlags_WidthStretch);
                for (size_t row=0;row<GetWidgets().size();++row)
                {
                    auto* widget=GetWidgets()[row].first;
                    if (!widget->enabled || !widget->visible) continue;
                    const size_t index=m_page*32+row;
                    ImGui::PushID(static_cast<int>(index)); ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                    auto start=ImGui::GetCursorScreenPos();
                    ImGui::BeginDisabled(!editable);
                    if (ImGui::InvisibleButton("grip",{18*scale,ImGui::GetFrameHeight()})) m_selected=index;
                    const auto grip=ItemRect();
                    if (ImGui::IsItemActivated()) m_selected=index;
                    const bool hover=ImGui::IsItemHovered();
                    auto* painter=ImGui::GetWindowDrawList();
                    const auto color=ImGui::GetColorU32(hover ? ImGuiCol_Text : ImGuiCol_TextDisabled);
                    for (int y=0;y<3;++y) for (int x=0;x<2;++x)
                        painter->AddCircleFilled({grip.min.x+(6+x*4)*scale,(grip.min.y+grip.max.y)*.5f+(y-1)*4*scale},scale,color);
                    if (hover) { ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll); ImGui::SetTooltip("Drag to reorder. Right-click for item actions."); }
                    if (editable && ImGui::BeginDragDropSource(ImGuiDragDropFlags_PayloadAutoExpire|ImGuiDragDropFlags_PayloadNoCrossContext|ImGuiDragDropFlags_PayloadNoCrossProcess))
                    {
                        const DragItem item{m_dragOwner,m_itemEpoch,index};
                        ImGui::SetDragDropPayload(ItemPayload,&item,sizeof(item),ImGuiCond_Once);
                        ImGui::Text("%s / Element %zu",m_name.c_str(),index); ImGui::EndDragDropSource();
                    }
                    ImGui::EndDisabled();
                    ImGui::TableSetColumnIndex(1);
                    ImGui::BeginGroup(); ImGui::BeginDisabled(m_mixedShape);
                    // Record headers are foldouts within the row, rather than another filled section bar.
                    ImGui::PushStyleColor(ImGuiCol_Header,{0,0,0,0});
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,{1,1,1,.06f});
                    ImGui::PushStyleColor(ImGuiCol_HeaderActive,{1,1,1,.1f});
                    widget->Draw(); ImGui::PopStyleColor(3); ImGui::EndDisabled(); ImGui::EndGroup();
                    const auto body=ItemRect();
                    const ImRect bounds({start.x-4*scale,start.y-2*scale},
                        {body.max.x+4*scale,std::max(body.max.y,grip.max.y)+2*scale});
                    m_layout.rows.push_back({index,{bounds.Min,bounds.Max},grip});
                    // Table column clipping must not exclude the grip from row hit tests.
                    const auto clip=ImGui::GetCurrentWindow()->InnerClipRect;
                    ImGui::PushClipRect(clip.Min,clip.Max,false);
                    const bool rowHover=ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseHoveringRect(bounds.Min,bounds.Max);
                    if (rowHover && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))) m_selected=index;
                    if (rowHover && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup("item_menu");
                    auto background=ImGui::GetStyleColorVec4(ImGuiCol_Header);
                    background.w=index==m_selected ? .8f : (row%2 ? .18f : 0);
                    if (rowHover && index!=m_selected) background.w=.35f;
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,ImGui::GetColorU32(background));
                    if (editable && ImGui::BeginDragDropTargetCustom(bounds,ImGui::GetID("drop")))
                    {
                        if (const auto* payload=ImGui::AcceptDragDropPayload(ItemPayload,ImGuiDragDropFlags_AcceptBeforeDelivery|ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
                            payload && payload->DataSize==sizeof(DragItem))
                        {
                            const auto item=*static_cast<const DragItem*>(payload->Data);
                            if (item.owner==m_dragOwner && item.epoch==m_itemEpoch && item.index<size)
                            {
                                const bool below=ImGui::GetMousePos().y>(bounds.Min.y+bounds.Max.y)*.5f;
                                const size_t gap=index+(below?1:0), destination=gap-(item.index<gap?1:0);
                                if (destination!=item.index)
                                {
                                    const float y=below ? bounds.Max.y : bounds.Min.y;
                                    painter->AddLine({bounds.Min.x,y},{bounds.Max.x,y},ImGui::GetColorU32(ImGuiCol_DragDropTarget),2*scale);
                                    if (payload->IsDelivery()) Move(item.index,destination);
                                }
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                    DrawItemMenu(index,size);
                    ImGui::PopClipRect();
                    ImGui::PopID();
                    // An operation expires the index epoch immediately, but destruction is deferred.
                    if (m_rebuild) break;
                }
                ImGui::EndTable();
            }
        }
        ImGui::EndChild(); ImGui::PopStyleColor(); ImGui::PopStyleVar();
    }
    void CollectionField::DrawFooter(size_t size)
    {
        const float height=ImGui::GetFrameHeight(), gap=ImGui::GetStyle().ItemSpacing.x;
        const auto start=ImGui::GetCursorScreenPos(); const float width=ImGui::GetContentRegionAvail().x;
        ImGui::AlignTextToFramePadding();
        if (size>32)
        {
            ImGui::BeginDisabled(m_page==0); if (ImGui::ArrowButton("previous",ImGuiDir_Left)) { --m_page; m_rebuild=true; } ImGui::EndDisabled();
            ImGui::SameLine(); ImGui::TextDisabled("%zu-%zu of %zu",m_page*32+1,std::min(size,(m_page+1)*32),size);
            ImGui::SameLine(); ImGui::BeginDisabled((m_page+1)*32>=size); if (ImGui::ArrowButton("next",ImGuiDir_Right)) { ++m_page; m_rebuild=true; } ImGui::EndDisabled();
        }
        else ImGui::TextDisabled("%zu %s%s",size,size==1?"item":"items",m_value.ops->dynamic?"":" (fixed)");
        if (m_value.ops->dynamic)
        {
            ImGui::SameLine(); ImGui::SetCursorScreenPos({std::max(ImGui::GetCursorScreenPos().x,start.x+width-2*height-gap),start.y});
            ImGui::BeginDisabled(m_mixedShape || size>=CollectionLimit(*m_binding.property));
            if (ImGui::Button("+##add",{height,height}) && Resize(size+1)) { m_selected=size; m_page=size/32; }
            m_layout.add=ItemRect();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Add element (maximum %zu)",CollectionLimit(*m_binding.property));
            ImGui::EndDisabled(); ImGui::SameLine();
            ImGui::BeginDisabled(m_mixedShape || m_selected==NoSelection || m_selected>=size);
            if (ImGui::Button("-##remove",{height,height})) Erase(m_selected);
            m_layout.remove=ItemRect();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Remove selected element");
            ImGui::EndDisabled();
        }
    }
    void CollectionField::_Draw_Impl()
    {
        m_layout={};
        if (!m_registry.Main().IsCurrent()) return;
        if (m_value.canonical!=m_seen) { m_items.Invalidate(); m_rebuild=true; }
        if (m_rebuild) Rebuild();
        if (!m_value.ops) { ImGui::TextDisabled("Collection unavailable"); return; }
        const auto size=m_value.ops->size(m_value.data);
        ImGui::PushID(m_widgetID.c_str());
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,{0,0});
        bool open=false;
        if (ImGui::BeginTable("header",2,ImGuiTableFlags_NoSavedSettings|ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("label",ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("size",ImGuiTableColumnFlags_WidthFixed,58*OVUI_SCALE);
            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
            auto label=m_name+(m_mixedValues?" (Mixed)":"")+"###foldout";
            auto* storage=ImGui::GetStateStorage(); auto id=ImGui::GetID(label.c_str()); auto previous=storage->GetInt(id,1);
            if (expandForSearch) ImGui::SetNextItemOpen(true,ImGuiCond_Always);
            open=ImGui::CollapsingHeader(label.c_str(),ImGuiTreeNodeFlags_DefaultOpen);
            m_layout.header=ItemRect();
            if (expandForSearch) storage->SetInt(id,previous);
            ExecutePlugins(OvUI::Plugins::EPluginExecutionContext::WIDGET);
            ImGui::TableSetColumnIndex(1);
            if (m_value.ops->dynamic)
            {
                if (m_lastSize!=size) { std::snprintf(m_sizeText.data(),m_sizeText.size(),"%zu",size); m_lastSize=size; m_sizeInvalid=false; }
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (m_mixedShape) ImGui::PushItemFlag(ImGuiItemFlags_MixedValue,true);
                if (ImGui::InputText("##size",m_sizeText.data(),m_sizeText.size(),ImGuiInputTextFlags_EnterReturnsTrue|ImGuiInputTextFlags_CharsDecimal|ImGuiInputTextFlags_AutoSelectAll))
                {
                    size_t count{}; auto* end=m_sizeText.data()+std::char_traits<char>::length(m_sizeText.data()); auto parsed=std::from_chars(m_sizeText.data(),end,count);
                    m_sizeInvalid=parsed.ec!=std::errc{} || parsed.ptr!=end || count>CollectionLimit(*m_binding.property);
                    if (!m_sizeInvalid) Resize(count);
                }
                if (m_mixedShape) ImGui::PopItemFlag();
                m_layout.size=ItemRect();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Size: press Enter to apply (0..%zu)",CollectionLimit(*m_binding.property));
            }
            else { ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("%zu",size); }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
        if (open)
        {
            if (m_sizeInvalid) ImGui::TextColored({1,.45f,.35f,1},"Size must be an integer from 0 to %zu",CollectionLimit(*m_binding.property));
            if (m_mixedShape) ImGui::TextDisabled("Mixed lengths. Set Size to edit elements.");
            DrawRows(size); DrawFooter(m_value.ops->size(m_value.data));
        }
        ImGui::PopID();
    }
}
