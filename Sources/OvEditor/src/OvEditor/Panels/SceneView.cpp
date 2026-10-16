/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <cmath>
#include <algorithm>
#include <array>
#include <OvUI/Styling/Style.h>
#include <OvCore/ECS/Components/CMaterialRenderer.h>
#include <OvCore/ECS/Components/CCamera.h>

#include <OvEditor/Core/EditorActions.h>
#include <OvEditor/Core/SelectionBounds.h>
#include <OvEditor/Panels/SceneView.h>
#include <OvEditor/Panels/GameView.h>
#include <OvEditor/Settings/EditorSettings.h>

#include <OvUI/Plugins/DDTarget.h>

namespace
{
    class NavigationWidget final : public OvUI::Widgets::AWidget
    {
    public:
        explicit NavigationWidget(std::function<void()> draw) : m_draw(std::move(draw)) {}
    protected:
        void _Draw_Impl() override { m_draw(); }
    private:
        std::function<void()> m_draw;
    };
	OvTools::Utils::OptRef<OvCore::ECS::Actor> GetActorFromPickingResult(
		OvEditor::Rendering::PickingResult p_result
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

	m_camera.SetFar(5000.0f);
    CreateWidget<NavigationWidget>([this] { DrawViewportTools(); DrawNavigationOverlay(); DrawCameraPreview(); });
    m_focusLostListener = EDITOR_CONTEXT(window)->LostFocusEvent += [this] {
        m_boxPending = false;
        if (m_gizmoOperations.IsPicking())
        {
            m_gizmoOperations.StopPicking(); EDITOR_EXEC(EndTransformEdit()); m_gizmoTargetGUID = 0;
        }
        InvalidateNavigationPicking();
    };

	m_fallbackMaterial.SetShader(EDITOR_CONTEXT(shaderManager)[":Shaders\\Unlit.ovfx"]);
	m_fallbackMaterial.SetProperty("u_Diffuse", OvMaths::FVector4{ 1.f, 0.f, 1.f, 1.0f });
	m_fallbackMaterial.SetProperty("u_DiffuseMap", static_cast<OvRendering::Resources::Texture*>(nullptr));
    m_displayFallbackMaterial.SetShader(EDITOR_CONTEXT(shaderManager)[":Shaders\\Unlit.ovfx"]);
    m_displayFallbackMaterial.SetFeatures({"ALPHA_CLIPPING"});
    m_displayFallbackMaterial.SetProperty("u_AlphaClippingThreshold",0.f);

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
    EDITOR_CONTEXT(window)->LostFocusEvent -= m_focusLostListener;
    OvCore::ECS::Actor::DestroyedEvent -= m_destroyedListener;
}

OvRendering::Data::RenderCommandList OvEditor::Panels::SceneView::RecordRenderCommands(bool p_deferEncoding)
{
    m_recordedInteraction.reset();
    GetCameraPreviewView();
    auto commands = AViewControllable::RecordRenderCommands(true);
    if (auto* preview = GetCameraPreviewView()) commands.AppendRecording(preview->RecordRenderCommands(true));
    if (!p_deferEncoding) commands.Seal(m_jobs);
    return commands;
}

void OvEditor::Panels::SceneView::Update(float p_deltaTime)
{
    GetCameraPreviewView();
    if (m_boxPending && (!IsOpened() || !GetScene() || m_boxScene != GetScene()->GetLifetimeToken())) m_boxPending = false;
    m_cameraController.SetInputBlocked((IsNavigationHovered() && !m_cameraController.IsOperating()) || m_gizmoOperations.IsPicking() || m_boxPending);
	AViewControllable::Update(p_deltaTime);

	using namespace OvWindowing::Inputs;

	if (IsFocused() && !m_navigationPopupOpen && !m_toolsPopupOpen && !m_boxPending && !m_cameraController.IsOperating() && !ImGui::IsAnyItemActive() && !ImGui::GetIO().WantTextInput && !m_gizmoOperations.IsPicking())
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

		if (EDITOR_CONTEXT(inputManager)->IsKeyPressed(EKey::KEY_T))
		{
			SetGizmoOperation(Core::EGizmoOperation::RECT);
		}
        const auto& input = *EDITOR_CONTEXT(inputManager);
        const bool reverse = input.GetKeyState(EKey::KEY_LEFT_CONTROL) == EKeyState::KEY_DOWN || input.GetKeyState(EKey::KEY_RIGHT_CONTROL) == EKeyState::KEY_DOWN;
        if (input.IsKeyPressed(EKey::KEY_2)) Set2D(!Is2D());
        if (input.IsKeyPressed(EKey::KEY_KP_5)) ToggleProjection();
        if (input.IsKeyPressed(EKey::KEY_KP_1)) SetViewDirection(reverse ? -OvMaths::FVector3::Forward : OvMaths::FVector3::Forward);
        if (input.IsKeyPressed(EKey::KEY_KP_3)) SetViewDirection(reverse ? -OvMaths::FVector3::Right : OvMaths::FVector3::Right);
        if (input.IsKeyPressed(EKey::KEY_KP_7)) SetViewDirection(reverse ? -OvMaths::FVector3::Up : OvMaths::FVector3::Up);
	}
}

