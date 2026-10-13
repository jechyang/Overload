/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <tracy/Tracy.hpp>

#include <OvEditor/Core/Application.h>
#include <OvEditor/Core/EditorActions.h>
#include <OvTools/Time/Clock.h>

OvEditor::Core::Application::Application(const std::filesystem::path& p_projectFolder) :
	m_context(p_projectFolder),
	m_editor(m_context)
{
    m_editor.SetFrameSubmissionEnabled(m_context.projectSettings.GetOrDefault<bool>("render_commands", true));
}

OvEditor::Core::Application::~Application()
{
}

void OvEditor::Core::Application::Run()
{
	OvTools::Time::Clock clock;

	while (true)
	{
		if (!IsRunning())
		{
			// Closing the window (or File > Exit) gives a chance to save a modified scene.
			if (EDITOR_EXEC(ConfirmDiscardSceneChanges())) break;
			m_context.window->SetShouldClose(false);
		}

		m_editor.PreUpdate();
		m_editor.Update(clock.GetDeltaTime());
		m_editor.PostUpdate();
		clock.Update();
		FrameMark;
	}
    m_editor.DrainRenderFrames();
}

bool OvEditor::Core::Application::IsRunning() const
{
	return !m_context.window->ShouldClose();
}
