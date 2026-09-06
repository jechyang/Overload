/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <cmath>
#include <OvCore/ECS/Components/CMaterialRenderer.h>

#include <OvEditor/Rendering/DebugSceneRenderer.h>
#include <OvEditor/Rendering/PickingRenderPass.h>
#include <OvEditor/Core/EditorActions.h>
#include <OvEditor/Panels/SceneView.h>
#include <OvEditor/Panels/GameView.h>
#include <OvEditor/Settings/EditorSettings.h>

#include <OvUI/Plugins/DDTarget.h>

namespace
{
	OvTools::Utils::OptRef<OvCore::ECS::Actor> GetActorFromPickingResult(
		OvEditor::Rendering::PickingRenderPass::PickingResult p_result
	)
	{
		if (p_result)
		{
			if (const auto actor = std::get_if<OvTools::Utils::OptRef<OvCore::ECS::Actor>>(&p_result.value()))
			{
				return *actor;
			}
		}

		return std::nullopt;
	}
}

OvEditor::Panels::SceneView::SceneView
(
	const std::string& p_title,
	bool p_opened,
	const OvUI::Settings::PanelWindowSettings& p_windowSettings
) : AViewControllable(p_title, p_opened, p_windowSettings),
	m_sceneManager(EDITOR_CONTEXT(sceneManager))
{
	m_renderer = std::make_unique<OvEditor::Rendering::DebugSceneRenderer>(*EDITOR_CONTEXT(driver));

	m_camera.SetFar(5000.0f);

	m_fallbackMaterial.SetShader(EDITOR_CONTEXT(shaderManager)[":Shaders\\Unlit.ovfx"]);
	m_fallbackMaterial.SetProperty("u_Diffuse", OvMaths::FVector4{ 1.f, 0.f, 1.f, 1.0f });
	m_fallbackMaterial.SetProperty("u_DiffuseMap", static_cast<OvRendering::Resources::Texture*>(nullptr));

	m_image->AddPlugin<OvUI::Plugins::DDTarget<std::pair<std::string, OvUI::Widgets::Layout::Group*>>>("File").DataReceivedEvent += [this](auto p_data)
	{
		const std::string path = p_data.first;

		using namespace OvTools::Utils;
		using enum PathParser::EFileType;

		switch (PathParser::GetFileType(path))
		{
			case SCENE: OnSceneDropped(path); break;
			case MODEL: OnModelDropped(path); break;
			case MATERIAL: OnMaterialDropped(path); break;
			case PREFAB: OnPrefabDropped(path); break;
			default: break;
		}
	};
	
	m_destroyedListener = OvCore::ECS::Actor::DestroyedEvent += [this](const OvCore::ECS::Actor& actor)
	{
		if (m_gizmoTargetGUID == actor.GetGUID()) { m_gizmoOperations.StopPicking(); m_gizmoTargetGUID = 0; }
		if (m_highlightedActor.has_value() && m_highlightedActor.value().GetID() == actor.GetID())
		{
			m_highlightedActor.reset();
		}
	};
}

OvEditor::Panels::SceneView::~SceneView()
{
    OvCore::ECS::Actor::DestroyedEvent -= m_destroyedListener;
}

OvRendering::Data::RenderCommandList OvEditor::Panels::SceneView::RecordRenderCommands()
{
    m_recordedInteraction.reset();
    return AViewControllable::RecordRenderCommands();
}

void OvEditor::Panels::SceneView::Update(float p_deltaTime)
{
	AViewControllable::Update(p_deltaTime);

	using namespace OvWindowing::Inputs;

	if (IsFocused() && !m_cameraController.IsRightMousePressed())
	{
		if (EDITOR_CONTEXT(inputManager)->IsKeyPressed(EKey::KEY_W))
		{
			SetGizmoOperation(Core::EGizmoOperation::TRANSLATE);
		}

		if (EDITOR_CONTEXT(inputManager)->IsKeyPressed(EKey::KEY_E))
		{
			SetGizmoOperation(Core::EGizmoOperation::ROTATE);
		}

		if (EDITOR_CONTEXT(inputManager)->IsKeyPressed(EKey::KEY_R))
		{
			SetGizmoOperation(Core::EGizmoOperation::SCALE);
		}
	}
}