void OvEditor::Panels::SceneView::InitFrame() { AViewControllable::InitFrame(); }
OvEditor::Rendering::EditorOverlayRequest OvEditor::Panels::SceneView::CreateOverlayRequest()
{
    auto request=AViewControllable::CreateOverlayRequest();request.debugObjects=true;
    using Settings::EditorSettings;
    request.grid = EditorSettings::SceneShowGrid.Get();
    request.is2D = Is2D();
    request.lightIcons = EditorSettings::SceneShowLightIcons.Get();
    request.cameraIcons = EditorSettings::SceneShowCameraIcons.Get();
    request.reflectionIcons = EditorSettings::SceneShowReflectionIcons.Get();
    request.colliders = EditorSettings::SceneShowColliders.Get();
    request.cameraFrustums = EditorSettings::SceneShowCameraFrustums.Get();
    request.geometryBounds = EditorSettings::ShowGeometryBounds.Get();
    request.lightBounds = EditorSettings::ShowLightBounds.Get();
    request.picking=(m_boxPending || (IsHovered()&&!IsNavigationHovered()))&&!m_gizmoOperations.IsPicking()&&!m_cameraController.IsOperating();
    request.operation=m_currentOperation;request.highlightedDirection=m_highlightedGizmoDirection;
    if(EDITOR_EXEC(IsAnyActorSelected()))request.selected=EDITOR_EXEC(GetSelectedActor()).GetGUID();
    for (auto* actor : EDITOR_EXEC(GetSelectedActors())) request.selection.push_back(actor->GetGUID());
    request.gizmoFrame = GetGizmoFrame();
    if(m_highlightedActor)request.highlighted=m_highlightedActor->GetGUID();
    request.gameViewSize=EDITOR_PANEL(OvEditor::Panels::GameView,"Game View").GetSafeSize();
    return request;
}

OvCore::SceneSystem::Scene* OvEditor::Panels::SceneView::GetScene()
{
	return m_sceneManager.GetCurrentScene();
}

void OvEditor::Panels::SceneView::SetGizmoOperation(OvEditor::Core::EGizmoOperation p_operation)
{
    if (m_gizmoOperations.IsPicking() || m_boxPending || m_currentOperation == p_operation) return;
	m_currentOperation = p_operation;
    InvalidateNavigationPicking();
	EDITOR_EVENT(EditorOperationChanged).Invoke(m_currentOperation);
}

OvEditor::Core::EGizmoOperation OvEditor::Panels::SceneView::GetGizmoOperation() const
{
	return m_currentOperation;
}

OvCore::Rendering::SceneRenderRequest OvEditor::Panels::SceneView::CreateSceneRequest()
{
	auto descriptor = AViewControllable::CreateSceneRequest();
	descriptor.fallbackMaterial = &m_fallbackMaterial;
    const auto mode = GetDisplayMode();
    if (mode != DisplayMode::LIT)
    {
        descriptor.meshTechnique = mode == DisplayMode::UNLIT ? "UNLIT_PASS" : "WIREFRAME_PASS";
        descriptor.meshTechniqueFallback = &m_displayFallbackMaterial;
        descriptor.wireframe = mode == DisplayMode::WIREFRAME;
        descriptor.applyPostProcess = false;
    }

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

OvEditor::Panels::SceneView::DisplayMode OvEditor::Panels::SceneView::GetDisplayMode() const
{
    return DisplayMode(std::clamp(Settings::EditorSettings::SceneDisplayMode.Get(),0,2));
}

void OvEditor::Panels::SceneView::SetDisplayMode(DisplayMode mode)
{
    if (m_gizmoOperations.IsPicking() || m_boxPending || m_cameraController.IsOperating() || mode == GetDisplayMode()) return;
    Settings::EditorSettings::SceneDisplayMode = std::clamp(int(mode),0,2);
    InvalidateNavigationPicking();
}

void OvEditor::Panels::SceneView::OnViewRecorded() { m_recordedInteraction = CaptureInteraction(); }

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
    input->hovered = IsHovered() && !IsNavigationHovered(); input->resizing = IsResizing();
    input->navigationRevision = m_navigationRevision;
    input->pressed = manager.IsMouseButtonPressed(EMouseButton::MOUSE_BUTTON_LEFT);
    input->released = manager.IsMouseButtonReleased(EMouseButton::MOUSE_BUTTON_LEFT);
    input->cancelled = manager.IsKeyPressed(EKey::KEY_ESCAPE);
    input->additiveSelection = manager.GetKeyState(EKey::KEY_LEFT_CONTROL) == EKeyState::KEY_DOWN || manager.GetKeyState(EKey::KEY_RIGHT_CONTROL) == EKeyState::KEY_DOWN;
    input->rightPressed = m_cameraController.IsRightMousePressed() || manager.GetMouseButtonState(EMouseButton::MOUSE_BUTTON_RIGHT) == EMouseButtonState::MOUSE_DOWN;
    input->cameraPosition = m_camera.GetPosition(); input->view = m_camera.GetViewMatrix(); input->projection = m_camera.GetProjectionMatrix();
    input->operation = m_currentOperation;
    input->snapping = {Settings::EditorSettings::SnapEnabled.Get() || input->additiveSelection,
        Settings::EditorSettings::TranslationSnapUnit, Settings::EditorSettings::RotationSnapUnit, Settings::EditorSettings::ScalingSnapUnit};
    if (auto* scene = GetScene()) input->scene = scene->GetLifetimeToken();
    if (EDITOR_EXEC(IsAnyActorSelected())) input->selectedGUID = EDITOR_EXEC(GetSelectedActor()).GetGUID();
    for (auto* actor : EDITOR_EXEC(GetSelectedActors())) input->selection.push_back(actor->GetGUID());
    input->gizmoFrame = GetGizmoFrame();
    input->pickingAvailable=m_overlayRequest.picking;
    input->completion=m_completion;
    if(input->pickingAvailable&&m_picking)input->readback=m_picking->GetReadback();
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
    { m_boxPending = false; m_gizmoOperations.StopPicking(); m_gizmoTargetGUID = 0; m_highlightedActor.reset(); m_highlightedGizmoDirection.reset(); return false; }
    if (input->navigationRevision != m_navigationRevision) return false;
    Rendering::PickingResult result;
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
    Rendering::PickingResult result;
    if ((!m_gizmoOperations.IsPicking() || input->released) && input->hovered && !input->resizing && input->pickingAvailable)
        result = GetPickingResult();
    ApplyInteraction(*input, result);
}

