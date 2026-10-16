#include <OvCore/ECS/Components/CUI.h>
#include <OvCore/ECS/Components/CCamera.h>
#include <OvCore/ECS/Actor.h>
#include <OvCore/Helpers/ReflectionDrawer.h>
#include <OvCore/Helpers/ReflectionResourceCodecs.h>
#include <OvCore/Helpers/GUIHelpers.h>
#include <OvCore/SceneSystem/Scene.h>
#include <OvUI/Plugins/DataDispatcher.h>
#include <OvUI/Plugins/DDTarget.h>
#include <OvUI/Widgets/InputFields/ActorField.h>
#include <OvUI/Widgets/Layout/TreeNode.h>
#include <OvUI/Widgets/Selection/ComboBox.h>
#include <OvUI/Widgets/Buttons/Button.h>
#include <tinyxml2.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

using namespace OvCore::ECS;
using namespace OvCore::ECS::Components;
using namespace OvCore::Helpers;
namespace UI = OvCore::UI;
namespace
{
    struct ImagePreview final : OvUI::Plugins::IPlugin
    {
        OvUI::Widgets::InputFields::AssetField& widget;
        std::function<OvUI::Internal::TextureID()> gather;
        ImagePreview(OvUI::Widgets::InputFields::AssetField& field, std::function<OvUI::Internal::TextureID()> read) : widget(field), gather(std::move(read)) {}
        void Execute(OvUI::Plugins::EPluginExecutionContext) override { widget.previewTextureID = gather(); }
    };
    void Finite(OvMaths::FVector2 value)
    { if (!UI::IsFinite(value)) throw std::invalid_argument("UI vector must be finite"); }
    void Positive(float value)
    { if (!std::isfinite(value) || value <= 0) throw std::invalid_argument("UI value must be positive and finite"); }
    float Unit(float value)
    { if (!std::isfinite(value)) throw std::invalid_argument("UI value must be finite"); return std::clamp(value, 0.0f, 1.0f); }
}
CRectTransform::CRectTransform(Actor& actor) : AComponent(actor) {}
CCanvas::CCanvas(Actor& actor) : AComponent(actor) { actor.AddComponent<CRectTransform>(); }
CUIImage::CUIImage(Actor& actor) : AComponent(actor) { actor.AddComponent<CRectTransform>(); }
CCanvasRenderer::CCanvasRenderer(Actor& actor) : AComponent(actor) { actor.AddComponent<CRectTransform>(); }
CCanvasGroup::CCanvasGroup(Actor& actor) : AComponent(actor) {}
CRectMask2D::CRectMask2D(Actor& actor) : AComponent(actor) { actor.AddComponent<CRectTransform>(); }

