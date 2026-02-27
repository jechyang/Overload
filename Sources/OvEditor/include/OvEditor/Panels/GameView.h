#pragma once

#include <OvCore/SceneSystem/SceneManager.h>

#include "OvEditor/Panels/AView.h"

namespace OvEditor::Panels
{
	class GameView : public OvEditor::Panels::AView
	{
	public:
		GameView(
			const std::string& p_title,
			bool p_opened,
			const OvUI::Settings::PanelWindowSettings& p_windowSettings
		);

		virtual OvRendering::Entities::Camera* GetCamera();
		virtual OvCore::SceneSystem::Scene* GetScene();
		virtual const OvRendering::Data::FrameInfo& GetFrameInfo() const override;

	private:
		OvCore::SceneSystem::SceneManager& m_sceneManager;
	};
}