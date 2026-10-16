/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once
#include <OvUI/Internal/TextureID.h>

#include <functional>
#include <vector>

#include <OvTools/Eventing/Event.h>

#include "OvUI/Widgets/Layout/Group.h"

namespace OvUI::Widgets::Layout
{
	/**
	* Widget that can contains other widgets and is collapsable
	*/
	class GroupCollapsable : public Group
	{
	public:
		/**
		* Represents a clickable icon button drawn inside the collapsable header
		*/
		struct HeaderAction
		{
			Internal::TextureID textureID;
			std::function<void()> callback;
		};

		/**
		* Constructor
		* @param p_name
		*/
		GroupCollapsable(const std::string& p_name = "");

	protected:
		virtual void _Draw_Impl() override;

	public:
		std::string name;
		bool closable = false;
		bool opened = true;
		bool defaultExpanded = true;
        bool expandForSearch = false;
        std::function<void()> contextMenu;
		bool reorderable = false;
		bool canMoveUp = true;
		bool canMoveDown = true;
		bool checkable = false;
		bool checked = true;
		std::function<bool()> checkedGatherer;
		std::function<bool()> mixedGatherer;
		std::function<void(bool)> checkedProvider;
		ImVec2 checkboxMin{0.f, 0.f};
		ImVec2 checkboxMax{0.f, 0.f};
		std::vector<HeaderAction> actions;
		OvTools::Eventing::Event<> CloseEvent;
		OvTools::Eventing::Event<> OpenEvent;
		OvTools::Eventing::Event<> MoveUpEvent;
		OvTools::Eventing::Event<> MoveDownEvent;
		OvTools::Eventing::Event<bool> CheckChangedEvent;
	};
}