std::string CRectTransform::GetName() { return "Rect Transform"; }
std::string CRectTransform::GetTypeName() { return std::string(ComponentTraits<CRectTransform>::Name); }
void CRectTransform::OnInspector(OvUI::Internal::WidgetContainer& root)
{
    if (m_driven.Mask()) GUIDrawer::CreateTitle(root, "Driven by managed layout (mask " + std::to_string(m_driven.Mask()) + ")");

    // 1. Anchor Preset Selector
    GUIDrawer::CreateTitle(root, "Anchor Preset");
    auto& presetBtn = root.CreateWidget<OvUI::Widgets::Buttons::Button>(GetAnchorPresetName(GetAnchorPreset()));
    auto token = std::make_shared<bool>(true);
    presetBtn.ClickedEvent += [this, token, &presetBtn] {
        std::weak_ptr<bool> weak = token;
        GUIHelpers::PickerItemList items;
        const std::vector<std::pair<AnchorPreset, std::string>> presets = {
            { AnchorPreset::TopLeft, "Top - Left (Anchor: 0, 1 | Pivot: 0, 1)" },
            { AnchorPreset::TopCenter, "Top - Center (Anchor: 0.5, 1 | Pivot: 0.5, 1)" },
            { AnchorPreset::TopRight, "Top - Right (Anchor: 1, 1 | Pivot: 1, 1)" },
            { AnchorPreset::TopStretch, "Top - Stretch (Horizontal Stretch at Top)" },
            { AnchorPreset::MiddleLeft, "Middle - Left (Anchor: 0, 0.5 | Pivot: 0, 0.5)" },
            { AnchorPreset::MiddleCenter, "Middle - Center (Anchor: 0.5, 0.5 | Pivot: 0.5, 0.5)" },
            { AnchorPreset::MiddleRight, "Middle - Right (Anchor: 1, 0.5 | Pivot: 1, 0.5)" },
            { AnchorPreset::MiddleStretch, "Middle - Stretch (Horizontal Stretch in Center)" },
            { AnchorPreset::BottomLeft, "Bottom - Left (Anchor: 0, 0 | Pivot: 0, 0)" },
            { AnchorPreset::BottomCenter, "Bottom - Center (Anchor: 0.5, 0 | Pivot: 0.5, 0)" },
            { AnchorPreset::BottomRight, "Bottom - Right (Anchor: 1, 0 | Pivot: 1, 0)" },
            { AnchorPreset::BottomStretch, "Bottom - Stretch (Horizontal Stretch at Bottom)" },
            { AnchorPreset::StretchLeft, "Stretch - Left (Vertical Stretch on Left)" },
            { AnchorPreset::StretchCenter, "Stretch - Center (Vertical Stretch in Center)" },
            { AnchorPreset::StretchRight, "Stretch - Right (Vertical Stretch on Right)" },
            { AnchorPreset::StretchAll, "Stretch - All (Stretch and Fill Entire Parent)" },
        };
        for (const auto& [p, desc] : presets)
        {
            const auto name = GetAnchorPresetName(p);
            items.Add({
                name, name, desc, 0u,
                [this, p, weak, &presetBtn] {
                    if (weak.expired()) return;
                    ApplyAnchorPreset(p, true, true);
                    presetBtn.label = GetAnchorPresetName(p);
                }
            });
        }
        GUIHelpers::OpenPicker(std::move(items), "Select Anchor Preset");
    };

    // 2. Dynamic Point vs Stretch fields (matching Unity RectTransform inspector)
    const bool stretchX = m_layout.anchorMin.x != m_layout.anchorMax.x;
    const bool stretchY = m_layout.anchorMin.y != m_layout.anchorMax.y;

    if (!stretchX)
    {
        GUIDrawer::DrawScalar<float>(root, "Pos X", [this] { return m_layout.anchoredPosition.x; }, [this](float v) { SetAnchoredPosition({v, m_layout.anchoredPosition.y}); }, 1.0f);
        GUIDrawer::DrawScalar<float>(root, "Width", [this] { return m_layout.sizeDelta.x; }, [this](float v) { SetSizeDelta({v, m_layout.sizeDelta.y}); }, 1.0f);
    }
    else
    {
        GUIDrawer::DrawScalar<float>(root, "Left", [this] { return GetOffsetMin().x; }, [this](float v) { SetOffsetMin({v, GetOffsetMin().y}); }, 1.0f);
        GUIDrawer::DrawScalar<float>(root, "Right", [this] { return -GetOffsetMax().x; }, [this](float v) { SetOffsetMax({-v, GetOffsetMax().y}); }, 1.0f);
    }

    if (!stretchY)
    {
        GUIDrawer::DrawScalar<float>(root, "Pos Y", [this] { return m_layout.anchoredPosition.y; }, [this](float v) { SetAnchoredPosition({m_layout.anchoredPosition.x, v}); }, 1.0f);
        GUIDrawer::DrawScalar<float>(root, "Height", [this] { return m_layout.sizeDelta.y; }, [this](float v) { SetSizeDelta({m_layout.sizeDelta.x, v}); }, 1.0f);
    }
    else
    {
        GUIDrawer::DrawScalar<float>(root, "Top", [this] { return -GetOffsetMax().y; }, [this](float v) { SetOffsetMax({GetOffsetMax().x, -v}); }, 1.0f);
        GUIDrawer::DrawScalar<float>(root, "Bottom", [this] { return GetOffsetMin().y; }, [this](float v) { SetOffsetMin({GetOffsetMin().x, v}); }, 1.0f);
    }

    GUIDrawer::DrawScalar<float>(root, "Pos Z", [this] { return owner.transform.GetLocalPosition().z; }, [this](float v) { auto pos = owner.transform.GetLocalPosition(); pos.z = v; owner.transform.SetLocalPosition(pos); }, 1.0f);

    // 3. Raw Anchors & Pivot
    GUIDrawer::DrawVec2(root, "Anchor Min", [this] { return m_layout.anchorMin; }, [this](OvMaths::FVector2 v) { SetAnchorMin(v); }, 0.01f, 0.0f, 1.0f);
    GUIDrawer::DrawVec2(root, "Anchor Max", [this] { return m_layout.anchorMax; }, [this](OvMaths::FVector2 v) { SetAnchorMax(v); }, 0.01f, 0.0f, 1.0f);
    GUIDrawer::DrawVec2(root, "Pivot", [this] { return m_layout.pivot; }, [this](OvMaths::FVector2 v) { SetPivot(v); }, 0.01f, 0.0f, 1.0f);

    // 4. Rotation & Scale (matching Unity RectTransform layout)
    GUIDrawer::DrawQuat(root, "Rotation", [this] { return owner.transform.GetLocalRotation(); }, [this](OvMaths::FQuaternion q) { owner.transform.SetLocalRotation(q); });
    GUIDrawer::DrawVec3(root, "Scale", [this] { return owner.transform.GetLocalScale(); }, [this](OvMaths::FVector3 v) { owner.transform.SetLocalScale(v); }, 0.01f);
}

