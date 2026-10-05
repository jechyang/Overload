#include <OvCore/UI/DrivenProperties.h>
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <array>
using OvCore::UI::DrivenProperties;
using OvCore::UI::RectLayout;
static void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() try {
    DrivenProperties driven; RectLayout layout;
    layout.sizeDelta={120,80}; layout.anchoredPosition={7,9};
    driven.Acquire(layout,64,1,100);
    layout.sizeDelta.x=300;
    driven.Acquire(layout,128,2,200); layout.sizeDelta.y=150;
    Require(driven.Authored(layout).sizeDelta.x==120 && driven.Authored(layout).sizeDelta.y==80,"authored XML values");
    bool rejected=false;try {driven.Acquire(layout,64,2,300);}catch(const std::invalid_argument&){rejected=true;}
    Require(rejected,"overlapping controllers rejected");
    rejected=false;try {driven.Release(layout,100,64,2);}catch(const std::invalid_argument&){rejected=true;}
    Require(rejected&&layout.sizeDelta.x==300,"cross-session release rejected without mutation");
    Require(!driven.Owns(100,2)&&driven.Owns(100,1),"claim session ownership");
    driven.Acquire(layout,1,1,100); layout.anchoredPosition.x=42;
    driven.Release(layout,100,64,1);
    Require(layout.sizeDelta.x==120&&layout.sizeDelta.y==150&&layout.anchoredPosition.x==42,"partial release preserves other claims");
    driven.Release(layout,100,DrivenProperties::All,1); driven.Release(layout,200,DrivenProperties::All,2);
    Require(driven.Mask()==0&&layout.sizeDelta.y==80&&layout.anchoredPosition.x==7,"all claims restored");
    driven.Acquire(layout,64,4,400); driven.Acquire(layout,128,5,500); layout.sizeDelta={400,250};
    auto imported=driven.Authored(layout); imported.sizeDelta={180,95}; imported.pivot={.2f,.7f};
    driven.SetAuthored(layout,imported);
    Require(layout.sizeDelta==OvMaths::FVector2(400,250)&&layout.pivot==imported.pivot&&driven.Authored(layout).sizeDelta==imported.sizeDelta,"loading authored state preserves driven values and updates saved claims");
    driven.Release(layout,400,64,4); Require(layout.sizeDelta==OvMaths::FVector2(180,250),"partial release uses newly loaded authored axis");
    driven.Release(layout,500,128,5); Require(layout.sizeDelta==imported.sizeDelta&&driven.Mask()==0,"all releases restore newly loaded authored axes across sessions");
    for(uint32_t bit=0;bit<10;++bit){DrivenProperties individual;RectLayout original;individual.Acquire(original,1u<<bit,3,1);RectLayout changed=original;
        changed.anchorMin={.1f,.2f};changed.anchorMax={.8f,.9f};changed.pivot={.3f,.7f};changed.sizeDelta={1,2};changed.anchoredPosition={3,4};
        RectLayout before=changed;
        individual.Release(changed,1,1u<<bit,3);Require(individual.Mask()==0,"each property releases");
        auto values=[](const RectLayout& r){return std::array<float,10>{r.anchoredPosition.x,r.anchoredPosition.y,r.anchorMin.x,r.anchorMin.y,r.anchorMax.x,r.anchorMax.y,r.sizeDelta.x,r.sizeDelta.y,r.pivot.x,r.pivot.y};};
        auto actual=values(changed),authored=values(original),untouched=values(before);
        for(uint32_t axis=0;axis<10;++axis)Require(actual[axis]==(axis==bit?authored[axis]:untouched[axis]),"restores only the leased scalar");
    }
    for(uint32_t mask : {0u,1024u,0xffffffffu}) { bool bad=false;try{driven.Acquire(layout,mask,1,99);}catch(const std::invalid_argument&){bad=true;}Require(bad&&driven.Mask()==0,"invalid mask makes no claim"); }
    std::cout<<"PASS native driven ownership, restoration, authored serialization and session isolation\n";return 0;
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
