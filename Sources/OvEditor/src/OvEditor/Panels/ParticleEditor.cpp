/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <imgui.h>

#include <OvCore/ParticleSystem/CParticleSystem.h>
#include <OvCore/ParticleSystem/AParticleSystemEmitter.h>
#include <OvCore/ParticleSystem/ParticleSystemAffector.h>
#include <OvCore/ParticleSystem/ParticleSystemLoader.h>
#include <OvCore/ResourceManagement/MaterialManager.h>
#include <OvCore/Global/ServiceLocator.h>
#include <OvUI/Widgets/Layout/Group.h>
#include <OvRendering/Data/FrameDescriptor.h>
#include <OvRendering/Features/FrameInfoRenderFeature.h>
#include <OvRendering/HAL/Texture.h>
#include <OvRendering/Settings/EFramebufferAttachment.h>

#include <OvEditor/Core/EditorActions.h>
#include <OvEditor/Panels/ParticleEditor.h>
#include <OvRendering/Features/DebugShapeRenderFeature.h>
#include <OvEditor/Rendering/DebugModelRenderFeature.h>
#include <OvRendering/FrameGraph/FrameGraph.h>
#include <OvRendering/FrameGraph/FrameGraphBuilder.h>
#include <OvRendering/FrameGraph/FrameGraphResources.h>

#include <tracy/Tracy.hpp>

namespace
{
	class ParticleEditorRenderer : public OvCore::Rendering::SceneRenderer
	{
	public:
		ParticleEditorRenderer(OvRendering::Context::Driver& p_driver)
			: OvCore::Rendering::SceneRenderer(p_driver)
		{
			using namespace OvRendering::Features;
			using enum EFeatureExecutionPolicy;

			m_debugModelFeature = std::make_unique<OvEditor::Rendering::DebugModelRenderFeature>(*this, NEVER);
			m_debugShapeFeature = std::make_unique<DebugShapeRenderFeature>(*this, NEVER);
			m_frameInfoFeature  = std::make_unique<FrameInfoRenderFeature>(*this, ALWAYS);
		}

		const OvRendering::Data::FrameInfo& GetFrameInfo() const
		{
			return m_frameInfoFeature->GetFrameInfo();
		}

		void EndFrame() override
		{
			m_frameInfoFeature->OnEndFrame();
			OvCore::Rendering::SceneRenderer::EndFrame();
		}