std::string CCanvas::GetName() { return "Canvas"; }
std::string CCanvas::GetTypeName() { return std::string(ComponentTraits<CCanvas>::Name); }
void CCanvas::OnInspector(OvUI::Internal::WidgetContainer& root)
{
    AComponent::OnInspector(root);
    GUIDrawer::CreateTitle(root, "Render Camera");
    auto& camera = root.CreateWidget<OvUI::Widgets::InputFields::ActorField>(m_worldCameraGuid);
    camera.iconTextureID = GUIHelpers::GetActorIconID();
    camera.displayFormatter = [this](uint64_t guid) {
        if (!guid) return std::string{};
        auto* scene = owner.GetScene(); auto* actor = scene ? scene->FindActorByGUID(guid) : nullptr;
        return actor ? actor->GetName() : std::string("(Missing)");
    };
    camera.tooltipFormatter = [](uint64_t guid) { return guid ? "Camera actor GUID: " + std::to_string(guid) : std::string{}; };
    camera.AddPlugin<OvUI::Plugins::DataDispatcher<uint64_t>>().RegisterGatherer([this] { return m_worldCameraGuid; });
    using ActorPayload = std::pair<OvCore::ECS::Actor*, OvUI::Widgets::Layout::TreeNode*>;
    camera.AddPlugin<OvUI::Plugins::DDTarget<ActorPayload>>("Actor").DataReceivedEvent += [this, &camera](ActorPayload payload) {
        if (payload.first && payload.first->GetComponent<CCamera>()) { camera.content = payload.first->GetGUID(); SetWorldCamera(payload.first->GetComponent<CCamera>()); }
    };
    camera.DoubleClickedEvent += [&camera] { GUIHelpers::SelectActor(camera.content); };
    auto cameraWidget = std::shared_ptr<OvUI::Widgets::InputFields::ActorField>(&camera, [](void*) {});
    auto cameraToken = std::make_shared<bool>(true);
    camera.ClickedEvent += [this, cameraWidget, cameraToken] {
        auto* scene = owner.GetScene(); if (!scene) return;
        std::weak_ptr<bool> weak = cameraToken;
        GUIHelpers::PickerItemList items;
        items.Add({"__none__", "None", "Clear the render camera", 0u, [this, cameraWidget, weak] {
            if (weak.expired()) return; cameraWidget->content = 0; SetWorldCamera(nullptr);
        }});
        for (auto* actor : scene->GetActors())
        {
            if (!actor->GetComponent<CCamera>()) continue;
            const auto guid = actor->GetGUID();
            items.Add({std::to_string(guid), actor->GetName(), "Camera actor GUID: " + std::to_string(guid),
                GUIHelpers::GetActorIconID(), [this, cameraWidget, guid, weak] {
                    if (weak.expired()) return;
                    if (auto* scene = owner.GetScene()) if (auto* actor = scene->FindActorByGUID(guid))
                    { cameraWidget->content = guid; SetWorldCamera(actor->GetComponent<CCamera>()); }
                }});
        }
        GUIHelpers::OpenPicker(std::move(items), "Select Render Camera");
    };

}

