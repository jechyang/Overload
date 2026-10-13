/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <filesystem>
#include <string>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#include <OvDebug/Logger.h>
#include <OvEditor/Core/CSharpProject.h>
#include <OvEditor/Core/EditorActions.h>
#include <OvEditor/Panels/Console.h>
#include <OvEditor/Panels/StatusBar.h>
#include <OvUI/Internal/Converter.h>
#include <OvUI/Styling/Style.h>

namespace
{
	ImVec4 StyleColor(const OvUI::Types::Color& p_color)
	{
		return OvUI::Internal::Converter::ToImVec4(p_color);
	}

	ImVec4 LogColor(OvDebug::ELogLevel p_level)
	{
		const auto& style = OvUI::Styling::Style::Get();
		switch (p_level)
		{
		case OvDebug::ELogLevel::LOG_INFO:    return StyleColor(style.Info);
		case OvDebug::ELogLevel::LOG_WARNING: return StyleColor(style.Warning);
		case OvDebug::ELogLevel::LOG_ERROR:   return StyleColor(style.Danger);
		default:                              return ImGui::GetStyleColorVec4(ImGuiCol_Text);
		}
	}

	ImVec4 ScriptStatusColor(const std::string& p_status)
	{
		const auto& style = OvUI::Styling::Style::Get();
		if (p_status.find("failed") != std::string::npos) return StyleColor(style.Danger);
		if (p_status.find("building") != std::string::npos) return StyleColor(style.Warning);
		return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
	}

	struct Segment
	{
		std::string text;
		ImVec4 color;
	};
}

OvEditor::Panels::StatusBar::StatusBar()
{
	m_logListener = OvDebug::Logger::LogEvent += [this](const OvDebug::LogData& p_log) { m_lastLog = p_log; };
}

OvEditor::Panels::StatusBar::~StatusBar()
{
	OvDebug::Logger::LogEvent -= m_logListener;
}

void OvEditor::Panels::StatusBar::_Draw_Impl()
{
	using EEditorMode = Core::EditorActions::EEditorMode;

	const float scale = OVUI_SCALE;
	const float lineHeight = ImGui::GetTextLineHeight();
	const float height = lineHeight + 8.0f * scale;
	const float padding = 8.0f * scale;
	const float spacing = 18.0f * scale;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { padding, (height - lineHeight) * 0.5f });
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));

	constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav;
	if (ImGui::BeginViewportSideBar(("Status Bar" + GetPanelID()).c_str(), ImGui::GetMainViewport(), ImGuiDir_Down, height, flags))
	{
		const ImVec2 windowPos = ImGui::GetWindowPos(), windowSize = ImGui::GetWindowSize();
		auto* drawList = ImGui::GetWindowDrawList();
		drawList->AddLine(windowPos, { windowPos.x + windowSize.x, windowPos.y }, ImGui::GetColorU32(ImGuiCol_Border));

		// Right-aligned state segments.
		std::vector<Segment> segments;
		const auto& style = OvUI::Styling::Style::Get();

		switch (EDITOR_EXEC(GetCurrentEditorMode()))
		{
		case EEditorMode::PLAY:           segments.push_back({ "Playing", StyleColor(style.Accent) }); break;
		case EEditorMode::PAUSE:          segments.push_back({ "Paused", StyleColor(style.Warning) }); break;
		case EEditorMode::FRAME_BY_FRAME: segments.push_back({ "Stepping", StyleColor(style.Warning) }); break;
		default: break;
		}

		if (const auto& csharp = EDITOR_CONTEXT(csharpProject))
			segments.push_back({ csharp->Status(), ScriptStatusColor(csharp->Status()) });

		const auto source = EDITOR_CONTEXT(sceneManager).GetCurrentSceneSourcePath();
		const bool modified = EDITOR_EXEC(IsSceneModified());
		segments.push_back({
			(source.empty() ? std::string{ "Untitled Scene" } : std::filesystem::path{ source }.stem().string()) + (modified ? "*" : ""),
			ImGui::GetStyleColorVec4(modified ? ImGuiCol_Text : ImGuiCol_TextDisabled)
		});

		float segmentsWidth = 0.0f;
		for (const auto& segment : segments)
			segmentsWidth += ImGui::CalcTextSize(segment.text.c_str()).x + spacing;
		segmentsWidth -= spacing;

		const float rightStart = windowSize.x - padding - segmentsWidth;

		// Latest console message, clipped before the state segments; click to open the Console.
		if (m_lastLog)
		{
			const float available = rightStart - padding - spacing;
			if (available > lineHeight)
			{
				const ImVec2 start = ImGui::GetCursorScreenPos();
				if (ImGui::InvisibleButton("##LastLog", { available, lineHeight }))
				{
					auto& console = EDITOR_PANEL(Console, "Console");
					console.Open();
					console.Focus();
				}

				const bool hovered = ImGui::IsItemHovered();
				const auto message = m_lastLog->message.substr(0, m_lastLog->message.find_first_of("\r\n"));
				const float bullet = lineHeight * 0.5f;

				drawList->AddCircleFilled({ start.x + bullet * 0.5f, start.y + lineHeight * 0.5f }, bullet * 0.35f, ImGui::GetColorU32(LogColor(m_lastLog->logLevel)));
				ImGui::PushStyleColor(ImGuiCol_Text, hovered ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : LogColor(m_lastLog->logLevel));
				ImGui::RenderTextEllipsis(drawList, { start.x + bullet * 1.5f, start.y }, { start.x + available, start.y + lineHeight },
					start.x + available, message.c_str(), nullptr, nullptr);
				ImGui::PopStyleColor();

				if (hovered)
					ImGui::SetTooltip("%s\n\nClick to open the Console", m_lastLog->message.c_str());
			}
		}

		ImGui::SetCursorPos({ rightStart, ImGui::GetStyle().WindowPadding.y });
		for (size_t i = 0; i < segments.size(); ++i)
		{
			if (i > 0) ImGui::SameLine(0.0f, spacing);
			ImGui::TextColored(segments[i].color, "%s", segments[i].text.c_str());
		}
	}
	ImGui::End();

	ImGui::PopStyleColor();
	ImGui::PopStyleVar();
}