void OvEditor::Panels::SceneView::InitFrame()
{
	AViewControllable::InitFrame();

	OvTools::Utils::OptRef<OvCore::ECS::Actor> selectedActor;

	if (EDITOR_EXEC(IsAnyActorSelected()))
	{
		selectedActor = EDITOR_EXEC(GetSelectedActor());
	}

	m_renderer->AddDescriptor<Rendering::DebugSceneRenderer::DebugSceneDescriptor>({
		m_currentOperation,
		m_highlightedActor,
		selectedActor,
		m_highlightedGizmoDirection
	});

	auto& pickingPass = m_renderer->GetPass<OvEditor::Rendering::PickingRenderPass>("Picking");

	// Enable picking pass only when the scene view is hovered, not picking, and not operating the camera
	pickingPass.SetEnabled(
		IsHovered() &&
		!m_gizmoOperations.IsPicking() &&
		!m_cameraController.IsOperating()
	);
}

OvCore::SceneSystem::Scene* OvEditor::Panels::SceneView::GetScene()
{
	return m_sceneManager.GetCurrentScene();
}

void OvEditor::Panels::SceneView::SetGizmoOperation(OvEditor::Core::EGizmoOperation p_operation)
{
	m_currentOperation = p_operation;
	EDITOR_EVENT(EditorOperationChanged).Invoke(m_currentOperation);
}

OvEditor::Core::EGizmoOperation OvEditor::Panels::SceneView::GetGizmoOperation() const
{
	return m_currentOperation;
}

OvCore::Rendering::SceneRenderer::SceneDescriptor OvEditor::Panels::SceneView::CreateSceneDescriptor()
{
	auto descriptor = AViewControllable::CreateSceneDescriptor();
	descriptor.fallbackMaterial = m_fallbackMaterial;

	if (Settings::EditorSettings::DebugFrustumCulling)
	{
		auto& scene = *GetScene();

		if (auto mainCameraComponent = scene.FindMainCamera())
		{
			auto& sceneCamera = mainCameraComponent->GetCamera();
			m_camera.SetFrustumGeometryCulling(sceneCamera.HasFrustumGeometryCulling());
			m_camera.SetFrustumLightCulling(sceneCamera.HasFrustumLightCulling());
			descriptor.frustumOverride = sceneCamera.GetFrustum();
		}
	}

	return descriptor;
}

void OvEditor::Panels::SceneView::DrawFrame()
{
    m_recordedInteraction.reset();
	OvEditor::Panels::AViewControllable::DrawFrame();
    if (m_renderer->IsRecordingCommands()) m_recordedInteraction = CaptureInteraction();
}

void OvEditor::Panels::SceneView::CompleteRender()
{
    if (m_recordedInteraction) CompleteInteraction(m_recordedInteraction);
    else HandleActorPicking();
}

bool IsResizing()
{
	auto cursor = ImGui::GetMouseCursor();

	return
		cursor == ImGuiMouseCursor_ResizeEW ||
		cursor == ImGuiMouseCursor_ResizeNS ||
		cursor == ImGuiMouseCursor_ResizeNWSE ||
		cursor == ImGuiMouseCursor_ResizeNESW ||
		cursor == ImGuiMouseCursor_ResizeAll;
}

