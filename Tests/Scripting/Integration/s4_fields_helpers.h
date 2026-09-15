#pragma once
#pragma warning(push)
#pragma warning(disable: 4100 4201)
#include <OvCore/ECS/Components/Behaviour.h>
#include <OvUI/Widgets/InputFields/InputText.h>
#include <OvUI/Widgets/Selection/CheckBox.h>
#include <OvUI/Widgets/Layout/Group.h>
#include <tinyxml2.h>
#pragma warning(pop)
using FieldBehaviour=OvCore::ECS::Components::Behaviour;
using FieldValue=OvCore::Scripting::ManagedFieldValue;
static const OvCore::Scripting::ManagedField& Field(FieldBehaviour& b,const std::string& id) {
    for(const auto& f:b.GetScript()->ManagedFields()) if(f.id==id) return f;
    throw std::runtime_error("Missing field: "+id);
}
static FieldValue Read(FieldBehaviour& b,const std::string& id) {
    if(!b.PrepareManagedFields()) throw std::runtime_error("Preparation failed");
    auto value=b.GetScript()->ReadManagedField(Field(b,id).token); if(!value) throw std::runtime_error("Read failed: "+id); return *value;
}
static void Set(FieldBehaviour& b,const std::string& id,const std::string& text) {
    const auto& f=Field(b,id); auto value=FieldValue::Parse(f.tag,f.auxiliary,text);
    Require(value && b.EditManagedField(id,*value),"typed Inspector model write reaches managed field");
}
static std::string SaveFieldRecord(FieldBehaviour& b) {
    tinyxml2::XMLDocument doc; auto* root=doc.NewElement("behaviours");doc.InsertEndChild(root);b.SerializeRecord(doc,root);
    tinyxml2::XMLPrinter p;root->FirstChild()->Accept(&p);return p.CStr();
}
static void InspectorEdit(FieldBehaviour& b,const std::string& text) {
    OvUI::Widgets::Layout::Group root; b.OnInspector(root);
    OvUI::Widgets::InputFields::InputText* input=nullptr;
    OvUI::Widgets::Selection::CheckBox* toggle=nullptr;
    for(auto [widget,mode]:root.GetWidgets()) {
        (void)mode;
        if(auto* w=dynamic_cast<OvUI::Widgets::InputFields::InputText*>(widget);w && w->label=="##speed") input=w;
        if(auto* w=dynamic_cast<OvUI::Widgets::Selection::CheckBox*>(widget);w && w->label=="Speed") toggle=w;
    }
    Require(input && toggle,"production Inspector created live field controls");
    toggle->ValueChangedEvent.Invoke(true); Require(!input->disabled,"Inspector unlock enables input");
    input->ContentChangedEvent.Invoke(text);
    Require(Read(b,"speed").Display()==text,"real Inspector ContentChanged modifies CSharp field");
    auto stale=input->ContentChangedEvent;
    b.OnInspector(root); stale.Invoke("999");
    Require(Read(b,"speed").Display()==text,"refresh invalidates old Inspector callbacks");
    for(auto [widget,mode]:root.GetWidgets()) {
        (void)mode;
        if(auto* w=dynamic_cast<OvUI::Widgets::InputFields::InputText*>(widget);w && w->label=="##speed") input=w;
        if(auto* w=dynamic_cast<OvUI::Widgets::Selection::CheckBox*>(widget);w && w->label=="Speed") toggle=w;
    }
    auto disposedInput=input->ContentChangedEvent;auto disposedToggle=toggle->ValueChangedEvent;
    root.RemoveAllWidgets();disposedInput.Invoke("999");disposedToggle.Invoke(false);
    Require(Read(b,"speed").Display()==text,"destroyed Inspector controls invalidate retained input and toggle callbacks");
}
