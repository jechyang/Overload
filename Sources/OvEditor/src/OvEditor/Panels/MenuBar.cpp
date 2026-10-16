/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string_view>

#include <OvDebug/Logger.h>
#include <OvTools/Utils/SystemCalls.h>

#include <OvUI/Widgets/Visual/Separator.h>
#include <OvUI/Widgets/Sliders/SliderInt.h>
#include <OvUI/Widgets/Sliders/SliderFloat.h>
#include <OvUI/Widgets/Drags/DragFloat.h>
#include <OvUI/Widgets/InputFields/InputText.h>
#include <OvUI/Widgets/Selection/ColorEdit.h>
#include <OvUI/Widgets/Selection/ComboBox.h>
#include <OvUI/Widgets/Texts/Text.h>

#include "OvEditor/Core/EditorActions.h"
#include "OvEditor/Panels/AssetBrowser.h"
#include "OvEditor/Panels/AssetView.h"
#include "OvEditor/Panels/Console.h"
#include "OvEditor/Panels/Inspector.h"
#include "OvEditor/Panels/MenuBar.h"
#include "OvEditor/Panels/SceneView.h"
#include "OvEditor/Settings/EditorSettings.h"
#include "OvEditor/Utils/ActorCreationMenu.h"

using namespace OvUI::Panels;
using namespace OvUI::Widgets;
using namespace OvUI::Widgets::Menu;

namespace
{
	// Scene-replacing and dialog-opening actions run after the current frame is submitted.
	template<typename Action>
	auto Deferred(Action p_action)
	{
		return [p_action] { EDITOR_EXEC(DelayAction(p_action, 0)); };
	}

	bool IsDown(OvWindowing::Inputs::InputManager& p_input, OvWindowing::Inputs::EKey p_left, OvWindowing::Inputs::EKey p_right)
	{
		using OvWindowing::Inputs::EKeyState;
		return p_input.GetKeyState(p_left) == EKeyState::KEY_DOWN || p_input.GetKeyState(p_right) == EKeyState::KEY_DOWN;
	}
}

OvEditor::Panels::MenuBar::MenuBar()
{
	CreateFileMenu();
	CreateEditMenu();
	CreateAssetsMenu();
	CreateGameObjectMenu();
	CreateComponentMenu();
	CreateWindowMenu();
	CreateHelpMenu();
}

void OvEditor::Panels::MenuBar::HandleShortcuts(float p_deltaTime)
{
	using EEditorMode = Core::EditorActions::EEditorMode;
	using OvWindowing::Inputs::EKey;

	const auto mode = EDITOR_EXEC(GetCurrentEditorMode());
	m_undoItem->name = "Undo " + EDITOR_EXEC(UndoLabel());
	m_redoItem->name = "Redo " + EDITOR_EXEC(RedoLabel());
	m_undoItem->disabled = !EDITOR_EXEC(CanUndo());
	m_redoItem->disabled = !EDITOR_EXEC(CanRedo());
	m_saveItem->disabled = m_saveAsItem->disabled = mode != EEditorMode::EDIT;
	m_playItem->checked = mode != EEditorMode::EDIT;
	m_pauseItem->checked = mode == EEditorMode::PAUSE;
	m_pauseItem->disabled = m_stepItem->disabled = mode == EEditorMode::EDIT;
	if (m_selectBackItem && m_selectForwardItem)
	{
		const auto& inspector = EDITOR_PANEL(Inspector, "Inspector");
		m_selectBackItem->disabled = !inspector.CanSelectBack();
		m_selectForwardItem->disabled = !inspector.CanSelectForward();
	}
	UpdateToggleableItems();

	auto& input = *EDITOR_CONTEXT(inputManager);
	if (!IsDown(input, EKey::KEY_LEFT_CONTROL, EKey::KEY_RIGHT_CONTROL))
		return;

	const bool shift = IsDown(input, EKey::KEY_LEFT_SHIFT, EKey::KEY_RIGHT_SHIFT);
	const bool alt = IsDown(input, EKey::KEY_LEFT_ALT, EKey::KEY_RIGHT_ALT);

	if (!shift && !alt && input.IsKeyPressed(EKey::KEY_N)) Deferred([] { EDITOR_EXEC(NewScene()); })();
	if (input.IsKeyPressed(EKey::KEY_O)) Deferred([] { EDITOR_EXEC(OpenScene()); })();
	if (input.IsKeyPressed(EKey::KEY_S))
	{
		if (shift) Deferred([] { EDITOR_EXEC(SaveAs()); })();
		else Deferred([] { EDITOR_EXEC(SaveSceneChanges()); })();
	}
	if (input.IsKeyPressed(EKey::KEY_B)) Deferred([] { EDITOR_EXEC(Build(true, false)); })();
	if (input.IsKeyPressed(EKey::KEY_P))
	{
		if (alt) Deferred([] { EDITOR_EXEC(NextFrame()); })();
		else if (shift) Deferred([] { EDITOR_EXEC(TogglePause()); })();
		else Deferred([] { EDITOR_EXEC(TogglePlay()); })();
	}
}

