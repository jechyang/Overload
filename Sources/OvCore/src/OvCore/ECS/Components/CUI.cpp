#include <OvCore/ECS/Components/CUI.h>
#include <OvCore/ECS/Components/CCamera.h>
#include <OvCore/ECS/Actor.h>
#include <OvCore/Helpers/Serializer.h>
#include <OvCore/Helpers/GUIHelpers.h>
#include <OvCore/SceneSystem/Scene.h>
#include <OvUI/Plugins/DataDispatcher.h>
#include <OvUI/Plugins/DDTarget.h>
#include <OvUI/Widgets/InputFields/ActorField.h>
#include <OvUI/Widgets/Layout/TreeNode.h>
#include <OvUI/Widgets/Selection/ComboBox.h>
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
void CRectTransform::OnSerialize(tinyxml2::XMLDocument& doc, tinyxml2::XMLNode* node)
{
    Serializer::SerializeVec2(doc, node, "anchor_min", m_layout.anchorMin);
    Serializer::SerializeVec2(doc, node, "anchor_max", m_layout.anchorMax);
    Serializer::SerializeVec2(doc, node, "pivot", m_layout.pivot);
    Serializer::SerializeVec2(doc, node, "anchored_position", m_layout.anchoredPosition);
    Serializer::SerializeVec2(doc, node, "size_delta", m_layout.sizeDelta);
}
void CRectTransform::OnDeserialize(tinyxml2::XMLDocument& doc, tinyxml2::XMLNode* node)
{
    if (node->FirstChildElement("anchor_min")) { auto value = m_layout.anchorMin; Serializer::DeserializeVec2(doc, node, "anchor_min", value); SetAnchorMin(value); }
    if (node->FirstChildElement("anchor_max")) { auto value = m_layout.anchorMax; Serializer::DeserializeVec2(doc, node, "anchor_max", value); SetAnchorMax(value); }
    if (node->FirstChildElement("pivot")) { auto value = m_layout.pivot; Serializer::DeserializeVec2(doc, node, "pivot", value); SetPivot(value); }
    if (node->FirstChildElement("anchored_position")) { auto value = m_layout.anchoredPosition; Serializer::DeserializeVec2(doc, node, "anchored_position", value); SetAnchoredPosition(value); }
    if (node->FirstChildElement("size_delta")) { auto value = m_layout.sizeDelta; Serializer::DeserializeVec2(doc, node, "size_delta", value); SetSizeDelta(value); }
}
void CRectTransform::OnInspector(OvUI::Internal::WidgetContainer& root)
{
    GUIDrawer::DrawVec2(root, "Anchor Min", std::function<OvMaths::FVector2()>([this] { return m_layout.anchorMin; }), std::function<void(OvMaths::FVector2)>([this](OvMaths::FVector2 value) { SetAnchorMin(value); }), 0.01f);
    GUIDrawer::DrawVec2(root, "Anchor Max", std::function<OvMaths::FVector2()>([this] { return m_layout.anchorMax; }), std::function<void(OvMaths::FVector2)>([this](OvMaths::FVector2 value) { SetAnchorMax(value); }), 0.01f);
    GUIDrawer::DrawVec2(root, "Pivot", std::function<OvMaths::FVector2()>([this] { return m_layout.pivot; }), std::function<void(OvMaths::FVector2)>([this](OvMaths::FVector2 value) { SetPivot(value); }), 0.01f);
    GUIDrawer::DrawVec2(root, "Anchored Position", std::function<OvMaths::FVector2()>([this] { return m_layout.anchoredPosition; }), std::function<void(OvMaths::FVector2)>([this](OvMaths::FVector2 value) { SetAnchoredPosition(value); }), 0.01f);
    GUIDrawer::DrawVec2(root, "Size Delta", std::function<OvMaths::FVector2()>([this] { return m_layout.sizeDelta; }), std::function<void(OvMaths::FVector2)>([this](OvMaths::FVector2 value) { SetSizeDelta(value); }), 0.01f);
}

