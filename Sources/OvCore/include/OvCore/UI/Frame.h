#pragma once

#include <OvCore/UI/Layout.h>
#include <OvCore/ECS/Components/CUI.h>
#include <OvMaths/FMatrix4.h>
#include <OvMaths/FVector4.h>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace baregl { class Texture; }
namespace OvCore::SceneSystem { class Scene; }
namespace OvRendering::Entities { class Camera; }

namespace OvCore::UI
{
    struct Image
    {
        uint64_t actorGuid = 0;
        uint64_t sequence = 0;
        int sortingOrder = 0;
        ECS::Components::CCanvas::RenderMode renderMode = ECS::Components::CCanvas::RenderMode::ScreenSpaceOverlay;
        Rect rect, visibleRect; // Overlay: framebuffer pixels. Other modes: root-canvas local units.
        OvMaths::FMatrix4 canvasToWorld = OvMaths::FMatrix4::Identity;
        std::array<OvMaths::FVector2, 4> screenCorners{}; // bottom-left, top-left, top-right, bottom-right
        bool hasScreenCorners = false;
        OvMaths::FVector4 color{1, 1, 1, 1};
        std::shared_ptr<baregl::Texture> texture;
        bool raycastTarget = true, interactable = true;
    };
    struct MaterialSlot
    {
        std::string materialPath;
        std::shared_ptr<baregl::Texture> texture;
        std::shared_ptr<const Fonts::AtlasPage> fontPage;
        uint32_t firstIndex = 0, indexCount = 0;
    };
    struct Mesh
    {
        uint64_t actorGuid = 0;
        uint64_t sequence = 0;
        uint64_t sourceGeneration = 0;
        int sortingOrder = 0;
        ECS::Components::CCanvas::RenderMode renderMode = ECS::Components::CCanvas::RenderMode::ScreenSpaceOverlay;
        Rect clip;
        OvMaths::FMatrix4 canvasToWorld = OvMaths::FMatrix4::Identity;
        std::vector<ECS::Components::UIVertex> vertices; // Canvas coordinates, immutable after extraction.
        std::vector<uint32_t> indices;
        std::shared_ptr<baregl::Texture> texture;
        std::string materialPath;
        std::vector<MaterialSlot> materialSlots, popMaterialSlots;
        ECS::Components::UIStencilState stencil;
        bool pop = false;
        OvMaths::FMatrix4 screenToCanvas = OvMaths::FMatrix4::Identity;
        OvMaths::FVector2 localOrigin{};
        float coordinateScale = 1;
        OvMaths::FVector3 cameraPosition{};
        std::array<OvMaths::FVector2, 4> screenCorners{};
        bool hasScreenCorners = false;
        bool raycastTarget = true, interactable = true;
    };
    struct GraphicHit { OvMaths::FVector2 localPoint; OvMaths::FVector3 worldPoint, worldNormal; float distance = 0; int depth = -1, sortingOrder = 0; bool reversed = false; };
    struct Hit
    {
        uint64_t actorGuid;
        bool interactable;
    };
    // Published by value; contains no Actor, component, or asset pointers.
    // Consumers receive a const frame. Texture generations stay pinned while in flight.
    struct Frame
    {
        uint64_t sceneGeneration = 0;
        OvMaths::FVector2 size;
        std::vector<Image> images; // painter order, shared by rendering and raycasts
        std::vector<Mesh> meshes;
        std::optional<Hit> HitTest(OvMaths::FVector2 bottomLeftPosition) const;
        std::optional<GraphicHit> RaycastGraphic(uint64_t actorGuid, OvMaths::FVector2 bottomLeftPosition) const;
        // Input coordinates relative to the game viewport, not the whole editor window.
        std::optional<Hit> HitTestTopLeft(OvMaths::FVector2 position) const;
    };
    struct CanvasGeometry
    {
        Rect rect;
        OvMaths::FMatrix4 localToWorld = OvMaths::FMatrix4::Identity;
    };
    // Resolves an explicitly assigned camera, or the scene's main camera when the
    // Screen Space - Camera slot is left empty (matching uGUI's useful fallback).
    const ECS::Components::CCamera* ResolveCanvasCamera(const ECS::Components::CCanvas& canvas);
    // Root-canvas geometry shared by runtime rendering and the Scene view gizmo.
    std::optional<CanvasGeometry> ResolveCanvasGeometry(const ECS::Components::CCanvas& canvas,
        OvMaths::FVector2 framebufferSize);
    void UpdateRectContexts(SceneSystem::Scene& scene, OvMaths::FVector2 framebufferSize);
    Frame ExtractFrame(SceneSystem::Scene& scene, OvMaths::FVector2 framebufferSize,
        uint64_t sceneGeneration = 0, const OvRendering::Entities::Camera* renderCamera = nullptr);
}