void OvEditor::Panels::MenuBar::CreateFileMenu()
{
	auto& fileMenu = CreateWidget<MenuList>("File");
	fileMenu.CreateWidget<MenuItem>("New Scene", "Ctrl+N").ClickedEvent += Deferred([] { EDITOR_EXEC(NewScene()); });
	fileMenu.CreateWidget<MenuItem>("Open Scene...", "Ctrl+O").ClickedEvent += Deferred([] { EDITOR_EXEC(OpenScene()); });
	fileMenu.CreateWidget<Visual::Separator>();
	m_saveItem = &fileMenu.CreateWidget<MenuItem>("Save", "Ctrl+S");
	m_saveItem->ClickedEvent += Deferred([] { EDITOR_EXEC(SaveSceneChanges()); });
	m_saveAsItem = &fileMenu.CreateWidget<MenuItem>("Save As...", "Ctrl+Shift+S");
	m_saveAsItem->ClickedEvent += Deferred([] { EDITOR_EXEC(SaveAs()); });
	fileMenu.CreateWidget<Visual::Separator>();
	fileMenu.CreateWidget<MenuItem>("Build...").ClickedEvent += Deferred([] { EDITOR_EXEC(Build(false, false)); });
	fileMenu.CreateWidget<MenuItem>("Build And Run", "Ctrl+B").ClickedEvent += Deferred([] { EDITOR_EXEC(Build(true, false)); });
	fileMenu.CreateWidget<MenuItem>("Temporary Build And Run").ClickedEvent += Deferred([] { EDITOR_EXEC(Build(true, true)); });
	fileMenu.CreateWidget<Visual::Separator>();
	fileMenu.CreateWidget<MenuItem>("Exit", "Alt+F4").ClickedEvent += [] { EDITOR_CONTEXT(window)->SetShouldClose(true); };
}

