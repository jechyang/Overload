/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <algorithm>
#include <array>
#include <functional>
#include <cctype>
#include <cstddef>
#include <string>
#include <unordered_map>

#include <OvCore/Helpers/GUIDrawer.h>
#include <OvEditor/Core/EditorActions.h>
#include <OvEditor/Core/EditorResources.h>
#include <OvEditor/Panels/Console.h>
#include <OvEditor/Settings/EditorSettings.h>

#include <OvCore/Global/ServiceLocator.h>
#include <OvUI/Internal/Converter.h>
#include <OvUI/Styling/Style.h>
#include <imgui.h>
#include <OvUI/Types/Color.h>
#include <OvUI/Widgets/Buttons/Button.h>
#include <OvUI/Widgets/InputFields/InputText.h>
#include <OvUI/Widgets/Layout/Group.h>
#include <OvUI/Widgets/Layout/TextLogs.h>
#include <OvUI/Widgets/Selection/CheckBox.h>
#include <OvUI/Widgets/Visual/Separator.h>

namespace
{
	bool ContainsCaseInsensitive(const std::string& p_str, const std::string& p_search)
	{
		if (p_search.empty())
			return true;

		return std::search(
			p_str.begin(), p_str.end(),
			p_search.begin(), p_search.end(),
			[](char p_left, char p_right)
			{
				return std::tolower(static_cast<unsigned char>(p_left))
					== std::tolower(static_cast<unsigned char>(p_right));
			}
		) != p_str.end();
	}

	std::string FormatLogDate(const std::string& p_date)
	{
		std::string result = "[";
		bool isTimePart = false;

		for (char character : p_date)
		{
			if (isTimePart)
				result.push_back(character == '-' ? ':' : character);

			if (character == '_')
				isTimePart = true;
		}

		result += ']';
		return result;
	}

	OvUI::Types::ColorEffector GetLogLevelColor(OvDebug::ELogLevel p_logLevel)
	{
		switch (p_logLevel)
		{
		case OvDebug::ELogLevel::LOG_DEFAULT: return OVUI_STYLE(Text);
		case OvDebug::ELogLevel::LOG_INFO:    return OVUI_STYLE(Info);
		case OvDebug::ELogLevel::LOG_WARNING: return OVUI_STYLE(Warning);
		case OvDebug::ELogLevel::LOG_ERROR:   return OVUI_STYLE(Danger);
		}

		return OVUI_STYLE(Text);
	}

	class ConsoleToolbar final : public OvUI::Widgets::AWidget
	{
	public:
		explicit ConsoleToolbar(std::function<void()> p_draw) : m_draw(std::move(p_draw)) {}
	private:
		void _Draw_Impl() override { m_draw(); }
		std::function<void()> m_draw;
	};
}

using namespace OvUI::Widgets;

OvEditor::Panels::Console::Console
(
	const std::string& p_title,
	bool p_opened,
	const OvUI::Settings::PanelWindowSettings& p_windowSettings
) :
	PanelWindow(p_title, p_opened, p_windowSettings)
{
	scrollable = false;

	CreateWidget<ConsoleToolbar>([this] { DrawToolbar(); });
	m_logsWidget = &CreateWidget<Layout::TextLogs>();
	m_logsWidget->SetID("console-log-list");
	m_logsWidget->emptyText = "No logs yet.";
	m_defaultLogEmptyText = m_logsWidget->emptyText;

	EDITOR_EVENT(PlayEvent) += [this]
	{
		ClearOnPlay();
	};
	m_logListener = OvDebug::Logger::LogEvent += [this](const OvDebug::LogData& p_logData)
	{
		OnLogIntercepted(p_logData);
	};
}

OvEditor::Panels::Console::~Console()
{
	OvDebug::Logger::LogEvent -= m_logListener;
}

