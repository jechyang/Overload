#include <OvUI/Widgets/Layout/ReorderableList.h>
#include <OvUI/Styling/Style.h>
#include <imgui_internal.h>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstdio>

namespace OvUI::Widgets::Layout
{
    ReorderableList::ReorderableList()
    { static std::atomic<uint64_t> nextOwner{1}; m_owner=nextOwner++; }
    void ReorderableList::Clamp(size_t size)
    {
        page=std::min(page,size?(size-1)/PageSize:size_t{});
        if (!size) selected=NoSelection;
        else if (selected!=NoSelection) selected=std::min(selected,size-1);
    }
    bool ReorderableList::Resize(const Model& model,size_t size)
    {
        if (model.readOnly || !model.resizable || size>model.limit || !model.resize || !model.resize(size)) return false;
        Invalidate(); m_structureChanged=true; Clamp(model.count()); return true;
    }
    bool ReorderableList::Erase(const Model& model,size_t index)
    {
        const auto previous=selected;
        if (model.readOnly || model.mixedShape || !model.resizable || !model.erase || !model.erase(index)) return false;
        selected=previous!=NoSelection && previous>index ? previous-1 : previous;
        Invalidate(); m_structureChanged=true; Clamp(model.count()); return true;
    }
    bool ReorderableList::Move(const Model& model,size_t from,size_t to)
    {
        if (model.readOnly || model.mixedShape || from==to || !model.move || !model.move(from,to)) return false;
        selected=to; page=to/PageSize; Invalidate(); m_structureChanged=true; return true;
    }
    bool ReorderableList::Duplicate(const Model& model,size_t index)
    {
        if (model.readOnly || model.mixedShape || !model.resizable || model.count()>=model.limit || !model.duplicate || !model.duplicate(index)) return false;
        selected=index+1; page=selected/PageSize; Invalidate(); m_structureChanged=true; return true;
    }
    namespace
    {
        constexpr const char* ItemPayload = "OV_REORDERABLE_LIST_ITEM";
        struct DragItem { uint64_t owner, epoch; size_t index; };
        ReorderableList::Rect ItemRect() { return {ImGui::GetItemRectMin(),ImGui::GetItemRectMax()}; }
    }
    void ReorderableList::DrawItemMenu(const Model& model,size_t index,size_t size)
    {
        if (!ImGui::BeginPopup("item_menu")) return;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::TextDisabled("Element %zu",index); ImGui::Separator();
        const bool editable=!model.readOnly && !model.mixedShape && !(GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled);
        if (model.resizable)
        {
            if (ImGui::MenuItem("Duplicate",nullptr,false,editable && size<model.limit)) Duplicate(model,index);
            if (ImGui::MenuItem("Delete",nullptr,false,editable)) Erase(model,index);
            ImGui::Separator();
        }
        if (ImGui::MenuItem("Move Up",nullptr,false,editable && index>0)) Move(model,index,index-1);
        if (ImGui::MenuItem("Move Down",nullptr,false,editable && index+1<size)) Move(model,index,index+1);
        if (model.resizable)
        {
            ImGui::Separator();
            if (ImGui::MenuItem("Clear List",nullptr,false,editable && size>0)) Resize(model,0);
        }
        ImGui::EndPopup();
    }
    void ReorderableList::DrawRows(const Model& model,size_t size)
    {
        const float scale=OVUI_SCALE;
        const bool editable=!model.readOnly && !model.mixedShape && !(GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled);
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,{4*scale,2*scale});
        // Keep the list footer reachable even when many record elements are expanded.
        ImGui::SetNextWindowSizeConstraints({0,0},{FLT_MAX,ImGui::GetFrameHeightWithSpacing()*9});
        ImGui::PushStyleColor(ImGuiCol_ChildBg,ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        if (ImGui::BeginChild("items",{0,0},ImGuiChildFlags_Borders|ImGuiChildFlags_AutoResizeY,ImGuiWindowFlags_NoSavedSettings))
        {
            if (!size)
            {
                ImGui::Dummy({0,8*scale}); ImGui::TextDisabled("%s",model.emptyText.c_str()); ImGui::Dummy({0,8*scale});
            }
            else if (!StopDrawing(model) && ImGui::BeginTable("rows",2,ImGuiTableFlags_NoSavedSettings|ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("handle",ImGuiTableColumnFlags_WidthFixed,18*scale);
                ImGui::TableSetupColumn("value",ImGuiTableColumnFlags_WidthStretch);
                for (size_t index=page*PageSize;index<std::min(size,(page+1)*PageSize);++index)
                {
                    if (model.visible && !model.visible(index)) continue;
                    const size_t row=index-page*PageSize;
                    ImGui::PushID(static_cast<int>(index)); ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                    auto start=ImGui::GetCursorScreenPos();
                    ImGui::BeginDisabled(!editable);
                    if (ImGui::InvisibleButton("grip",{18*scale,ImGui::GetFrameHeight()})) selected=index;
                    const auto grip=ItemRect();
                    if (ImGui::IsItemActivated()) selected=index;
                    const bool hover=ImGui::IsItemHovered();
                    auto* painter=ImGui::GetWindowDrawList();
                    const auto color=ImGui::GetColorU32(hover ? ImGuiCol_Text : ImGuiCol_TextDisabled);
                    for (int y=0;y<3;++y) for (int x=0;x<2;++x)
                        painter->AddCircleFilled({grip.min.x+(6+x*4)*scale,(grip.min.y+grip.max.y)*.5f+(y-1)*4*scale},scale,color);
                    if (hover) { ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll); ImGui::SetTooltip("Drag to reorder. Right-click for item actions."); }
                    if (editable && ImGui::BeginDragDropSource(ImGuiDragDropFlags_PayloadAutoExpire|ImGuiDragDropFlags_PayloadNoCrossContext|ImGuiDragDropFlags_PayloadNoCrossProcess))
                    {
                        const DragItem item{m_owner,m_epoch,index};
                        ImGui::SetDragDropPayload(ItemPayload,&item,sizeof(item),ImGuiCond_Once);
                        ImGui::Text("%s / Element %zu",model.name.c_str(),index); ImGui::EndDragDropSource();
                    }
                    ImGui::EndDisabled();
                    ImGui::TableSetColumnIndex(1);
                    ImGui::BeginGroup(); ImGui::BeginDisabled(model.mixedShape);
                    // Record headers are foldouts within the row, rather than another filled section bar.
                    ImGui::PushStyleColor(ImGuiCol_Header,{0,0,0,0});
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,{1,1,1,.06f});
                    ImGui::PushStyleColor(ImGuiCol_HeaderActive,{1,1,1,.1f});
                    model.drawElement(index); ImGui::PopStyleColor(3); ImGui::EndDisabled(); ImGui::EndGroup();
                    const auto body=ItemRect();
                    const ImRect bounds({start.x-4*scale,start.y-2*scale},
                        {body.max.x+4*scale,std::max(body.max.y,grip.max.y)+2*scale});
                    m_layout.rows.push_back({index,{bounds.Min,bounds.Max},grip});
                    // Table column clipping must not exclude the grip from row hit tests.
                    const auto clip=ImGui::GetCurrentWindow()->InnerClipRect;
                    ImGui::PushClipRect(clip.Min,clip.Max,false);
                    const bool rowHover=ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseHoveringRect(bounds.Min,bounds.Max);
                    if (rowHover && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))) selected=index;
                    if (rowHover && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup("item_menu");
                    auto background=ImGui::GetStyleColorVec4(ImGuiCol_Header);
                    background.w=index==selected ? .8f : (row%2 ? .18f : 0);
                    if (rowHover && index!=selected) background.w=.35f;
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,ImGui::GetColorU32(background));
                    if (editable && ImGui::BeginDragDropTargetCustom(bounds,ImGui::GetID("drop")))
                    {
                        if (const auto* payload=ImGui::AcceptDragDropPayload(ItemPayload,ImGuiDragDropFlags_AcceptBeforeDelivery|ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
                            payload && payload->DataSize==sizeof(DragItem))
                        {
                            const auto item=*static_cast<const DragItem*>(payload->Data);
                            if (item.owner==m_owner && item.epoch==m_epoch && item.index<size)
                            {
                                const bool below=ImGui::GetMousePos().y>(bounds.Min.y+bounds.Max.y)*.5f;
                                const size_t gap=index+(below?1:0), destination=gap-(item.index<gap?1:0);
                                if (destination!=item.index)
                                {
                                    const float y=below ? bounds.Max.y : bounds.Min.y;
                                    painter->AddLine({bounds.Min.x,y},{bounds.Max.x,y},ImGui::GetColorU32(ImGuiCol_DragDropTarget),2*scale);
                                    if (payload->IsDelivery()) Move(model,item.index,destination);
                                }
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                    DrawItemMenu(model,index,size);
                    ImGui::PopClipRect();
                    ImGui::PopID();
                    // An operation expires the index epoch immediately, but destruction is deferred.
                    if (StopDrawing(model)) break;
                }
                ImGui::EndTable();
            }
        }
        ImGui::EndChild(); ImGui::PopStyleColor(); ImGui::PopStyleVar();
    }
    void ReorderableList::DrawFooter(const Model& model,size_t size)
    {
        const float height=ImGui::GetFrameHeight(), gap=ImGui::GetStyle().ItemSpacing.x;
        const auto start=ImGui::GetCursorScreenPos(); const float width=ImGui::GetContentRegionAvail().x;
        ImGui::AlignTextToFramePadding();
        if (size>32)
        {
            ImGui::BeginDisabled(page==0); if (ImGui::ArrowButton("previous",ImGuiDir_Left)) { --page; m_structureChanged=true; Invalidate(); if (model.pageChanged) model.pageChanged(); } ImGui::EndDisabled();
            ImGui::SameLine(); ImGui::TextDisabled("%zu-%zu of %zu",page*32+1,std::min(size,(page+1)*32),size);
            ImGui::SameLine(); ImGui::BeginDisabled((page+1)*32>=size); if (ImGui::ArrowButton("next",ImGuiDir_Right)) { ++page; m_structureChanged=true; Invalidate(); if (model.pageChanged) model.pageChanged(); } ImGui::EndDisabled();
        }
        else ImGui::TextDisabled("%zu %s%s",size,size==1?"item":"items",model.resizable?"":" (fixed)");
        if (model.resizable)
        {
            ImGui::SameLine(); ImGui::SetCursorScreenPos({std::max(ImGui::GetCursorScreenPos().x,start.x+width-2*height-gap),start.y});
            ImGui::BeginDisabled(model.mixedShape || size>=model.limit);
            if (ImGui::Button("+##add",{height,height}) && Resize(model,size+1)) { selected=size; page=size/32; }
            m_layout.add=ItemRect();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Add element (maximum %zu)",model.limit);
            ImGui::EndDisabled(); ImGui::SameLine();
            ImGui::BeginDisabled(model.mixedShape || selected==NoSelection || selected>=size);
            if (ImGui::Button("-##remove",{height,height})) Erase(model,selected);
            m_layout.remove=ItemRect();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Remove selected element");
            ImGui::EndDisabled();
        }
    }
    void ReorderableList::Draw(const Model& model)
    {
        ClearLayout(); m_structureChanged=false;
        const auto size=model.count(); Clamp(size);
        ImGui::BeginDisabled(model.readOnly);
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,{0,0});
        bool open=false;
        if (ImGui::BeginTable("header",2,ImGuiTableFlags_NoSavedSettings|ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("label",ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("size",ImGuiTableColumnFlags_WidthFixed,58*OVUI_SCALE);
            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
            auto label=model.name+(model.mixedValues?" (Mixed)":"")+"###foldout";
            auto* storage=ImGui::GetStateStorage(); auto id=ImGui::GetID(label.c_str()); auto previous=storage->GetInt(id,1);
            if (model.expandForSearch) ImGui::SetNextItemOpen(true,ImGuiCond_Always);
            open=ImGui::CollapsingHeader(label.c_str(),ImGuiTreeNodeFlags_DefaultOpen);
            m_layout.header=ItemRect();
            if (model.expandForSearch) storage->SetInt(id,previous);
            if (model.header) model.header();
            ImGui::TableSetColumnIndex(1);
            if (model.resizable)
            {
                if (m_lastSize!=size) { std::snprintf(m_sizeText.data(),m_sizeText.size(),"%zu",size); m_lastSize=size; m_sizeInvalid=false; }
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (model.mixedShape) ImGui::PushItemFlag(ImGuiItemFlags_MixedValue,true);
                if (ImGui::InputText("##size",m_sizeText.data(),m_sizeText.size(),ImGuiInputTextFlags_EnterReturnsTrue|ImGuiInputTextFlags_CharsDecimal|ImGuiInputTextFlags_AutoSelectAll))
                {
                    size_t count{}; auto* end=m_sizeText.data()+std::char_traits<char>::length(m_sizeText.data()); auto parsed=std::from_chars(m_sizeText.data(),end,count);
                    m_sizeInvalid=parsed.ec!=std::errc{} || parsed.ptr!=end || count>model.limit;
                    if (!m_sizeInvalid) Resize(model,count);
                }
                if (model.mixedShape) ImGui::PopItemFlag();
                m_layout.size=ItemRect();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Size: press Enter to apply (0..%zu)",model.limit);
            }
            else { ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("%zu",size); }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
        if (open)
        {
            if (m_sizeInvalid) ImGui::TextColored({1,.45f,.35f,1},"Size must be an integer from 0 to %zu",model.limit);
            if (model.mixedShape) ImGui::TextDisabled("Mixed lengths. Set Size to edit elements.");
            DrawRows(model,model.count()); DrawFooter(model,model.count());
        }
        ImGui::EndDisabled();
    }
}
