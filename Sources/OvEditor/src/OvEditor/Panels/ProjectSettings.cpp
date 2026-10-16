/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include "OvEditor/Panels/ProjectSettings.h"
#include "OvEditor/Core/EditorActions.h"
#include "OvTools/Utils/PathParser.h"

#include <imgui.h>
#include <OvCore/Resources/Loaders/MaterialLoader.h>
#include <OvCore/Helpers/GUIDrawer.h>
#include <OvUI/Styling/Style.h>
#include <OvUI/Widgets/Layout/Columns.h>
#include <OvUI/Widgets/Layout/GroupCollapsable.h>
#include <OvUI/Widgets/Visual/Separator.h>
#include <OvUI/Widgets/Buttons/Button.h>
#include <OvUI/Widgets/Selection/ComboBox.h>
#include <OvUI/Plugins/DataDispatcher.h>

using namespace OvUI::Panels;
using namespace OvUI::Widgets;
using namespace OvCore::Helpers;

namespace
{
	class CustomCallbackWidget final : public AWidget
	{
	public:
		explicit CustomCallbackWidget(std::function<void()> draw) : m_draw(std::move(draw)) {}
	private:
		void _Draw_Impl() override { m_draw(); }
		std::function<void()> m_draw;
	};
}

OvEditor::Panels::ProjectSettings::ProjectSettings(const std::string & p_title, bool p_opened, const OvUI::Settings::PanelWindowSettings & p_windowSettings) :
	PanelWindow(p_title, p_opened, p_windowSettings),
	m_projectFile(EDITOR_CONTEXT(projectSettings))
{
	auto& saveButton = CreateWidget<Buttons::Button>("Apply");
	saveButton.backgroundColor = OVUI_STYLE(SuccessButton);
	saveButton.hoveredBackgroundColor = OVUI_STYLE(SuccessButtonHovered);
	saveButton.clickedBackgroundColor = OVUI_STYLE(SuccessButtonActive);
	saveButton.ClickedEvent += [this]
	{
		EDITOR_CONTEXT(ApplyProjectSettings());
		m_projectFile.Rewrite();
	};

	saveButton.lineBreak = false;

	auto& resetButton = CreateWidget<Buttons::Button>("Reset");
	resetButton.backgroundColor = OVUI_STYLE(DangerButton);
	resetButton.hoveredBackgroundColor = OVUI_STYLE(DangerButtonHovered);
	resetButton.clickedBackgroundColor = OVUI_STYLE(DangerButtonActive);
	resetButton.ClickedEvent += [this]
	{
		EDITOR_CONTEXT(ResetProjectSettings());
	};

	CreateWidget<Visual::Separator>();

	{
		/* Physics settings */
		auto& root = CreateWidget<Layout::GroupCollapsable>("Physics");
		auto& columns = root.CreateWidget<Layout::Columns<2>>();
		columns.widths[0] = 125 * OVUI_SCALE;

		GUIDrawer::DrawScalar<float>(columns, "Gravity", GenerateGatherer<float>("gravity"), GenerateProvider<float>("gravity"), 0.1f, GUIDrawer::_MIN_FLOAT, GUIDrawer::_MAX_FLOAT);
	}

	{
		/* Build settings */
		auto& generationRoot = CreateWidget<Layout::GroupCollapsable>("Build");
		auto& columns = generationRoot.CreateWidget<Layout::Columns<2>>();
		columns.widths[0] = 125 * OVUI_SCALE;

		GUIDrawer::CreateTitle(columns, "Build Type");
		auto& comboBox = columns.CreateWidget<OvUI::Widgets::Selection::ComboBox>(m_projectFile.Get<int>("build_type"));
		comboBox.choices = {
			{ static_cast<int>(OvEditor::Core::EBuildType::Debug),   OvEditor::Core::GetBuildTypeName(OvEditor::Core::EBuildType::Debug) },
			{ static_cast<int>(OvEditor::Core::EBuildType::Release), OvEditor::Core::GetBuildTypeName(OvEditor::Core::EBuildType::Release) },
			{ static_cast<int>(OvEditor::Core::EBuildType::Publish), OvEditor::Core::GetBuildTypeName(OvEditor::Core::EBuildType::Publish) },
		};
		auto& dispatcher = comboBox.AddPlugin<OvUI::Plugins::DataDispatcher<int>>();
		dispatcher.RegisterGatherer(GenerateGatherer<int>("build_type"));
		dispatcher.RegisterProvider(GenerateProvider<int>("build_type"));
	}

	{
		/* Windowing settings */
		auto& windowingRoot = CreateWidget<Layout::GroupCollapsable>("Windowing");
		auto& columns = windowingRoot.CreateWidget<Layout::Columns<2>>();
		columns.widths[0] = 125 * OVUI_SCALE;

		GUIDrawer::DrawScalar<int>(columns, "Resolution X", GenerateGatherer<int>("x_resolution"), GenerateProvider<int>("x_resolution"), 1, 0, 10000);
		GUIDrawer::DrawScalar<int>(columns, "Resolution Y", GenerateGatherer<int>("y_resolution"), GenerateProvider<int>("y_resolution"), 1, 0, 10000);
		GUIDrawer::DrawBoolean(columns, "Fullscreen", GenerateGatherer<bool>("fullscreen"), GenerateProvider<bool>("fullscreen"));
		GUIDrawer::DrawString(columns, "Executable name", GenerateGatherer<std::string>("executable_name"), GenerateProvider<std::string>("executable_name"));
		GUIDrawer::DrawAsset(columns, "Window Icon", GenerateGatherer<std::string>("window_icon"), GenerateProvider<std::string>("window_icon"), OvTools::Utils::PathParser::EFileType::TEXTURE);
	}

	{
		/* Rendering settings */
		auto& renderingRoot = CreateWidget<Layout::GroupCollapsable>("Rendering");
		auto& columns = renderingRoot.CreateWidget<Layout::Columns<2>>();
		columns.widths[0] = 125 * OVUI_SCALE;

		GUIDrawer::DrawBoolean(columns, "Vertical Sync.", GenerateGatherer<bool>("vsync"), GenerateProvider<bool>("vsync"));
		GUIDrawer::DrawBoolean(columns, "Multi-sampling", GenerateGatherer<bool>("multisampling"), GenerateProvider<bool>("multisampling"));
		GUIDrawer::DrawScalar<int>(columns, "Samples", GenerateGatherer<int>("samples"), GenerateProvider<int>("samples"), 1, 2, 16);
	}

	{
		/* Scene Management settings */
		auto& gameRoot = CreateWidget<Layout::GroupCollapsable>("Scene Management");
		auto& columns = gameRoot.CreateWidget<Layout::Columns<2>>();
		columns.widths[0] = 125 * OVUI_SCALE;

		GUIDrawer::DrawScene(columns, "Start scene", GenerateGatherer<std::string>("start_scene"), GenerateProvider<std::string>("start_scene"));
	}

	{
		/* Tags settings (Unity Tags & Layers style) */
		m_tagsGroup = &CreateWidget<Layout::GroupCollapsable>("Tags");
		m_tagsGroup->SetID("project_settings_tags");
		m_tagsGroup->CreateWidget<CustomCallbackWidget>([this]
		{
			if (m_tagsGroup && m_tagsGroup->expandForSearch)
			{
				ImGui::GetStateStorage()->SetInt(ImGui::GetID("Tags###project_settings_tags"), 1);
				m_tagsGroup->expandForSearch = false;
			}
			const float scale = OVUI_SCALE;
			ImGui::PushID("ProjectSettingsTags");
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextDisabled("Built-in: Untagged, Respawn, Finish, EditorOnly, MainCamera, Player, GameController");
			ImGui::PopTextWrapPos();
			const auto customTags = EDITOR_CONTEXT(GetCustomTags)();
			if (customTags.empty())
			{
				ImGui::TextDisabled("No custom tags");
			}
			else
			{
				for (size_t i = 0; i < customTags.size(); ++i)
				{
					ImGui::PushID(static_cast<int>(i));
					ImGui::AlignTextToFramePadding();
					ImGui::Text("Tag %zu", i);
					ImGui::SameLine(125.0f * scale);
					const float removeWidth = 24.0f * scale;
					char buffer[256]{};
					std::copy_n(customTags[i].data(), std::min(customTags[i].size(), sizeof(buffer) - 1), buffer);
					ImGui::SetNextItemWidth(std::max(60.0f * scale, ImGui::GetContentRegionAvail().x - removeWidth - 6.0f * scale));
					ImGui::InputText("##TagValue", buffer, sizeof(buffer), ImGuiInputTextFlags_ReadOnly);
					ImGui::SameLine(0.0f, 6.0f * scale);
					if (ImGui::Button("-##RemoveTag", { removeWidth, 0.0f }))
						EDITOR_CONTEXT(RemoveCustomTag)(customTags[i]);
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("Remove custom tag");
					ImGui::PopID();
				}
			}

			char draft[256]{};
			std::copy_n(m_newTagDraft.data(), std::min(m_newTagDraft.size(), sizeof(draft) - 1), draft);
			const float addWidth = 64.0f * scale;
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("New Tag");
			ImGui::SameLine(125.0f * scale);
			ImGui::SetNextItemWidth(std::max(60.0f * scale, ImGui::GetContentRegionAvail().x - addWidth - 6.0f * scale));
			if (m_focusTagInput)
			{
				ImGui::SetKeyboardFocusHere();
				m_focusTagInput = false;
			}
			const bool enterPressed = ImGui::InputTextWithHint("##NewTag", "New tag name", draft, sizeof(draft), ImGuiInputTextFlags_EnterReturnsTrue);
			m_newTagDraft = draft;
			ImGui::SameLine(0.0f, 6.0f * scale);
			const bool canAdd = !m_newTagDraft.empty();
			ImGui::BeginDisabled(!canAdd);
			const bool addClicked = ImGui::Button("+ Add", { addWidth, 0.0f });
			ImGui::EndDisabled();
			if ((enterPressed || addClicked) && EDITOR_CONTEXT(AddCustomTag)(m_newTagDraft))
				m_newTagDraft.clear();
			ImGui::PopID();
		});
	}
}

void OvEditor::Panels::ProjectSettings::FocusTags()
{
	Open();
	Focus();
	if (m_tagsGroup)
	{
		m_tagsGroup->opened = true;
		m_tagsGroup->expandForSearch = true;
	}
	m_focusTagInput = true;
}