void OvEditor::Panels::Console::DrawToolbar()
{
	const float scale = OVUI_SCALE;
	const float spacing = 2.0f * scale;
	const auto& style = OvUI::Styling::Style::Get();

	ImGui::PushID("ConsoleToolbar");
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

	if (ImGui::Button("Clear")) Clear();

	ImGui::SameLine(0.0f, 8.0f * scale);
	if (DrawToggle("Collapse", m_collapseIdenticalLogs, "Merge identical messages and show a count"))
		SetCollapseIdenticalLogs(!m_collapseIdenticalLogs);
	ImGui::SameLine(0.0f, spacing);
	if (DrawToggle("Clear on Play", m_clearOnPlay, "Clear the Console when entering Play mode"))
		m_clearOnPlay = !m_clearOnPlay;
	ImGui::SameLine(0.0f, spacing);
	if (DrawToggle("Error Pause", m_errorPause, "Pause Play mode when an error is logged"))
		m_errorPause = !m_errorPause;

	// Right side: search, then one toggle per severity with its count (Unity order).
	size_t counts[3] = { 0, 0, 0 };
	for (const auto& log : m_logs)
	{
		switch (log.data.logLevel)
		{
		case OvDebug::ELogLevel::LOG_WARNING: ++counts[1]; break;
		case OvDebug::ELogLevel::LOG_ERROR:   ++counts[2]; break;
		default:                              ++counts[0]; break;
		}
	}

	const auto countLabel = [](size_t p_count) { return p_count > 999 ? std::string{ "999+" } : std::to_string(p_count); };
	const float iconWidth = ImGui::GetFontSize() + 4.0f * scale;
	const auto levelWidth = [&](size_t p_count) { return iconWidth + ImGui::CalcTextSize(countLabel(p_count).c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f; };
	const float levelsWidth = levelWidth(counts[0]) + levelWidth(counts[1]) + levelWidth(counts[2]) + spacing * 2.0f;

	ImGui::SameLine(0.0f, 8.0f * scale);
	const float searchWidth = std::clamp(ImGui::GetContentRegionAvail().x - levelsWidth - 8.0f * scale, 60.0f * scale, 240.0f * scale);
	ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - levelsWidth - searchWidth - 8.0f * scale));
	std::array<char, 256> search{};
	std::copy_n(m_search.data(), std::min(m_search.size(), search.size() - 1), search.data());
	ImGui::SetNextItemWidth(searchWidth);
	if (ImGui::InputTextWithHint("##Search", "Search", search.data(), search.size()))
	{
		m_search = search.data();
		FilterLogs();
	}

	const auto level = [&](const char* p_id, size_t p_count, bool p_enabled, const ImVec4& p_color, int p_shape, const char* p_tooltip)
	{
		ImGui::SameLine(0.0f, spacing);
		const std::string label = countLabel(p_count);
		ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, { 1.0f, 0.5f });
		const bool clicked = DrawToggle((label + p_id).c_str(), p_enabled, p_tooltip, levelWidth(p_count));
		ImGui::PopStyleVar();

		// Severity glyph: info circle, warning triangle, error disc.
		const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
		const float size = ImGui::GetFontSize() * 0.75f;
		const ImVec2 center{ min.x + ImGui::GetStyle().FramePadding.x + size * 0.5f, (min.y + max.y) * 0.5f };
		auto* drawList = ImGui::GetWindowDrawList();
		const ImU32 color = ImGui::GetColorU32(p_enabled ? p_color : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		if (p_shape == 0) drawList->AddCircle(center, size * 0.45f, color, 0, 1.5f * scale);
		else if (p_shape == 1) drawList->AddTriangleFilled({ center.x, center.y - size * 0.5f }, { center.x + size * 0.55f, center.y + size * 0.45f }, { center.x - size * 0.55f, center.y + size * 0.45f }, color);
		else drawList->AddCircleFilled(center, size * 0.5f, color);
		return clicked;
	};

	if (level("##Info", counts[0], m_showInfoLog || m_showDefaultLog, ImGui::GetStyleColorVec4(ImGuiCol_Text), 0, "Show messages"))
	{
		const bool show = !(m_showInfoLog || m_showDefaultLog);
		m_showDefaultLog = show;
		SetShowInfoLogs(show);
	}
	if (level("##Warning", counts[1], m_showWarningLog, OvUI::Internal::Converter::ToImVec4(style.Warning), 1, "Show warnings"))
		SetShowWarningLogs(!m_showWarningLog);
	if (level("##Error", counts[2], m_showErrorLog, OvUI::Internal::Converter::ToImVec4(style.Danger), 2, "Show errors"))
		SetShowErrorLogs(!m_showErrorLog);

	ImGui::PopStyleVar();
	ImGui::PopID();
}

bool OvEditor::Panels::Console::DrawToggle(const char* p_label, bool p_active, const char* p_tooltip, float p_width)
{
	if (p_active)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
	}
	else
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f });
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
	}
	const bool clicked = ImGui::Button(p_label, { p_width, 0.0f });
	ImGui::PopStyleColor(2);
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", p_tooltip);
	return clicked;
}

void OvEditor::Panels::Console::OnLogIntercepted(const OvDebug::LogData& p_logData)
{
	// Pause at the next frame boundary rather than inside whatever code is logging.
	if (m_errorPause && p_logData.logLevel == OvDebug::ELogLevel::LOG_ERROR)
	{
		if (auto* actions = OvCore::Global::ServiceLocator::TryGet<Core::EditorActions>();
			actions && actions->GetCurrentEditorMode() == Core::EditorActions::EEditorMode::PLAY)
		{
			actions->DelayAction([]
			{
				if (EDITOR_EXEC(GetCurrentEditorMode()) == Core::EditorActions::EEditorMode::PLAY)
					EDITOR_EXEC(PauseGame());
			}, 0);
		}
	}

	m_logs.push_back({ p_logData });
	m_logsWidget->RequestScrollToBottom();
	TruncateLogs();
}

