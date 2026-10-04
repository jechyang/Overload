#pragma once
#pragma warning(push)
#pragma warning(disable: 4100 4201)
#include <OvCore/ECS/Components/Behaviour.h>
#include <OvCore/Helpers/ManagedFieldInput.h>
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
    OvCore::Helpers::ManagedFieldInput* input=nullptr;
    for(auto [widget,mode]:root.GetWidgets()) {
        (void)mode;
        if(auto* w=dynamic_cast<OvCore::Helpers::ManagedFieldInput*>(widget);w && w->fieldId=="speed") input=w;
    }
    Require(input && !input->disabled,"production Inspector field is editable without an override toggle");
    const bool playing=b.owner.GetScene() && b.owner.GetScene()->IsPlaying();
    const auto saved=SaveFieldRecord(b);
    input->reset();
    Require(Read(b,"speed").Display()=="2" && (playing ? SaveFieldRecord(b)==saved : !b.HasManagedFieldOverride("speed")),"Inspector reset restores default while preserving Play/edit separation");
    input->ValueChangedEvent.Invoke(*FieldValue::Parse(10,0,text));
    Require(Read(b,"speed").Display()==text && (playing || b.HasManagedFieldOverride("speed")),"typed Inspector edit reaches the field and persists in Edit mode");
    auto stale=input->ValueChangedEvent;
    b.OnInspector(root); stale.Invoke(*FieldValue::Parse(10,0,"999"));
    Require(Read(b,"speed").Display()==text,"refresh invalidates old Inspector callbacks");
    for(auto [widget,mode]:root.GetWidgets()) {
        (void)mode;
        if(auto* w=dynamic_cast<OvCore::Helpers::ManagedFieldInput*>(widget);w && w->fieldId=="speed") input=w;
    }
    auto disposedInput=input->ValueChangedEvent;auto disposedReset=input->reset;
    root.RemoveAllWidgets();disposedInput.Invoke(*FieldValue::Parse(10,0,"999"));disposedReset();
    Require(Read(b,"speed").Display()==text,"destroyed Inspector controls invalidate retained edit and reset callbacks");
}
