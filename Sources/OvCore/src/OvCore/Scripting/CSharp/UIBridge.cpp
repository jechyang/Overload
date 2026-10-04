#include <OvCore/Scripting/CSharp/UIBridge.h>
#include <OvCore/ECS/Components/CUI.h>
#include <OvCore/UI/Frame.h>
#include <OvCore/Global/ServiceLocator.h>
#include <OvCore/ResourceManagement/TextureManager.h>
#include <OvRendering/Resources/Texture.h>
#include <OvCore/ECS/Actor.h>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <span>
#include <vector>
namespace OvCore::Scripting::CSharp
{
    namespace
    {
        uint64_t Pack(OvMaths::FVector2 v)
        { return uint64_t(std::bit_cast<uint32_t>(v.x)) | (uint64_t(std::bit_cast<uint32_t>(v.y)) << 32); }
        OvMaths::FVector2 Unpack(uint64_t v)
        { return {std::bit_cast<float>(uint32_t(v)), std::bit_cast<float>(uint32_t(v >> 32))}; }
    }
    void UploadUIMesh(OvReflection::Script::State& state, uint64_t handle,
        std::span<const uint8_t> payload, uint64_t& output)
    {
        using namespace OvReflection::Script;
        using namespace ECS::Components;
        auto& renderer = *state.Get<CCanvasRenderer>(handle);
        Require(renderer.owner.CanRunScripts(), OV_SCRIPT_INVALID_HANDLE);
        Require(payload.size() >= sizeof(uint32_t) * 2);
        uint32_t vertexCount = 0, indexCount = 0;
        std::memcpy(&vertexCount, payload.data(), sizeof(vertexCount));
        std::memcpy(&indexCount, payload.data() + sizeof(vertexCount), sizeof(indexCount));
        const bool extended = vertexCount == 0x3249554f;
        size_t headerBytes = 8;
        if (extended) {
            Require(payload.size() >= 16 && indexCount == 2);
            std::memcpy(&vertexCount, payload.data()+8, 4);
            std::memcpy(&indexCount, payload.data()+12, 4);
            headerBytes = 16;
        }
        const size_t vertexBytes = sizeof(float) * (extended ? 22 : 9);
        Require(vertexCount <= 65535 && indexCount <= 65535 * 6 && indexCount % 3 == 0);
        const auto expected = headerBytes + size_t(vertexCount) * vertexBytes + size_t(indexCount) * sizeof(uint32_t);
        Require(expected == payload.size());
        std::vector<UIVertex> vertices(vertexCount);
        const uint8_t* cursor = payload.data() + headerBytes;
        for (auto& vertex : vertices)
        {
            float values[22]{}; std::memcpy(values, cursor, vertexBytes); cursor += vertexBytes;
            vertex.position = {values[0], values[1], values[2]}; vertex.uv = {values[3], values[4]};
            vertex.color = {values[5], values[6], values[7], values[8]};
            if (extended) {
                vertex.normal = {values[9],values[10],values[11]};
                vertex.tangent = {values[12],values[13],values[14],values[15]};
                vertex.uv1 = {values[16],values[17]}; vertex.uv2 = {values[18],values[19]}; vertex.uv3 = {values[20],values[21]};
            }
        }
        std::vector<uint32_t> indices(indexCount);
        if (!indices.empty()) std::memcpy(indices.data(), cursor, indices.size() * sizeof(uint32_t));
        renderer.SetMesh(vertices, indices); output = renderer.GetMeshGeneration();
    }
    void QueryUI(OvReflection::Script::State& state, uint64_t handle, uint32_t op,
        uint64_t argument, const std::string& property, uint64_t& output)
    {
        using namespace OvReflection::Script;
        using namespace ECS::Components;
        if (op == 40 || op == 41)
        {
            auto& rect = *state.Get<CRectTransform>(handle);
            if (op == 40)
            {
                OvMaths::FVector2 value;
                if (property == "anchorMin") value = rect.GetAnchorMin();
                else if (property == "anchorMax") value = rect.GetAnchorMax();
                else if (property == "pivot") value = rect.GetPivot();
                else if (property == "anchoredPosition") value = rect.GetAnchoredPosition();
                else if (property == "sizeDelta") value = rect.GetSizeDelta();
                else if (property == "offsetMin") value = rect.GetOffsetMin();
                else if (property == "offsetMax") value = rect.GetOffsetMax();
                else if (property == "rectMin") value = rect.GetRect().min;
                else if (property == "rectSize") value = rect.GetRect().size;
                else Require(false);
                output = Pack(value);
            }
            else
            {
                Require(rect.owner.CanRunScripts(), OV_SCRIPT_INVALID_HANDLE);
                const auto value = Unpack(argument);
                if (property == "anchorMin") rect.SetAnchorMin(value);
                else if (property == "anchorMax") rect.SetAnchorMax(value);
                else if (property == "pivot") rect.SetPivot(value);
                else if (property == "anchoredPosition") rect.SetAnchoredPosition(value);
                else if (property == "sizeDelta") rect.SetSizeDelta(value);
                else if (property == "offsetMin") rect.SetOffsetMin(value);
                else if (property == "offsetMax") rect.SetOffsetMax(value);
                else Require(false);
            }
        }
        else if (op == 42 || op == 43)
        {
            auto& canvas = *state.Get<CCanvas>(handle);
            if (op == 42)
            {
                if (property == "scaleFactor") output = Pack({canvas.GetScaleFactor(), 0});
                else if (property == "renderMode") output = static_cast<uint64_t>(canvas.GetRenderMode());
                else if (property == "planeDistance") output = Pack({canvas.GetPlaneDistance(), 0});
                else if (property == "worldCamera") output = canvas.GetWorldCameraGuid();
                else if (property == "sortingOrder") output = uint64_t(int64_t(canvas.GetSortingOrder()));
                else if (property == "overrideSorting") output = canvas.OverridesSorting();
                else Require(false);
            }
            else
            {
                Require(canvas.owner.CanRunScripts(), OV_SCRIPT_INVALID_HANDLE);
                if (property == "scaleFactor") { canvas.SetScaleFactor(Unpack(argument).x); }
                else if (property == "renderMode") canvas.SetRenderMode(static_cast<CCanvas::RenderMode>(argument));
                else if (property == "planeDistance") canvas.SetPlaneDistance(Unpack(argument).x);
                else if (property == "worldCamera") canvas.SetWorldCameraGuid(argument);
                else if (property == "sortingOrder") canvas.SetSortingOrder(int(int64_t(argument)));
                else if (property == "overrideSorting") { Require(argument <= 1); canvas.SetOverrideSorting(argument != 0); }
                else Require(false);
            }
        }
        else if (op >= 44 && op <= 48)
        {
            auto& renderer = *state.Get<CCanvasRenderer>(handle);
            if (op == 44)
            {
                Require(renderer.owner.CanRunScripts(), OV_SCRIPT_INVALID_HANDLE); renderer.Clear();
            }
            else if (op == 46)
            {
                Require(renderer.owner.CanRunScripts(), OV_SCRIPT_INVALID_HANDLE);
                const auto kind = argument & 0xff;
                const auto slot = (argument >> 8) & 0xffff;
                const bool pop = (argument & (1ull << 24)) != 0;
                Require((argument >> 25) == 0 && (kind == 2 || kind == 4));
                Require(property.find(':') == std::string::npos && property.find("..") == std::string::npos &&
                    (property.empty() || (property.front() != '/' && property.front() != '\\')));
                renderer.SetSlotPath(slot, kind == 4, property, pop);
            }
            else if (op == 47)
            {
                Require(renderer.owner.CanRunScripts(), OV_SCRIPT_INVALID_HANDLE);
                if (property.empty()) { Require(argument <= 1); renderer.SetRaycastTarget(argument != 0); }
                else if (property == "materialCount" || property == "popMaterialCount") renderer.SetMaterialCount(argument, property == "popMaterialCount");
                else if (property == "cull") { Require(argument <= 1); renderer.SetCull(argument != 0); }
                else if (property == "stencil" || property == "popStencil") {
                    Require((argument >> 32) == 0 && (argument & 0xf8) == 0);
                    renderer.SetStencil({(argument&1)!=0,(argument&2)!=0,(argument&4)!=0,
                        uint8_t(argument>>8),uint8_t(argument>>16),uint8_t(argument>>24)},property == "popStencil");
                }
                else if (property == "colorRG" || property == "colorBA") {
                    auto color = renderer.GetColor(); const auto v = Unpack(argument);
                    if (property == "colorRG") { color.x = v.x; color.y = v.y; } else { color.z = v.x; color.w = v.y; }
                    renderer.SetColor(color);
                }
                else if (property == "clipMin" || property == "clipSize") {
                    auto rect = renderer.GetClipRect();
                    if (property == "clipMin") rect.min = Unpack(argument); else rect.size = Unpack(argument);
                    renderer.SetClipRect(rect);
                }
                else if (property == "disableClip") renderer.DisableRectClipping();
                else if (property == "ignoreParentMasks") { Require(argument<=1);renderer.SetIgnoreParentRectMasks(argument!=0); }
                else if (property.starts_with("range:")) {
                    const auto suffix = property.substr(6);
                    Require(!suffix.empty() && suffix.find_first_not_of("0123456789") == std::string::npos && suffix.size() <= 2);
                    renderer.SetIndexRange(std::stoul(suffix), uint32_t(argument), uint32_t(argument >> 32));
                }
                else Require(false);
            }
            else if (op == 48)
            {
                if (property == "generation") output = renderer.GetMeshGeneration();
                else if (property == "vertices") output = renderer.GetVertices().size();
                else if (property == "indices") output = renderer.GetIndices().size();
                else if (property == "materialCount") output = renderer.GetMaterialSlots().size();
                else if (property == "popMaterialCount") output = renderer.GetPopMaterialSlots().size();
                else if (property == "cull") output = renderer.IsCulled();
                else if (property == "depth") output = uint64_t(int64_t(renderer.GetAbsoluteDepth()));
                else if (property == "colorRG") { const auto c = renderer.GetColor(); output = Pack({c.x,c.y}); }
                else if (property == "colorBA") { const auto c = renderer.GetColor(); output = Pack({c.z,c.w}); }
                else Require(false);
            }
            else Require(false); // Upload operation 45 is routed as binary by Gameplay.
        }
        else if (op == 49 || op == 50) {
            auto& group = *state.Get<CCanvasGroup>(handle);
            if(op == 49) {
                if(property == "alpha") output = Pack({group.GetAlpha(),0});
                else if(property == "flags") output = (group.IsInteractable()?1:0)|(group.BlocksRaycasts()?2:0)|(group.IgnoresParentGroups()?4:0)|(group.IsEnabled()?8:0);
                else Require(false);
            } else {
                Require(group.owner.CanRunScripts(),OV_SCRIPT_INVALID_HANDLE);
                if(property == "release") { Require(argument==0);group.SetEnabled(false); }
                else if(property == "alpha") group.SetAlpha(Unpack(argument).x);
                else if(property == "flags") { Require(argument<=15);group.SetInteractable(argument&1);group.SetBlocksRaycasts(argument&2);group.SetIgnoreParentGroups(argument&4);group.SetEnabled(argument&8); }
                else Require(false);
            }
        }
        else if(op == 51 || op == 52) {
            auto& mask = *state.Get<CRectMask2D>(handle);
            if(op==51)output=mask.IsEnabled();
            else {Require(mask.owner.CanRunScripts(),OV_SCRIPT_INVALID_HANDLE);Require(argument<=1);mask.SetEnabled(argument!=0);}
        }
        else if(op == 54) {
            auto& renderer=*state.Get<CCanvasRenderer>(handle);
            const auto uv=Unpack(argument);Require(std::isfinite(uv.x)&&std::isfinite(uv.y));
            auto* manager=Global::ServiceLocator::TryGet<ResourceManagement::TextureManager>();
            auto* texture=manager&&!property.empty()?manager->GetResource(property):nullptr;
            output=Pack({texture?texture->SampleAlpha(uv.x,uv.y):1.0f,0});
        }
        else if(op == 53) {
            auto& renderer=*state.Get<CCanvasRenderer>(handle);
            const auto frame=renderer.GetHitFrame();
            if (property == "projectValid" || property == "projectLocal") {
                const auto point = frame ? frame->ProjectGraphic(renderer.owner.GetGUID(), Unpack(argument)) : std::nullopt;
                if (property == "projectValid") output = point.has_value();
                else { Require(point.has_value()); output = Pack(*point); }
                return;
            }
            const auto hit=frame?frame->RaycastGraphic(renderer.owner.GetGUID(),Unpack(argument)):std::nullopt;
            if(property=="valid")output=hit.has_value();
            else if(hit) {
                if(property=="local")output=Pack(hit->localPoint);
                else if(property=="distance")output=Pack({hit->distance,0});
                else if(property=="depth")output=uint64_t(hit->depth);
                else if(property=="order")output=uint64_t(int64_t(hit->sortingOrder));
                else if(property=="worldXY")output=Pack({hit->worldPoint.x,hit->worldPoint.y});
                else if(property=="worldZ")output=Pack({hit->worldPoint.z,0});
                else if(property=="normalXY")output=Pack({hit->worldNormal.x,hit->worldNormal.y});
                else if(property=="normalZ")output=Pack({hit->worldNormal.z,0});
                else if(property=="reversed")output=hit->reversed;
                else Require(false);
            } else Require(false);
        }
        else Require(false);
    }
}
