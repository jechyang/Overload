/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <OvRendering/Features/FrameInfoRenderFeature.h>
#include <OvRendering/FrameGraph/FrameGraph.h>
#include <OvRendering/FrameGraph/FrameGraphBuilder.h>
#include <OvRendering/FrameGraph/FrameGraphResources.h>

#include <OvCore/ECS/Components/CCamera.h>
#include <OvCore/Rendering/SceneRenderer.h>

#include "OvEditor/Panels/GameView.h"
#include "OvEditor/Core/EditorActions.h"
#include "OvEditor/Settings/EditorSettings.h"

namespace
{
	class GameViewRenderer : public OvCore::Rendering::SceneRenderer
	{
	public:
		GameViewRenderer(OvRendering::Context::Driver& p_driver)
			: OvCore::Rendering::SceneRenderer(p_driver)
		{
			m_frameInfoFeature = std::make_unique<OvRendering::Features::FrameInfoRenderFeature>(
				*this, OvRendering::Features::EFeatureExecutionPolicy::ALWAYS
			);
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
		}

	private:
		std::unique_ptr<OvRendering::Features::FrameInfoRenderFeature> m_frameInfoFeature;
	};
}

OvEditor::Panels::GameView::GameView
(
	const std::string & p_title,
	bool p_opened,
	const OvUI::Settings::PanelWindowSettings & p_windowSettings
) :
	AView(p_title, p_opened, p_windowSettings),
	m_sceneManager(EDITOR_CONTEXT(sceneManager))
{
	m_renderer = std::make_unique<GameViewRenderer>(*EDITOR_CONTEXT(driver));
}

OvRendering::Entities::Camera* OvEditor::Panels::GameView::GetCamera()
{
	if (auto scene = m_sceneManager.GetCurrentScene())
	{
		if (auto camera = scene->FindMainCamera())
		{
			return &camera->GetCamera();
		}
	}

	return nullptr;
}

OvCore::SceneSystem::Scene* OvEditor::Panels::GameView::GetScene()
{
	return m_sceneManager.GetCurrentScene();
}

const OvRendering::Data::FrameInfo& OvEditor::Panels::GameView::GetFrameInfo() const
{
	return static_cast<const GameViewRenderer&>(*m_renderer).GetFrameInfo();
}