	protected:
		void BuildFrameGraph(OvRendering::FrameGraph::FrameGraph& p_fg) override
		{
			m_frameInfoFeature->OnBeginFrame(m_frameDescriptor);
			SceneRenderer::BuildFrameGraph(p_fg);

			// Grid Pass - inlined from GridRenderPass
			struct GridPassData
			{
				OvCore::Resources::Material gridMaterial;
			};

			p_fg.AddPass<GridPassData>("Grid",
				[this](OvRendering::FrameGraph::FrameGraphBuilder& builder, GridPassData& data) {
					// Initialize grid material
					data.gridMaterial.SetShader(EDITOR_CONTEXT(editorResources)->GetShader("Grid"));
					data.gridMaterial.SetBlendable(true);
					data.gridMaterial.SetBackfaceCulling(false);
					data.gridMaterial.SetDepthWriting(false);
					data.gridMaterial.SetDepthTest(true);
					builder.SetAsOutput({});
				},
				[this](const OvRendering::FrameGraph::FrameGraphResources&, GridPassData& data) {
					ZoneScoped;

					auto& frameDescriptor = m_frameDescriptor;
					auto pso = CreatePipelineState();

					// Set up camera matrices
					auto viewPos = frameDescriptor.camera->transform->GetWorldPosition();
					auto transposedView = OvMaths::FMatrix4::Transpose(frameDescriptor.camera->GetViewMatrix());
					auto proj = frameDescriptor.camera->GetProjectionMatrix();

					// Upload camera matrices to engine UBO
					auto& engineUBO = GetEngineBuffer();
					engineUBO.Upload(&transposedView, OvRendering::HAL::BufferMemoryRange{ .offset = sizeof(OvMaths::FMatrix4), .size = sizeof(OvMaths::FMatrix4) });
					engineUBO.Upload(&proj, OvRendering::HAL::BufferMemoryRange{ .offset = sizeof(OvMaths::FMatrix4) * 2, .size = sizeof(OvMaths::FMatrix4) });
					engineUBO.Bind(0);

					constexpr float gridSize = 5000.0f;
					OvMaths::FMatrix4 model =
						OvMaths::FMatrix4::Translation({ viewPos.x, 0.0f, viewPos.z }) *
						OvMaths::FMatrix4::Scaling({ gridSize * 2.0f, 1.f, gridSize * 2.0f });

					data.gridMaterial.SetProperty("u_Color", OvMaths::FVector3{ 0.0f, 0.447f, 1.0f });

					m_debugModelFeature->DrawModelWithSingleMaterial(
						pso,
						*EDITOR_CONTEXT(editorResources)->GetModel("Plane"),
						data.gridMaterial,
						model
					);

					constexpr float kLineWidth = 1.0f;
					m_debugShapeFeature->DrawLine(pso, OvMaths::FVector3(-gridSize + viewPos.x, 0.0f, 0.0f), OvMaths::FVector3(gridSize + viewPos.x, 0.0f, 0.0f), OvMaths::FVector3::Right, kLineWidth);
					m_debugShapeFeature->DrawLine(pso, OvMaths::FVector3(0.0f, -gridSize + viewPos.y, 0.0f), OvMaths::FVector3(0.0f, gridSize + viewPos.y, 0.0f), OvMaths::FVector3::Up, kLineWidth);
					m_debugShapeFeature->DrawLine(pso, OvMaths::FVector3(0.0f, 0.0f, -gridSize + viewPos.z), OvMaths::FVector3(0.0f, 0.0f, gridSize + viewPos.z), OvMaths::FVector3::Forward, kLineWidth);
				}
			);
		}

	private:
		std::unique_ptr<OvEditor::Rendering::DebugModelRenderFeature> m_debugModelFeature;
		std::unique_ptr<OvRendering::Features::DebugShapeRenderFeature> m_debugShapeFeature;
		std::unique_ptr<OvRendering::Features::FrameInfoRenderFeature> m_frameInfoFeature;
	};
}

using namespace OvEditor::Panels;
using namespace OvCore::ParticleSystem;

OvEditor::Panels::ParticleEditor::ParticleEditor(
	const std::string& p_title,
	bool p_opened,
	const OvUI::Settings::PanelWindowSettings& p_windowSettings
) : AViewControllable(p_title, p_opened, p_windowSettings)
{
	m_renderer = std::make_unique<ParticleEditorRenderer>(*EDITOR_CONTEXT(driver));

	m_camera.SetFar(5000.0f);

	m_scene.AddDefaultLights();
	m_scene.AddDefaultPostProcessStack();
	m_scene.AddDefaultAtmosphere();

	m_particleActor = &m_scene.CreateActor("Particles");
	m_particleSystem = &m_particleActor->AddComponent<OvCore::ECS::Components::CParticleSystem>();
	m_particleSystem->SetEmitter(std::make_unique<PointParticleEmitter>());

	// Position camera to look at the emitter origin
	m_camera.transform->SetWorldPosition({ 0.0f, 2.0f, 8.0f });
	m_camera.transform->SetWorldRotation(OvMaths::FQuaternion({ -10.0f, 0.0f, 0.0f }));
	m_cameraController.LockTargetActor(*m_particleActor);

	// We draw the viewport image manually via ImGui::Image
	m_image->enabled = false;

	// Give the framebuffer a valid initial size so Render() works before the first _Draw_Impl
	m_viewportWidth  = 800.0f;
	m_viewportHeight = 600.0f;

	// Auto-load the default particle material
	auto& matManager = OvCore::Global::ServiceLocator::Get<OvCore::ResourceManagement::MaterialManager>();
	if (auto* mat = matManager.GetResource(":Materials\\Particle.ovmat"))
		m_particleSystem->material = mat;

	// Wake up all actors so OnUpdate is called each frame
	m_scene.Play();
}