void OvEditor::Panels::MenuBar::CreateEditMenu()
{
	auto& menu = CreateWidget<MenuList>("Edit");
	m_undoItem = &menu.CreateWidget<MenuItem>("Undo", "Ctrl+Z");
	m_redoItem = &menu.CreateWidget<MenuItem>("Redo", "Ctrl+Y");
	m_undoItem->ClickedEvent += Deferred([] { EDITOR_EXEC(Undo()); });
	m_redoItem->ClickedEvent += Deferred([] { EDITOR_EXEC(Redo()); });
	menu.CreateWidget<Visual::Separator>();

	menu.CreateWidget<MenuItem>("Select All", "Ctrl+A").ClickedEvent += Deferred([] {
		if (auto* scene = EDITOR_CONTEXT(sceneManager).GetCurrentScene()) EDITOR_EXEC(SetSelection(scene->GetActors()));
	});
	menu.CreateWidget<MenuItem>("Deselect All").ClickedEvent += Deferred([] { EDITOR_EXEC(UnselectActor()); });
	m_selectBackItem = &menu.CreateWidget<MenuItem>("Previous Selection");
	m_selectBackItem->ClickedEvent += Deferred([] { EDITOR_PANEL(Inspector, "Inspector").SelectBack(); });
	m_selectForwardItem = &menu.CreateWidget<MenuItem>("Next Selection");
	m_selectForwardItem->ClickedEvent += Deferred([] { EDITOR_PANEL(Inspector, "Inspector").SelectForward(); });
	menu.CreateWidget<Visual::Separator>();

	menu.CreateWidget<MenuItem>("Copy", "Ctrl+C").ClickedEvent += [] {
		if (EDITOR_EXEC(IsAnyActorSelected())) EDITOR_EXEC(CopyActor(EDITOR_EXEC(GetSelectedActor())));
	};
	menu.CreateWidget<MenuItem>("Paste", "Ctrl+V").ClickedEvent += Deferred([] {
		EDITOR_EXEC(PasteActor(EDITOR_EXEC(IsAnyActorSelected()) ? &EDITOR_EXEC(GetSelectedActor()) : nullptr));
	});
	menu.CreateWidget<MenuItem>("Duplicate", "Ctrl+D").ClickedEvent += Deferred([] { EDITOR_EXEC(DuplicateSelectedActors()); });
	menu.CreateWidget<MenuItem>("Delete", "Delete").ClickedEvent += Deferred([] { EDITOR_EXEC(DestroySelectedActors()); });
	menu.CreateWidget<Visual::Separator>();

	menu.CreateWidget<MenuItem>("Frame Selected", "F").ClickedEvent += Deferred([] {
		if (EDITOR_EXEC(IsAnyActorSelected())) EDITOR_PANEL(SceneView, "Scene View").FrameSelection();
	});
	menu.CreateWidget<Visual::Separator>();

	m_playItem = &menu.CreateWidget<MenuItem>("Play", "Ctrl+P", true);
	m_playItem->ClickedEvent += Deferred([] { EDITOR_EXEC(TogglePlay()); });
	m_pauseItem = &menu.CreateWidget<MenuItem>("Pause", "Ctrl+Shift+P", true);
	m_pauseItem->ClickedEvent += Deferred([] { EDITOR_EXEC(TogglePause()); });
	m_stepItem = &menu.CreateWidget<MenuItem>("Step", "Ctrl+Alt+P");
	m_stepItem->ClickedEvent += Deferred([] { EDITOR_EXEC(NextFrame()); });
	menu.CreateWidget<Visual::Separator>();

	menu.CreateWidget<MenuItem>("Project Settings...").ClickedEvent += [] {
		auto& panel = EDITOR_PANEL(OvUI::Panels::PanelWindow, "Project Settings");
		panel.Open();
		panel.Focus();
	};
	m_settingsMenu = &menu.CreateWidget<MenuList>("Preferences");
}

void OvEditor::Panels::MenuBar::CreateAssetsMenu()
{
	auto& menu = CreateWidget<MenuList>("Assets");
	menu.CreateWidget<MenuItem>("Import New Asset...").ClickedEvent += Deferred([] {
		EDITOR_EXEC(ImportAsset(EDITOR_CONTEXT(projectAssetsPath).string()));
	});
	menu.CreateWidget<MenuItem>("Refresh").ClickedEvent += [] { EDITOR_PANEL(AssetBrowser, "Asset Browser").Refresh(); };
	menu.CreateWidget<MenuItem>("Show in Explorer").ClickedEvent += [] {
		OvTools::Utils::SystemCalls::ShowInExplorer(EDITOR_CONTEXT(projectAssetsPath).string());
	};
	menu.CreateWidget<Visual::Separator>();
	menu.CreateWidget<MenuItem>("Open Code Editor").ClickedEvent += [] { EDITOR_EXEC(OpenInCodeEditor(EDITOR_CONTEXT(projectFolder))); };
	menu.CreateWidget<MenuItem>("Regenerate Scripting Project Files").ClickedEvent += EDITOR_BIND(RegenerateScriptingProjectFiles);
	menu.CreateWidget<MenuItem>("Reload Scripts").ClickedEvent += EDITOR_BIND(RefreshScripts);
	menu.CreateWidget<Visual::Separator>();
	menu.CreateWidget<MenuItem>("Compile Shaders").ClickedEvent += EDITOR_BIND(CompileShaders);
	menu.CreateWidget<MenuItem>("Save Materials").ClickedEvent += EDITOR_BIND(SaveMaterials);
}