std::string CUIImage::GetName() { return "UI Image"; }
std::string CUIImage::GetTypeName() { return std::string(ComponentTraits<CUIImage>::Name); }
void CUIImage::OnInspector(OvUI::Internal::WidgetContainer& root)
{
    AComponent::OnInspector(root);
    auto& texture = ReflectionProperties::DrawResourceReference<OvRendering::Resources::Texture>(root, "Texture", InspectorTicket(),
        [this] { return GetSerializedTexture(); }, [this](const auto& value) { SetSerializedTexture(value); }, OvTools::Utils::PathParser::EFileType::TEXTURE);
    texture.AddPlugin<ImagePreview>(texture, [this, ticket = InspectorTicket(), main = &GetComponentRegistry().Reflection().Main()]() -> OvUI::Internal::TextureID {
        if (!main->IsCurrent() || ticket.expired()) return {};
        auto* asset = m_texture ? m_texture : GUIHelpers::GetEmptyTexture();
        return asset ? OvUI::Internal::TextureID(asset->RetainTexture()) : OvUI::Internal::TextureID{};
    });

    GUIDrawer::CreateTitle(root, "Image Type");
    auto& typeCombo = root.CreateWidget<OvUI::Widgets::Selection::ComboBox>(static_cast<int>(GetImageType()));
    typeCombo.choices.emplace(0, "Simple");
    typeCombo.choices.emplace(1, "Sliced");
    typeCombo.choices.emplace(2, "Tiled");
    typeCombo.choices.emplace(3, "Filled");
    typeCombo.ValueChangedEvent += [this](int choice) {
        SetImageType(static_cast<ImageType>(choice));
        if (auto* rect = owner.GetComponent<CRectTransform>())
            rect->LayoutChangedEvent.Invoke();
    };

    if (m_type == ImageType::Sliced || m_type == ImageType::Tiled)
    {
        GUIDrawer::DrawBoolean(root, "Fill Center", [this] { return m_fillCenter; }, [this](bool v) { m_fillCenter = v; });
        GUIDrawer::DrawScalar<float>(root, "Border Left", [this] { return m_border.x; }, [this](float v) { m_border.x = std::max(0.0f, v); }, 1.0f);
        GUIDrawer::DrawScalar<float>(root, "Border Bottom", [this] { return m_border.y; }, [this](float v) { m_border.y = std::max(0.0f, v); }, 1.0f);
        GUIDrawer::DrawScalar<float>(root, "Border Right", [this] { return m_border.z; }, [this](float v) { m_border.z = std::max(0.0f, v); }, 1.0f);
        GUIDrawer::DrawScalar<float>(root, "Border Top", [this] { return m_border.w; }, [this](float v) { m_border.w = std::max(0.0f, v); }, 1.0f);
    }
}

std::string CCanvasRenderer::GetName() { return "Canvas Renderer"; }
std::string CCanvasRenderer::GetTypeName() { return std::string(ComponentTraits<CCanvasRenderer>::Name); }
void CCanvasRenderer::SetMesh(std::span<const UIVertex> vertices, std::span<const uint32_t> indices)
{
    constexpr size_t maximumVertices = 65535;
    constexpr size_t maximumIndices = maximumVertices * 6;
    if (vertices.size() > maximumVertices || indices.size() > maximumIndices || indices.size() % 3 != 0)
        throw std::invalid_argument("Invalid UI mesh size");
    for (const auto& vertex : vertices)
    {
        if (!std::isfinite(vertex.position.x) || !std::isfinite(vertex.position.y) || !std::isfinite(vertex.position.z) ||
            !std::isfinite(vertex.uv.x) || !std::isfinite(vertex.uv.y) ||
            !std::isfinite(vertex.color.x) || !std::isfinite(vertex.color.y) ||
            !std::isfinite(vertex.color.z) || !std::isfinite(vertex.color.w) ||
            !std::isfinite(vertex.normal.x) || !std::isfinite(vertex.normal.y) || !std::isfinite(vertex.normal.z) ||
            !std::isfinite(vertex.tangent.x) || !std::isfinite(vertex.tangent.y) || !std::isfinite(vertex.tangent.z) || !std::isfinite(vertex.tangent.w) ||
            !UI::IsFinite(vertex.uv1) || !UI::IsFinite(vertex.uv2) || !UI::IsFinite(vertex.uv3))
            throw std::invalid_argument("UI mesh contains a non-finite vertex");
    }
    for (const auto index : indices) if (index >= vertices.size())
        throw std::invalid_argument("UI mesh index is out of range");
    m_vertices.assign(vertices.begin(), vertices.end());
    m_indices.assign(indices.begin(), indices.end());
    ++m_meshGeneration;
}
void CCanvasRenderer::Clear()
{
    m_vertices.clear(); m_indices.clear(); m_texturePath.clear(); m_materialPath.clear();
    m_slots.assign(1, {}); m_popSlots.clear(); m_stencil = {}; m_popStencil = {}; m_hitFrame.reset(); m_cull = false; m_hasClip = false; m_managedRectClipping = false; m_depth = -1; ++m_meshGeneration;
}
void CCanvasRenderer::SetTexturePath(std::string value) { SetSlotPath(0, false, std::move(value)); }
void CCanvasRenderer::SetMaterialPath(std::string value) { SetSlotPath(0, true, std::move(value)); }
void CCanvasRenderer::SetMaterialCount(size_t count, bool pop)
{
    if (count > MaximumMaterialSlots) throw std::invalid_argument("Too many UI material slots");
    (pop ? m_popSlots : m_slots).resize(count); ++m_meshGeneration;
}
void CCanvasRenderer::SetSlotPath(size_t slot, bool material, std::string path, bool pop)
{
    auto& slots = pop ? m_popSlots : m_slots;
    if (slot >= slots.size()) throw std::out_of_range("UI material slot");
    auto& target = material ? slots[slot].materialPath : slots[slot].texturePath;
    if (target == path) return;
    target = std::move(path);
    if (!pop && slot == 0) (material ? m_materialPath : m_texturePath) = target;
    ++m_meshGeneration;
}
void CCanvasRenderer::SetIndexRange(size_t slot, uint32_t first, uint32_t count)
{
    if (slot >= m_slots.size() || first % 3 || count % 3 || first > m_indices.size() || count > m_indices.size()-first)
        throw std::invalid_argument("Invalid UI submesh range");
    auto& range = m_slots[slot]; range.firstIndex = first; range.indexCount = count; range.explicitRange = true;
    ++m_meshGeneration;
}
void CCanvasRenderer::SetFontPage(size_t slot, std::shared_ptr<const UI::Fonts::AtlasPage> page)
{
    if (slot >= m_slots.size()) throw std::out_of_range("UI font slot");
    m_slots[slot].fontPage = std::move(page); ++m_meshGeneration;
}
void CCanvasRenderer::SetColor(OvMaths::FVector4 value)
{ m_color = {Unit(value.x), Unit(value.y), Unit(value.z), Unit(value.w)}; }
void CCanvasRenderer::SetClipRect(UI::Rect value)
{ Finite(value.min); Finite(value.size); m_clip = value; m_hasClip = true; }
// CanvasRenderer data is generated by Graphic and is intentionally transient.
void CCanvasRenderer::OnSerialize(tinyxml2::XMLDocument&, tinyxml2::XMLNode*) {}
void CCanvasRenderer::OnDeserialize(tinyxml2::XMLDocument&, tinyxml2::XMLNode*) { Clear(); }
void CCanvasRenderer::OnInspector(OvUI::Internal::WidgetContainer& root)
{
    GUIDrawer::CreateTitle(root, "Managed Graphic output: " + std::to_string(m_vertices.size()) + " vertices, " +
        std::to_string(m_indices.size()) + " indices");
}