void OvEditor::Panels::SceneView::ApplyInteraction(const Interaction& input, Rendering::PickingResult result)
{
    auto* scene = GetScene();
    if (!scene || input.scene != scene->GetLifetimeToken()) return;
    if (input.navigationRevision != m_navigationRevision) return;
    const uint64_t selected = EDITOR_EXEC(IsAnyActorSelected()) ? EDITOR_EXEC(GetSelectedActor()).GetGUID() : 0;
    std::vector<uint64_t> currentSelection;
    for (auto* actor : EDITOR_EXEC(GetSelectedActors())) currentSelection.push_back(actor->GetGUID());
    const bool selectionMatches = selected == input.selectedGUID && m_currentOperation == input.operation && currentSelection == input.selection;
    if (m_boxPending)
    {
        if (input.cancelled || input.rightPressed || input.scene != m_boxScene || currentSelection != m_boxSelection || input.size.x != m_boxSize.x || input.size.y != m_boxSize.y)
        { m_boxPending = false; return; }
        m_boxEnd = {std::clamp(input.mouse.x,0.f,input.size.x),std::clamp(input.mouse.y,0.f,input.size.y)};
        if (input.released)
        {
            m_boxPending = false;
            const bool dragged = OvMaths::FVector2::Length(m_boxEnd-m_boxStart) >= 5.f * OVUI_SCALE;
            if (dragged && input.readback && input.readback->GetSize() == std::pair<uint32_t,uint32_t>{uint32_t(input.size.x),uint32_t(input.size.y)})
            {
                auto actors = input.readback->ResolveRegion(*scene,int(m_boxStart.x),int(input.size.y-m_boxStart.y),int(m_boxEnd.x),int(input.size.y-m_boxEnd.y));
                if (m_boxAdditive)
                {
                    auto existing = EDITOR_EXEC(GetSelectedActors());
                    for (auto* actor : actors) if (std::find(existing.begin(),existing.end(),actor) == existing.end()) existing.push_back(actor);
                    actors = std::move(existing);
                }
                EDITOR_EXEC(SetSelection(actors));
            }
            else if (!dragged && !m_boxAdditive) EDITOR_EXEC(UnselectActor());
        }
        return;
    }
    if (input.cancelled && m_gizmoOperations.IsPicking())
    { m_gizmoOperations.CancelPicking(); EDITOR_EXEC(EndTransformEdit(true)); m_gizmoTargetGUID = 0; m_highlightedGizmoDirection.reset(); return; }
    if (input.released || (m_gizmoOperations.IsPicking() && (!selectionMatches || !scene->FindActorByGUID(m_gizmoTargetGUID))))
    { m_gizmoOperations.StopPicking(); EDITOR_EXEC(EndTransformEdit()); m_gizmoTargetGUID = 0; }
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
                EDITOR_EXEC(BeginTransformEdit());
                m_gizmoOperations.StartPicking(EDITOR_EXEC(GetSelectedActor()),input.cameraPosition,input.operation,*m_highlightedGizmoDirection,input.gizmoFrame);
            }
            else if (m_highlightedActor)
            {
                if (input.additiveSelection) EDITOR_EXEC(ToggleActorSelection(m_highlightedActor.value()));
                else EDITOR_EXEC(SelectActor(m_highlightedActor.value()));
            }
            else
            {
                m_boxPending = true; m_boxStart = m_boxEnd = input.mouse; m_boxSize = input.size; m_boxScene = input.scene;
                m_boxAdditive = input.additiveSelection; m_boxSelection = currentSelection;
            }
        }
    }
    if (m_gizmoOperations.IsPicking() && input.size.x > 0 && input.size.y > 0)
    {
        m_gizmoOperations.SetCurrentMouse(input.mouse);
        const auto frame = m_gizmoOperations.GetFrame();
        EDITOR_EXEC(TransformSelection([&] { m_gizmoOperations.ApplyOperation(input.view,input.projection,input.cameraPosition,input.size,input.snapping); },frame.position,frame.rotation));
        m_highlightedGizmoDirection = m_gizmoOperations.GetDirection();
    }
}

