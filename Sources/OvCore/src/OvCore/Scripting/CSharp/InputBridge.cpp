#include <OvCore/Scripting/CSharp/InputBridge.h>
#include <OvCore/ECS/Components/CTransform.h>
#include <OvCore/ECS/Actor.h>
#include <OvCore/Global/ServiceLocator.h>
#include <OvWindowing/Inputs/InputManager.h>
#include "OvCore/Reflection.generated.h"
#include <OvCore/UI/Input/Physics2DQuery.h>
#include <OvCore/ECS/Components/CCamera.h>
#include <OvCore/ECS/Components/CUI.h>
#include <OvCore/UI/Frame.h>
#include <OvCore/SceneSystem/Scene.h>
#include <OvCore/ECS/Components/CPhysicalObject.h>
#include <OvPhysics/Core/PhysicsEngine.h>
#include <bit>
#include <sstream>
#include <locale>

namespace OvCore::Scripting::CSharp
{
    void QueryInput(OvReflection::Script::State& state, uint64_t owner, uint32_t operation,
        uint64_t argument, const std::string& text, uint64_t& output)
    {
        using namespace OvReflection::Script;
        auto& actor = state.Get<ECS::Components::CTransform>(owner)->owner;
        Require(actor.IsAlive(), OV_SCRIPT_INVALID_HANDLE);
        if (operation == 85)
        {
            UI::Input::QueryRay ray; UI::Input::QueryShape2D shape; int kind;
            std::istringstream stream(text); stream.imbue(std::locale::classic());
            Require(bool(stream >> ray.origin.x >> ray.origin.y >> ray.origin.z >> ray.direction.x >> ray.direction.y >> ray.direction.z >> ray.distance
                >> kind >> shape.center.x >> shape.center.y >> shape.center.z >> shape.halfWidth >> shape.halfHeight >> shape.radians));
            stream >> std::ws; Require(stream.eof()); shape.kind = static_cast<UI::Input::QueryShape2D::Kind>(kind);
            const auto result = UI::Input::Raycast2D(ray,shape); output = std::bit_cast<uint64_t>(result.value_or(-1)); return;
        }
        if (operation == 86 || operation == 87)
        {
            OvMaths::FVector3 origin,direction; float distance; int mask;
            std::istringstream stream(text); stream.imbue(std::locale::classic());
            Require(bool(stream >> origin.x >> origin.y >> origin.z >> direction.x >> direction.y >> direction.z >> distance >> mask));
            stream >> std::ws; Require(stream.eof());
            auto hits = OVSERVICE(OvPhysics::Core::PhysicsEngine).RaycastAll(origin,direction,distance,mask);
            std::vector<std::pair<ECS::Actor*,OvPhysics::Core::PhysicsEngine::QueryHit>> live;
            for (const auto& hit : hits) {
                try { auto& target = hit.object->GetUserData<std::reference_wrapper<ECS::Components::CPhysicalObject>>().get().owner;
                    if (target.IsAlive() && target.IsActive() && target.GetScene() == actor.GetScene()) live.emplace_back(&target,hit);
                } catch (const std::bad_any_cast&) { }
            }
            if (operation == 86) { output = live.size(); return; }
            Require(argument/8 < live.size()); const auto& [target,hit] = live[size_t(argument/8)];
            auto pair = [](float x,float y) { return uint64_t(std::bit_cast<uint32_t>(x)) | (uint64_t(std::bit_cast<uint32_t>(y)) << 32); };
            switch (argument%8) {
                case 0: output = target->GetGUID(); break;
                case 1: output = std::bit_cast<uint64_t>(double(hit.distance)); break;
                case 2: output = pair(hit.point.x,hit.point.y); break;
                case 3: output = pair(hit.point.z,hit.normal.x); break;
                case 4: output = pair(hit.normal.y,hit.normal.z); break;
                default: Require(false);
            }
            return;
        }
        auto& input = OVSERVICE(OvWindowing::Inputs::InputManager);
        if (operation == 88)
        {
            const auto& frame = input.CaptureUIWords();
            const float width = std::bit_cast<float>(uint32_t(frame[6])), height = std::bit_cast<float>(uint32_t(frame[6] >> 32));
            const float x = std::bit_cast<float>(uint32_t(argument)), y = std::bit_cast<float>(uint32_t(argument >> 32));
            Require(std::isfinite(x) && std::isfinite(y) && width > 0 && height > 0);
            const auto* camera = actor.GetComponent<ECS::Components::CCamera>();
            if (const auto* canvas = actor.GetComponent<ECS::Components::CCanvas>()) camera = UI::ResolveCanvasCamera(*canvas);
            if (!camera && actor.GetScene()) camera = actor.GetScene()->FindMainCamera();
            if (!camera || x < 0 || y < 0 || x >= width || y >= height) { output = 0; return; }
            const auto& view = camera->GetCamera();
            const auto projection = view.GetProjectionMode() == OvRendering::Settings::EProjectionMode::ORTHOGRAPHIC ?
                OvMaths::FMatrix4::CreateOrthographic(view.GetSize(),width/height,view.GetNear(),view.GetFar()) :
                OvMaths::FMatrix4::CreatePerspective(view.GetFov(),width/height,view.GetNear(),view.GetFar());
            const auto position=view.GetPosition(), forward=view.transform->GetWorldForward(), up=view.transform->GetWorldUp();
            const auto cameraView=OvMaths::FMatrix4::CreateView(position.x,position.y,position.z,position.x+forward.x,position.y+forward.y,position.z+forward.z,up.x,up.y,up.z);
            const auto inverse = OvMaths::FMatrix4::Inverse(projection*cameraView);
            auto near = inverse*OvMaths::FVector4{x/width*2-1,y/height*2-1,-1,1};
            auto far = inverse*OvMaths::FVector4{x/width*2-1,y/height*2-1,1,1};
            Require(std::abs(near.w)>1e-8f && std::abs(far.w)>1e-8f); near=near*(1/near.w);far=far*(1/far.w);
            OvMaths::FVector3 direction{far.x-near.x,far.y-near.y,far.z-near.z};
            const float length=std::sqrt(direction.x*direction.x+direction.y*direction.y+direction.z*direction.z);
            Require(std::isfinite(length) && length>1e-8f); direction/=length;
            auto pair=[](float a,float b) { return uint64_t(std::bit_cast<uint32_t>(a)) | (uint64_t(std::bit_cast<uint32_t>(b))<<32); };
            if(text=="valid") output=1;
            else if(text=="originXY") output=pair(near.x,near.y);
            else if(text=="originZdirectionX") output=pair(near.z,direction.x);
            else if(text=="directionYZ") output=pair(direction.y,direction.z);
            else if(text=="distance") output=pair(length,0);
            else Require(false);
            return;
        }
        switch (operation)
        {
        case 80: output = input.CaptureUIWords().size(); break;
        case 81: {
            const auto& words = input.CaptureUIWords(); Require(argument < words.size()); output = words[size_t(argument)]; break;
        }
        case 82: output = input.CaptureUIClipboard().size(); break;
        case 83: {
            const auto& bytes = input.GetUIClipboard(); Require(argument <= 131072 && argument*8 < bytes.size());
            output = 0;
            for (size_t i = 0; i < 8 && argument*8+i < bytes.size(); ++i) output |= uint64_t(uint8_t(bytes[size_t(argument)*8+i])) << (i*8);
            break;
        }
        case 84: Require(text.size() <= 1048576); input.GetWindow().SetClipboardText(text); output = 0; break;
        default: Require(false); break;
        }
    }
}