std::string CCanvasGroup::GetName() { return "Canvas Group"; }
std::string CCanvasGroup::GetTypeName() { return std::string(ComponentTraits<CCanvasGroup>::Name); }
std::string CRectMask2D::GetName() { return "Rect Mask 2D"; }
std::string CRectMask2D::GetTypeName() { return std::string(ComponentTraits<CRectMask2D>::Name); }
void CRectTransform::SetAnchorMin(OvMaths::FVector2 value)
{
    Finite(value);
    const bool wasStretchX = m_layout.anchorMin.x != m_layout.anchorMax.x;
    const bool wasStretchY = m_layout.anchorMin.y != m_layout.anchorMax.y;
    m_layout.anchorMin = value;
    const bool isStretchX = m_layout.anchorMin.x != m_layout.anchorMax.x;
    const bool isStretchY = m_layout.anchorMin.y != m_layout.anchorMax.y;
    if (wasStretchX != isStretchX || wasStretchY != isStretchY) LayoutChangedEvent.Invoke();
}

void CRectTransform::SetAnchorMax(OvMaths::FVector2 value)
{
    Finite(value);
    const bool wasStretchX = m_layout.anchorMin.x != m_layout.anchorMax.x;
    const bool wasStretchY = m_layout.anchorMin.y != m_layout.anchorMax.y;
    m_layout.anchorMax = value;
    const bool isStretchX = m_layout.anchorMin.x != m_layout.anchorMax.x;
    const bool isStretchY = m_layout.anchorMin.y != m_layout.anchorMax.y;
    if (wasStretchX != isStretchX || wasStretchY != isStretchY) LayoutChangedEvent.Invoke();
}