OvCore::SceneSystem::Scene* OvEditor::Panels::ParticleEditor::GetScene()
{
	return &m_scene;
}

const OvRendering::Data::FrameInfo& OvEditor::Panels::ParticleEditor::GetFrameInfo() const
{
	return static_cast<const ParticleEditorRenderer&>(*m_renderer).GetFrameInfo();
}

void OvEditor::Panels::ParticleEditor::SetTarget(OvCore::ECS::Components::CParticleSystem& p_system)
{
	// Copy material reference
	m_particleSystem->material = p_system.material;

	// Copy emitter settings - support both Point and Circle emitters
	if (auto* src = dynamic_cast<PointParticleEmitter*>(p_system.GetEmitter()))
	{
		auto newEmitter = std::make_unique<PointParticleEmitter>(
			src->emissionRate,
			src->lifetime,
			src->initialSpeed,
			src->size,
			src->spread
		);
		m_particleSystem->SetEmitter(std::move(newEmitter));
	}
	else if (auto* src = dynamic_cast<CircleParticleEmitter*>(p_system.GetEmitter()))
	{
		auto newEmitter = std::make_unique<CircleParticleEmitter>(
			src->emissionRate,
			src->lifetime,
			src->initialSpeed,
			src->size,
			src->radius,
			src->direction,
			src->spread
		);
		m_particleSystem->SetEmitter(std::move(newEmitter));
	}

	// Copy gravity affector if present
	if (auto* src = p_system.GetAffectorAs<GravityAffector>())
	{
		m_particleSystem->AddAffector(std::make_unique<GravityAffector>(src->gravity));
	}

	// Copy color gradient affector if present
	if (auto* src = p_system.GetAffectorAs<ColorGradientAffector>())
	{
		m_particleSystem->AddAffector(std::make_unique<ColorGradientAffector>(
			src->startColor, src->midColor, src->endColor, src->midTime
		));
	}
}

void OvEditor::Panels::ParticleEditor::LoadFromFile(const std::string& p_path)
{
	OvCore::ParticleSystem::ParticleSystemLoader::Load(*m_particleSystem, p_path);
	m_currentFilePath = p_path;
	m_playing = true;
}

void OvEditor::Panels::ParticleEditor::Update(float p_deltaTime)
{
	AViewControllable::Update(p_deltaTime);

	if (m_playing)
		m_scene.Update(p_deltaTime);
}

void OvEditor::Panels::ParticleEditor::Render()
{
	const auto w = static_cast<uint16_t>(m_viewportWidth);
	const auto h = static_cast<uint16_t>(m_viewportHeight);

	auto* camera = GetCamera();
	auto* scene  = GetScene();

	if (w == 0 || h == 0 || !camera || !scene)
		return;

	m_framebuffer.Resize(w, h);

	InitFrame();

	OvRendering::Data::FrameDescriptor frameDescriptor;
	frameDescriptor.renderWidth  = w;
	frameDescriptor.renderHeight = h;
	frameDescriptor.camera       = camera;
	frameDescriptor.outputBuffer = m_framebuffer;

	m_renderer->BeginFrame(frameDescriptor);
	DrawFrame();
	m_renderer->EndFrame();

	EDITOR_CONTEXT(driver)->OnFrameCompleted();
}