void OvEditor::Panels::Console::ClearOnPlay()
{
	if (m_clearOnPlay)
		Clear();
}

void OvEditor::Panels::Console::Clear()
{
	m_logs.clear();
	m_visibleLogs.clear();
	RefreshDisplayedLogs();
}

void OvEditor::Panels::Console::FilterLogs()
{
	RebuildVisibleLogs();
	RefreshDisplayedLogs();
}

bool OvEditor::Panels::Console::IsAllowedByFilter(OvDebug::ELogLevel p_logLevel) const
{
	switch (p_logLevel)
	{
	case OvDebug::ELogLevel::LOG_DEFAULT: return m_showDefaultLog;
	case OvDebug::ELogLevel::LOG_INFO: return m_showInfoLog;
	case OvDebug::ELogLevel::LOG_WARNING: return m_showWarningLog;
	case OvDebug::ELogLevel::LOG_ERROR: return m_showErrorLog;
	}

	return false;
}

void OvEditor::Panels::Console::TruncateLogs()
{
	const auto maxLogs = static_cast<size_t>(Settings::EditorSettings::ConsoleMaxLogs.Get());
	if (m_logs.size() > maxLogs)
	{
		const auto excessLogs = m_logs.size() - maxLogs;
		m_logs.erase(m_logs.begin(), m_logs.begin() + static_cast<std::ptrdiff_t>(excessLogs));
	}

	RebuildVisibleLogs();
	RefreshDisplayedLogs();
}

void OvEditor::Panels::Console::SetShowDefaultLogs(bool p_value)
{
	m_showDefaultLog = p_value;
	FilterLogs();
}

void OvEditor::Panels::Console::SetShowInfoLogs(bool p_value)
{
	m_showInfoLog = p_value;
	FilterLogs();
}

void OvEditor::Panels::Console::SetShowWarningLogs(bool p_value)
{
	m_showWarningLog = p_value;
	FilterLogs();
}

void OvEditor::Panels::Console::SetShowErrorLogs(bool p_value)
{
	m_showErrorLog = p_value;
	FilterLogs();
}

void OvEditor::Panels::Console::SetCollapseIdenticalLogs(bool p_value)
{
	m_collapseIdenticalLogs = p_value;
	FilterLogs();
}

void OvEditor::Panels::Console::RebuildVisibleLogs()
{
	m_visibleLogs.clear();

	std::vector<size_t> filteredIndices;
	filteredIndices.reserve(m_logs.size());

	for (size_t index = 0; index < m_logs.size(); ++index)
	{
		const auto& logEntry = m_logs[index];
		if (IsAllowedByFilter(logEntry.data.logLevel) && MatchesSearch(logEntry))
			filteredIndices.push_back(index);
	}

	if (m_collapseIdenticalLogs)
	{
		std::unordered_map<std::string, size_t> collapsedByMessage;
		m_visibleLogs.reserve(filteredIndices.size());

		for (const auto index : filteredIndices)
		{
			const auto& logEntry = m_logs[index];
			const auto [iterator, inserted] = collapsedByMessage.emplace(logEntry.data.message, m_visibleLogs.size());
			if (inserted)
			{
				m_visibleLogs.push_back({ index, 1 });
			}
			else
			{
				auto& visibleEntry = m_visibleLogs[iterator->second];
				visibleEntry.latestRawIndex = index;
				++visibleEntry.count;
			}
		}

		std::ranges::sort(m_visibleLogs, {}, &VisibleLogEntry::latestRawIndex);
	}
	else
	{
		m_visibleLogs.reserve(filteredIndices.size());
		for (const auto index : filteredIndices)
			m_visibleLogs.push_back({ index, 1 });
	}
}

void OvEditor::Panels::Console::RefreshDisplayedLogs()
{
	m_logsWidget->entries.clear();
	m_logsWidget->entries.reserve(m_visibleLogs.size());
	m_logsWidget->emptyText = m_logs.empty() ? m_defaultLogEmptyText : "No logs match the current filters.";

	for (const auto& visibleEntry : m_visibleLogs)
	{
		const auto& logEntry = m_logs[visibleEntry.latestRawIndex];
		m_logsWidget->entries.push_back({
			FormatLogDate(logEntry.data.date),
			logEntry.data.message,
			GetLogLevelColor(logEntry.data.logLevel),
			visibleEntry.count
		});
	}
}

bool OvEditor::Panels::Console::MatchesSearch(const LogEntry& p_logEntry) const
{
	return ContainsCaseInsensitive(p_logEntry.data.message, m_search);
}