void OvEditor::Panels::MenuBar::CreateGameObjectMenu()
{
	auto& menu = CreateWidget<MenuList>("GameObject");
	Utils::ActorCreationMenu::GenerateActorCreationMenu(menu);

	auto& emptyChild = menu.CreateWidget<MenuItem>("Create Empty Child", "Alt+Shift+N");
	emptyChild.ClickedEvent += [] {
		if (EDITOR_EXEC(IsAnyActorSelected())) EDITOR_EXEC(CreateEmptyActor(true, &EDITOR_EXEC(GetSelectedActor())));
	};
	menu.CreateWidget<Visual::Separator>();
	menu.CreateWidget<MenuItem>("Frame Selected", "F").ClickedEvent += Deferred([] {
		if (EDITOR_EXEC(IsAnyActorSelected())) EDITOR_PANEL(SceneView, "Scene View").FrameSelection();
	});
}

void OvEditor::Panels::MenuBar::CreateComponentMenu()
{
	auto& menu = CreateWidget<MenuList>("Component");
	menu.CreateWidget<MenuItem>("Add...").ClickedEvent += Deferred([] {
		if (!EDITOR_EXEC(IsAnyActorSelected()))
		{
			OVLOG_WARNING("Select an entity to add a component to.");
			return;
		}

		auto& inspector = EDITOR_PANEL(Inspector, "Inspector");
		inspector.Open();
		inspector.OpenAddComponentPicker();
	});
}

void OvEditor::Panels::MenuBar::CreateWindowMenu()
{
	m_windowMenu = &CreateWidget<MenuList>("Window");
}

void OvEditor::Panels::MenuBar::InitializeWindowMenu()
{
	auto& layouts = m_windowMenu->CreateWidget<MenuList>("Layouts");
	layouts.CreateWidget<MenuItem>("Default").ClickedEvent += [] { EDITOR_EXEC(ApplyLayout(Core::EEditorLayout::DEFAULT)); };
	layouts.CreateWidget<MenuItem>("2 by 3").ClickedEvent += [] { EDITOR_EXEC(ApplyLayout(Core::EEditorLayout::TWO_BY_THREE)); };
	m_windowMenu->CreateWidget<Visual::Separator>();

	const std::pair<const char*, std::vector<std::string>> groups[] = {
		{ "General", { "Scene View", "Game View", "Inspector", "Hierarchy", "Asset Browser", "Console", "Toolbar" } },
		{ "Rendering", { "Material Editor", "Asset View", "Texture Debugger", "Frame Graph" } },
		{ "Analysis", { "Frame Info", "Hardware Info" } },
		{ "Scripting", { "C# Scripts" } }
	};

	std::vector<std::string> listed;
	for (const auto& [group, panels] : groups)
	{
		auto& menu = m_windowMenu->CreateWidget<MenuList>(group);
		for (const auto& id : panels)
		{
			AddPanelToggle(menu, id);
			listed.push_back(id);
		}

// No Tracy profiler (front-end) on non-Windows platforms at the moment.
// You'll need to build it yourself if you want to use it on other platforms.
// https://github.com/Overload-Technologies/Overload/issues/614
#ifdef _WIN32
		if (std::string_view{ group } == "Analysis")
		{
			menu.CreateWidget<Visual::Separator>();
			menu.CreateWidget<MenuItem>("Profiler").ClickedEvent += EDITOR_BIND(OpenProfiler);
		}
#endif
	}

	m_windowMenu->CreateWidget<Visual::Separator>();
	for (const auto& [id, panel] : m_panels)
		if (std::find(listed.begin(), listed.end(), id) == listed.end())
			AddPanelToggle(*m_windowMenu, id);
}

void OvEditor::Panels::MenuBar::AddPanelToggle(MenuList& p_menu, const std::string& p_panelID)
{
	const auto found = m_panels.find(p_panelID);
	if (found == m_panels.end())
		return;

	auto& panel = found->second.get();
	auto& item = p_menu.CreateWidget<MenuItem>(panel.name, "", true, panel.IsOpened());
	item.ValueChangedEvent += [&panel](bool p_opened)
	{
		panel.SetOpened(p_opened);
		if (p_opened) panel.Focus();
	};
	m_panelToggles.emplace_back(&item, &panel);
}

