#include <OvEditor/Rendering/PickingReadback.h>
#include <algorithm>
#include <unordered_set>

std::vector<OvCore::ECS::Actor*> OvEditor::Rendering::PickingReadback::ResolveRegion(
    const OvCore::SceneSystem::Scene& current, int left, int bottom, int right, int top) const
{
    std::vector<OvCore::ECS::Actor*> result;
    if (!IsReady() || scene != current.GetLifetimeToken()) return result;
    if (left > right) std::swap(left,right);
    if (bottom > top) std::swap(bottom,top);
    left = std::clamp(left,0,int(width)); right = std::clamp(right,0,int(width));
    bottom = std::clamp(bottom,0,int(height)); top = std::clamp(top,0,int(height));
    std::unordered_set<uint32_t> ids;
    for (int y = bottom; y < top; ++y) for (int x = left; x < right; ++x)
    {
        const auto* pixel = pixels.data() + (size_t(y)*width+x)*4;
        ids.insert(uint32_t(pixel[0]) | uint32_t(pixel[1])<<8 | uint32_t(pixel[2])<<16);
    }
    for (const auto id : ids)
        if (auto found = actors.find(id); found != actors.end())
            if (auto* actor = current.FindActorByID(id); actor && actor->IsAlive() && actor->IsActive() && actor->GetGUID() == found->second) result.push_back(actor);
    std::sort(result.begin(),result.end(),[](auto* a,auto* b) { return a->GetID() < b->GetID(); });
    return result;
}
OvEditor::Rendering::PickingResult OvEditor::Rendering::PickingReadback::Resolve(
    const OvCore::SceneSystem::Scene& p_scene, uint32_t p_x, uint32_t p_y) const
{
    if (!IsReady() || scene != p_scene.GetLifetimeToken() || p_x >= width || p_y >= height) return std::nullopt;
    const auto* pixel = pixels.data() + (size_t(p_y) * width + p_x) * 4;
    const uint32_t actorID = (uint32_t(pixel[2]) << 16) | (uint32_t(pixel[1]) << 8) | pixel[0];
    if (const auto found = actors.find(actorID); found != actors.end())
    {
        if (auto* actor = p_scene.FindActorByID(actorID); actor && actor->GetGUID() == found->second)
            return OvTools::Utils::OptRef(*actor);
        return std::nullopt;
    }
    if (pixel[0] == 255 && pixel[1] == 255 && pixel[2] >= 252 && pixel[2] <= 254 &&
        gizmoActorGUID && p_scene.FindActorByGUID(gizmoActorGUID))
        return static_cast<OvEditor::Core::GizmoBehaviour::EDirection>(pixel[2] - 252);
    return std::nullopt;
}
