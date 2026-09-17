#include <OvCore/Scripting/CSharp/UIBridge.h>
#include <OvCore/ECS/Components/CUI.h>
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
        constexpr size_t vertexBytes = sizeof(float) * 9;
        Require(vertexCount <= 65535 && indexCount <= 65535 * 6 && indexCount % 3 == 0);
        const auto expected = sizeof(uint32_t) * 2 + size_t(vertexCount) * vertexBytes + size_t(indexCount) * sizeof(uint32_t);
        Require(expected == payload.size());
        std::vector<UIVertex> vertices(vertexCount);
        const uint8_t* cursor = payload.data() + sizeof(uint32_t) * 2;
        for (auto& vertex : vertices)
        {
            float values[9]; std::memcpy(values, cursor, sizeof(values)); cursor += sizeof(values);
            vertex.position = {values[0], values[1], values[2]}; vertex.uv = {values[3], values[4]};
            vertex.color = {values[5], values[6], values[7], values[8]};
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
                Require(argument == 2 || argument == 4);
                Require(property.find(':') == std::string::npos && property.find("..") == std::string::npos &&
                    (property.empty() || (property.front() != '/' && property.front() != '\\')));
                if (argument == 2) renderer.SetTexturePath(property); else renderer.SetMaterialPath(property);
            }
            else if (op == 47)
            {
                Require(renderer.owner.CanRunScripts(), OV_SCRIPT_INVALID_HANDLE); Require(argument <= 1);
                renderer.SetRaycastTarget(argument != 0);
            }
            else if (op == 48)
            {
                if (property == "generation") output = renderer.GetMeshGeneration();
                else if (property == "vertices") output = renderer.GetVertices().size();
                else if (property == "indices") output = renderer.GetIndices().size();
                else Require(false);
            }
            else Require(false); // Upload operation 45 is routed as binary by Gameplay.
        }
        else Require(false);
    }
}
