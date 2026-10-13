#pragma once
/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <filesystem>

#include <OvTools/Utils/SystemCalls.h>

namespace OvEditor::Utils::FileSystem
{
	const std::filesystem::path kAppdataPath = OvTools::Utils::SystemCalls::GetPathToAppdata();
	const std::filesystem::path kEditorDataPath = kAppdataPath / "OverloadTech" / "OvEditor";
	// Panel windows use stable IDs since the Unity-style layouts; older "layout.ini" files
	// reference generated IDs, so the editor starts from a fresh default layout instead.
	const std::filesystem::path kLayoutFilePath = kEditorDataPath / "editor_layout.ini";
	const std::filesystem::path kSettingsFilePath = kEditorDataPath / "settings.ini";
	const std::filesystem::path kProjectRegistryFilePath = kEditorDataPath / "projects.ini";
}