void OvEditor::Panels::MenuBar::InitializeSettingsMenu()
{
	InitializeWindowMenu();

	auto& appearanceButton = m_settingsMenu->CreateWidget<MenuList>("Appearance");
	appearanceButton.CreateWidget<Texts::Text>("Some themes may require a restart");

	auto& colorTheme = appearanceButton.CreateWidget<Selection::ComboBox>(static_cast<int>(Settings::EditorSettings::ColorTheme.Get()));
	colorTheme.choices = {
		{ static_cast<int>(OvUI::Styling::EStyle::UNITY_DARK), "Unity Dark (Default)"},
		{ static_cast<int>(OvUI::Styling::EStyle::DEFAULT_DARK), "Alternative Dark"},
		{ static_cast<int>(OvUI::Styling::EStyle::EVEN_DARKER), "Even Darker"},
		{ static_cast<int>(OvUI::Styling::EStyle::DUNE_DARK), "Dune Dark"},
		{ static_cast<int>(OvUI::Styling::EStyle::IM_CLASSIC_STYLE), "ImGui Classic"},
		{ static_cast<int>(OvUI::Styling::EStyle::IM_DARK_STYLE), "ImGui Dark"},
		{ static_cast<int>(OvUI::Styling::EStyle::IM_LIGHT_STYLE), "ImGui Light"}
	};
	colorTheme.ValueChangedEvent += [this](int p_value)
	{
		Settings::EditorSettings::ColorTheme = p_value;
		EDITOR_CONTEXT(uiManager)->ApplyStyle(static_cast<OvUI::Styling::EStyle>(p_value));
	};

	auto& uiScale = appearanceButton.CreateWidget<Selection::ComboBox>(static_cast<int>(Settings::EditorSettings::UIScale.Get()));
	uiScale.choices = {
		{ 0, "[BETA] Automatic (DPI Aware)"},
		{ 100, "100%"},
		{ 150, "150%"},
		{ 200, "200%"},
	};
	uiScale.ValueChangedEvent += [this](int p_value) {
		Settings::EditorSettings::UIScale = p_value;
		EDITOR_CONTEXT(uiManager)->SetScale(p_value == 0 ? std::nullopt : std::make_optional(p_value / 100.0f));
	};

	// Unity-style play mode tint: the whole editor is tinted while the game runs.
	appearanceButton.CreateWidget<Visual::Separator>();
	auto& tintEnabled = appearanceButton.CreateWidget<MenuItem>("Play Mode Tint", "", true, Settings::EditorSettings::PlayModeTintEnabled.Get());
	tintEnabled.ValueChangedEvent += [](bool p_value)
	{
		Settings::EditorSettings::PlayModeTintEnabled = p_value;
		EDITOR_EXEC(UpdatePlayModeTint());
	};
	auto& tintColor = appearanceButton.CreateWidget<Selection::ColorEdit>(false, OvUI::Types::Color{
		Settings::EditorSettings::PlayModeTintR.Get(),
		Settings::EditorSettings::PlayModeTintG.Get(),
		Settings::EditorSettings::PlayModeTintB.Get()
	});
	tintColor.ColorChangedEvent += [](const auto& p_color)
	{
		Settings::EditorSettings::PlayModeTintR = p_color.r;
		Settings::EditorSettings::PlayModeTintG = p_color.g;
		Settings::EditorSettings::PlayModeTintB = p_color.b;
		EDITOR_EXEC(UpdatePlayModeTint());
	};

	m_settingsMenu->CreateWidget<MenuItem>("Spawn actors at origin", "", true, true).ValueChangedEvent += EDITOR_BIND(SetActorSpawnAtOrigin, std::placeholders::_1);
	m_settingsMenu->CreateWidget<MenuItem>("Vertical Synchronization", "", true, true).ValueChangedEvent += [this](bool p_value) { EDITOR_CONTEXT(device)->SetVsync(p_value); };
	auto& cameraSpeedMenu = m_settingsMenu->CreateWidget<MenuList>("Camera Speed");
	cameraSpeedMenu.CreateWidget<OvUI::Widgets::Sliders::SliderInt>(1, 50, 15, OvUI::Widgets::Sliders::ESliderOrientation::HORIZONTAL, "Scene View").ValueChangedEvent += EDITOR_BIND(SetSceneViewCameraSpeed, std::placeholders::_1);
	cameraSpeedMenu.CreateWidget<OvUI::Widgets::Sliders::SliderInt>(1, 50, 15, OvUI::Widgets::Sliders::ESliderOrientation::HORIZONTAL, "Asset View").ValueChangedEvent += EDITOR_BIND(SetAssetViewCameraSpeed, std::placeholders::_1);
	auto& cameraPositionMenu = m_settingsMenu->CreateWidget<MenuList>("Reset Camera");
	cameraPositionMenu.CreateWidget<MenuItem>("Scene View").ClickedEvent += EDITOR_BIND(ResetSceneViewCameraPosition);
	cameraPositionMenu.CreateWidget<MenuItem>("Asset View").ClickedEvent += EDITOR_BIND(ResetAssetViewCameraPosition);

	auto& sceneView = EDITOR_PANEL(Panels::SceneView, "Scene View");
	auto& assetView = EDITOR_PANEL(Panels::AssetView, "Asset View");

	auto& viewColors = m_settingsMenu->CreateWidget<MenuList>("View Colors");
	auto& sceneViewBackground = viewColors.CreateWidget<MenuList>("Scene View Background");
	auto& sceneViewBackgroundPicker = sceneViewBackground.CreateWidget<Selection::ColorEdit>(false, sceneView.GetCamera()->GetClearColor());
	sceneViewBackgroundPicker.ColorChangedEvent += [&](const auto& color)
	{
		sceneView.GetCamera()->SetClearColor({ color.r, color.g, color.b });
	};
	sceneViewBackground.CreateWidget<MenuItem>("Reset").ClickedEvent += [&]
	{
		sceneView.ResetClearColor();
		sceneViewBackgroundPicker.color = sceneView.GetCamera()->GetClearColor();
	};
	auto& sceneViewGrid = viewColors.CreateWidget<MenuList>("Scene View Grid");

	auto& sceneViewGridPicker = sceneViewGrid.CreateWidget<Selection::ColorEdit>(false, sceneView.GetGridColor());
	sceneViewGridPicker.ColorChangedEvent += [this](const auto& color)
	{
		EDITOR_PANEL(Panels::SceneView, "Scene View").SetGridColor({ color.r, color.g, color.b });
	};
	sceneViewGrid.CreateWidget<MenuItem>("Reset").ClickedEvent += [&]
	{
		sceneView.ResetGridColor();
		sceneViewGridPicker.color = sceneView.GetGridColor();
	};

	auto& assetViewBackground = viewColors.CreateWidget<MenuList>("Asset View Background");
	auto& assetViewBackgroundPicker = assetViewBackground.CreateWidget<Selection::ColorEdit>(false, assetView.GetCamera()->GetClearColor());
	assetViewBackgroundPicker.ColorChangedEvent += [&](const auto& color)
	{
		assetView.GetCamera()->SetClearColor({ color.r, color.g, color.b });
	};
	assetViewBackground.CreateWidget<MenuItem>("Reset").ClickedEvent += [&]
	{
		assetView.ResetClearColor();
		assetViewBackgroundPicker.color = assetView.GetCamera()->GetClearColor();
	};

	auto& assetViewGrid = viewColors.CreateWidget<MenuList>("Asset View Grid");
	auto& assetViewGridPicker = assetViewGrid.CreateWidget<Selection::ColorEdit>(false, assetView.GetGridColor());
	assetViewGridPicker.ColorChangedEvent += [&](const auto& color)
	{
		assetView.SetGridColor({ color.r, color.g, color.b });
	};
	assetViewGrid.CreateWidget<MenuItem>("Reset").ClickedEvent += [&]
	{
		assetView.ResetGridColor();
		assetViewGridPicker.color = assetView.GetGridColor();
	};

	auto& sceneViewBillboardScaleMenu = m_settingsMenu->CreateWidget<MenuList>("Gizmos and Scene Icons");
	sceneViewBillboardScaleMenu.CreateWidget<Drags::DragFloat>(40.0f, 180.0f, Settings::EditorSettings::GizmoSize, 1.0f, "Handle Size (px)").ValueChangedEvent += [](float value) { Settings::EditorSettings::GizmoSize = value; };
	auto& lightBillboardScaleSlider = sceneViewBillboardScaleMenu.CreateWidget<Sliders::SliderInt>(0, 100, static_cast<int>(Settings::EditorSettings::LightBillboardScale * 100.0f), OvUI::Widgets::Sliders::ESliderOrientation::HORIZONTAL, "Lights");
	lightBillboardScaleSlider.ValueChangedEvent += [this](int p_value) { Settings::EditorSettings::LightBillboardScale = p_value / 100.0f; };
	lightBillboardScaleSlider.format = "%d %%";
	auto& reflectionProbesScaleSlider = sceneViewBillboardScaleMenu.CreateWidget<Sliders::SliderInt>(0, 100, static_cast<int>(Settings::EditorSettings::ReflectionProbeScale * 100.0f), OvUI::Widgets::Sliders::ESliderOrientation::HORIZONTAL, "Reflection Probes");
	reflectionProbesScaleSlider.ValueChangedEvent += [this](int p_value) { Settings::EditorSettings::ReflectionProbeScale = p_value / 100.0f; };
	reflectionProbesScaleSlider.format = "%d %%";

	auto& snappingMenu = m_settingsMenu->CreateWidget<MenuList>("Snapping");
	snappingMenu.CreateWidget<Drags::DragFloat>(0.001f, 999999.0f, Settings::EditorSettings::TranslationSnapUnit, 0.05f, "Translation Unit").ValueChangedEvent += [this](float p_value) { Settings::EditorSettings::TranslationSnapUnit = p_value; };
	snappingMenu.CreateWidget<Drags::DragFloat>(0.001f, 999999.0f, Settings::EditorSettings::RotationSnapUnit, 1.0f, "Rotation Unit").ValueChangedEvent += [this](float p_value) { Settings::EditorSettings::RotationSnapUnit = p_value; };
	snappingMenu.CreateWidget<Drags::DragFloat>(0.001f, 999999.0f, Settings::EditorSettings::ScalingSnapUnit, 0.05f, "Scaling Unit").ValueChangedEvent += [this](float p_value) { Settings::EditorSettings::ScalingSnapUnit = p_value; };

	auto& debuggingMenu = m_settingsMenu->CreateWidget<MenuList>("Debugging");
    auto& geometryBounds = debuggingMenu.CreateWidget<MenuItem>("Show geometry bounds", "", true, Settings::EditorSettings::ShowGeometryBounds);
    geometryBounds.ValueChangedEvent += [](bool value) { Settings::EditorSettings::ShowGeometryBounds = value; };
    auto& lightBounds = debuggingMenu.CreateWidget<MenuItem>("Show lights bounds", "", true, Settings::EditorSettings::ShowLightBounds);
    lightBounds.ValueChangedEvent += [](bool value) { Settings::EditorSettings::ShowLightBounds = value; };
    debuggingMenu.ClickedEvent += [geometry = &geometryBounds, lights = &lightBounds] {
        geometry->checked = Settings::EditorSettings::ShowGeometryBounds.Get();
        lights->checked = Settings::EditorSettings::ShowLightBounds.Get();
    };
	debuggingMenu.CreateWidget<MenuItem>("Debug Frustum Culling", "", true, Settings::EditorSettings::DebugFrustumCulling).ValueChangedEvent += [this](bool p_value) { Settings::EditorSettings::DebugFrustumCulling = p_value; };
	debuggingMenu.CreateWidget<MenuItem>("Editor Frustum Geometry Culling", "", true, Settings::EditorSettings::EditorFrustumGeometryCulling).ValueChangedEvent += [this](bool p_value) { Settings::EditorSettings::EditorFrustumGeometryCulling = p_value; };
	debuggingMenu.CreateWidget<MenuItem>("Editor Frustum Light Culling", "", true, Settings::EditorSettings::EditorFrustumLightCulling).ValueChangedEvent += [this](bool p_value) { Settings::EditorSettings::EditorFrustumLightCulling = p_value; };

	auto& scriptingMenu = m_settingsMenu->CreateWidget<MenuList>("Scripting");
	scriptingMenu.CreateWidget<MenuItem>("Regenerate Scripting Project Files On Startup", "", true, Settings::EditorSettings::RegenerateScriptingProjectFilesOnStartup).ValueChangedEvent += [this](bool p_value) { Settings::EditorSettings::RegenerateScriptingProjectFilesOnStartup = p_value; };

	auto& consoleSettingsMenu = m_settingsMenu->CreateWidget<MenuList>("Console Settings");
	auto& consoleMaxLogsSlider = consoleSettingsMenu.CreateWidget<OvUI::Widgets::Sliders::SliderInt>(1, 1000, Settings::EditorSettings::ConsoleMaxLogs.Get(), OvUI::Widgets::Sliders::ESliderOrientation::HORIZONTAL, "Max Logs");
	consoleMaxLogsSlider.ValueChangedEvent += [this](int p_value) {
		Settings::EditorSettings::ConsoleMaxLogs = p_value;
		EDITOR_PANEL(Panels::Console, "Console").TruncateLogs();
	};

	auto& codeEditorMenu = m_settingsMenu->CreateWidget<MenuList>("Code Editor Command");
	codeEditorMenu.CreateWidget<Texts::Text>("Use {workdir} to retrieve the project folder.");
	codeEditorMenu.CreateWidget<Texts::Text>("Use {path} to retrieve the file/folder path.");
	auto& codeEditorInput = codeEditorMenu.CreateWidget<OvUI::Widgets::InputFields::InputText>(Settings::EditorSettings::CodeEditorCommand.Get());
	codeEditorInput.ContentChangedEvent += [](const std::string& p_value)
	{
		Settings::EditorSettings::CodeEditorCommand = p_value;
	};
}