CRectTransform::AnchorPreset CRectTransform::GetAnchorPreset() const
{
    auto match = [](float a, float b) { return std::abs(a - b) < 0.002f; };
    const auto& min = m_layout.anchorMin;
    const auto& max = m_layout.anchorMax;

    if (match(min.x, 0.0f) && match(max.x, 0.0f)) {
        if (match(min.y, 1.0f) && match(max.y, 1.0f)) return AnchorPreset::TopLeft;
        if (match(min.y, 0.5f) && match(max.y, 0.5f)) return AnchorPreset::MiddleLeft;
        if (match(min.y, 0.0f) && match(max.y, 0.0f)) return AnchorPreset::BottomLeft;
        if (match(min.y, 0.0f) && match(max.y, 1.0f)) return AnchorPreset::StretchLeft;
    }
    if (match(min.x, 0.5f) && match(max.x, 0.5f)) {
        if (match(min.y, 1.0f) && match(max.y, 1.0f)) return AnchorPreset::TopCenter;
        if (match(min.y, 0.5f) && match(max.y, 0.5f)) return AnchorPreset::MiddleCenter;
        if (match(min.y, 0.0f) && match(max.y, 0.0f)) return AnchorPreset::BottomCenter;
        if (match(min.y, 0.0f) && match(max.y, 1.0f)) return AnchorPreset::StretchCenter;
    }
    if (match(min.x, 1.0f) && match(max.x, 1.0f)) {
        if (match(min.y, 1.0f) && match(max.y, 1.0f)) return AnchorPreset::TopRight;
        if (match(min.y, 0.5f) && match(max.y, 0.5f)) return AnchorPreset::MiddleRight;
        if (match(min.y, 0.0f) && match(max.y, 0.0f)) return AnchorPreset::BottomRight;
        if (match(min.y, 0.0f) && match(max.y, 1.0f)) return AnchorPreset::StretchRight;
    }
    if (match(min.x, 0.0f) && match(max.x, 1.0f)) {
        if (match(min.y, 1.0f) && match(max.y, 1.0f)) return AnchorPreset::TopStretch;
        if (match(min.y, 0.5f) && match(max.y, 0.5f)) return AnchorPreset::MiddleStretch;
        if (match(min.y, 0.0f) && match(max.y, 0.0f)) return AnchorPreset::BottomStretch;
        if (match(min.y, 0.0f) && match(max.y, 1.0f)) return AnchorPreset::StretchAll;
    }
    return AnchorPreset::Custom;
}

std::string CRectTransform::GetAnchorPresetName(AnchorPreset preset)
{
    switch (preset) {
        case AnchorPreset::TopLeft: return "Top Left";
        case AnchorPreset::TopCenter: return "Top Center";
        case AnchorPreset::TopRight: return "Top Right";
        case AnchorPreset::TopStretch: return "Top Stretch";
        case AnchorPreset::MiddleLeft: return "Middle Left";
        case AnchorPreset::MiddleCenter: return "Middle Center";
        case AnchorPreset::MiddleRight: return "Middle Right";
        case AnchorPreset::MiddleStretch: return "Middle Stretch";
        case AnchorPreset::BottomLeft: return "Bottom Left";
        case AnchorPreset::BottomCenter: return "Bottom Center";
        case AnchorPreset::BottomRight: return "Bottom Right";
        case AnchorPreset::BottomStretch: return "Bottom Stretch";
        case AnchorPreset::StretchLeft: return "Stretch Left";
        case AnchorPreset::StretchCenter: return "Stretch Center";
        case AnchorPreset::StretchRight: return "Stretch Right";
        case AnchorPreset::StretchAll: return "Stretch All";
        default: return "Custom";
    }
}

