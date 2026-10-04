#pragma once
#include <OvCore/SceneSystem/Scene.h>
#include <OvCore/ECS/Actor.h>
#include <OvEditor/Core/GizmoBehaviour.h>
#include <OvRendering/Data/RenderCommandList.h>
#include <unordered_map>
#include <variant>
namespace OvEditor::Rendering
{
		using PickingResult =
			std::optional<
			std::variant<OvTools::Utils::OptRef<OvCore::ECS::Actor>,
			OvEditor::Core::GizmoBehaviour::EDirection>
		>;

        class PickingReadback
        {
            friend class PickingRenderPass;
            friend class PickingCollector;
        public:
            bool IsReady() const { return completion.Succeeded(); }
            std::pair<uint32_t, uint32_t> GetSize() const { return {width, height}; }
            uint64_t GetGizmoActorGUID() const { return gizmoActorGUID; }
            OvEditor::Core::EGizmoOperation GetGizmoOperation() const { return gizmoOperation; }
            PickingResult Resolve(const OvCore::SceneSystem::Scene& p_scene, uint32_t p_x, uint32_t p_y) const;
            std::vector<OvCore::ECS::Actor*> ResolveRegion(const OvCore::SceneSystem::Scene& scene, int left, int bottom, int right, int top) const;
        private:
            OvRendering::Data::RenderCommandList::Completion completion;
            OvCore::SceneSystem::Scene::LifetimeToken scene;
            uint32_t width = 0, height = 0;
            uint64_t gizmoActorGUID = 0;
            OvEditor::Core::EGizmoOperation gizmoOperation = OvEditor::Core::EGizmoOperation::TRANSLATE;
            std::vector<uint8_t> pixels;
            std::unordered_map<uint32_t, uint64_t> actors;
        };

}