void OvEditor::Panels::ParticleEditor::_Draw_Impl()
{
	if (!IsOpened())
		return;

	// Replicate PanelWindow flag logic
	int windowFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
	if (!resizable)  windowFlags |= ImGuiWindowFlags_NoResize;
	if (!movable)    windowFlags |= ImGuiWindowFlags_NoMove;
	if (!dockable)   windowFlags |= ImGuiWindowFlags_NoDocking;
	if (!collapsable) windowFlags |= ImGuiWindowFlags_NoCollapse;

	const std::string windowID = name + GetPanelID();
	bool opened = true;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	const bool visible = ImGui::Begin(windowID.c_str(), closable ? &opened : nullptr, windowFlags);
	ImGui::PopStyleVar();

	m_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
	m_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);

	if (visible)
	{
		const ImVec2 available = ImGui::GetContentRegionAvail();
		const float  viewportW = std::max(1.0f, available.x - kPropertiesWidth);
		const float  viewportH = std::max(1.0f, available.y);

		m_viewportWidth  = viewportW;
		m_viewportHeight = viewportH;

		// ── Left: 3D viewport ────────────────────────────────────────────
		ImGui::BeginChild("##pe_viewport", ImVec2(viewportW, viewportH), false,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

		if (auto tex = m_framebuffer.GetAttachment<OvRendering::HAL::Texture>(
			OvRendering::Settings::EFramebufferAttachment::COLOR))
		{
			ImGui::Image(static_cast<ImTextureID>(tex->GetID()), ImVec2(viewportW, viewportH), ImVec2(0, 1), ImVec2(1, 0));
		}

		ImGui::EndChild();
		ImGui::SameLine(0, 0);

		// ── Right: properties ────────────────────────────────────────────
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
		ImGui::BeginChild("##pe_props", ImVec2(kPropertiesWidth, viewportH), false);
		ImGui::PopStyleVar();

		DrawProperties();

		ImGui::EndChild();
	}

	ImGui::End();

	// Sync close state back to PanelWindow (fires CloseEvent)
	if (!opened)
		SetOpened(false);
}

