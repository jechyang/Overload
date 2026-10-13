/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <optional>

#include <OvDebug/ILogHandler.h>
#include <OvTools/Eventing/Event.h>
#include <OvUI/Panels/APanel.h>

namespace OvEditor::Panels
{
	/**
	* Bottom status bar, as in Unity: the latest console message (click to open the Console)
	* on the left, script build status, play state and the active scene on the right.
	*/
	class StatusBar : public OvUI::Panels::APanel
	{
	public:
		StatusBar();
		~StatusBar() override;

	protected:
		void _Draw_Impl() override;

	private:
		std::optional<OvDebug::LogData> m_lastLog;
		OvTools::Eventing::ListenerID m_logListener = 0;
	};
}