std::string CCanvas::GetName() { return "Canvas"; }
std::string CCanvas::GetTypeName() { return std::string(ComponentTraits<CCanvas>::Name); }
void CCanvas::OnSerialize(tinyxml2::XMLDocument& doc, tinyxml2::XMLNode* node)
{
    Serializer::SerializeInt(doc, node, "render_mode", static_cast<int>(m_renderMode));
    Serializer::SerializeFloat(doc, node, "plane_distance", m_planeDistance);
    Serializer::SerializeUInt64(doc, node, "world_camera", m_worldCameraGuid);
    Serializer::SerializeFloat(doc, node, "scale_factor", m_scaleFactor);
    Serializer::SerializeInt(doc, node, "sorting_order", m_sortingOrder);
    Serializer::SerializeBoolean(doc, node, "override_sorting", m_overrideSorting);
}
void CCanvas::OnDeserialize(tinyxml2::XMLDocument& doc, tinyxml2::XMLNode* node)
{
    if (node->FirstChildElement("render_mode")) { auto value = static_cast<int>(m_renderMode); Serializer::DeserializeInt(doc, node, "render_mode", value); SetRenderMode(static_cast<RenderMode>(value)); }
    if (node->FirstChildElement("plane_distance")) { auto value = m_planeDistance; Serializer::DeserializeFloat(doc, node, "plane_distance", value); SetPlaneDistance(value); }
    if (node->FirstChildElement("world_camera")) Serializer::DeserializeUInt64(doc, node, "world_camera", m_worldCameraGuid);
    if (node->FirstChildElement("scale_factor")) { auto value = m_scaleFactor; Serializer::DeserializeFloat(doc, node, "scale_factor", value); SetScaleFactor(value); }
    if (node->FirstChildElement("sorting_order")) { auto value = m_sortingOrder; Serializer::DeserializeInt(doc, node, "sorting_order", value); SetSortingOrder(value); }
    if (node->FirstChildElement("override_sorting")) Serializer::DeserializeBoolean(doc, node, "override_sorting", m_overrideSorting);
}
void CCanvas::OnInspector(OvUI::Internal::WidgetContainer& root)
{
    GUIDrawer::CreateTitle(root, "Render Mode");
    auto& mode = root.CreateWidget<OvUI::Widgets::Selection::ComboBox>(static_cast<int>(m_renderMode));
    mode.choices.emplace(0, "Screen Space - Overlay");
    mode.choices.emplace(1, "Screen Space - Camera");
    mode.choices.emplace(2, "World Space");
    mode.ValueChangedEvent += [this](int value) { SetRenderMode(static_cast<RenderMode>(value)); };
    GUIDrawer::DrawScalar<float>(root, "Plane Distance", std::function<float()>([this] { return m_planeDistance; }), std::function<void(float)>([this](float value) { SetPlaneDistance(value); }), 0.1f, 0.001f, 1000000.0f);
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
    GUIDrawer::DrawScalar<float>(root, "Scale Factor", std::function<float()>([this] { return m_scaleFactor; }), std::function<void(float)>([this](float value) { SetScaleFactor(value); }), 0.01f, 0.001f, 10000.0f);
    GUIDrawer::DrawScalar<int>(root, "Sorting Order", std::function<int()>([this] { return m_sortingOrder; }), std::function<void(int)>([this](int value) { SetSortingOrder(value); }), 1.0f, std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
    GUIDrawer::DrawBoolean(root, "Override Sorting", m_overrideSorting);

}

std::string CUIImage::GetName() { return "UI Image"; }
std::string CUIImage::GetTypeName() { return std::string(ComponentTraits<CUIImage>::Name); }
void CUIImage::OnSerialize(tinyxml2::XMLDocument& doc, tinyxml2::XMLNode* node)
{
    Serializer::SerializeVec4(doc, node, "color", m_color);
    Serializer::SerializeTexture(doc, node, "texture", m_texture);
    Serializer::SerializeBoolean(doc, node, "raycast_target", m_raycastTarget);
}
void CUIImage::OnDeserialize(tinyxml2::XMLDocument& doc, tinyxml2::XMLNode* node)
{
    if (node->FirstChildElement("color")) { auto value = m_color; Serializer::DeserializeVec4(doc, node, "color", value); SetColor(value); }
    if (node->FirstChildElement("texture")) { auto value = m_texture; Serializer::DeserializeTexture(doc, node, "texture", value); SetTexture(value); }
    if (node->FirstChildElement("raycast_target")) { auto value = m_raycastTarget; Serializer::DeserializeBoolean(doc, node, "raycast_target", value); SetRaycastTarget(value); }
}
void CUIImage::OnInspector(OvUI::Internal::WidgetContainer& root)
{
    GUIDrawer::DrawVec4(root, "Color", std::function<OvMaths::FVector4()>([this] { return m_color; }), std::function<void(OvMaths::FVector4)>([this](OvMaths::FVector4 value) { SetColor(value); }), 0.01f);
    GUIDrawer::DrawTexture(root, "Texture", m_texture);
    GUIDrawer::DrawBoolean(root, "Raycast Target", m_raycastTarget);
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
            !std::isfinite(vertex.color.z) || !std::isfinite(vertex.color.w))
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
    m_vertices.clear(); m_indices.clear(); m_texturePath.clear(); m_materialPath.clear(); ++m_meshGeneration;
}
void CCanvasRenderer::SetTexturePath(std::string value) { m_texturePath = std::move(value); }
void CCanvasRenderer::SetMaterialPath(std::string value) { m_materialPath = std::move(value); }
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
void CCanvasGroup::OnSerialize(tinyxml2::XMLDocument& doc, tinyxml2::XMLNode* node)
{
    Serializer::SerializeFloat(doc, node, "alpha", m_alpha);
    Serializer::SerializeBoolean(doc, node, "interactable", m_interactable);
    Serializer::SerializeBoolean(doc, node, "blocks_raycasts", m_blocksRaycasts);
}
void CCanvasGroup::OnDeserialize(tinyxml2::XMLDocument& doc, tinyxml2::XMLNode* node)
{
    if (node->FirstChildElement("alpha")) { auto value = m_alpha; Serializer::DeserializeFloat(doc, node, "alpha", value); SetAlpha(value); }
    if (node->FirstChildElement("interactable")) { auto value = m_interactable; Serializer::DeserializeBoolean(doc, node, "interactable", value); SetInteractable(value); }
    if (node->FirstChildElement("blocks_raycasts")) { auto value = m_blocksRaycasts; Serializer::DeserializeBoolean(doc, node, "blocks_raycasts", value); SetBlocksRaycasts(value); }
}
void CCanvasGroup::OnInspector(OvUI::Internal::WidgetContainer& root)
{
    GUIDrawer::DrawScalar<float>(root, "Alpha", std::function<float()>([this] { return m_alpha; }), std::function<void(float)>([this](float value) { SetAlpha(value); }), 0.01f, 0.0f, 1.0f);
    GUIDrawer::DrawBoolean(root, "Interactable", m_interactable);
    GUIDrawer::DrawBoolean(root, "Blocks Raycasts", m_blocksRaycasts);
}

std::string CRectMask2D::GetName() { return "Rect Mask 2D"; }
std::string CRectMask2D::GetTypeName() { return std::string(ComponentTraits<CRectMask2D>::Name); }
void CRectMask2D::OnSerialize(tinyxml2::XMLDocument& doc, tinyxml2::XMLNode* node)
{
    Serializer::SerializeBoolean(doc, node, "enabled", m_enabled);
}
void CRectMask2D::OnDeserialize(tinyxml2::XMLDocument& doc, tinyxml2::XMLNode* node)
{
    if (node->FirstChildElement("enabled")) { auto value = m_enabled; Serializer::DeserializeBoolean(doc, node, "enabled", value); SetEnabled(value); }
}
void CRectMask2D::OnInspector(OvUI::Internal::WidgetContainer& root)
{
    GUIDrawer::DrawBoolean(root, "Enabled", m_enabled);
}
void CRectTransform::SetAnchorMin(OvMaths::FVector2 value) { Finite(value); m_layout.anchorMin = value; }
void CRectTransform::SetAnchorMax(OvMaths::FVector2 value) { Finite(value); m_layout.anchorMax = value; }
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