void CRectTransform::ApplyAnchorPreset(AnchorPreset preset, bool setPivot, bool setPosition)
{
    OvMaths::FVector2 newMin = m_layout.anchorMin, newMax = m_layout.anchorMax;
    OvMaths::FVector2 newPivot = m_layout.pivot;

    switch (preset) {
        case AnchorPreset::TopLeft:       newMin = {0.0f, 1.0f}; newMax = {0.0f, 1.0f}; newPivot = {0.0f, 1.0f}; break;
        case AnchorPreset::TopCenter:     newMin = {0.5f, 1.0f}; newMax = {0.5f, 1.0f}; newPivot = {0.5f, 1.0f}; break;
        case AnchorPreset::TopRight:      newMin = {1.0f, 1.0f}; newMax = {1.0f, 1.0f}; newPivot = {1.0f, 1.0f}; break;
        case AnchorPreset::TopStretch:    newMin = {0.0f, 1.0f}; newMax = {1.0f, 1.0f}; newPivot = {0.5f, 1.0f}; break;
        case AnchorPreset::MiddleLeft:    newMin = {0.0f, 0.5f}; newMax = {0.0f, 0.5f}; newPivot = {0.0f, 0.5f}; break;
        case AnchorPreset::MiddleCenter:  newMin = {0.5f, 0.5f}; newMax = {0.5f, 0.5f}; newPivot = {0.5f, 0.5f}; break;
        case AnchorPreset::MiddleRight:   newMin = {1.0f, 0.5f}; newMax = {1.0f, 0.5f}; newPivot = {1.0f, 0.5f}; break;
        case AnchorPreset::MiddleStretch: newMin = {0.0f, 0.5f}; newMax = {1.0f, 0.5f}; newPivot = {0.5f, 0.5f}; break;
        case AnchorPreset::BottomLeft:    newMin = {0.0f, 0.0f}; newMax = {0.0f, 0.0f}; newPivot = {0.0f, 0.0f}; break;
        case AnchorPreset::BottomCenter:  newMin = {0.5f, 0.0f}; newMax = {0.5f, 0.0f}; newPivot = {0.5f, 0.0f}; break;
        case AnchorPreset::BottomRight:   newMin = {1.0f, 0.0f}; newMax = {1.0f, 0.0f}; newPivot = {1.0f, 0.0f}; break;
        case AnchorPreset::BottomStretch: newMin = {0.0f, 0.0f}; newMax = {1.0f, 0.0f}; newPivot = {0.5f, 0.0f}; break;
        case AnchorPreset::StretchLeft:   newMin = {0.0f, 0.0f}; newMax = {0.0f, 1.0f}; newPivot = {0.0f, 0.5f}; break;
        case AnchorPreset::StretchCenter: newMin = {0.5f, 0.0f}; newMax = {0.5f, 1.0f}; newPivot = {0.5f, 0.5f}; break;
        case AnchorPreset::StretchRight:  newMin = {1.0f, 0.0f}; newMax = {1.0f, 1.0f}; newPivot = {1.0f, 0.5f}; break;
        case AnchorPreset::StretchAll:    newMin = {0.0f, 0.0f}; newMax = {1.0f, 1.0f}; newPivot = {0.5f, 0.5f}; break;
        default: return;
    }

    SetAnchorMin(newMin);
    SetAnchorMax(newMax);

    if (setPivot)
    {
        SetPivot(newPivot);
    }

    if (setPosition)
    {
        const bool stretchH = newMin.x != newMax.x;
        const bool stretchV = newMin.y != newMax.y;
        if (stretchH) {
            SetOffsetMin({0.0f, GetOffsetMin().y});
            SetOffsetMax({0.0f, GetOffsetMax().y});
        } else {
            SetAnchoredPosition({0.0f, m_layout.anchoredPosition.y});
        }
        if (stretchV) {
            SetOffsetMin({GetOffsetMin().x, 0.0f});
            SetOffsetMax({GetOffsetMax().x, 0.0f});
        } else {
            SetAnchoredPosition({m_layout.anchoredPosition.x, 0.0f});
        }
    }

    LayoutChangedEvent.Invoke();
}
void CRectTransform::SetPivot(OvMaths::FVector2 value) { Finite(value); m_layout.pivot = value; }
void CRectTransform::SetAnchoredPosition(OvMaths::FVector2 value) { Finite(value); m_layout.anchoredPosition = value; }
void CRectTransform::SetSizeDelta(OvMaths::FVector2 value) { Finite(value); m_layout.sizeDelta = value; }
OvMaths::FVector2 CRectTransform::GetOffsetMin() const
{ return {m_layout.anchoredPosition.x - m_layout.sizeDelta.x * m_layout.pivot.x,
    m_layout.anchoredPosition.y - m_layout.sizeDelta.y * m_layout.pivot.y}; }
OvMaths::FVector2 CRectTransform::GetOffsetMax() const
{ return {m_layout.anchoredPosition.x + m_layout.sizeDelta.x * (1 - m_layout.pivot.x),
    m_layout.anchoredPosition.y + m_layout.sizeDelta.y * (1 - m_layout.pivot.y)}; }