void OvEditor::Panels::ParticleEditor::DrawProperties()
{
	// ── Playback controls ────────────────────────────────────────────────
	if (ImGui::Button(m_playing ? "  Pause  " : "  Play   "))
		m_playing = !m_playing;

	ImGui::SameLine();

	if (ImGui::Button("  Reset  "))
	{
		// Save current affector settings before reset
		auto* colorGrad = m_particleSystem->GetAffectorAs<ColorGradientAffector>();
		if (colorGrad)
		{
			m_particleSystem->Reset();
			m_particleSystem->AddAffector(std::make_unique<ColorGradientAffector>(
				colorGrad->startColor, colorGrad->midColor, colorGrad->endColor, colorGrad->midTime
			));
		}
		else
		{
			m_particleSystem->Reset();
		}
		m_playing = true;
	}

	if (!m_currentFilePath.empty())
	{
		ImGui::SameLine();
		if (ImGui::Button("  Save  "))
			OvCore::ParticleSystem::ParticleSystemLoader::Save(*m_particleSystem, m_currentFilePath);
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	// ── Emitter ──────────────────────────────────────────────────────────
	if (ImGui::CollapsingHeader("Emitter", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Indent(8.0f);

		if (auto* emitter = dynamic_cast<PointParticleEmitter*>(m_particleSystem->GetEmitter()))
		{
			constexpr float kLabelW = 110.0f;
			if (ImGui::BeginTable("##emitter_tbl", 2, ImGuiTableFlags_None))
			{
				ImGui::TableSetupColumn("##lbl", ImGuiTableColumnFlags_WidthFixed, kLabelW);
				ImGui::TableSetupColumn("##val", ImGuiTableColumnFlags_WidthStretch);

				auto row = [](const char* label, const char* desc) {
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::AlignTextToFramePadding();
					ImGui::TextUnformatted(label);
					if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
						ImGui::SetTooltip("%s", desc);
					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1);
				};

				row("Emission Rate", "Particles emitted per second.");
				ImGui::DragFloat("##emissionRate", &emitter->emissionRate, 0.5f,  0.0f,   1000.0f, "%.1f /s");

				row("Lifetime", "How long each particle lives (seconds).");
				ImGui::DragFloat("##lifetime",     &emitter->lifetime,     0.05f, 0.01f,  60.0f,   "%.2f s");

				row("Initial Speed", "Speed at the moment of emission (units/s).");
				ImGui::DragFloat("##initSpeed",    &emitter->initialSpeed, 0.05f, 0.0f,   100.0f,  "%.2f u/s");

				row("Size", "Billboard quad side length per particle (world units).");
				ImGui::DragFloat("##size",         &emitter->size,         0.005f,0.001f, 10.0f,   "%.3f u");

				row("Spread", "Emission cone half-angle (radians). 0 = straight up, ~3.14 = all directions.");
				ImGui::DragFloat("##spread",       &emitter->spread,       0.01f, 0.0f,   3.1416f, "%.3f rad");

				ImGui::EndTable();
			}
		}
		else if (auto* emitter = dynamic_cast<CircleParticleEmitter*>(m_particleSystem->GetEmitter()))
		{
			constexpr float kLabelW = 110.0f;
			if (ImGui::BeginTable("##emitter_tbl", 2, ImGuiTableFlags_None))
			{
				ImGui::TableSetupColumn("##lbl", ImGuiTableColumnFlags_WidthFixed, kLabelW);
				ImGui::TableSetupColumn("##val", ImGuiTableColumnFlags_WidthStretch);

				auto row = [](const char* label, const char* desc) {
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::AlignTextToFramePadding();
					ImGui::TextUnformatted(label);
					if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
						ImGui::SetTooltip("%s", desc);
					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1);
				};

				row("Emission Rate", "Particles emitted per second.");
				ImGui::DragFloat("##emissionRate", &emitter->emissionRate, 0.5f,  0.0f,   1000.0f, "%.1f /s");

				row("Lifetime", "How long each particle lives (seconds).");
				ImGui::DragFloat("##lifetime",     &emitter->lifetime,     0.05f, 0.01f,  60.0f,   "%.2f s");

				row("Initial Speed", "Speed at the moment of emission (units/s).");
				ImGui::DragFloat("##initSpeed",    &emitter->initialSpeed, 0.05f, 0.0f,   100.0f,  "%.2f u/s");

				row("Size", "Billboard quad side length per particle (world units).");
				ImGui::DragFloat("##size",         &emitter->size,         0.005f,0.001f, 10.0f,   "%.3f u");

				row("Radius", "Circle radius in world units.");
				ImGui::DragFloat("##radius",       &emitter->radius,       0.01f, 0.0f,   100.0f,  "%.2f u");

				row("Spread", "Emission cone half-angle (radians). 0 = straight up, ~3.14 = all directions.");
				ImGui::DragFloat("##spread",       &emitter->spread,       0.01f, 0.0f,   3.1416f, "%.3f rad");

				row("Direction X", "Direction vector X component.");
				ImGui::DragFloat("##dirX",         &emitter->direction.x,  0.01f, -1.0f,   1.0f,    "%.2f");

				row("Direction Y", "Direction vector Y component.");
				ImGui::DragFloat("##dirY",         &emitter->direction.y,  0.01f, -1.0f,   1.0f,    "%.2f");

				row("Direction Z", "Direction vector Z component.");
				ImGui::DragFloat("##dirZ",         &emitter->direction.z,  0.01f, -1.0f,   1.0f,    "%.2f");

				ImGui::EndTable();
			}
		}
		else
		{
			ImGui::TextDisabled("(no emitter)");
		}

		ImGui::Unindent(8.0f);
	}

	ImGui::Spacing();

	// ── Affectors ────────────────────────────────────────────────────────
	if (ImGui::CollapsingHeader("Affectors", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Indent(8.0f);

		// Gravity affector
		auto* gravity = m_particleSystem->GetAffectorAs<GravityAffector>();
		bool  hasGravity = gravity != nullptr;

		if (ImGui::Checkbox("Gravity", &hasGravity))
		{
			if (hasGravity)
				m_particleSystem->AddAffector(std::make_unique<GravityAffector>());
		}

		if (gravity)
		{
			constexpr float kLabelW = 110.0f;
			if (ImGui::BeginTable("##affector_gravity_tbl", 2, ImGuiTableFlags_None))
			{
				ImGui::TableSetupColumn("##lbl", ImGuiTableColumnFlags_WidthFixed, kLabelW);
				ImGui::TableSetupColumn("##val", ImGuiTableColumnFlags_WidthStretch);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted("Acceleration");
				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-1);
				ImGui::DragFloat("##gravity_val", &gravity->gravity, 0.1f, 0.0f, 100.0f, "%.2f m/s^2");

				ImGui::EndTable();
			}
		}

		ImGui::Separator();

		// Color gradient affector
		auto* colorGrad = m_particleSystem->GetAffectorAs<ColorGradientAffector>();
		bool  hasColorGradient = colorGrad != nullptr;

		if (ImGui::Checkbox("Color Gradient", &hasColorGradient))
		{
			if (hasColorGradient)
				m_particleSystem->AddAffector(std::make_unique<ColorGradientAffector>());
		}

		if (colorGrad)
		{
			constexpr float kLabelW = 110.0f;
			if (ImGui::BeginTable("##affector_color_tbl", 2, ImGuiTableFlags_None))
			{
				ImGui::TableSetupColumn("##lbl", ImGuiTableColumnFlags_WidthFixed, kLabelW);
				ImGui::TableSetupColumn("##val", ImGuiTableColumnFlags_WidthStretch);

				auto row = [](const char* label, const char* desc) {
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::AlignTextToFramePadding();
					ImGui::TextUnformatted(label);
					if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
						ImGui::SetTooltip("%s", desc);
					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1);
				};

				row("Start Color", "Color at particle birth.");
				{
					ImGui::ColorEdit4("##startColor", &colorGrad->startColor.x, ImGuiColorEditFlags_Float);
				}

				row("Mid Color", "Color at midTime.");
				{
					ImGui::ColorEdit4("##midColor", &colorGrad->midColor.x, ImGuiColorEditFlags_Float);
				}

				row("End Color", "Color at particle death.");
				{
					ImGui::ColorEdit4("##endColor", &colorGrad->endColor.x, ImGuiColorEditFlags_Float);
				}

				row("Mid Time", "Normalized time (0-1) when midColor is reached.");
				ImGui::DragFloat("##midTime", &colorGrad->midTime, 0.01f, 0.0f, 1.0f, "%.2f");

				ImGui::EndTable();
			}
		}

		ImGui::Unindent(8.0f);
	}

	ImGui::Spacing();

	// ── Material ─────────────────────────────────────────────────────────
	if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Indent(8.0f);

		const char* matPath = m_particleSystem->material
			? m_particleSystem->material->path.c_str()
			: "None";

		ImGui::TextUnformatted("Material:");
		ImGui::SameLine();
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
		ImGui::Button(matPath, ImVec2(-1, 0));
		ImGui::PopStyleColor();

		// Accept .ovmat drag-drop from the Asset Browser
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("File"))
			{
				const auto* data = static_cast<const std::pair<std::string, OvUI::Widgets::Layout::Group*>*>(payload->Data);
				const std::string& path = data->first;

				if (path.ends_with(".ovmat"))
				{
					auto& matManager = OvCore::Global::ServiceLocator::Get<
						OvCore::ResourceManagement::MaterialManager>();
					if (auto* mat = matManager.GetResource(path))
						m_particleSystem->material = mat;
				}
			}
			ImGui::EndDragDropTarget();
		}

		if (m_particleSystem->material)
		{
			ImGui::SameLine();
			if (ImGui::SmallButton("X"))
				m_particleSystem->material = nullptr;
		}

		ImGui::Unindent(8.0f);
	}

	ImGui::Spacing();

	// ── Stats ────────────────────────────────────────────────────────────
	if (ImGui::CollapsingHeader("Stats"))
	{
		ImGui::Indent(8.0f);
		ImGui::Text("Live particles: %u", m_particleSystem->GetParticleCount());
		ImGui::Unindent(8.0f);
	}
}