std::shared_ptr<OvEditor::Panels::SceneView::Interaction> OvEditor::Panels::SceneView::CaptureInteraction()
{
    using namespace OvWindowing::Inputs;
    auto input = std::make_shared<Interaction>();
    auto& manager = *EDITOR_CONTEXT(inputManager);
    const auto [x,y] = manager.GetMousePosition();
    const auto [width,height] = GetSafeSize();
    input->mouse = {float(x-m_position.x), float(y-m_position.y-ImGui::GetFrameHeight())};
    input->size = {float(width),float(height)};
    input->hovered = IsHovered(); input->resizing = IsResizing();
    input->pressed = manager.IsMouseButtonPressed(EMouseButton::MOUSE_BUTTON_LEFT);
    input->released = manager.IsMouseButtonReleased(EMouseButton::MOUSE_BUTTON_LEFT);
    input->rightPressed = m_cameraController.IsRightMousePressed();
    input->cameraPosition = m_camera.GetPosition(); input->view = m_camera.GetViewMatrix(); input->projection = m_camera.GetProjectionMatrix();
    input->operation = m_currentOperation;
    input->snapping = {manager.GetKeyState(EKey::KEY_LEFT_CONTROL) == EKeyState::KEY_DOWN || manager.GetKeyState(EKey::KEY_RIGHT_CONTROL) == EKeyState::KEY_DOWN,
        Settings::EditorSettings::TranslationSnapUnit, Settings::EditorSettings::RotationSnapUnit, Settings::EditorSettings::ScalingSnapUnit};
    if (auto* scene = GetScene()) input->scene = scene->GetLifetimeToken();
    if (EDITOR_EXEC(IsAnyActorSelected())) input->selectedGUID = EDITOR_EXEC(GetSelectedActor()).GetGUID();
    input->pickingAvailable = m_renderer->GetPass<Rendering::PickingRenderPass>("Picking").IsEnabled();
    if (m_renderer->IsRecordingCommands())
    {
        input->completion = m_renderer->GetCommandCompletion();
        auto& pass = m_renderer->GetPass<Rendering::PickingRenderPass>("Picking");
        if (pass.IsEnabled()) input->readback = pass.GetRecordedReadback();
    }
    return input;
}

bool OvEditor::Panels::SceneView::CompleteInteraction(const std::shared_ptr<Interaction>& pending)
{
    // Copy ownership: pending may refer to the member reset below.
    auto input = pending;
    if (!input || input->consumed || !input->completion.Succeeded()) return false;
    input->consumed = true;
    if (m_recordedInteraction == input) m_recordedInteraction.reset();
    auto* scene = GetScene();
    if (!scene || input->scene != scene->GetLifetimeToken())
    { m_gizmoOperations.StopPicking(); m_gizmoTargetGUID = 0; m_highlightedActor.reset(); m_highlightedGizmoDirection.reset(); return false; }
    Rendering::PickingRenderPass::PickingResult result;
    const float x = input->mouse.x, y = input->size.y-input->mouse.y;
    if (input->readback && std::isfinite(x) && std::isfinite(y) && x >= 0 && y >= 0 && x < input->size.x && y < input->size.y &&
        input->readback->GetSize() == std::pair<uint32_t,uint32_t>{uint32_t(input->size.x),uint32_t(input->size.y)})
        result = input->readback->Resolve(*scene,uint32_t(x),uint32_t(y));
    ApplyInteraction(*input,result);
    if (input->materialDrop) ApplyMaterialDrop(*input->materialDrop,result);
    return true;
}

void OvEditor::Panels::SceneView::HandleActorPicking()
{
    const auto input = CaptureInteraction();
    Rendering::PickingRenderPass::PickingResult result;
    if ((!m_gizmoOperations.IsPicking() || input->released) && input->hovered && !input->resizing && input->pickingAvailable)
        result = GetPickingResult();
    ApplyInteraction(*input, result);
}

void OvEditor::Panels::SceneView::ApplyInteraction(const Interaction& input, Rendering::PickingRenderPass::PickingResult result)
{
    auto* scene = GetScene();
    if (!scene || input.scene != scene->GetLifetimeToken()) return;
    const uint64_t selected = EDITOR_EXEC(IsAnyActorSelected()) ? EDITOR_EXEC(GetSelectedActor()).GetGUID() : 0;
    const bool selectionMatches = selected == input.selectedGUID && m_currentOperation == input.operation;
    if (input.released || (m_gizmoOperations.IsPicking() && (!selectionMatches || !scene->FindActorByGUID(m_gizmoTargetGUID))))
    { m_gizmoOperations.StopPicking(); m_gizmoTargetGUID = 0; }
    m_highlightedActor.reset(); m_highlightedGizmoDirection.reset();
    if (!m_gizmoOperations.IsPicking() && input.hovered && !input.resizing && input.pickingAvailable)
    {
        if (!input.rightPressed && result)
        {
            if (auto* actor = std::get_if<OvTools::Utils::OptRef<OvCore::ECS::Actor>>(&*result)) m_highlightedActor = *actor;
            else if (selectionMatches && selected && (!input.readback || input.readback->GetGizmoActorGUID() == selected))
                m_highlightedGizmoDirection = std::get<Core::GizmoBehaviour::EDirection>(*result);
        }
        if (input.pressed && !input.rightPressed && selectionMatches)
        {
            if (m_highlightedGizmoDirection)
            {
                m_gizmoTargetGUID = selected;
                m_gizmoOperations.StartPicking(EDITOR_EXEC(GetSelectedActor()),input.cameraPosition,input.operation,*m_highlightedGizmoDirection);
            }
            else if (m_highlightedActor) EDITOR_EXEC(SelectActor(m_highlightedActor.value()));
            else EDITOR_EXEC(UnselectActor());
        }
    }
    if (m_gizmoOperations.IsPicking() && input.size.x > 0 && input.size.y > 0)
    {
        m_gizmoOperations.SetCurrentMouse(input.mouse);
        m_gizmoOperations.ApplyOperation(input.view,input.projection,input.cameraPosition,input.size,input.snapping);
        m_highlightedGizmoDirection = m_gizmoOperations.GetDirection();
    }
}

