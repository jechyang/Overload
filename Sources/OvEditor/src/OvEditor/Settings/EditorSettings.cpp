/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <filesystem>

#include <OvEditor/Settings/EditorSettings.h>
#include <OvTools/Filesystem/IniFile.h>
#include <OvTools/Utils/SystemCalls.h>

template<class T>
void LoadIniEntry(OvTools::Filesystem::IniFile& iniFile, const std::string& entry, OvEditor::Settings::EditorSettings::Property<T>& out)
{
	if (iniFile.IsKeyExisting(entry))
	{
		out = iniFile.Get<T>(entry);
	}
}

OvTools::Filesystem::IniFile GetEditorIniFile()
{
	const auto filePath = std::filesystem::path{ OvTools::Utils::SystemCalls::GetPathToAppdata() } /
		"OverloadTech" /
		"OvEditor" /
		"editor.ini";

	return OvTools::Filesystem::IniFile{ filePath.string() };
}

void OvEditor::Settings::EditorSettings::Save()
{
	OvTools::Filesystem::IniFile iniFile = GetEditorIniFile();

	iniFile.RemoveAll();
	iniFile.Add("show_geometry_bounds", ShowGeometryBounds.Get());
	iniFile.Add("show_light_bounds", ShowLightBounds.Get());
	iniFile.Add("editor_frustum_geometry_culling", EditorFrustumGeometryCulling.Get());
	iniFile.Add("editor_frustum_light_culling", EditorFrustumLightCulling.Get());
	iniFile.Add("light_billboard_scale", LightBillboardScale.Get());
	iniFile.Add("gizmo_size", GizmoSize.Get());
	iniFile.Add("reflection_probe_scale", ReflectionProbeScale.Get());
	iniFile.Add("translation_snap_unit", TranslationSnapUnit.Get());
    iniFile.Add("snap_enabled", SnapEnabled.Get());
    iniFile.Add("game_preview_mode", GamePreviewMode.Get());
    iniFile.Add("game_preview_width", GamePreviewWidth.Get());
    iniFile.Add("game_preview_height", GamePreviewHeight.Get());
    iniFile.Add("camera_preview_enabled", CameraPreviewEnabled.Get());
    iniFile.Add("scene_display_mode", SceneDisplayMode.Get());
    iniFile.Add("scene_show_grid", SceneShowGrid.Get());
    iniFile.Add("scene_show_light_icons", SceneShowLightIcons.Get());
    iniFile.Add("scene_show_camera_icons", SceneShowCameraIcons.Get());
    iniFile.Add("scene_show_reflection_icons", SceneShowReflectionIcons.Get());
    iniFile.Add("scene_show_colliders", SceneShowColliders.Get());
    iniFile.Add("scene_show_camera_frustums", SceneShowCameraFrustums.Get());
	iniFile.Add("rotation_snap_unit", RotationSnapUnit.Get());
	iniFile.Add("scaling_snap_unit", ScalingSnapUnit.Get());
	iniFile.Add("color_theme", ColorTheme.Get());
	iniFile.Add("ui_scale", UIScale.Get());
	iniFile.Add("beta_dpi_awareness_dialog_presented", BetaDpiAwarenessDialogPresented.Get());
	iniFile.Add("console_max_logs", ConsoleMaxLogs.Get());
	iniFile.Add("code_editor_command", CodeEditorCommand.Get());
	iniFile.Add("always_regenerate_scripting_symbols", RegenerateScriptingProjectFilesOnStartup.Get());
	iniFile.Rewrite();
}

void OvEditor::Settings::EditorSettings::Load()
{
	OvTools::Filesystem::IniFile iniFile = GetEditorIniFile();

	LoadIniEntry<bool>(iniFile, "show_geometry_bounds", ShowGeometryBounds);
	LoadIniEntry<bool>(iniFile, "show_light_bounds", ShowLightBounds);
	LoadIniEntry<bool>(iniFile, "show_geometry_frustum_culling_in_scene_view", EditorFrustumGeometryCulling);
	LoadIniEntry<bool>(iniFile, "show_light_frustum_culling_in_scene_view", EditorFrustumLightCulling);
	LoadIniEntry<float>(iniFile, "light_billboard_scale", LightBillboardScale);
	LoadIniEntry<float>(iniFile, "gizmo_size", GizmoSize);
	LoadIniEntry<float>(iniFile, "reflection_probe_scale", ReflectionProbeScale);
	LoadIniEntry<float>(iniFile, "translation_snap_unit", TranslationSnapUnit);
    LoadIniEntry<bool>(iniFile, "snap_enabled", SnapEnabled);
    LoadIniEntry<int>(iniFile, "game_preview_mode", GamePreviewMode);
    LoadIniEntry<int>(iniFile, "game_preview_width", GamePreviewWidth);
    LoadIniEntry<int>(iniFile, "game_preview_height", GamePreviewHeight);
    LoadIniEntry<bool>(iniFile, "camera_preview_enabled", CameraPreviewEnabled);
    LoadIniEntry<int>(iniFile, "scene_display_mode", SceneDisplayMode);
    LoadIniEntry<bool>(iniFile, "scene_show_grid", SceneShowGrid);
    LoadIniEntry<bool>(iniFile, "scene_show_light_icons", SceneShowLightIcons);
    LoadIniEntry<bool>(iniFile, "scene_show_camera_icons", SceneShowCameraIcons);
    LoadIniEntry<bool>(iniFile, "scene_show_reflection_icons", SceneShowReflectionIcons);
    LoadIniEntry<bool>(iniFile, "scene_show_colliders", SceneShowColliders);
    LoadIniEntry<bool>(iniFile, "scene_show_camera_frustums", SceneShowCameraFrustums);
	LoadIniEntry<float>(iniFile, "rotation_snap_unit", RotationSnapUnit);
	LoadIniEntry<float>(iniFile, "scaling_snap_unit", ScalingSnapUnit);
	LoadIniEntry<int>(iniFile, "color_theme", ColorTheme);
	LoadIniEntry<int>(iniFile, "ui_scale", UIScale);
	LoadIniEntry<bool>(iniFile, "beta_dpi_awareness_dialog_presented", BetaDpiAwarenessDialogPresented);
	LoadIniEntry<int>(iniFile, "console_max_logs", ConsoleMaxLogs);
	LoadIniEntry<std::string>(iniFile, "code_editor_command", CodeEditorCommand);
	LoadIniEntry<bool>(iniFile, "always_regenerate_scripting_symbols", RegenerateScriptingProjectFilesOnStartup);
}