void CRectTransform::SetOffsetMin(OvMaths::FVector2 value)
{
    Finite(value); const auto change = value - GetOffsetMin();
    SetSizeDelta(m_layout.sizeDelta - change);
    SetAnchoredPosition(m_layout.anchoredPosition + OvMaths::FVector2{
        change.x * (1 - m_layout.pivot.x), change.y * (1 - m_layout.pivot.y)});
}
void CRectTransform::SetOffsetMax(OvMaths::FVector2 value)
{
    Finite(value); const auto change = value - GetOffsetMax();
    SetSizeDelta(m_layout.sizeDelta + change);
    SetAnchoredPosition(m_layout.anchoredPosition + OvMaths::FVector2{
        change.x * m_layout.pivot.x, change.y * m_layout.pivot.y});
}
void CRectTransform::SetInsetAndSizeFromParentEdge(Edge edge, float inset, float size)
{
    if (!std::isfinite(inset) || !std::isfinite(size)) throw std::invalid_argument("Invalid UI inset");
    if (edge < Edge::Left || edge > Edge::Bottom) throw std::invalid_argument("Invalid UI edge");
    const bool horizontal = edge == Edge::Left || edge == Edge::Right;
    const bool far = edge == Edge::Right || edge == Edge::Top;
    auto low = m_layout.anchorMin, high = m_layout.anchorMax;
    auto delta = m_layout.sizeDelta, position = m_layout.anchoredPosition;
    if (horizontal)
    {
        low.x = high.x = far ? 1.0f : 0.0f; delta.x = size;
        position.x = far ? -inset - size * (1 - m_layout.pivot.x) : inset + size * m_layout.pivot.x;
    }
    else
    {
        low.y = high.y = far ? 1.0f : 0.0f; delta.y = size;
        position.y = far ? -inset - size * (1 - m_layout.pivot.y) : inset + size * m_layout.pivot.y;
    }
    SetAnchorMin(low); SetAnchorMax(high); SetSizeDelta(delta); SetAnchoredPosition(position);
}
void CRectTransform::SetSizeWithCurrentAnchors(Axis axis, float size)
{
    if (!std::isfinite(size)) throw std::invalid_argument("Invalid UI size");
    auto delta = m_layout.sizeDelta;
    const auto parent = GetParentSize();
    if (axis == Axis::Horizontal) delta.x = size - parent.x * (m_layout.anchorMax.x - m_layout.anchorMin.x);
    else if (axis == Axis::Vertical) delta.y = size - parent.y * (m_layout.anchorMax.y - m_layout.anchorMin.y);
    else throw std::invalid_argument("Invalid UI axis");
    SetSizeDelta(delta);
}
void CRectTransform::SetLayoutContext(OvMaths::FVector2 parentSize, bool rootCanvas)
{ Finite(parentSize); m_parentSize = parentSize; m_rootCanvas = rootCanvas; }
UI::Rect CRectTransform::GetRect() const
{
    const auto size = m_rootCanvas ? m_parentSize : Resolve({{0, 0}, GetParentSize()}).size;
    return {{-size.x * m_layout.pivot.x, -size.y * m_layout.pivot.y}, size};
}
OvMaths::FVector2 CRectTransform::GetParentSize() const
{
    if (!m_rootCanvas)
        if (const auto* parent = owner.GetParent())
            if (const auto* rect = parent->GetComponent<CRectTransform>()) return rect->GetRect().size;
    return m_parentSize;
}
std::array<OvMaths::FVector2, 4> CRectTransform::GetLocalCorners() const
{
    const auto r = GetRect(); const auto end = r.min + r.size;
    return {r.min, OvMaths::FVector2{r.min.x, end.y}, end, OvMaths::FVector2{end.x, r.min.y}};
}
void CCanvas::SetScaleFactor(float value) { Positive(value); m_scaleFactor = value; }
void CCanvas::SetRenderMode(RenderMode value)
{
    if (value < RenderMode::ScreenSpaceOverlay || value > RenderMode::WorldSpace)
        throw std::invalid_argument("Invalid canvas render mode");
    m_renderMode = value;
}
void CCanvas::SetPlaneDistance(float value) { Positive(value); m_planeDistance = value; }
void CCanvas::SetWorldCameraGuid(uint64_t value) { m_worldCameraGuid = value; }
CCamera* CCanvas::GetWorldCamera() const
{
    auto* scene = owner.GetScene();
    auto* actor = scene && m_worldCameraGuid ? scene->FindActorByGUID(m_worldCameraGuid) : nullptr;
    return actor ? actor->GetComponent<CCamera>() : nullptr;
}
void CCanvas::SetWorldCamera(CCamera* value)
{
    if (value && value->owner.GetScene() != owner.GetScene()) throw std::invalid_argument("Canvas camera belongs to another scene");
    m_worldCameraGuid = value ? value->owner.GetGUID() : 0;
}
void CUIImage::SetColor(OvMaths::FVector4 value)
{
    if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z) || !std::isfinite(value.w))
        throw std::invalid_argument("UI color must be finite");
    value.w = Unit(value.w); m_color = value;
}
void CCanvasGroup::SetAlpha(float value) { m_alpha = Unit(value); }

void CRectTransform::SetAuthoredLayout(const UI::RectLayout& value)
{
    Finite(value.anchorMin); Finite(value.anchorMax); Finite(value.pivot);
    Finite(value.anchoredPosition); Finite(value.sizeDelta);
    m_driven.SetAuthored(m_layout, value);
}
OvCore::Resources::ResourceReference<OvRendering::Resources::Texture> CUIImage::GetSerializedTexture() const
{ return Resources::ResourceReference<OvRendering::Resources::Texture>::From(m_texture, m_texturePath); }
void CUIImage::SetSerializedTexture(const Resources::ResourceReference<OvRendering::Resources::Texture>& value)
{ SetTexture(value.resource); m_texturePath = value.path; }