void OvEditor::Panels::MenuBar::CreateHelpMenu()
{
	const std::string repoURL = "https://github.com/Overload-Technologies/Overload";

	auto& helpMenu = CreateWidget<MenuList>("Help");
	helpMenu.CreateWidget<MenuItem>("GitHub").ClickedEvent += [repoURL] {OvTools::Utils::SystemCalls::OpenURL(repoURL); };
	helpMenu.CreateWidget<MenuItem>("Wiki").ClickedEvent += [repoURL] {OvTools::Utils::SystemCalls::OpenURL(repoURL + "/wiki"); };
	helpMenu.CreateWidget<MenuItem>("Lua Reference").ClickedEvent += [] {
		const std::filesystem::path luaDefinitions = EDITOR_CONTEXT(engineAssetsPath) / "Lua";
		if (!EDITOR_EXEC(OpenInCodeEditor(luaDefinitions, luaDefinitions)))
		{
			OvTools::Utils::SystemCalls::ShowInExplorer(luaDefinitions.string());

		}
	};
	helpMenu.CreateWidget<Visual::Separator>();
	helpMenu.CreateWidget<MenuItem>("Bug Report").ClickedEvent += [repoURL] {OvTools::Utils::SystemCalls::OpenURL(repoURL + "/issues/new?assignees=&labels=Bug&template=bug_report.md&title="); };
	helpMenu.CreateWidget<MenuItem>("Feature Request").ClickedEvent += [repoURL] {OvTools::Utils::SystemCalls::OpenURL(repoURL + "/issues/new?assignees=&labels=Feature&template=feature_request.md&title="); };
	helpMenu.CreateWidget<Visual::Separator>();
	helpMenu.CreateWidget<Texts::Text>("Version: " + std::string(OVERLOAD_VERSION));
}

void OvEditor::Panels::MenuBar::RegisterPanel(const std::string& p_name, OvUI::Panels::PanelWindow& p_panel)
{
	m_panels.emplace(p_name, std::ref(p_panel));
}

void OvEditor::Panels::MenuBar::UpdateToggleableItems()
{
	for (auto& [item, panel] : m_panelToggles)
		item->checked = panel->IsOpened();
}
