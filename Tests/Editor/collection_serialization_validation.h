#pragma once
#include <Reflection.generated.h>
#include <OvCore/Helpers/ReflectionCollectionField.h>

template<class Draw> static void ValidateCollections(OvEditor::Core::Context& context,OvEditor::Core::EditorActions& actions,OvEditor::Panels::Inspector& inspector,Draw draw)
{
    using namespace OvCore::Helpers::ReflectionProperties;
    using C=ReflectionFixture::CCollection;
    using Dispatcher=OvUI::Plugins::DataDispatcher<CollectionValue>;
    using Menu=OvEditor::Panels::InspectorBindings::PropertyMenu;
    context.sceneManager.LoadEmptyScene(); inspector.SetLocked(false); inspector.SetSearchFilter("");
    auto scene=[&] { return context.sceneManager.GetCurrentScene(); };
    auto id=scene()->CreateActor("Collection A").GetGUID();
    auto actor=[&] { return scene()->FindActorByGUID(id); };
    actor()->SetActive(false); actor()->AddComponent<C>();
    auto component=[&] { return actor()->GetComponent<C>(); };
    tinyxml2::XMLDocument doc;
    auto load=[&](C& c,const std::string& xml) { doc.Clear(); if (doc.Parse(xml.c_str())!=tinyxml2::XML_SUCCESS) throw std::runtime_error("Bad collection fixture"); c.OnDeserialize(doc,doc.RootElement()); };
    auto save=[&](C& c) { doc.Clear(); auto* root=doc.NewElement("data"); doc.InsertEndChild(root); c.OnSerialize(doc,root); };
    auto rejects=[&](const std::string& xml) { try { load(*component(),xml); } catch (const std::runtime_error&) { return true; } return false; };
    load(*component(),"<data><fixed><item>7</item><item>8</item><item>9</item></fixed><names><item></item><item>a &amp; b</item></names><flags><item>false</item><item>true</item></flags><modes><item>18446744073709551615</item><item>42</item></modes><records><item><weight>2.5</weight><position><x>7</x></position></item></records></data>");
    Require(component()->fixed==std::array<int,3>{7,8,9} && component()->names==std::vector<std::string>{"","a & b"} && !component()->flags[0] && component()->flags[1] && component()->records.size()==1 && component()->records[0].position==OvMaths::FVector3(7,2,3),"generated collections load scalars, vector<bool>, exact enums and nested partial records");
    save(*component());
    auto& clone=scene()->CreateActor("Collection clone").AddComponent<C>(); clone.OnDeserialize(doc,doc.RootElement());
    Require(clone.fixed==component()->fixed && clone.names==component()->names && clone.flags==component()->flags && clone.modes==component()->modes && clone.records[0].weight==2.5f,"generated collection XML round trips escaped strings, empty items and unknown 64-bit enum values");
    load(*component(),"<data><records><item><texture>__missing__/nested.png</texture></item></records></data>"); save(*component());
    Require(!component()->records[0].texture.resource && component()->records[0].texture.path=="__missing__/nested.png" &&
        std::string(doc.RootElement()->FirstChildElement("records")->FirstChildElement("item")->FirstChildElement("texture")->GetText())=="__missing__/nested.png",
        "Record list elements preserve unresolved manager-owned asset identities through typed codecs");
    for (auto xml:{"<data><fixed><item>1</item></fixed></data>","<data><fixed><item>1</item><item>2</item><item>3</item><item>4</item></fixed></data>",
        "<data><names><wrong>bad</wrong></names></data>","<data><flags>junk<item>true</item></flags></data>",
        "<data><names><item>changed</item></names><records><item><weight>NaN</weight></item></records></data>",
        "<data><flags><item>1</item><item>0</item><item>1</item><item>0</item><item>1</item></flags></data>",
        "<data><modes><item>18446744073709551616</item></modes></data>"})
        Require(rejects(xml) && component()->fixed[0]==7 && component()->names[1]=="a & b" && component()->records[0].weight==2.5f,"malformed or oversized collections reject the complete component before any setter");
    load(*component(),"<data><names/></data>"); Require(component()->names.empty() && component()->flags.size()==2,"present empty list clears while missing collections retain their values");
    component()->names={"one","two"}; component()->records={{},{}};
    component()->records[0].weight=std::numeric_limits<float>::infinity();
    bool invalidSave=false; try { save(*component()); } catch (const std::runtime_error&) { invalidSave=true; }
    Require(invalidSave && !doc.RootElement()->FirstChild(),"invalid collection element rejects serialization without publishing partial XML"); component()->records[0].weight=1;
    actions.SelectActor(*actor()); inspector.SetSize({540,850}); draw(); draw();
    auto field=[&](const char* name)->CollectionField& { for (auto* f:Widgets<CollectionField>(inspector)) if (f->Name()==name) return *f; throw std::runtime_error(std::string("Missing collection field: ")+name); };
    Require(Widgets<CollectionField>(inspector).size()==7 && field("Records").GetWidgets().size()==2,"actual Inspector uses generated collection controls and nested record drawers");
    actions.ClearHistory();
    Require(field("Names").Duplicate(0) && component()->names==std::vector<std::string>{"one","one","two"},"Inspector duplicates a list item next to its source");
    draw(); draw(); actions.CommitSceneEdit(true);
    Require(actions.UndoCount()==1 && actions.Undo() && component()->names==std::vector<std::string>{"one","two"},"list structural edit creates one reversible scene history operation"); draw(); draw();
    Require(actions.Redo() && component()->names.size()==3,"redo restores a generated list through the scene factory"); draw(); draw();
    Require(field("Names").Move(2,0) && component()->names.front()=="two","Inspector reorders list values"); draw(); draw();
    Require(field("Names").Erase(1) && component()->names==std::vector<std::string>{"two","one"},"Inspector removes a list element"); draw(); draw();
    Require(!field("Fixed Numbers").Resize(4) && !field("Read Only").Move(0,1) && !field("Flags").Resize(5),"fixed, read-only and bounded collection controls reject unsupported changes");
    auto* number=Widgets<OvUI::Widgets::Drags::DragSingleScalar<int>>(field("Fixed Numbers")).front();
    auto stale=*number->GetPlugin<OvUI::Plugins::DataDispatcher<int>>();
    Require(field("Fixed Numbers").Move(0,2),"fixed arrays support reordering while keeping their length");
    const auto fixed=component()->fixed; stale.NotifyChange(); stale.Provide(99);
    Require(component()->fixed==fixed,"copied element callback expires immediately after structural reordering"); draw(); draw();
    auto* names=field("Names").GetPlugin<Menu>(); Require(names && names->actions->hasDefault && names->actions->copy(),"collection property menu captures constructor defaults and an owned clipboard");
    Require(!field("Modes").GetPlugin<Menu>()->actions->canPaste(),"collection clipboard rejects a different element type");
    auto secondID=scene()->CreateActor("Collection B").GetGUID(); scene()->FindActorByGUID(secondID)->SetActive(false); scene()->FindActorByGUID(secondID)->AddComponent<C>();
    auto other=[&] { return scene()->FindActorByGUID(secondID)->GetComponent<C>(); };
    component()->records={{},{}}; other()->records={{},{}}; other()->records[0].weight=8; other()->records[0].position={10,20,30}; other()->records[1].weight=4;
    actions.SetSelection({actor(),scene()->FindActorByGUID(secondID)}); draw(); draw(); actions.ClearHistory();
    Require(field("Records").MixedValues() && !field("Records").MixedShape(),"equal-length collections expose mixed values without disabling element editing");
    auto* weight=Widgets<OvUI::Widgets::Drags::DragSingleScalar<float>>(field("Records")).front()->GetPlugin<OvUI::Plugins::DataDispatcher<float>>();
    weight->NotifyChange(); weight->Provide(5); draw(); draw(); actions.CommitSceneEdit(true);
    Require(component()->records[0].weight==5 && other()->records[0].weight==5 && other()->records[0].position==OvMaths::FVector3(10,20,30) && other()->records[1].weight==4,"multi-edit changes only the selected record leaf and preserves secondary elements and coordinates");
    Require(actions.Undo() && component()->records[0].weight==1 && other()->records[0].weight==8,"undo restores distinct record lists for every selected entity"); draw(); draw();
    auto* position=Widgets<OvUI::Widgets::Drags::DragMultipleScalars<float,3>>(field("Records")).front()->GetPlugin<OvUI::Plugins::DataDispatcher<std::array<float,3>>>();
    position->NotifyChange(); position->Provide({9,2,3});
    Require(component()->records[0].position==OvMaths::FVector3(9,2,3) && other()->records[0].position==OvMaths::FVector3(9,20,30),"multi-edit preserves unedited axes inside a list record"); draw(); draw();
    other()->records.resize(1); draw();
    Require(field("Records").MixedShape() && !field("Records").Erase(0) && field("Records").Resize(3) && component()->records.size()==3 && other()->records.size()==3,"mixed list lengths disable index edits while Size safely unifies all targets"); draw(); draw();
    Require(field("Names").GetPlugin<Menu>()->actions->paste() && component()->names==std::vector<std::string>{"two","one"} && other()->names==component()->names,"collection clipboard replaces the whole list across selected targets"); draw(); draw();
    Require(field("Names").GetPlugin<Menu>()->actions->reset() && component()->names==std::vector<std::string>{"one","two"} && other()->names==component()->names,"collection reset restores constructor defaults on all targets"); draw(); draw();
    actions.SelectActor(*actor()); component()->records.resize(40); draw(); draw();
    Require(field("Records").GetWidgets().size()==32,"large collection Inspector builds only one page of element controls");
    inspector.SetSearchFilter("records weight"); draw(); Require(field("Records").visible && !field("Names").visible,"collection search matches group names and nested field names"); inspector.SetSearchFilter(""); draw();
    auto capture=[&](const char* prefix) {
        const auto [width,height]=context.window->GetFramebufferSize(); std::vector<uint8_t> pixels(size_t(width)*height*4); glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        std::ofstream out(std::string(prefix)+(context.renderThread?"-thread.ppm":".ppm"),std::ios::binary); out<<"P6\n"<<width<<" "<<height<<"\n255\n";
        for (int y=height-1;y>=0;--y) for (int x=0;x<width;++x) out.write(reinterpret_cast<char*>(pixels.data()+(size_t(y)*width+x)*4),3); Require(out.good(),"actual generated collection Inspector captured"); };
    for (auto [filter,prefix]:{std::pair{"records","collection-records"},std::pair{"collection fixture","collections"}})
    { inspector.SetSearchFilter(filter); draw(); draw(); if (context.renderThread) context.renderThread->Invoke([&](auto&) { capture(prefix); }); else capture(prefix); }
    inspector.SetSearchFilter("");
    OvUI::Internal::WidgetContainer standalone; component()->OnInspector(standalone);
    auto* control=Widgets<CollectionField>(standalone).front(); auto retained=*control->GetPlugin<Dispatcher>(); auto previous=retained.Gather();
    Require(std::async(std::launch::async,[&] { retained.NotifyChange(); retained.Provide(previous); return !retained.Gather().property; }).get(),"collection provider rejects worker access before borrowing targets");
    standalone.RemoveAllWidgets(); retained.NotifyChange(); retained.Provide(previous); Require(!retained.Gather().property,"copied collection dispatcher expires with its widget");
    OvUI::Internal::WidgetContainer alive; component()->OnInspector(alive); auto dead=*Widgets<CollectionField>(alive).front()->GetPlugin<Dispatcher>();
    context.sceneManager.LoadEmptyScene(); dead.NotifyChange(); dead.Provide(previous); Require(!dead.Gather().property,"collection bindings expire safely after scene replacement");
}
