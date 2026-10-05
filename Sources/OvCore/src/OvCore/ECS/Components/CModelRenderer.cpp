/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <OvCore/ECS/Actor.h>
#include <OvCore/ECS/Components/CModelRenderer.h>
#include <OvCore/Helpers/ReflectionResourceCodecs.h>
#include <OvCore/ECS/Components/CSkinnedMeshRenderer.h>

#include <OvUI/Plugins/DDTarget.h>
#include <OvUI/Styling/Style.h>
#include <OvUI/Widgets/Drags/DragFloat.h>
#include <OvUI/Widgets/Selection/CheckBox.h>
#include <OvUI/Widgets/Selection/ComboBox.h>
#include <OvUI/Widgets/Texts/Text.h>
#include <OvUI/Widgets/Texts/TextColored.h>
#include <OvUI/Widgets/Visual/Image.h>

OvCore::ECS::Components::CModelRenderer::CModelRenderer(ECS::Actor& p_owner) : AComponent(p_owner)
{
	m_modelChangedEvent += [this]()
	{
		if (auto skinnedMeshRenderer = owner.GetComponent<CSkinnedMeshRenderer>())
			skinnedMeshRenderer->NotifyModelChanged();
	};
}

std::string OvCore::ECS::Components::CModelRenderer::GetName()
{
	return "Model Renderer";
}

std::string OvCore::ECS::Components::CModelRenderer::GetTypeName()
{
	return std::string{ComponentTraits<CModelRenderer>::Name};
}

void OvCore::ECS::Components::CModelRenderer::SetModel(OvRendering::Resources::Model* p_model)
{
	m_model = p_model;
	m_modelPath.clear();
	m_modelChangedEvent.Invoke();
}

OvRendering::Resources::Model * OvCore::ECS::Components::CModelRenderer::GetModel() const
{
	return m_model;
}

void OvCore::ECS::Components::CModelRenderer::SetFrustumBehaviour(EFrustumBehaviour p_boundingMode)
{
	m_frustumBehaviour = p_boundingMode;
}

OvCore::ECS::Components::CModelRenderer::EFrustumBehaviour OvCore::ECS::Components::CModelRenderer::GetFrustumBehaviour() const
{
	return m_frustumBehaviour;
}

const OvRendering::Geometry::BoundingSphere& OvCore::ECS::Components::CModelRenderer::GetCustomBoundingSphere() const
{
	return m_customBoundingSphere;
}

void OvCore::ECS::Components::CModelRenderer::SetCustomBoundingSphere(const OvRendering::Geometry::BoundingSphere& p_boundingSphere)
{
	m_customBoundingSphere = p_boundingSphere;
}

void OvCore::ECS::Components::CModelRenderer::OnInspector(OvUI::Internal::WidgetContainer& p_root)
{
	using namespace OvCore::Helpers;

	ReflectionProperties::DrawResourceReference<OvRendering::Resources::Model>(p_root, "Model", InspectorTicket(),
        [this] { return GetSerializedModel(); }, [this](const auto& value) { SetSerializedModel(value); }, OvTools::Utils::PathParser::EFileType::MODEL);

	GUIDrawer::CreateTitle(p_root, "Frustum Culling Behaviour");
	auto& boundingMode = p_root.CreateWidget<OvUI::Widgets::Selection::ComboBox>(0);
	boundingMode.choices.emplace(0, "Disabled");
	// [[deprecated]] boundingMode.choices.emplace(1, "Model Bounds");
	boundingMode.choices.emplace(2, "Mesh Bounds");
	boundingMode.choices.emplace(3, "Custom Bounds");
	auto& boundingModeDispatcher = boundingMode.AddPlugin<OvUI::Plugins::DataDispatcher<int>>();
	boundingModeDispatcher.RegisterReference(reinterpret_cast<int&>(m_frustumBehaviour));

	auto& centerLabel = p_root.CreateWidget<OvUI::Widgets::Texts::TextColored>("Bounding Sphere Center", OVUI_STYLE(InspectorTitle));
	auto& centerWidget = p_root.CreateWidget<OvUI::Widgets::Drags::DragMultipleScalars<float, 3>>(GUIDrawer::GetDataType<float>(), GUIDrawer::_MIN_FLOAT, GUIDrawer::_MAX_FLOAT, 0.f, 0.05f, "", GUIDrawer::GetFormat<float>());
	auto& centerDispatcher = centerWidget.AddPlugin<OvUI::Plugins::DataDispatcher<std::array<float, 3>>>();
	centerDispatcher.RegisterReference(reinterpret_cast<std::array<float, 3>&>(m_customBoundingSphere.position));

	auto& radiusLabel = p_root.CreateWidget<OvUI::Widgets::Texts::TextColored>("Bounding Sphere Radius", OVUI_STYLE(InspectorTitle));
	auto& radiusWidget = p_root.CreateWidget<OvUI::Widgets::Drags::DragFloat>(0.0f, GUIDrawer::_MAX_FLOAT, 0.f, 0.1f);
	auto& radiusDispatcher = radiusWidget.AddPlugin<OvUI::Plugins::DataDispatcher<float>>();
	radiusDispatcher.RegisterReference(m_customBoundingSphere.radius);

	boundingMode.ValueChangedEvent += [&](int p_choice)
	{
		centerLabel.enabled = centerWidget.enabled = radiusLabel.enabled = radiusWidget.enabled = p_choice == 3;
	};

	centerLabel.enabled = centerWidget.enabled = radiusLabel.enabled = radiusWidget.enabled = m_frustumBehaviour == EFrustumBehaviour::CUSTOM_BOUNDS;
}

OvCore::Resources::ResourceReference<OvRendering::Resources::Model> OvCore::ECS::Components::CModelRenderer::GetSerializedModel() const
{ return Resources::ResourceReference<OvRendering::Resources::Model>::From(m_model, m_modelPath); }
void OvCore::ECS::Components::CModelRenderer::SetSerializedModel(const Resources::ResourceReference<OvRendering::Resources::Model>& value)
{ SetModel(value.resource); m_modelPath = value.path; }
OvMaths::FVector3 OvCore::ECS::Components::CModelRenderer::GetSerializedBoundsPosition() const { return m_customBoundingSphere.position; }
void OvCore::ECS::Components::CModelRenderer::SetSerializedBoundsPosition(const OvMaths::FVector3& value) { m_customBoundingSphere.position = value; }
float OvCore::ECS::Components::CModelRenderer::GetSerializedBoundsRadius() const { return m_customBoundingSphere.radius; }
void OvCore::ECS::Components::CModelRenderer::SetSerializedBoundsRadius(float value) { m_customBoundingSphere.radius = value; }
