#pragma once

#include <OvReflection/Annotations.h>
#include <OvCore/ECS/Components/AComponent.h>
#include <OvCore/UI/Layout.h>
#include <OvMaths/FVector2.h>
#include <OvMaths/FVector3.h>
#include <OvMaths/FVector4.h>
#include <array>
#include <span>
#include <string>
#include <vector>

namespace OvRendering::Resources { class Texture; }

namespace OvCore::ECS::Components
{
    class CCamera;

    // Overlay layout is independent of the Actor's 3D CTransform. Actor parenting
    // defines the UI tree; no second set of owning parent/child pointers is kept.
    class OV_TYPE(Component, Factory = AddComponent, Alias = "class OvCore::ECS::Components::CRectTransform", DisplayName = "Rect Transform", Order = 30) CRectTransform : public AComponent
    {
    public:
        explicit CRectTransform(Actor& actor);
        std::string GetName() override;
        std::string GetTypeName() override;
        const UI::RectLayout& GetLayout() const { return m_layout; }
        enum class Axis { Horizontal, Vertical };
        enum class Edge { Left, Right, Top, Bottom };
        OvMaths::FVector2 GetAnchorMin() const { return m_layout.anchorMin; }
        OvMaths::FVector2 GetAnchorMax() const { return m_layout.anchorMax; }
        OvMaths::FVector2 GetPivot() const { return m_layout.pivot; }
        OvMaths::FVector2 GetAnchoredPosition() const { return m_layout.anchoredPosition; }
        OvMaths::FVector2 GetSizeDelta() const { return m_layout.sizeDelta; }
        OvMaths::FVector2 GetOffsetMin() const;
        OvMaths::FVector2 GetOffsetMax() const;
        void SetOffsetMin(OvMaths::FVector2 value);
        void SetOffsetMax(OvMaths::FVector2 value);
        void SetInsetAndSizeFromParentEdge(Edge edge, float inset, float size);
        void SetSizeWithCurrentAnchors(Axis axis, float size);
        UI::Rect GetRect() const;
        std::array<OvMaths::FVector2, 4> GetLocalCorners() const;
        // The layout pass supplies viewport-dependent context before invoking controllers.
        void SetLayoutContext(OvMaths::FVector2 parentSize, bool rootCanvas = false);
        void SetAnchorMin(OvMaths::FVector2 value);
        void SetAnchorMax(OvMaths::FVector2 value);
        void SetPivot(OvMaths::FVector2 value);
        void SetAnchoredPosition(OvMaths::FVector2 value);
        void SetSizeDelta(OvMaths::FVector2 value);
        UI::Rect Resolve(const UI::Rect& parent) const { return UI::ResolveRect(m_layout, parent); }
        void OnSerialize(tinyxml2::XMLDocument&, tinyxml2::XMLNode*) override;
        void OnDeserialize(tinyxml2::XMLDocument&, tinyxml2::XMLNode*) override;
        void OnInspector(OvUI::Internal::WidgetContainer&) override;
    private:
        OvMaths::FVector2 GetParentSize() const;
        UI::RectLayout m_layout;
        OvMaths::FVector2 m_parentSize{0, 0};
        bool m_rootCanvas = false;
    };

    class OV_TYPE(Component, Factory = AddComponent, Alias = "class OvCore::ECS::Components::CCanvas", DisplayName = "Canvas", Order = 31) CCanvas : public AComponent
    {
    public:
        enum class RenderMode { ScreenSpaceOverlay = 0, ScreenSpaceCamera = 1, WorldSpace = 2 };
        explicit CCanvas(Actor& actor);
        std::string GetName() override;
        std::string GetTypeName() override;
        RenderMode GetRenderMode() const { return m_renderMode; }
        void SetRenderMode(RenderMode value);
        float GetPlaneDistance() const { return m_planeDistance; }
        void SetPlaneDistance(float value);
        uint64_t GetWorldCameraGuid() const { return m_worldCameraGuid; }
        void SetWorldCameraGuid(uint64_t value);
        CCamera* GetWorldCamera() const;
        void SetWorldCamera(CCamera* value);
        float GetScaleFactor() const { return m_scaleFactor; }
        void SetScaleFactor(float value);
        int GetSortingOrder() const { return m_sortingOrder; }
        void SetSortingOrder(int value) { m_sortingOrder = value; }
        bool OverridesSorting() const { return m_overrideSorting; }
        void SetOverrideSorting(bool value) { m_overrideSorting = value; }
        void OnSerialize(tinyxml2::XMLDocument&, tinyxml2::XMLNode*) override;
        void OnDeserialize(tinyxml2::XMLDocument&, tinyxml2::XMLNode*) override;
        void OnInspector(OvUI::Internal::WidgetContainer&) override;
    private:
        RenderMode m_renderMode = RenderMode::ScreenSpaceOverlay;
        float m_planeDistance = 100;
        uint64_t m_worldCameraGuid = 0;
        float m_scaleFactor = 1;
        int m_sortingOrder = 0;
        bool m_overrideSorting = false;
    };

