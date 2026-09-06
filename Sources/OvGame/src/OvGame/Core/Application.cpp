/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <tracy/Tracy.hpp>

#include <OvGame/Core/Application.h>
#include <OvTools/Time/Clock.h>

OvGame::Core::Application::Application() :
	m_game(m_context)
{
    m_game.SetFrameSubmissionEnabled(m_context.projectSettings.GetOrDefault<bool>("render_commands", true));

}

OvGame::Core::Application::~Application()
{
}

void OvGame::Core::Application::Run()
{
	OvTools::Time::Clock clock;

	while (IsRunning())
	{
		m_game.PreUpdate();
		m_game.Update(clock.GetDeltaTime());
		m_game.PostUpdate();
		clock.Update();
		FrameMark;
	}
}

bool OvGame::Core::Application::IsRunning() const
{
	return !m_context.window->ShouldClose();
}
