#pragma once

#include <OvReflection/Annotations.h>
#include <OvCore/ECS/Components/AComponent.h>
#include <OvCore/Resources/ResourceReference.h>
#include <OvCore/UI/Layout.h>
#include <OvCore/UI/DrivenProperties.h>
#include <OvMaths/FVector2.h>
#include <OvMaths/FVector3.h>
#include <OvMaths/FVector4.h>
#include <array>
#include <span>
#include <memory>
#include <string>
#include <vector>
#include <OvTools/Eventing/Event.h>

namespace OvRendering::Resources { class Texture; }
namespace OvCore::UI::Fonts { struct AtlasPage; }
namespace OvCore::UI { struct Frame; }

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
        UI::DrivenProperties& GetDrivenProperties() { return m_driven; }
        enum class Axis { Horizontal, Vertical };
        enum class Edge { Left, Right, Top, Bottom };
        enum class AnchorPreset
        {
            TopLeft, TopCenter, TopRight, TopStretch,
            MiddleLeft, MiddleCenter, MiddleRight, MiddleStretch,
            BottomLeft, BottomCenter, BottomRight, BottomStretch,
            StretchLeft, StretchCenter, StretchRight, StretchAll,
            Custom
        };
        AnchorPreset GetAnchorPreset() const;
        void ApplyAnchorPreset(AnchorPreset preset, bool setPivot = true, bool setPosition = true);
        static std::string GetAnchorPresetName(AnchorPreset preset);
        OvTools::Eventing::Event<> LayoutChangedEvent;
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
        void OnInspector(OvUI::Internal::WidgetContainer&) override;
    private:
        OvMaths::FVector2 GetParentSize() const;
        friend struct OvReflection::Access<CRectTransform>;
        OV_PROPERTY(Serialize, Flatten, Name = "authored_layout", Setter = SetAuthoredLayout)
        UI::RectLayout GetAuthoredLayout() const { return m_driven.Authored(m_layout); }
        void SetAuthoredLayout(const UI::RectLayout& value);
        UI::RectLayout m_layout;
        UI::DrivenProperties m_driven;
        OvMaths::FVector2 m_parentSize{0, 0};
        bool m_rootCanvas = false;
    };

    class OV_TYPE(Component, Factory = AddComponent, Alias = "class OvCore::ECS::Components::CCanvas", DisplayName = "Canvas", Order = 31) CCanvas : public AComponent
    {
    public:
        enum class OV_TYPE() RenderMode { ScreenSpaceOverlay = 0, ScreenSpaceCamera = 1, WorldSpace = 2 };
        explicit CCanvas(Actor& actor);
        std::string GetName() override;
        std::string GetTypeName() override;
        OV_PROPERTY(Serialize, Inspect, Name = "render_mode", Setter = SetRenderMode, DisplayName = "Render Mode", Order = 0, Validate = "EnumChoice")
        RenderMode GetRenderMode() const { return m_renderMode; }
        void SetRenderMode(RenderMode value);
        OV_PROPERTY(Serialize, Inspect, Name = "plane_distance", Setter = SetPlaneDistance, DisplayName = "Plane Distance", Order = 1, Step = 0.1, Min = 0.001, Max = 1000000, Validate = "Positive")
        float GetPlaneDistance() const { return m_planeDistance; }
        void SetPlaneDistance(float value);
        OV_PROPERTY(Serialize, Name = "world_camera", Setter = SetWorldCameraGuid, DisplayName = "Render Camera", Order = 2)
        uint64_t GetWorldCameraGuid() const { return m_worldCameraGuid; }
        void SetWorldCameraGuid(uint64_t value);
        CCamera* GetWorldCamera() const;
        void SetWorldCamera(CCamera* value);
        OV_PROPERTY(Serialize, Inspect, Name = "scale_factor", Setter = SetScaleFactor, DisplayName = "Scale Factor", Order = 3, Step = 0.01, Min = 0.001, Max = 10000, Validate = "Positive")
        float GetScaleFactor() const { return m_scaleFactor; }
        void SetScaleFactor(float value);
        OV_PROPERTY(Serialize, Inspect, Name = "sorting_order", Setter = SetSortingOrder, DisplayName = "Sorting Order", Order = 4, Step = 1)
        int GetSortingOrder() const { return m_sortingOrder; }
        void SetSortingOrder(int value) { m_sortingOrder = value; }
        OV_PROPERTY(Serialize, Inspect, Name = "override_sorting", Setter = SetOverrideSorting, DisplayName = "Override Sorting", Order = 5)
        bool OverridesSorting() const { return m_overrideSorting; }
        void SetOverrideSorting(bool value) { m_overrideSorting = value; }
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
        OV_PROPERTY(Serialize, Inspect, Name = "color", Setter = SetColor, DisplayName = "Color", Drawer = "Color", Order = 0)
        OvMaths::FVector4 GetColor() const { return m_color; }
        void SetColor(OvMaths::FVector4 value);
        OvRendering::Resources::Texture* GetTexture() const { return m_texture; }
        void SetTexture(OvRendering::Resources::Texture* value) { m_texture = value; m_texturePath.clear(); }
        OV_PROPERTY(Serialize, Inspect, Name = "raycast_target", Setter = SetRaycastTarget, DisplayName = "Raycast Target", Order = 2)
        bool IsRaycastTarget() const { return m_raycastTarget; }
        void SetRaycastTarget(bool value) { m_raycastTarget = value; }
        void OnInspector(OvUI::Internal::WidgetContainer&) override;
    private:
        friend struct OvReflection::Access<CUIImage>;
        OV_PROPERTY(Serialize, Name = "texture", Setter = SetSerializedTexture, Order = 1)
        Resources::ResourceReference<OvRendering::Resources::Texture> GetSerializedTexture() const;
        void SetSerializedTexture(const Resources::ResourceReference<OvRendering::Resources::Texture>& value);
        std::string m_texturePath;
        OvMaths::FVector4 m_color{1, 1, 1, 1};
        OvRendering::Resources::Texture* m_texture = nullptr; // Resource manager owns the asset.
        bool m_raycastTarget = true;
    };

    struct UIVertex
    {
        OvMaths::FVector3 position{0, 0, 0};
        OvMaths::FVector2 uv{0, 0};
        OvMaths::FVector4 color{1, 1, 1, 1};
        OvMaths::FVector3 normal{0, 0, -1};
        OvMaths::FVector4 tangent{1, 0, 0, -1};
        OvMaths::FVector2 uv1{0, 0}, uv2{0, 0}, uv3{0, 0};
    };

    struct UIStencilState { bool enabled = false, replace = false, colorWrite = true; uint8_t reference = 0, readMask = 255, writeMask = 0; };

    struct UIMaterialSlot
    {
        std::string materialPath, texturePath;
        std::shared_ptr<const UI::Fonts::AtlasPage> fontPage;
        uint32_t firstIndex = 0, indexCount = 0;
        bool explicitRange = false;
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
        static constexpr size_t MaximumMaterialSlots = 64;
        const std::vector<UIMaterialSlot>& GetMaterialSlots() const { return m_slots; }
        const std::vector<UIMaterialSlot>& GetPopMaterialSlots() const { return m_popSlots; }
        void SetMaterialCount(size_t count, bool pop = false);
        void SetSlotPath(size_t slot, bool material, std::string path, bool pop = false);
        void SetIndexRange(size_t slot, uint32_t first, uint32_t count);
        void SetFontPage(size_t slot, std::shared_ptr<const UI::Fonts::AtlasPage> page);
        const UIStencilState& GetStencil() const { return m_stencil; }
        const UIStencilState& GetPopStencil() const { return m_popStencil; }
        void SetStencil(UIStencilState value, bool pop = false) { (pop ? m_popStencil : m_stencil) = value; }
        OvMaths::FVector4 GetColor() const { return m_color; }
        void SetColor(OvMaths::FVector4 value);
        bool IsCulled() const { return m_cull; }
        void SetCull(bool value) { m_cull = value; }
        bool HasRectClipping() const { return m_hasClip; }
        const UI::Rect& GetClipRect() const { return m_clip; }
        void SetClipRect(UI::Rect value);
        void DisableRectClipping() { m_hasClip = false; }
        void SetIgnoreParentRectMasks(bool value) { m_managedRectClipping = value; }
        bool UsesManagedRectClipping() const { return m_managedRectClipping; }
        std::shared_ptr<const UI::Frame> GetHitFrame() const { return m_hitFrame; }
        void SetHitFrame(std::shared_ptr<const UI::Frame> value) { m_hitFrame = std::move(value); }
        int GetAbsoluteDepth() const { return m_depth; }
        void SetAbsoluteDepth(int value) { m_depth = value; }
        bool IsRaycastTarget() const { return m_raycastTarget; }
        void SetRaycastTarget(bool value) { m_raycastTarget = value; }
        void OnSerialize(tinyxml2::XMLDocument&, tinyxml2::XMLNode*) override;
        void OnDeserialize(tinyxml2::XMLDocument&, tinyxml2::XMLNode*) override;
        void OnInspector(OvUI::Internal::WidgetContainer&) override;
    private:
        std::vector<UIMaterialSlot> m_slots{1}, m_popSlots;
        OvMaths::FVector4 m_color{1,1,1,1};
        UI::Rect m_clip{};
        UIStencilState m_stencil, m_popStencil;
        std::shared_ptr<const UI::Frame> m_hitFrame;
        bool m_cull = false, m_hasClip = false, m_managedRectClipping = false;
        int m_depth = -1;
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
        OV_PROPERTY(Serialize, Inspect, Name = "alpha", Setter = SetAlpha, DisplayName = "Alpha", Order = 0, Step = 0.01, Min = 0, Max = 1)
        float GetAlpha() const { return m_alpha; }
        void SetAlpha(float value);
        OV_PROPERTY(Serialize, Inspect, Name = "interactable", Setter = SetInteractable, DisplayName = "Interactable", Order = 1)
        bool IsInteractable() const { return m_interactable; }
        void SetInteractable(bool value) { m_interactable = value; }
        OV_PROPERTY(Serialize, Inspect, Name = "enabled", Setter = SetEnabled, DisplayName = "Enabled", Order = 4)
        bool IsEnabled() const { return m_enabled; }
        void SetEnabled(bool value) { m_enabled = value; }
        OV_PROPERTY(Serialize, Inspect, Name = "ignore_parent_groups", Setter = SetIgnoreParentGroups, DisplayName = "Ignore Parent Groups", Order = 3)
        bool IgnoresParentGroups() const { return m_ignoreParentGroups; }
        void SetIgnoreParentGroups(bool value) { m_ignoreParentGroups = value; }
        OV_PROPERTY(Serialize, Inspect, Name = "blocks_raycasts", Setter = SetBlocksRaycasts, DisplayName = "Blocks Raycasts", Order = 2)
        bool BlocksRaycasts() const { return m_blocksRaycasts; }
        void SetBlocksRaycasts(bool value) { m_blocksRaycasts = value; }
    private:
        float m_alpha = 1;
        bool m_interactable = true, m_blocksRaycasts = true, m_ignoreParentGroups = false, m_enabled = true;
    };

    class OV_TYPE(Component, Factory = AddComponent, Alias = "class OvCore::ECS::Components::CRectMask2D", DisplayName = "Rect Mask 2D", Order = 34) CRectMask2D : public AComponent
    {
    public:
        explicit CRectMask2D(Actor& actor);
        std::string GetName() override;
        std::string GetTypeName() override;
        OV_PROPERTY(Serialize, Inspect, Name = "enabled", Setter = SetEnabled, DisplayName = "Enabled", Order = 0)
        bool IsEnabled() const { return m_enabled; }
        void SetEnabled(bool value) { m_enabled = value; }
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