    class OV_TYPE(Component, Factory = AddComponent, Alias = "class OvCore::ECS::Components::CUIImage", DisplayName = "UI Image", Order = 32) CUIImage : public AComponent
    {
    public:
        explicit CUIImage(Actor& actor);
        std::string GetName() override;
        std::string GetTypeName() override;
        OvMaths::FVector4 GetColor() const { return m_color; }
        void SetColor(OvMaths::FVector4 value);
        OvRendering::Resources::Texture* GetTexture() const { return m_texture; }
        void SetTexture(OvRendering::Resources::Texture* value) { m_texture = value; }
        bool IsRaycastTarget() const { return m_raycastTarget; }
        void SetRaycastTarget(bool value) { m_raycastTarget = value; }
        void OnSerialize(tinyxml2::XMLDocument&, tinyxml2::XMLNode*) override;
        void OnDeserialize(tinyxml2::XMLDocument&, tinyxml2::XMLNode*) override;
        void OnInspector(OvUI::Internal::WidgetContainer&) override;
    private:
        OvMaths::FVector4 m_color{1, 1, 1, 1};
        OvRendering::Resources::Texture* m_texture = nullptr; // Resource manager owns the asset.
        bool m_raycastTarget = true;
    };

    struct UIVertex
    {
        OvMaths::FVector3 position{0, 0, 0};
        OvMaths::FVector2 uv{0, 0};
        OvMaths::FVector4 color{1, 1, 1, 1};
    };

    // Low-level native sink for managed Graphic geometry. Mesh generation and
    // control policy stay in C#; this component owns only validated CPU data
    // until the immutable UI frame snapshot is extracted.
    class OV_TYPE(Component, Factory = AddComponent, Alias = "class OvCore::ECS::Components::CCanvasRenderer", DisplayName = "Canvas Renderer", Order = 32) CCanvasRenderer : public AComponent
    {
    public:
        explicit CCanvasRenderer(Actor& actor);
        std::string GetName() override;
        std::string GetTypeName() override;
        void SetMesh(std::span<const UIVertex> vertices, std::span<const uint32_t> indices);
        void Clear();
        const std::vector<UIVertex>& GetVertices() const { return m_vertices; }
        const std::vector<uint32_t>& GetIndices() const { return m_indices; }
        uint64_t GetMeshGeneration() const { return m_meshGeneration; }
        const std::string& GetTexturePath() const { return m_texturePath; }
        void SetTexturePath(std::string value);
        const std::string& GetMaterialPath() const { return m_materialPath; }
        void SetMaterialPath(std::string value);
        bool IsRaycastTarget() const { return m_raycastTarget; }
        void SetRaycastTarget(bool value) { m_raycastTarget = value; }
        void OnSerialize(tinyxml2::XMLDocument&, tinyxml2::XMLNode*) override;
        void OnDeserialize(tinyxml2::XMLDocument&, tinyxml2::XMLNode*) override;
        void OnInspector(OvUI::Internal::WidgetContainer&) override;
    private:
        std::vector<UIVertex> m_vertices;
        std::vector<uint32_t> m_indices;
        std::string m_texturePath;
        std::string m_materialPath;
        uint64_t m_meshGeneration = 0;
        bool m_raycastTarget = true;
    };

    class OV_TYPE(Component, Factory = AddComponent, Alias = "class OvCore::ECS::Components::CCanvasGroup", DisplayName = "Canvas Group", Order = 33) CCanvasGroup : public AComponent
    {
    public:
        explicit CCanvasGroup(Actor& actor);
        std::string GetName() override;
        std::string GetTypeName() override;
        float GetAlpha() const { return m_alpha; }
        void SetAlpha(float value);
        bool IsInteractable() const { return m_interactable; }
        void SetInteractable(bool value) { m_interactable = value; }
        bool BlocksRaycasts() const { return m_blocksRaycasts; }
        void SetBlocksRaycasts(bool value) { m_blocksRaycasts = value; }
        void OnSerialize(tinyxml2::XMLDocument&, tinyxml2::XMLNode*) override;
        void OnDeserialize(tinyxml2::XMLDocument&, tinyxml2::XMLNode*) override;
        void OnInspector(OvUI::Internal::WidgetContainer&) override;
    private:
        float m_alpha = 1;
        bool m_interactable = true, m_blocksRaycasts = true;
    };

    class OV_TYPE(Component, Factory = AddComponent, Alias = "class OvCore::ECS::Components::CRectMask2D", DisplayName = "Rect Mask 2D", Order = 34) CRectMask2D : public AComponent
    {
    public:
        explicit CRectMask2D(Actor& actor);
        std::string GetName() override;
        std::string GetTypeName() override;
        bool IsEnabled() const { return m_enabled; }
        void SetEnabled(bool value) { m_enabled = value; }
        void OnSerialize(tinyxml2::XMLDocument&, tinyxml2::XMLNode*) override;
        void OnDeserialize(tinyxml2::XMLDocument&, tinyxml2::XMLNode*) override;
        void OnInspector(OvUI::Internal::WidgetContainer&) override;
    private:
        bool m_enabled = true;
    };

    template<> struct ComponentTraits<CRectTransform> { static constexpr std::string_view Name = "class OvCore::ECS::Components::CRectTransform"; };
    template<> struct ComponentTraits<CCanvas> { static constexpr std::string_view Name = "class OvCore::ECS::Components::CCanvas"; };
    template<> struct ComponentTraits<CUIImage> { static constexpr std::string_view Name = "class OvCore::ECS::Components::CUIImage"; };
    template<> struct ComponentTraits<CCanvasRenderer> { static constexpr std::string_view Name = "class OvCore::ECS::Components::CCanvasRenderer"; };
    template<> struct ComponentTraits<CCanvasGroup> { static constexpr std::string_view Name = "class OvCore::ECS::Components::CCanvasGroup"; };
    template<> struct ComponentTraits<CRectMask2D> { static constexpr std::string_view Name = "class OvCore::ECS::Components::CRectMask2D"; };
}