OvEditor::Rendering::PickingRenderPass::PickingResult OvEditor::Panels::SceneView::GetPickingResult()
{
	auto [mouseX, mouseY] = EDITOR_CONTEXT(inputManager)->GetMousePosition();
	mouseX -= m_position.x;
	mouseY -= m_position.y;
	mouseY = GetSafeSize().second - mouseY + ImGui::GetFrameHeight();

	auto* currentScene = GetScene();
    if (!currentScene) return std::nullopt;
    auto& scene = *currentScene;

	auto& actorPickingFeature = m_renderer->GetPass<OvEditor::Rendering::PickingRenderPass>("Picking");

    const auto readback = actorPickingFeature.GetRecordedReadback();
    const auto [width, height] = GetSafeSize();
    if (!std::isfinite(mouseX) || !std::isfinite(mouseY) || mouseX < 0 || mouseY < 0 || mouseX >= width || mouseY >= height) return std::nullopt;
    if (readback && readback->GetSize() != std::pair<uint32_t, uint32_t>{width, height}) return std::nullopt;
	auto result = actorPickingFeature.ReadbackPickingResult(
		scene,
		static_cast<uint32_t>(mouseX),
		static_cast<uint32_t>(mouseY)
	);
    if (readback && result && std::holds_alternative<Core::GizmoBehaviour::EDirection>(*result))
    {
        if (!EDITOR_EXEC(IsAnyActorSelected()) ||
            EDITOR_EXEC(GetSelectedActor()).GetGUID() != readback->GetGizmoActorGUID() ||
            m_currentOperation != readback->GetGizmoOperation()) return std::nullopt;
    }
    return result;
}

void OvEditor::Panels::SceneView::OnSceneDropped(const std::string& p_path)
{
	EDITOR_EXEC(LoadSceneFromDisk(p_path));
}

void OvEditor::Panels::SceneView::OnModelDropped(const std::string& p_path)
{
	EDITOR_EXEC(CreateActorWithModel(p_path, true));
}

void OvEditor::Panels::SceneView::OnMaterialDropped(const std::string& p_path)
{
    if (m_recordedInteraction && !m_recordedInteraction->consumed)
    { m_recordedInteraction->materialDrop = p_path; return; }
    ApplyMaterialDrop(p_path, GetPickingResult());
}

void OvEditor::Panels::SceneView::ApplyMaterialDrop(const std::string& p_path, Rendering::PickingRenderPass::PickingResult pickingResult)
{

	if (auto actor = GetActorFromPickingResult(pickingResult))
	{
		if (auto materialRenderer = actor->GetComponent<OvCore::ECS::Components::CMaterialRenderer>())
		{
			const auto resourcePath = EDITOR_EXEC(GetResourcePath(p_path));

			if (const auto material = EDITOR_CONTEXT(materialManager)[resourcePath])
			{
				materialRenderer->SetMaterialAtIndex(0, *material);
			}
		}
	}
}

void OvEditor::Panels::SceneView::OnPrefabDropped(const std::string& p_path)
{
	if (auto* actor = EDITOR_EXEC(InstantiatePrefab(p_path)); actor)
	{
		EDITOR_EXEC(SelectActor(*actor));
	}
}
