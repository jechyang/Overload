#include <OvEditor/Rendering/PickingReadback.h>
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
