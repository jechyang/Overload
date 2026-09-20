/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/


#include <OvCore/ECS/Components/CCamera.h>

#include "OvEditor/Panels/GameView.h"
#include "OvEditor/Core/EditorActions.h"
#include "OvEditor/Settings/EditorSettings.h"

OvEditor::Panels::GameView::GameView
(
	const std::string & p_title,
	bool p_opened,
	const OvUI::Settings::PanelWindowSettings & p_windowSettings
) :
	AView(p_title, p_opened, p_windowSettings),
	m_sceneManager(EDITOR_CONTEXT(sceneManager))
{
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


void OvEditor::Panels::GameView::Update(float deltaTime)
{
    AView::Update(deltaTime);
    auto& input = *EDITOR_CONTEXT(inputManager);
    const auto [width,height] = GetSafeSize();
    const auto origin = ImGui::GetMainViewport()->Pos;
    if (width && height)
        input.SetUIViewport({{m_position.x-origin.x,m_position.y-origin.y+ImGui::GetFrameHeight()},
            {double(width),double(height)}, {1,1}});
    // The AView render target currently uses GetSafeSize pixels. Its scale to
    // ImGui content is 1 even on high-DPI desktops; never multiply OS DPI twice.
    input.SetUIFocused(IsOpened() && IsVisible() && IsFocused());
}