OvEditor::Rendering::PickingResult OvEditor::Panels::SceneView::GetPickingResult()
{
	auto [mouseX, mouseY] = EDITOR_CONTEXT(inputManager)->GetMousePosition();
	mouseX -= m_position.x;
	mouseY -= m_position.y;
	mouseY = GetSafeSize().second - mouseY + ImGui::GetFrameHeight();

	auto* currentScene = GetScene();
    if (!currentScene) return std::nullopt;
    auto& scene = *currentScene;

	if(!m_picking)return std::nullopt;

    const auto readback = m_picking->GetReadback();
    const auto [width, height] = GetSafeSize();
    if (!std::isfinite(mouseX) || !std::isfinite(mouseY) || mouseX < 0 || mouseY < 0 || mouseX >= width || mouseY >= height) return std::nullopt;
    if (readback && readback->GetSize() != std::pair<uint32_t, uint32_t>{width, height}) return std::nullopt;
	auto result = readback ? readback->Resolve(scene,static_cast<uint32_t>(mouseX),static_cast<uint32_t>(mouseY)) : Rendering::PickingResult{};
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
	EDITOR_EXEC(DelayAction([p_path] { EDITOR_EXEC(OpenScene(p_path)); }, 0));
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

void OvEditor::Panels::SceneView::ApplyMaterialDrop(const std::string& p_path, Rendering::PickingResult pickingResult)
{

	if (auto actor = GetActorFromPickingResult(pickingResult))
	{
		if (auto materialRenderer = actor->GetComponent<OvCore::ECS::Components::CMaterialRenderer>())
		{
			const auto resourcePath = EDITOR_EXEC(GetResourcePath(p_path));

            if (const auto material = EDITOR_CONTEXT(materialManager)[resourcePath])
			{
				EDITOR_EXEC(BeginSceneEdit("Assign material"));
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

bool OvEditor::Panels::SceneView::IsNavigationHovered() const
{
    if (m_navigationPopupOpen || m_toolsPopupOpen) return true;
    if (IsCameraPreviewHovered()) return true;
    const float scale = OVUI_SCALE;
    if (!IsHovered()) return false;
    const auto [x,y] = EDITOR_CONTEXT(inputManager)->GetMousePosition();
    const float contentTop = m_position.y+ImGui::GetFrameHeight();
    if (x >= m_position.x+8*scale && x <= m_position.x+8*scale+m_toolsWidth && y >= contentTop+8*scale && y <= contentTop+8*scale+m_toolsHeight) return true;
    if (GetSize().x < 200*scale || GetSafeSize().second < 180*scale) return false;
    const float left = m_position.x+GetSize().x-164*scale;
    const float top = contentTop+8*scale+(GetSize().x < 570*scale ? m_toolsHeight+8*scale : 0);
    return x >= left && x <= left+156*scale && y >= top && y <= top+162*scale;
}

void OvEditor::Panels::SceneView::InvalidateNavigationPicking()
{
    ++m_navigationRevision;
    m_boxPending = false;
    m_highlightedActor.reset();
    m_highlightedGizmoDirection.reset();
}

void OvEditor::Panels::SceneView::Set2D(bool p_2d)
{
    if (m_gizmoOperations.IsPicking() || m_cameraController.IsOperating()) return;
    m_cameraController.Set2D(p_2d);
    InvalidateNavigationPicking();
}

bool OvEditor::Panels::SceneView::Is2D() const
{
    return m_cameraController.Is2D();
}

void OvEditor::Panels::SceneView::ToggleProjection()
{
    if (m_gizmoOperations.IsPicking() || m_cameraController.IsOperating()) return;
    using enum OvRendering::Settings::EProjectionMode;
    m_cameraController.SetProjectionMode(m_camera.GetProjectionMode() == PERSPECTIVE ? ORTHOGRAPHIC : PERSPECTIVE);
    InvalidateNavigationPicking();
}

void OvEditor::Panels::SceneView::SetViewDirection(const OvMaths::FVector3& direction)
{
    if (m_gizmoOperations.IsPicking() || m_cameraController.IsOperating()) return;
    m_cameraController.SetViewDirection(direction);
    InvalidateNavigationPicking();
}

void OvEditor::Panels::SceneView::SetLocalSpace(bool local)
{
    if (m_gizmoOperations.IsPicking() || m_boxPending || m_localSpace == local) return;
    m_localSpace = local; InvalidateNavigationPicking();
}

void OvEditor::Panels::SceneView::SetCenterPivot(bool center)
{
    if (m_gizmoOperations.IsPicking() || m_boxPending || m_centerPivot == center) return;
    m_centerPivot = center; InvalidateNavigationPicking();
}

OvEditor::Core::GizmoBehaviour::Frame OvEditor::Panels::SceneView::GetGizmoFrame() const
{
    if (m_gizmoOperations.IsPicking()) return m_gizmoOperations.GetFrame();
    const auto actors = EDITOR_EXEC(GetSelectedActors());
    if (actors.empty()) return {{},OvMaths::FQuaternion::Identity};
    const auto bounds = m_centerPivot ? Core::SelectionBounds::From(actors) : Core::SelectionBounds{};
    return {bounds.valid ? bounds.Center() : actors.front()->transform.GetWorldPosition(),
        (m_localSpace || m_currentOperation == Core::EGizmoOperation::SCALE) ? actors.front()->transform.GetWorldRotation() : OvMaths::FQuaternion::Identity};
}

void OvEditor::Panels::SceneView::FrameSelection()
{
    if (m_gizmoOperations.IsPicking() || m_boxPending || m_cameraController.IsOperating()) return;
    m_cameraController.FrameSelection(EDITOR_EXEC(GetSelectedActors()));
    InvalidateNavigationPicking();
}

OvEditor::Panels::AView* OvEditor::Panels::SceneView::GetCameraPreviewView()
{
    auto* scene = GetScene();
    const auto clear = [&]() -> AView* {
        if (m_cameraPreview) m_cameraPreview->Configure({},0,{});
        m_previewPosition = {}; m_previewSize = {}; return nullptr;
    };
    if (!scene || !IsOpened() || !Settings::EditorSettings::CameraPreviewEnabled.Get() ||
        GetSize().x < 420*OVUI_SCALE || GetSafeSize().second < 360*OVUI_SCALE) return clear();
    for (auto* actor : EDITOR_EXEC(GetSelectedActors()))
    {
        if (!actor->IsActive() || !actor->GetComponent<OvCore::ECS::Components::CCamera>()) continue;
        // Allocate on first use, after SetupUI has assigned the regular panels'
        // IDs. This preserves saved docking layouts and avoids an idle target.
        if (!m_cameraPreview) m_cameraPreview = std::make_unique<CameraPreview>();
        auto size = EDITOR_PANEL(GameView,"Game View").GetSafeSize();
        if (!size.first || !size.second)
            size = {uint16_t(std::clamp(EDITOR_CONTEXT(projectSettings).GetOrDefault<int>("x_resolution",1280),64,4096)),
                uint16_t(std::clamp(EDITOR_CONTEXT(projectSettings).GetOrDefault<int>("y_resolution",720),64,4096))};
        m_cameraPreview->SetCommandRecordingEnabled(m_jobs != nullptr,m_jobs);
        m_cameraPreview->Configure(scene->GetLifetimeToken(),actor->GetGUID(),size);
        UpdateCameraPreviewRect(size);
        return m_cameraPreview.get();
    }
    return clear();
}

bool OvEditor::Panels::SceneView::IsCameraPreviewHovered() const
{
    if (!IsHovered() || !Settings::EditorSettings::CameraPreviewEnabled.Get() ||
        m_previewSize.x <= 0 || m_previewSize.y <= 0 || !m_cameraPreview || !m_cameraPreview->GetCamera()) return false;
    const auto [x,y] = EDITOR_CONTEXT(inputManager)->GetMousePosition();
    const auto origin = ImGui::GetMainViewport()->Pos;
    const float left = m_previewPosition.x-origin.x, top = m_previewPosition.y-origin.y;
    return x >= left && y >= top && x <= left+m_previewSize.x && y <= top+m_previewSize.y;
}

void OvEditor::Panels::SceneView::DrawCameraPreview()
{
    m_previewPosition = {}; m_previewSize = {};
    if (!Settings::EditorSettings::CameraPreviewEnabled.Get() || !m_cameraPreview || !m_cameraPreview->GetCamera() ||
        GetSize().x < 420*OVUI_SCALE || GetSafeSize().second < 360*OVUI_SCALE) return;
    auto* actor = GetScene()->FindActorByGUID(m_cameraPreview->GetCameraGUID());
    if (!actor || !EDITOR_EXEC(IsActorSelected(*actor))) return;
    const auto [width,height] = m_cameraPreview->GetOutputSize();
    if (!width || !height) return;
    UpdateCameraPreviewRect({width,height});
    const float scale = OVUI_SCALE, header = ImGui::GetFrameHeight()+8*scale;
    const float imageWidth = m_previewSize.x-8*scale, imageHeight = m_previewSize.y-header-4*scale;
    const ImVec2 pos{m_previewPosition.x,m_previewPosition.y};
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(pos,{pos.x+m_previewSize.x,pos.y+m_previewSize.y},IM_COL32(23,25,30,245),5*scale);
    draw->AddRect(pos,{pos.x+m_previewSize.x,pos.y+m_previewSize.y},IM_COL32(88,94,105,255),5*scale);
    draw->PushClipRect({pos.x+7*scale,pos.y},{pos.x+m_previewSize.x-30*scale,pos.y+header},true);
    const auto label = "Camera: "+actor->GetName();
    draw->AddText({pos.x+7*scale,pos.y+4*scale},IM_COL32(231,233,240,255),label.c_str());
    draw->PopClipRect();
    OvUI::Internal::TextureID texture(m_cameraPreview->GetOutputTexture()); texture.RetainForCapture();
    draw->AddImage(texture.id,{pos.x+4*scale,pos.y+header},{pos.x+4*scale+imageWidth,pos.y+header+imageHeight},{0,1},{1,0});
    const auto saved = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos({pos.x+m_previewSize.x-25*scale,pos.y+3*scale});
    ImGui::PushID(this);
    if (ImGui::SmallButton("x##CameraPreview")) Settings::EditorSettings::CameraPreviewEnabled = false;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hide camera preview. Enable it again in viewport Help.");
    ImGui::PopID(); ImGui::SetCursorScreenPos(saved); ImGui::Dummy({0,0});
}

void OvEditor::Panels::SceneView::UpdateCameraPreviewRect(std::pair<uint16_t,uint16_t> size)
{
    const auto [width,height] = size;
    if (!width || !height) { m_previewPosition = {}; m_previewSize = {}; return; }
    const float scale = OVUI_SCALE, header = ImGui::GetFrameHeight()+8*scale;
    float imageWidth = std::min(284*scale,GetSize().x*.38f);
    float imageHeight = imageWidth*height/width;
    const float maxHeight = GetSafeSize().second*.36f-header;
    if (imageHeight > maxHeight) { imageHeight = maxHeight; imageWidth = imageHeight*width/height; }
    const auto window = GetPosition();
    m_previewSize = {imageWidth+8*scale,imageHeight+header+4*scale};
    m_previewPosition = {window.x+GetSize().x-m_previewSize.x-12*scale,
        window.y+ImGui::GetFrameHeight()+GetSafeSize().second-m_previewSize.y-12*scale};
}

void OvEditor::Panels::SceneView::DrawViewportTools()
{
    using Settings::EditorSettings;
    const float scale = OVUI_SCALE;
    const auto saved = ImGui::GetCursorScreenPos(), window = ImGui::GetWindowPos();
    const ImVec2 content{window.x,window.y+ImGui::GetFrameHeight()};
    auto* draw = ImGui::GetWindowDrawList();
    if (m_boxPending && GetScene() && m_boxScene == GetScene()->GetLifetimeToken())
    {
        const ImVec2 a{content.x+std::min(m_boxStart.x,m_boxEnd.x),content.y+std::min(m_boxStart.y,m_boxEnd.y)};
        const ImVec2 b{content.x+std::max(m_boxStart.x,m_boxEnd.x),content.y+std::max(m_boxStart.y,m_boxEnd.y)};
        draw->AddRectFilled(a,b,IM_COL32(76,155,240,38));
        draw->AddRect(a,b,IM_COL32(110,181,255,240),0,0,scale);
    }
    m_toolsPopupOpen = false;
    m_toolsWidth = m_toolsHeight = 0;
    if (GetSize().x < 330*scale || GetSafeSize().second < 125*scale) return;
    // Transform tools and handle settings live in the main toolbar (Unity layout);
    // this overlay keeps snapping, framing and scene display options.
    m_toolsWidth = std::min(300*scale,GetSize().x-16*scale);
    m_toolsHeight = 2*ImGui::GetFrameHeight()+20*scale;
    const ImVec2 origin{content.x+8*scale,content.y+8*scale};
    draw->AddRectFilled(origin,{origin.x+m_toolsWidth,origin.y+m_toolsHeight},IM_COL32(24,25,29,230),6*scale);
    ImGui::PushID(this); ImGui::PushID("ViewportTools");
    ImGui::BeginDisabled(m_gizmoOperations.IsPicking() || m_boxPending || m_cameraController.IsOperating());
    const bool scaling = m_currentOperation == Core::EGizmoOperation::SCALE;
    ImGui::SetCursorScreenPos({origin.x+6*scale,origin.y+6*scale});
    bool snap = EditorSettings::SnapEnabled.Get();
    if (ImGui::Checkbox("Snap",&snap)) EditorSettings::SnapEnabled = snap;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Snap to increments. Hold Ctrl to snap temporarily.");
    ImGui::SameLine(0,6*scale);
    float step = m_currentOperation == Core::EGizmoOperation::TRANSLATE ? EditorSettings::TranslationSnapUnit.Get() : scaling ? EditorSettings::ScalingSnapUnit.Get() : EditorSettings::RotationSnapUnit.Get();
    ImGui::SetNextItemWidth(64*scale);
    if (ImGui::DragFloat("##Step",&step,.01f,.001f,1000.f,"%.3g",ImGuiSliderFlags_AlwaysClamp))
    {
        step = std::isfinite(step) ? std::clamp(step,.001f,1000.f) : 1.f;
        if (m_currentOperation == Core::EGizmoOperation::TRANSLATE) EditorSettings::TranslationSnapUnit = step;
        else if (scaling) EditorSettings::ScalingSnapUnit = step;
        else EditorSettings::RotationSnapUnit = step;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(scaling ? "Scale increment" : m_currentOperation == Core::EGizmoOperation::ROTATE ? "Angle increment (degrees)" : "Translation increment (world units)");
    ImGui::SameLine(0,6*scale);
    ImGui::BeginDisabled(!EDITOR_EXEC(IsAnyActorSelected()));
    if (ImGui::Button("Frame (F)",{78*scale,0})) FrameSelection();
    ImGui::EndDisabled(); ImGui::SameLine(0,6*scale);
    if (ImGui::Button("Help",{45*scale,0})) ImGui::OpenPopup("ViewportHelp");
    if (ImGui::BeginPopup("ViewportHelp"))
    {
        ImGui::TextUnformatted("Click: select | Ctrl+click: toggle");
        ImGui::TextUnformatted("Drag empty space: select visible objects");
        ImGui::TextUnformatted("Ctrl+drag: add to selection | Esc: cancel");
        ImGui::TextUnformatted("F: frame selection | W / E / R: transform");
        ImGui::TextUnformatted("Gold: primary | Blue: other selected objects");
        ImGui::Separator();
        bool preview = EditorSettings::CameraPreviewEnabled.Get();
        if (ImGui::Checkbox("Selected camera preview",&preview)) EditorSettings::CameraPreviewEnabled = preview;
        ImGui::EndPopup();
    }
    m_toolsPopupOpen = ImGui::IsPopupOpen("ViewportHelp");
    ImGui::SetCursorScreenPos({origin.x+6*scale,origin.y+ImGui::GetFrameHeight()+12*scale});
    const char* modes[] = {"Lit", "Unlit", "Wireframe"};
    ImGui::SetNextItemWidth(118*scale);
    if (ImGui::BeginCombo("##DisplayMode",modes[int(GetDisplayMode())]))
    {
        m_toolsPopupOpen = true;
        for (int i=0;i<3;++i)
            if (ImGui::Selectable(modes[i],int(GetDisplayMode()) == i)) SetDisplayMode(DisplayMode(i));
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scene display mode. Unlit shows base color; Wireframe shows mesh triangles.");
    ImGui::SameLine(0,6*scale);
    if (ImGui::Button("Overlays",{85*scale,0})) ImGui::OpenPopup("SceneOverlays");
    if (ImGui::BeginPopup("SceneOverlays"))
    {
        auto toggle = [&](const char* label, auto& setting) {
            bool enabled = setting.Get();
            if (ImGui::MenuItem(label,nullptr,&enabled)) { setting = enabled; InvalidateNavigationPicking(); }
        };
        toggle("Grid",EditorSettings::SceneShowGrid);
        toggle("Light icons",EditorSettings::SceneShowLightIcons);
        toggle("Camera icons",EditorSettings::SceneShowCameraIcons);
        toggle("Reflection probe icons",EditorSettings::SceneShowReflectionIcons);
        ImGui::Separator();
        toggle("Colliders (selection)",EditorSettings::SceneShowColliders);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Colliders on selected entities and their children");
        toggle("Camera frustums (selection)",EditorSettings::SceneShowCameraFrustums);
        toggle("Geometry bounds (selection)",EditorSettings::ShowGeometryBounds);
        toggle("Light volumes (selection)",EditorSettings::ShowLightBounds);
        ImGui::Separator();
        toggle("Selected camera preview",EditorSettings::CameraPreviewEnabled);
        ImGui::Separator();
        if (ImGui::MenuItem("Reset scene display"))
        {
            SetDisplayMode(DisplayMode::LIT);
            EditorSettings::SceneShowGrid = EditorSettings::SceneShowLightIcons = true;
            EditorSettings::SceneShowCameraIcons = EditorSettings::SceneShowReflectionIcons = true;
            EditorSettings::SceneShowColliders = EditorSettings::SceneShowCameraFrustums = true;
            EditorSettings::ShowGeometryBounds = EditorSettings::ShowLightBounds = false;
            EditorSettings::CameraPreviewEnabled = true;
            InvalidateNavigationPicking();
        }
        ImGui::EndPopup();
    }
        m_toolsPopupOpen |= ImGui::IsPopupOpen("SceneOverlays");
    ImGui::SameLine(0,6*scale);
    const bool is2d = Is2D();
    if (is2d)
    {
        const auto accent = ImGui::GetStyleColorVec4(ImGuiCol_Header);
        ImGui::PushStyleColor(ImGuiCol_Button, accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, accent);
    }
    if (ImGui::Button("2D##Mode", {38*scale, 0}))
    {
        Set2D(!is2d);
    }
    if (is2d) ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle 2D / 3D mode (2). Aligns camera orthographically with 2D/UI plane.");
    ImGui::EndDisabled(); ImGui::PopID(); ImGui::PopID();
    const auto count = EDITOR_EXEC(GetSelectedActors()).size();
    if (count && GetSafeSize().second > 160*scale)
    {
        const auto label = std::to_string(count)+" selected";
        const ImVec2 pos{content.x+12*scale,content.y+GetSafeSize().second-27*scale};
        const auto size = ImGui::CalcTextSize(label.c_str());
        draw->AddRectFilled({pos.x-5*scale,pos.y-3*scale},{pos.x+size.x+5*scale,pos.y+size.y+3*scale},IM_COL32(24,25,29,215),4*scale);
        draw->AddText(pos,IM_COL32(235,237,242,255),label.c_str());
    }
    ImGui::SetCursorScreenPos(saved); ImGui::Dummy({0,0});
}

void OvEditor::Panels::SceneView::DrawNavigationOverlay()
{
    using namespace OvMaths;
    const float scale = OVUI_SCALE;
    m_navigationPopupOpen = false;
    if (GetSize().x < 200*scale || GetSafeSize().second < 180*scale) return;
    const auto saved = ImGui::GetCursorScreenPos();
    const auto window = ImGui::GetWindowPos();
    const ImVec2 origin{window.x+ImGui::GetWindowSize().x-164*scale,window.y+ImGui::GetFrameHeight()+8*scale+(GetSize().x < 570*scale ? m_toolsHeight+8*scale : 0)};
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin,{origin.x+156*scale,origin.y+162*scale},IM_COL32(24,25,29,218),7*scale);
    const ImVec2 center{origin.x+78*scale,origin.y+59*scale};
    const auto& view = m_camera.GetViewMatrix();
    auto project = [&](const FVector3& point) {
        const auto p = view * FVector4{point.x,point.y,point.z,0};
        return ImVec2{center.x+p.x*19*scale,center.y-p.y*19*scale};
    };
    // The cube and axis endpoints use the same camera basis as the scene.
    const FVector3 axes[] = {FVector3::Right,FVector3::Up,FVector3::Forward};
    const char* labels[] = {"X","Y","Z"};
    const char* names[] = {"Right","Left","Top","Bottom","Front","Back"};
    const ImU32 colors[] = {IM_COL32(227,91,95,255),IM_COL32(115,198,110,255),IM_COL32(99,157,235,255)};
    struct Face { int axis, sign; float depth; };
    std::array<Face,6> faces{};
    for (int a=0;a<3;++a) for(int s=0;s<2;++s) {
        const auto n=axes[a]*float(s ? -1 : 1);
        const auto p=view*FVector4{n.x,n.y,n.z,0};
        faces[a*2+s]={a,s ? -1 : 1,p.z};
    }
    std::sort(faces.begin(),faces.end(),[](auto a,auto b){return a.depth<b.depth;});
    ImGui::PushID(this);
    ImGui::BeginDisabled(m_gizmoOperations.IsPicking() || m_cameraController.IsOperating());
    for (auto face : faces) {
        if (face.depth <= 0.001f) continue;
        const auto normal=axes[face.axis]*float(face.sign);
        const auto u=axes[(face.axis+1)%3],v=axes[(face.axis+2)%3];
        const ImVec2 corners[]={project(normal-u-v),project(normal+u-v),project(normal+u+v),project(normal-u+v)};
        draw->AddQuadFilled(corners[0],corners[1],corners[2],corners[3],IM_COL32(86,90,103,255));
        for(int i=0;i<4;++i)draw->AddLine(corners[i],corners[(i+1)%4],IM_COL32(180,185,195,255),scale);
    }
    // Draw rear endpoints first so a front endpoint owns overlapping hit regions.
    for (auto face : faces) {
        const auto point = project(axes[face.axis]*float(face.sign)*2.6f);
        if (std::abs(face.depth)>0.98f && face.sign<0) continue;
        draw->AddLine(center,point,colors[face.axis],1.5f*scale);
        ImGui::PushID(face.axis*2+(face.sign<0));
        ImGui::SetCursorScreenPos({point.x-10*scale,point.y-10*scale});
        if(ImGui::InvisibleButton("Axis",{20*scale,20*scale})) SetViewDirection(axes[face.axis]*float(face.sign));
        const bool hover=ImGui::IsItemHovered();
        draw->AddCircleFilled(point,10*scale,hover ? IM_COL32(245,205,98,255) : face.sign>0 ? colors[face.axis] : IM_COL32(57,61,72,255));
        const char* text=labels[face.axis];const auto size=ImGui::CalcTextSize(text);
        draw->AddText({point.x-size.x*.5f,point.y-size.y*.5f},IM_COL32(240,240,244,255),text);
        if(hover)ImGui::SetTooltip("%s view",names[face.axis*2+(face.sign<0)]);
        ImGui::PopID();
    }
    ImGui::SetCursorScreenPos({center.x-18*scale,center.y-18*scale});
    if (ImGui::InvisibleButton("ProjectionCube",{36*scale,36*scale})) ToggleProjection();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle perspective / orthographic (Numpad 5)");
    ImGui::SetCursorScreenPos({origin.x+8*scale,origin.y+116*scale});
    const bool ortho=m_camera.GetProjectionMode()==OvRendering::Settings::EProjectionMode::ORTHOGRAPHIC;
    if(ImGui::Button(ortho ? "Ortho" : "Persp",{68*scale,30*scale}))ToggleProjection();
    ImGui::SameLine(0,4*scale);
    if(ImGui::Button("Views",{68*scale,30*scale}))ImGui::OpenPopup("ViewsMenu");
    if(ImGui::BeginPopup("ViewsMenu")) {
        for(int i=0;i<6;++i)if(ImGui::MenuItem(names[i]))SetViewDirection(axes[i/2]*float(i%2 ? -1 : 1));
        ImGui::Separator();
        if(ImGui::MenuItem("2D Mode","2",Is2D()))Set2D(!Is2D());
        if(ImGui::MenuItem("Perspective",nullptr,!ortho)&&ortho)ToggleProjection();
        if(ImGui::MenuItem("Orthographic",nullptr,ortho)&&!ortho)ToggleProjection();
        ImGui::EndPopup();
    }
    m_navigationPopupOpen=ImGui::IsPopupOpen("ViewsMenu");
    ImGui::EndDisabled();
    ImGui::PopID();
    ImGui::SetCursorScreenPos(saved);
    ImGui::Dummy({0,0});
}
