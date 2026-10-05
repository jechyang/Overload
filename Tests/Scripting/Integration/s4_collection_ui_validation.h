#pragma once
#pragma warning(push)
#pragma warning(disable: 4100 4201 4324)
#include <OvUI/Core/UIManager.h>
#include <OvUI/Panels/PanelWindow.h>
#include <imgui_internal.h>
#include <baregl/ResourceExecutor.h>
#pragma warning(pop)

static std::vector<FieldValue> CollectionItems(const FieldValue& value)
{
    tinyxml2::XMLDocument doc;
    if (doc.Parse(value.text.c_str())!=tinyxml2::XML_SUCCESS) throw std::runtime_error("Invalid managed collection XML");
    std::vector<FieldValue> result;
    for (auto* node=doc.RootElement()->FirstChildElement("item");node;node=node->NextSiblingElement("item"))
    {
        FieldValue item; node->QueryUnsignedAttribute("tag",&item.tag); node->QueryUnsignedAttribute("auxiliary",&item.auxiliary);
        auto unhex=[](const char* text) { std::string output; for (size_t i=0;text && text[i];i+=2) output+=static_cast<char>(std::stoul(std::string(text+i,2),nullptr,16)); return output; };
        const auto scalar=unhex(node->Attribute("scalar"));
        if (scalar.size()!=16) throw std::runtime_error("Invalid managed item scalar");
        std::memcpy(&item.scalar,scalar.data(),16); item.text=unhex(node->FirstChildElement("text")->GetText()); result.push_back(std::move(item));
    }
    return result;
}
template<class E> static void ValidateManagedListUI(FieldBehaviour& behaviour,E& engine,const std::filesystem::path& evidence)
{
    using Input=OvCore::Helpers::ManagedFieldInput;
    using List=OvUI::Widgets::Layout::ReorderableList;
    // Match the product's Main-to-GL-owner resource dispatcher before UI startup.
    baregl::ResourceExecutor resources([&](std::function<void()> operation) {
        if (engine.render) engine.render->Invoke([&](auto&) { operation(); }); else operation();
    });
    engine.window.SetSize(820,700);
    OvUI::Core::UIManager ui(engine.window,OvUI::Styling::EStyle::DEFAULT_DARK);
    ui.EnableEditorLayoutSave(false);
    OvUI::Panels::PanelWindow panel("C# Inspector"); panel.dockable=false; panel.autoSize=false;
    panel.SetPosition({0,0}); panel.SetSize({800,680});
    behaviour.OnInspector(panel);
    auto field=[&](const char* id)->Input& { for (auto [widget,mode]:panel.GetWidgets()) { (void)mode; if (auto* input=dynamic_cast<Input*>(widget);input && input->fieldId==id) return *input; } throw std::runtime_error("Missing managed list control"); };
    OvUI::Modules::Canvas canvas; canvas.AddPanel(panel); ui.SetCanvas(canvas);
    auto draw=[&]
    {
        auto commands=ui.RecordRenderCommands();
        auto execute=[&](auto& driver) { glBindFramebuffer(GL_FRAMEBUFFER,0); glDisable(GL_SCISSOR_TEST); glClearColor(.08f,.08f,.08f,1); glClear(GL_COLOR_BUFFER_BIT); commands.Execute(driver); };
        if (engine.render) engine.render->Invoke(execute); else execute(*engine.driver);
    };
    auto show=[&](const char* id) { for (auto [widget,mode]:panel.GetWidgets()) { (void)mode; auto* input=dynamic_cast<Input*>(widget); widget->visible=input && input->fieldId==id; } draw(); draw(); draw(); };
    auto& io=ImGui::GetIO(); io.BackendFlags &= ~ImGuiBackendFlags_HasMouseHoveredViewport;
    ImGui_ImplGlfw_CursorEnterCallback(engine.window.GetGlfwWindow(),true); io.AddFocusEvent(true);
    auto center=[](List::Rect r) { return ImVec2{(r.min.x+r.max.x)*.5f,(r.min.y+r.max.y)*.5f}; };
    auto click=[&](ImVec2 p,int button=0) { io.AddMousePosEvent(p.x,p.y); draw(); draw(); io.AddMouseButtonEvent(button,true); draw(); io.AddMouseButtonEvent(button,false); draw(); draw(); draw(); };
    auto key=[&](ImGuiKey k) { io.AddKeyEvent(k,true); draw(); io.AddKeyEvent(k,false); draw(); draw(); };
    show("Labels");
    Require(field("Labels").fullWidth && field("Labels").CollectionUI()->LastLayout().rows.size()==2,"C# lists use the full-width shared Inspector list control");
    click(center(field("Labels").CollectionUI()->LastLayout().rows[1].handle));
    click(center(field("Labels").CollectionUI()->LastLayout().add));
    auto labels=CollectionItems(Read(behaviour,"Labels"));
    Require(labels.size()==3 && labels[0].text=="one" && labels[1].text=="two" && labels[2].text.empty() && field("Labels").CollectionUI()->selected==2,"C# footer plus creates a typed default and selects it through the live host");
    click(center(field("Labels").CollectionUI()->LastLayout().remove));
    Require(CollectionItems(Read(behaviour,"Labels")).size()==2,"C# footer minus removes the selected list item through the live host");
    const auto start=center(field("Labels").CollectionUI()->LastLayout().rows[0].handle);
    io.AddMousePosEvent(start.x,start.y); draw(); draw(); io.AddMouseButtonEvent(0,true); draw(); io.AddMousePosEvent(start.x+12,start.y); draw(); draw();
    auto destination=field("Labels").CollectionUI()->LastLayout().rows[1].row;
    io.AddMousePosEvent((destination.min.x+destination.max.x)*.5f,destination.max.y-3); draw(); draw();
    Require(CollectionItems(Read(behaviour,"Labels"))[0].text=="one","C# list reorder preview does not write managed data");
    io.AddMouseButtonEvent(0,false); draw(); draw(); draw(); labels=CollectionItems(Read(behaviour,"Labels"));
    Require(labels[0].text=="two" && labels[1].text=="one" && field("Labels").CollectionUI()->selected==1,"C# drag delivery reorders exact typed elements through the live host");
    const auto textRow=field("Labels").CollectionUI()->LastLayout().rows[0].row;
    click({textRow.min.x+(textRow.max.x-textRow.min.x)*.75f,(textRow.min.y+textRow.max.y)*.5f});
    io.AddInputCharactersUTF8("A"); draw(); draw(); io.AddInputCharactersUTF8("B"); draw(); draw();
    Require(CollectionItems(Read(behaviour,"Labels"))[0].text=="twoAB" && ImGui::GetInputTextState(GImGui->ActiveId),"C# list text editing keeps keyboard focus across host XML normalization"); key(ImGuiKey_Escape);
    field("Labels").disabled=true;
    const auto beforeDisabled=Read(behaviour,"Labels").text; click(center(field("Labels").CollectionUI()->LastLayout().add));
    Require(Read(behaviour,"Labels").text==beforeDisabled,"disabled C# collection controls cannot edit managed data"); field("Labels").disabled=false; draw(); draw();
    click(center(field("Labels").CollectionUI()->LastLayout().rows[0].handle),1);
    Require(!GImGui->OpenPopupStack.empty(),"C# list item context menu uses the shared list interaction"); key(ImGuiKey_Escape);
    Require(GImGui->OpenPopupStack.empty(),"Escape closes the C# list item menu");
    show("Numbers"); click(center(field("Numbers").CollectionUI()->LastLayout().size)); io.AddInputCharactersUTF8("5"); draw(); draw();
    Require(CollectionItems(Read(behaviour,"Numbers")).size()==3,"C# array Size stays a draft before Enter"); key(ImGuiKey_Enter);
    auto numbers=CollectionItems(Read(behaviour,"Numbers"));
    Require(numbers.size()==5 && numbers[0].scalar.int32_value==1 && numbers[3].scalar.int32_value==0,"C# array Size creates a replacement array with typed defaults"); key(ImGuiKey_Escape);
    show("Modes"); click(center(field("Modes").CollectionUI()->LastLayout().add));
    const auto modes=CollectionItems(Read(behaviour,"Modes"));
    Require(modes.size()==3 && modes[0].scalar.uint64_value==UINT64_MAX && modes[2].scalar.uint64_value==0,"C# enum list editing preserves all 64 bits and exact enum defaults");
    show("NullList"); click(center(field("NullList").CollectionUI()->LastLayout().add));
    Require(CollectionItems(Read(behaviour,"NullList")).size()==1 && Read(behaviour,"NullList").text.find("null=\"0\"")!=std::string::npos,"C# plus safely creates a previously null list");
    show("Nested"); auto* nested=field("Nested").CollectionUI("root/0");
    Require(nested && nested->LastLayout().rows.size()==2,"nested C# collections have independent shared list state");
    click(center(nested->LastLayout().add));
    const auto outer=CollectionItems(Read(behaviour,"Nested"));
    Require(outer.size()==2 && CollectionItems(outer[0]).size()==3 && CollectionItems(outer[1]).size()==1,"nested C# list edits preserve sibling collections");
    show("Entries"); const auto entry=field("Entries").CollectionUI()->LastLayout().rows[0];
    click({entry.handle.max.x+18,(entry.row.min.y+entry.row.max.y)*.5f});
    auto* values=field("Entries").CollectionUI("root/0/Values");
    Require(values && values->LastLayout().rows.size()==2,"C# record list foldout presents its nested list fields");
    click(center(values->LastLayout().add));
    const auto entries=CollectionItems(Read(behaviour,"Entries")); tinyxml2::XMLDocument entryDoc; entryDoc.Parse(entries[0].text.c_str());
    bool retained=false; for (auto* member=entryDoc.RootElement()->FirstChildElement("member");member;member=member->NextSiblingElement("member"))
        if (member->Attribute("name","Weight")) retained=std::string(member->Attribute("scalar")).starts_with("0000803F");
    Require(entries.size()==2 && retained,"nested list edits inside a C# record preserve unrelated exact scalar bytes");
    io.AddMousePosEvent(-10000,-10000); draw(); draw();
    auto capture=[&]
    {
        const auto [width,height]=engine.window.GetFramebufferSize(); std::vector<uint8_t> pixels(size_t(width)*height*4); glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        std::ofstream out(evidence/(engine.render?"managed-lists-thread.ppm":"managed-lists.ppm"),std::ios::binary); out<<"P6\n"<<width<<" "<<height<<"\n255\n";
        for (int y=height-1;y>=0;--y) for (int x=0;x<width;++x) out.write(reinterpret_cast<char*>(pixels.data()+(size_t(y)*width+x)*4),3);
        Require(out.good(),"actual C# list Inspector rendering captured");
    };
    if (engine.render) engine.render->Invoke([&](auto&) { capture(); }); else capture();
    ui.RemoveCanvas();
}
