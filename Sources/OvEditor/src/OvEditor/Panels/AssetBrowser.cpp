/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <format>
#include <fstream>
#include <iostream>
#include <regex>
#include <utility>
#include <vector>
#include <tinyxml2.h>

#include <OvCore/ECS/Actor.h>
#include <OvCore/Global/ServiceLocator.h>
#include <OvCore/Helpers/GUIDrawer.h>
#include <OvCore/Helpers/GUIHelpers.h>
#include <OvCore/ResourceManagement/MaterialManager.h>
#include <OvCore/ResourceManagement/ModelManager.h>
#include <OvCore/ResourceManagement/ShaderManager.h>
#include <OvCore/ResourceManagement/TextureManager.h>

#include <OvDebug/Logger.h>

#include <OvEditor/Core/EditorActions.h>
#include <OvEditor/Core/EditorResources.h>
#include <OvEditor/Panels/AssetBrowser.h>
#include <OvEditor/Panels/AssetProperties.h>
#include <OvEditor/Panels/Inspector.h>
#include <OvEditor/Panels/MaterialEditor.h>
#include <OvEditor/Settings/EditorSettings.h>

#include <OvRendering/Resources/Parsers/EmbeddedAssetPath.h>

#include <OvTools/Utils/PathParser.h>
#include <OvTools/Utils/String.h>
#include <OvTools/Utils/SystemCalls.h>

#include <imgui_internal.h>

#include <OvUI/Internal/TextureID.h>
#include <OvUI/Plugins/ContextualMenu.h>
#include <OvUI/Plugins/DDSource.h>
#include <OvUI/Plugins/DDTarget.h>
#include <OvUI/Styling/Style.h>
#include <OvUI/Widgets/Buttons/Button.h>
#include <OvUI/Widgets/Layout/Group.h>
#include <OvUI/Widgets/Texts/TextClickable.h>
#include <OvUI/Widgets/Texts/TextColored.h>
#include <OvUI/Widgets/Visual/Image.h>
#include <OvUI/Widgets/Visual/Separator.h>

#include <OvWindowing/Dialogs/MessageBox.h>
#include <OvWindowing/Dialogs/OpenFileDialog.h>
#include <OvWindowing/Dialogs/SaveFileDialog.h>

using namespace OvUI::Panels;
using namespace OvUI::Widgets;

namespace
{
	constexpr std::string_view kAllowedFilenameChars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-_=+ 0123456789()[]";

	std::filesystem::path GetAssociatedMetaFile(const std::filesystem::path& p_assetPath)
	{
		return p_assetPath.string() + ".meta";
	}

	void RenameAsset(const std::filesystem::path& p_prev, const std::filesystem::path& p_new)
	{
		std::filesystem::rename(p_prev, p_new);

		if (const auto previousMetaPath = GetAssociatedMetaFile(p_prev); std::filesystem::exists(previousMetaPath))
		{
			if (const auto newMetaPath = GetAssociatedMetaFile(p_new); !std::filesystem::exists(newMetaPath))
			{
				std::filesystem::rename(previousMetaPath, newMetaPath);
			}
			else
			{
				OVLOG_ERROR(std::format("{} is already existing, .meta creation failed", newMetaPath.string()));
			}
		}
	}

	void RemoveAsset(const std::string& p_toDelete)
	{
		std::filesystem::remove(p_toDelete);

		if (const auto metaPath = GetAssociatedMetaFile(p_toDelete); std::filesystem::exists(metaPath))
		{
			std::filesystem::remove(metaPath);
		}
	}

	std::filesystem::path FindAvailableFilePath(const std::filesystem::path& p_path)
	{
		if (!std::filesystem::exists(p_path))
		{
			return p_path;
		}

		// Split the path into directory, filename, and extension
		const std::filesystem::path dir = p_path.parent_path();
		const std::string filename = p_path.stem().string();
		const std::string extension = p_path.extension().string();

		std::optional<std::string> baseName;
		std::optional<uint32_t> increment;

		const std::regex pattern(R"((.*?)(?:-(\d+))?)");
		std::smatch matches;

		if (std::regex_match(filename, matches, pattern))
		{
			baseName = matches[1].str();

			if (matches[2].matched)
			{
				increment = std::atoi(matches[2].str().c_str());
			}
		}

		constexpr uint32_t kMaxAttempts = 256;

		for (uint32_t i = increment.value_or(1); i < kMaxAttempts; ++i)
		{
			const auto newPath = dir / std::format("{}-{}{}", baseName.value_or("new_file"), i, extension);

			if (!std::filesystem::exists(newPath))
			{
				return newPath;
			}
		}

		OVASSERT(false, "Failed to generate a unique file name.");
		return p_path;
	}

	bool IsPathSameOrDescendant(const std::filesystem::path& p_path, const std::filesystem::path& p_ancestor)
	{
		const std::filesystem::path relativePath = p_path.lexically_normal().lexically_relative(p_ancestor.lexically_normal());
		return !relativePath.empty() && *relativePath.begin() != "..";
	}

	bool ValidateFolderPath(const std::filesystem::path& p_path, const std::string& p_actionName)
	{
		if (std::filesystem::exists(p_path) && std::filesystem::is_directory(p_path))
			return true;

		OVLOG_ERROR(std::format("Cannot perform '{}' because the target folder was deleted externally", p_actionName));
		return false;
	}

	class BrowserItemContextualMenu : public OvUI::Plugins::ContextualMenu
	{
	public:
		BrowserItemContextualMenu(const std::string p_filePath, bool p_protected = false) : m_protected(p_protected), filePath(p_filePath) {}

		virtual void CreateList()
		{
			if (!m_protected && removable)
			{
				auto& deleteAction = CreateWidget<OvUI::Widgets::Menu::MenuItem>("Delete");
				deleteAction.ClickedEvent += [this] { DeleteItem(); };

				auto& renameMenu = CreateWidget<OvUI::Widgets::Menu::MenuList>("Rename to...");

				auto& nameEditor = renameMenu.CreateWidget<OvUI::Widgets::InputFields::InputText>("");
				nameEditor.selectAllOnClick = true;

				renameMenu.ClickedEvent +=[this, &nameEditor] {
					// Check if the item still exists before allowing rename
					if (!std::filesystem::exists(filePath))
					{
						OVLOG_ERROR("Cannot rename this item because it was deleted externally");
						return;
					}

					nameEditor.content = filePath.stem().string();

					if (!std::filesystem::is_directory(filePath))
					{
						if (size_t pos = nameEditor.content.rfind('.'); pos != std::string::npos)
						{
							nameEditor.content = nameEditor.content.substr(0, pos);
						}
					}
				};

				nameEditor.EnterPressedEvent += [this](std::string p_newName)
				{
					if (!std::filesystem::exists(filePath))
					{
						OVLOG_ERROR("Cannot complete rename because the item was deleted externally");
						return;
					}

					if (!std::filesystem::is_directory(filePath))
					{
						p_newName += filePath.extension().string();
					}

					// Remove non-allowed characters.
					std::erase_if(p_newName, [](char c) {
						return kAllowedFilenameChars.find(c) >= kAllowedFilenameChars.size();
					});

					const std::filesystem::path parentFolder = std::filesystem::path{ filePath }.parent_path();
					const std::filesystem::path newPath = parentFolder / p_newName;
					const std::filesystem::path oldPath = filePath;

					if (filePath != newPath && !std::filesystem::exists(newPath))
					{
						filePath = newPath;
					}

					RenamedEvent.Invoke(oldPath, newPath);
				};
			}
		}

		virtual void Execute(OvUI::Plugins::EPluginExecutionContext p_context) override
		{
			if (m_widgets.size() > 0)
			{
				OvUI::Plugins::ContextualMenu::Execute(p_context);
			}
		}

		virtual void DeleteItem() = 0;

	public:
		bool m_protected;
		bool removable = true;
		std::filesystem::path filePath;
		OvTools::Eventing::Event<std::filesystem::path> DestroyedEvent;
		OvTools::Eventing::Event<std::filesystem::path, std::filesystem::path> RenamedEvent;
	};

	class FolderContextualMenu : public BrowserItemContextualMenu
	{
	public:
		FolderContextualMenu(const std::string& p_filePath, bool p_protected = false) : BrowserItemContextualMenu(p_filePath, p_protected) {}

		void CreateScript(const std::string& p_name, const std::string& p_path)
		{
			const std::string fileContent = EDITOR_CONTEXT(scriptEngine)->GetDefaultScriptContent(p_name);

			std::ofstream outfile(p_path);
			outfile << fileContent << std::endl;

			ItemAddedEvent.Invoke(p_path);
			Close();
		}

		void CreateNewShader(const std::string& p_shaderName, std::optional<const std::string_view> p_type)
		{
			if (!ValidateFolderPath(filePath, "Create shader"))
				return;

			const auto finalPath = FindAvailableFilePath(filePath / (p_shaderName + ".ovfx"));

			if (p_type.has_value())
			{
				std::filesystem::copy_file(
					std::filesystem::path(EDITOR_CONTEXT(engineAssetsPath)) /
					"Shaders" /
					std::format("{}.ovfx", p_type.value()),
					finalPath
				);
			}
			else
			{
				// Empty shader.
				std::ofstream outfile(finalPath);
			}

			ItemAddedEvent.Invoke(finalPath.string());
			Close();
		}

		void CreateNewShaderCallback(
			OvUI::Widgets::InputFields::InputText& p_inputText,
			std::optional<const std::string_view> p_type = std::nullopt
		)
		{
			p_inputText.EnterPressedEvent += std::bind(
				&FolderContextualMenu::CreateNewShader,
				this, std::placeholders::_1,
				p_type
			);
		}

		void CreateNewMaterial(
			const std::string& p_materialName,
			std::optional<const std::string_view> p_type,
			std::optional<std::function<void(OvCore::Resources::Material&)>> p_setupCallback
		)
		{
			if (!ValidateFolderPath(filePath, "Create material"))
				return;

			OvCore::Resources::Material material;

			if (p_type.has_value())
			{
				const std::string shaderPath = std::format(":Shaders\\{}.ovfx", p_type.value());

				if (auto shader = EDITOR_CONTEXT(shaderManager)[shaderPath])
				{
					material.SetShader(shader);
				}
			}

			if (p_setupCallback.has_value())
			{
				p_setupCallback.value()(material);
			}

			const auto finalPath = FindAvailableFilePath(filePath / (p_materialName + ".ovmat"));
			OvCore::Resources::Loaders::MaterialLoader::Save(material, finalPath.string());

			ItemAddedEvent.Invoke(finalPath);

			OvCore::Helpers::GUIHelpers::Open(EDITOR_EXEC(GetResourcePath(finalPath.string())));

			Close();
		}

		void CreateNewMaterialCallback(
			OvUI::Widgets::InputFields::InputText& p_inputText,
			std::optional<const std::string_view> p_type = std::nullopt,
			std::optional<std::function<void(OvCore::Resources::Material&)>> p_setupCallback = std::nullopt
		)
		{
			p_inputText.EnterPressedEvent += std::bind(
				&FolderContextualMenu::CreateNewMaterial,
				this, std::placeholders::_1,
				p_type,
				p_setupCallback
			);
		}

		virtual void CreateList() override
		{
			auto& showInExplorer = CreateWidget<OvUI::Widgets::Menu::MenuItem>("Show in explorer");
			showInExplorer.ClickedEvent += [this]
			{
				if (!ValidateFolderPath(filePath, "Show in explorer"))
					return;
				OvTools::Utils::SystemCalls::ShowInExplorer(filePath.string());
			};

			if (!m_protected)
			{
				auto& importAssetHere = CreateWidget<OvUI::Widgets::Menu::MenuItem>("Import Here...");
				importAssetHere.ClickedEvent += [this]
				{
					if (!ValidateFolderPath(filePath, "Import"))
						return;

					if (EDITOR_EXEC(ImportAssetAtLocation(filePath.string())))
					{
						ItemAddedEvent.Invoke(filePath);
					}
				};

				auto& createMenu = CreateWidget<OvUI::Widgets::Menu::MenuList>("Create..");

				auto& createFolderMenu = createMenu.CreateWidget<OvUI::Widgets::Menu::MenuList>("Folder");
				auto& createSceneMenu = createMenu.CreateWidget<OvUI::Widgets::Menu::MenuList>("Scene");
				auto& createScriptMenu = createMenu.CreateWidget<OvUI::Widgets::Menu::MenuList>("Lua Script");
                auto& createCSharpMenu = createMenu.CreateWidget<OvUI::Widgets::Menu::MenuList>("C# Script");
                auto& createCSharp = createCSharpMenu.CreateWidget<OvUI::Widgets::InputFields::InputText>("");
                createCSharpMenu.ClickedEvent += [&createCSharp] { createCSharp.content = ""; };
                createCSharp.EnterPressedEvent += [this](std::string name) {
                    if (!ValidateFolderPath(filePath, "Create C# script") || name.empty() || name.find_first_of("/\\:") != std::string::npos) return;
                    const auto destination = FindAvailableFilePath(filePath / (name + ".cs"));
                    if (EDITOR_CONTEXT(csharpProject)->CreateScript(destination)) { ItemAddedEvent.Invoke(destination); Close(); }
                };
				auto& createShaderMenu = createMenu.CreateWidget<OvUI::Widgets::Menu::MenuList>("Shader");
				auto& createMaterialMenu = createMenu.CreateWidget<OvUI::Widgets::Menu::MenuList>("Material");

				auto& createEmptyShaderMenu = createShaderMenu.CreateWidget<OvUI::Widgets::Menu::MenuList>("Empty");
				auto& createPartialShaderMenu = createShaderMenu.CreateWidget<OvUI::Widgets::Menu::MenuList>("Partial");
				auto& createStandardShaderMenu = createShaderMenu.CreateWidget<OvUI::Widgets::Menu::MenuList>("Standard template");
				auto& createUnlitShaderMenu = createShaderMenu.CreateWidget<OvUI::Widgets::Menu::MenuList>("Unlit template");
				auto& createSkysphereShaderMenu = createShaderMenu.CreateWidget<OvUI::Widgets::Menu::MenuList>("Skysphere template");
				auto& createAtmosphereShaderMenu = createShaderMenu.CreateWidget<OvUI::Widgets::Menu::MenuList>("Atmosphere template");

				auto& createEmptyMaterialMenu = createMaterialMenu.CreateWidget<OvUI::Widgets::Menu::MenuList>("Empty");
				auto& createStandardMaterialMenu = createMaterialMenu.CreateWidget<OvUI::Widgets::Menu::MenuList>("Standard");
				auto& createUnlitMaterialMenu = createMaterialMenu.CreateWidget<OvUI::Widgets::Menu::MenuList>("Unlit");
				auto& createSkysphereMaterialMenu = createMaterialMenu.CreateWidget<OvUI::Widgets::Menu::MenuList>("Skysphere");
				auto& createAtmosphereMaterialMenu = createMaterialMenu.CreateWidget<OvUI::Widgets::Menu::MenuList>("Atmosphere");

				auto& createFolder = createFolderMenu.CreateWidget<OvUI::Widgets::InputFields::InputText>("");
				auto& createScene = createSceneMenu.CreateWidget<OvUI::Widgets::InputFields::InputText>("");
				auto& createScript = createScriptMenu.CreateWidget<OvUI::Widgets::InputFields::InputText>("");

				auto& createEmptyMaterial = createEmptyMaterialMenu.CreateWidget<OvUI::Widgets::InputFields::InputText>("");
				auto& createStandardMaterial = createStandardMaterialMenu.CreateWidget<OvUI::Widgets::InputFields::InputText>("");
				auto& createUnlitMaterial = createUnlitMaterialMenu.CreateWidget<OvUI::Widgets::InputFields::InputText>("");
				auto& createSkysphereMaterial = createSkysphereMaterialMenu.CreateWidget<OvUI::Widgets::InputFields::InputText>("");
				auto& createAtmosphereMaterial = createAtmosphereMaterialMenu.CreateWidget<OvUI::Widgets::InputFields::InputText>("");

				auto& createEmptyShader = createEmptyShaderMenu.CreateWidget<OvUI::Widgets::InputFields::InputText>("");
				auto& createPartialShader = createPartialShaderMenu.CreateWidget<OvUI::Widgets::InputFields::InputText>("");
				auto& createStandardShader = createStandardShaderMenu.CreateWidget<OvUI::Widgets::InputFields::InputText>("");
				auto& createUnlitShader = createUnlitShaderMenu.CreateWidget<OvUI::Widgets::InputFields::InputText>("");
				auto& createSkysphereShader = createSkysphereShaderMenu.CreateWidget<OvUI::Widgets::InputFields::InputText>("");
				auto& createAtmosphereShader = createAtmosphereShaderMenu.CreateWidget<OvUI::Widgets::InputFields::InputText>("");

				createFolderMenu.ClickedEvent += [&createFolder] { createFolder.content = ""; };
				createSceneMenu.ClickedEvent += [&createScene] { createScene.content = ""; };
				createScriptMenu.ClickedEvent += [&createScript] { createScript.content = ""; };
				createStandardShaderMenu.ClickedEvent += [&createStandardShader] { createStandardShader.content = ""; };
				createUnlitShaderMenu.ClickedEvent += [&createUnlitShader] { createUnlitShader.content = ""; };
				createSkysphereShaderMenu.ClickedEvent += [&createSkysphereShader] { createSkysphereShader.content = ""; };
				createAtmosphereShaderMenu.ClickedEvent += [&createAtmosphereShader] { createAtmosphereShader.content = ""; };
				createEmptyMaterialMenu.ClickedEvent += [&createEmptyMaterial] { createEmptyMaterial.content = ""; };
				createEmptyShaderMenu.ClickedEvent += [&createEmptyShader] { createEmptyShader.content = ""; };
				createPartialShaderMenu.ClickedEvent += [&createPartialShader] { createPartialShader.content = ""; };
				createStandardMaterialMenu.ClickedEvent += [&createStandardMaterial] { createStandardMaterial.content = ""; };
				createUnlitMaterialMenu.ClickedEvent += [&createUnlitMaterial] { createUnlitMaterial.content = ""; };
				createSkysphereMaterialMenu.ClickedEvent += [&createSkysphereMaterial] { createSkysphereMaterial.content = ""; };
				createAtmosphereMaterialMenu.ClickedEvent += [&createAtmosphereMaterial] { createAtmosphereMaterial.content = ""; };

				createFolder.EnterPressedEvent += [this](std::string newFolderName) {
					if (!ValidateFolderPath(filePath, "Create folder"))
						return;
					const auto finalPath = FindAvailableFilePath(filePath / newFolderName);
					std::filesystem::create_directory(finalPath);
					ItemAddedEvent.Invoke(finalPath);
					Close();
				};

				createScene.EnterPressedEvent += [this](std::string newSceneName) {
					if (!ValidateFolderPath(filePath, "Create scene"))
						return;
					const auto finalPath = FindAvailableFilePath(filePath / (newSceneName + ".ovscene"));

					auto emptyScene = OvCore::SceneSystem::Scene{};
					emptyScene.AddDefaultCamera();
					emptyScene.AddDefaultLights();

					EDITOR_EXEC(SaveSceneToDisk(emptyScene, finalPath.string()));

					ItemAddedEvent.Invoke(finalPath);
					Close();
				};

				createPartialShader.EnterPressedEvent += [this](std::string newShaderName) {
					if (!ValidateFolderPath(filePath, "Create shader"))
						return;
					const auto finalPath = FindAvailableFilePath(filePath / (newShaderName + ".ovfxh"));

					{
						std::ofstream outfile(finalPath);
					}

					ItemAddedEvent.Invoke(finalPath);
					Close();
				};

				createScript.EnterPressedEvent += [this](std::string p_newName) {
					if (!ValidateFolderPath(filePath, "Create script"))
						return;
					std::erase_if(p_newName, [](char c) {
						return std::find(kAllowedFilenameChars.begin(), kAllowedFilenameChars.end(), c) == kAllowedFilenameChars.end();
					});

					const auto extension = EDITOR_CONTEXT(scriptEngine)->GetDefaultExtension();
					const auto newPath = FindAvailableFilePath(filePath / (p_newName + extension));

					if (!p_newName.empty())
					{
						CreateScript(p_newName, newPath.string());
					}
				};

				CreateNewShaderCallback(createEmptyShader);
				CreateNewShaderCallback(createStandardShader, "Standard");
				CreateNewShaderCallback(createUnlitShader, "Unlit");
				CreateNewShaderCallback(createSkysphereShader, "Skysphere");
				CreateNewShaderCallback(createAtmosphereShader, "Atmosphere");

				CreateNewMaterialCallback(createEmptyMaterial);
				CreateNewMaterialCallback(createStandardMaterial, "Standard");
				CreateNewMaterialCallback(createUnlitMaterial, "Unlit");
				CreateNewMaterialCallback(createSkysphereMaterial, "Skysphere", [](OvCore::Resources::Material& material) {
					// A default skysphere material should have backface culling disabled
					// And frontface culling enabled (renders the inside of the sphere).
					material.SetDrawOrder(100);
					material.SetDepthTest(false);
					material.SetDepthWriting(false);
					material.SetBackfaceCulling(false);
					material.SetFrontfaceCulling(true);
				});
				CreateNewMaterialCallback(createAtmosphereMaterial, "Atmosphere", [](OvCore::Resources::Material& material) {
					// A default atmosphere material should have backface culling disabled
					// And frontface culling enabled (renders the inside of the cube).
					material.SetDrawOrder(10);
					material.SetDepthTest(false);
					material.SetDepthWriting(false);
					material.SetBackfaceCulling(false);
					material.SetFrontfaceCulling(true);
				});

				BrowserItemContextualMenu::CreateList();
			}
		}

		virtual void DeleteItem() override
		{
			using namespace OvWindowing::Dialogs;
			MessageBox message(
				"Delete folder",
				std::format(
					"Deleting a folder (and all its content) is irreversible, are you sure that you want to delete \"{}\"?",
					filePath.string()
				),
				MessageBox::EMessageType::WARNING,
				MessageBox::EButtonLayout::YES_NO
			);

			if (message.GetUserAction() == MessageBox::EUserAction::YES)
			{
				if (std::filesystem::exists(filePath) == true)
				{
					EDITOR_EXEC(PropagateFolderDestruction(filePath.string()));
					std::filesystem::remove_all(filePath);
				}
				
				DestroyedEvent.Invoke(filePath);
			}
		}

	public:
		OvTools::Eventing::Event<std::filesystem::path> ItemAddedEvent;
	};

	class FileContextualMenu : public BrowserItemContextualMenu
	{
	public:
		FileContextualMenu(const std::string& p_filePath, bool p_protected = false) : BrowserItemContextualMenu(p_filePath, p_protected) {}

		virtual void CreateList() override
		{
			auto& openAction = CreateWidget<OvUI::Widgets::Menu::MenuItem>("Open");

			openAction.ClickedEvent += [this] {
				OvCore::Helpers::GUIHelpers::Open(EDITOR_EXEC(GetResourcePath(filePath.string(), m_protected)));
			};

			auto& openExternallyAction = CreateWidget<OvUI::Widgets::Menu::MenuItem>("Open Externally...");

			openExternallyAction.ClickedEvent += [this] {
				OvTools::Utils::SystemCalls::OpenFile(filePath.string());
			};

			if (!m_protected)
			{
				auto& duplicateAction = CreateWidget<OvUI::Widgets::Menu::MenuItem>("Duplicate");

				duplicateAction.ClickedEvent += [this] {
					const auto finalPath = FindAvailableFilePath(filePath);
					std::filesystem::copy(filePath, finalPath);
					DuplicateEvent.Invoke(finalPath);
				};
			}

			BrowserItemContextualMenu::CreateList();

			auto& editMetadata = CreateWidget<OvUI::Widgets::Menu::MenuItem>("Properties");

			editMetadata.ClickedEvent += [this] {
				const std::string resourcePath = EDITOR_EXEC(GetResourcePath(filePath.string(), m_protected));
				auto& inspector = EDITOR_PANEL(OvEditor::Panels::Inspector, "Inspector");
				inspector.FocusAsset(resourcePath);
				inspector.Open();
				inspector.Focus();
				auto& panel = EDITOR_PANEL(OvEditor::Panels::AssetProperties, "Asset Properties");
				panel.SetTarget(resourcePath);
			};
		}

		virtual void DeleteItem() override
		{
			using namespace OvWindowing::Dialogs;
			MessageBox message(
				"Delete file",
				std::format("Deleting a file is irreversible, are you sure that you want to delete \"{}\"?",
					filePath.string()
				),
				MessageBox::EMessageType::WARNING,
				MessageBox::EButtonLayout::YES_NO
			);

			if (message.GetUserAction() == MessageBox::EUserAction::YES)
			{
				RemoveAsset(filePath.string());
				DestroyedEvent.Invoke(filePath);
				EDITOR_EXEC(PropagateFileRename(filePath.string(), "?"));
			}
		}

	public:
		OvTools::Eventing::Event<std::filesystem::path> DuplicateEvent;
	};

	class ShaderContextualMenu : public FileContextualMenu
	{
	public:
		ShaderContextualMenu(const std::string& p_filePath, bool p_protected = false) : FileContextualMenu(p_filePath, p_protected) {}

		virtual void CreateList() override
		{
			FileContextualMenu::CreateList();

			auto& compileAction = CreateWidget<OvUI::Widgets::Menu::MenuItem>("Compile");

			compileAction.ClickedEvent += [this] {
				const std::string resourcePath = EDITOR_EXEC(GetResourcePath(filePath.string(), m_protected));
				EDITOR_EXEC(CompileShader(resourcePath));
			};
		}
	};

	class ModelContextualMenu : public FileContextualMenu
	{
	public:
		ModelContextualMenu(const std::string& p_filePath, bool p_protected = false) : FileContextualMenu(p_filePath, p_protected) {}

		void CreateMaterialFiles(const std::string_view p_shaderType)
		{
			auto& modelManager = OVSERVICE(OvCore::ResourceManagement::ModelManager);
			const std::string resourcePath = EDITOR_EXEC(GetResourcePath(filePath.string(), m_protected));

			if (auto model = modelManager.GetResource(resourcePath))
			{
				for (const std::string& materialName : model->GetMaterialNames())
				{
					const auto finalPath = FindAvailableFilePath(filePath.parent_path() / (materialName + ".ovmat"));

					const std::string fileContent = std::format(
						"<root><shader>:Shaders\\{}.ovfx</shader></root>",
						p_shaderType
					);

					{
						std::ofstream outputFile(finalPath);
						outputFile << fileContent << std::endl;
					}

					DuplicateEvent.Invoke(finalPath);
				}
			}
		}

		void ExtractMaterialFiles()
		{
			auto& modelManager = OVSERVICE(OvCore::ResourceManagement::ModelManager);
			auto& materialManager = OVSERVICE(OvCore::ResourceManagement::MaterialManager);
			const std::string resourcePath = EDITOR_EXEC(GetResourcePath(filePath.string(), m_protected));

			if (auto model = modelManager.GetResource(resourcePath))
			{
				const auto& embeddedMaterials = model->GetEmbeddedMaterials();
				const auto& materialNames = model->GetMaterialNames();

				for (size_t materialIndex = 0; materialIndex < embeddedMaterials.size(); ++materialIndex)
				{
					const std::string embeddedMaterialPath = OvRendering::Resources::Parsers::MakeEmbeddedMaterialPath(
						resourcePath,
						static_cast<uint32_t>(materialIndex)
					);

					auto* embeddedMaterial = materialManager.GetResource(embeddedMaterialPath);
					if (!embeddedMaterial)
					{
						continue;
					}

					const bool hasNamedSlot = materialIndex < materialNames.size() && !materialNames[materialIndex].empty();
					const std::string materialName = hasNamedSlot
						? materialNames[materialIndex]
						: std::format("embedded_material_{}", materialIndex);

					const auto finalPath = FindAvailableFilePath(filePath.parent_path() / (materialName + ".ovmat"));
					OvCore::Resources::Loaders::MaterialLoader::Save(*embeddedMaterial, finalPath.string());
					DuplicateEvent.Invoke(finalPath);
				}
			}
		}

		void CreateMaterialCreationOption(OvUI::Internal::WidgetContainer& p_root, const std::string_view p_materialName)
		{
			const std::string materialName{ p_materialName };
			p_root.CreateWidget<OvUI::Widgets::Menu::MenuItem>(materialName).ClickedEvent += [this, materialName]
			{
				CreateMaterialFiles(materialName);
			};
		}

		virtual void CreateList() override
		{
			auto& reloadAction = CreateWidget<OvUI::Widgets::Menu::MenuItem>("Reload");

			reloadAction.ClickedEvent += [this]
			{
				auto& modelManager = OVSERVICE(OvCore::ResourceManagement::ModelManager);
				std::string resourcePath = EDITOR_EXEC(GetResourcePath(filePath.string(), m_protected));
				if (modelManager.IsResourceRegistered(resourcePath))
				{
					modelManager.AResourceManager::ReloadResource(resourcePath);
					EDITOR_PANEL(OvEditor::Panels::Inspector, "Inspector").Refresh();
					EDITOR_PANEL(OvEditor::Panels::MaterialEditor, "Material Editor").Refresh();
				}
			};

			if (!m_protected)
			{
				auto& generateMaterialsMenu = CreateWidget<OvUI::Widgets::Menu::MenuList>("Generate materials...");
				auto& extractMaterialsAction = CreateWidget<OvUI::Widgets::Menu::MenuItem>("Extract materials");

				CreateMaterialCreationOption(generateMaterialsMenu, "Standard");
				CreateMaterialCreationOption(generateMaterialsMenu, "Unlit");

				extractMaterialsAction.ClickedEvent += [this]
				{
					ExtractMaterialFiles();
				};
			}

			FileContextualMenu::CreateList();
		}
	};

	class TextureContextualMenu : public FileContextualMenu
	{
	public:
		TextureContextualMenu(const std::string& p_filePath, bool p_protected = false) : FileContextualMenu(p_filePath, p_protected) {}

		virtual void CreateList() override
		{
			auto& reloadAction = CreateWidget<OvUI::Widgets::Menu::MenuItem>("Reload");

			reloadAction.ClickedEvent += [this]
			{
				auto& textureManager = OVSERVICE(OvCore::ResourceManagement::TextureManager);
				const std::string resourcePath = EDITOR_EXEC(GetResourcePath(filePath.string(), m_protected));
				if (textureManager.IsResourceRegistered(resourcePath))
				{
					/* Trying to recompile */
					textureManager.AResourceManager::ReloadResource(resourcePath);
					EDITOR_PANEL(OvEditor::Panels::MaterialEditor, "Material Editor").Refresh();
				}
			};

			FileContextualMenu::CreateList();
		}
	};

	class MaterialContextualMenu : public FileContextualMenu
	{
	public:
		MaterialContextualMenu(const std::string& p_filePath, bool p_protected = false) : FileContextualMenu(p_filePath, p_protected) {}

		virtual void CreateList() override
		{
			auto& reload = CreateWidget<OvUI::Widgets::Menu::MenuItem>("Reload");
			reload.ClickedEvent += [this]
			{
				auto materialManager = OVSERVICE(OvCore::ResourceManagement::MaterialManager);
				auto resourcePath = EDITOR_EXEC(GetResourcePath(filePath.string(), m_protected));
				OvCore::Resources::Material* material = materialManager[resourcePath];
				if (material)
				{
					materialManager.AResourceManager::ReloadResource(resourcePath);
					EDITOR_PANEL(OvEditor::Panels::MaterialEditor, "Material Editor").Refresh();
				}
			};

			FileContextualMenu::CreateList();
		}
	};

	class EmbeddedFileContextualMenu : public OvUI::Plugins::ContextualMenu
	{
	public:
		EmbeddedFileContextualMenu(std::string p_resourcePath) : m_resourcePath(std::move(p_resourcePath)) {}

		void CreateList()
		{
			auto& openAction = CreateWidget<OvUI::Widgets::Menu::MenuItem>("Open");
			openAction.ClickedEvent += [this] {
				OvCore::Helpers::GUIHelpers::Open(m_resourcePath);
			};
		}

		virtual void Execute(OvUI::Plugins::EPluginExecutionContext p_context) override
		{
			if (!m_widgets.empty())
			{
				OvUI::Plugins::ContextualMenu::Execute(p_context);
			}
		}

	protected:
		std::string m_resourcePath;
	};

	std::unique_ptr<FileContextualMenu> MakeFileMenu(OvTools::Utils::PathParser::EFileType p_fileType, const std::string& p_path, bool p_protected)
	{
		using enum OvTools::Utils::PathParser::EFileType;
		switch (p_fileType)
		{
			case MODEL: return std::make_unique<ModelContextualMenu>(p_path, p_protected);
			case TEXTURE: return std::make_unique<TextureContextualMenu>(p_path, p_protected);
			case SHADER: return std::make_unique<ShaderContextualMenu>(p_path, p_protected);
			case MATERIAL: return std::make_unique<MaterialContextualMenu>(p_path, p_protected);
			default: return std::make_unique<FileContextualMenu>(p_path, p_protected);
		}
	}

	std::string ToLower(std::string p_text)
	{
		std::transform(p_text.begin(), p_text.end(), p_text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return p_text;
	}

	OvUI::Internal::TextureID RetainEditorTexture(OvRendering::Resources::Texture* p_texture)
	{
		return p_texture ? OvUI::Internal::TextureID{ p_texture->RetainTexture() } : OvUI::Internal::TextureID{};
	}

	// Drag payloads keep the historical layout: resource path + (unused here) source widget group.
	using AssetPayload = std::pair<std::string, OvUI::Widgets::Layout::Group*>;
	using ActorPayload = std::pair<OvCore::ECS::Actor*, OvUI::Widgets::Layout::TreeNode*>;

	// At the smallest zoom the content switches to a list, like Unity's Project window.
	constexpr float kListModeTileSize = 32.0f;

	class BrowserContent final : public OvUI::Widgets::AWidget
	{
	public:
		explicit BrowserContent(std::function<void()> p_draw) : m_draw(std::move(p_draw)) {}
	private:
		void _Draw_Impl() override { m_draw(); }
		std::function<void()> m_draw;
	};

	struct Entry
	{
		std::filesystem::path path;			// real path; empty for model sub-assets
		std::string name;
		std::string resourcePath;
		OvTools::Utils::PathParser::EFileType type = OvTools::Utils::PathParser::EFileType::UNKNOWN;
		bool directory = false;
		bool engine = false;
		bool embedded = false;
		AssetPayload payload;
		OvUI::Internal::TextureID icon;
		OvRendering::Resources::Texture* thumbnail = nullptr;
		bool thumbnailRequested = false;
		std::unique_ptr<OvUI::Plugins::ContextualMenu> menu;

		std::string Key() const { return embedded ? resourcePath : path.string(); }
	};
}

struct OvEditor::Panels::AssetBrowser::State
{
	std::filesystem::path engineRoot;
	std::filesystem::path projectRoot;
	std::filesystem::path current;
	std::string selected;
	std::set<std::filesystem::path> expanded;
	std::set<std::string> expandedModels;
	std::map<std::filesystem::path, std::vector<std::filesystem::path>> subfolders;
	std::map<std::filesystem::path, std::unique_ptr<FolderContextualMenu>> folderMenus;
	std::vector<std::unique_ptr<Entry>> entries;
	OvUI::Internal::TextureID folderIcon;
	AssetPayload dragPayload;
	std::array<char, 256> search{};
	bool dirty = true;
	bool scrollToSelection = false;
	float tileSize = 72.0f;
	float treeWidth = 210.0f;

	const std::vector<std::filesystem::path>& Subfolders(const std::filesystem::path& p_folder)
	{
		auto [it, inserted] = subfolders.try_emplace(p_folder);
		if (inserted)
		{
			std::error_code error;
			for (std::filesystem::directory_iterator iterator(p_folder, error), end; !error && iterator != end; iterator.increment(error))
				if (iterator->is_directory(error)) it->second.push_back(iterator->path());
			std::sort(it->second.begin(), it->second.end(), [](const auto& a, const auto& b) { return ToLower(a.filename().string()) < ToLower(b.filename().string()); });
		}
		return it->second;
	}
};

OvEditor::Panels::AssetBrowser::AssetBrowser
(
	const std::string& p_title,
	bool p_opened,
	const OvUI::Settings::PanelWindowSettings& p_windowSettings
) : PanelWindow(p_title, p_opened, p_windowSettings), m_state(std::make_unique<State>())
{
	using namespace OvWindowing::Dialogs;

	if (std::filesystem::create_directories(EDITOR_CONTEXT(projectAssetsPath)))
	{
		MessageBox message(
			"Assets folder not found",
			"The \"Assets/\" folders hasn't been found in your project directory.\nIt has been automatically generated",
			MessageBox::EMessageType::WARNING,
			MessageBox::EButtonLayout::OK
		);
	}

	EDITOR_EXEC(MigrateScripts());

	scrollable = false;
	m_state->engineRoot = std::filesystem::path{ EDITOR_CONTEXT(engineAssetsPath) }.lexically_normal();
	m_state->projectRoot = std::filesystem::path{ EDITOR_CONTEXT(projectAssetsPath) }.lexically_normal();
	m_state->current = m_state->projectRoot;
	m_state->expanded.insert(m_state->projectRoot);
	m_state->folderIcon = RetainEditorTexture(EDITOR_CONTEXT(editorResources)->GetTexture("Folder"));
	CreateWidget<BrowserContent>([this] { DrawBrowser(); });
}

OvEditor::Panels::AssetBrowser::~AssetBrowser() = default;

void OvEditor::Panels::AssetBrowser::Refresh()
{
	m_state->dirty = true;
}

void OvEditor::Panels::AssetBrowser::ShowFolder(const std::filesystem::path& p_folder)
{
	const auto folder = p_folder.lexically_normal();
	m_state->current = folder;
	m_state->search.fill('\0');
	const auto& root = IsEngine(folder) ? m_state->engineRoot : m_state->projectRoot;
	for (auto parent = folder.parent_path(); IsPathSameOrDescendant(parent, root); parent = parent.parent_path())
	{
		m_state->expanded.insert(parent);
		if (parent == root) break;
	}
	m_state->dirty = true;
}

const std::filesystem::path& OvEditor::Panels::AssetBrowser::GetCurrentFolder() const
{
	return m_state->current;
}

bool OvEditor::Panels::AssetBrowser::IsEngine(const std::filesystem::path& p_path) const
{
	return IsPathSameOrDescendant(p_path, m_state->engineRoot);
}

void OvEditor::Panels::AssetBrowser::Select(const std::filesystem::path& p_path)
{
	m_state->selected = p_path.lexically_normal().string();
	m_state->scrollToSelection = true;
}

void OvEditor::Panels::AssetBrowser::Rebuild()
{
	using EFileType = OvTools::Utils::PathParser::EFileType;
	auto& state = *m_state;
	state.dirty = false;
	state.entries.clear();
	state.subfolders.clear();
	state.folderMenus.clear();

	std::error_code error;
	if (!std::filesystem::is_directory(state.current, error))
		state.current = state.projectRoot;

	const auto addEntry = [&](const std::filesystem::path& p_path, bool p_directory)
	{
		auto entry = std::make_unique<Entry>();
		entry->path = p_path.lexically_normal();
		entry->name = p_path.filename().string();
		entry->directory = p_directory;
		entry->engine = IsEngine(entry->path);
		entry->type = p_directory ? EFileType::UNKNOWN : OvTools::Utils::PathParser::GetFileType(entry->name);
		entry->resourcePath = EDITOR_EXEC(GetResourcePath(entry->path.string(), entry->engine));
		entry->payload = { entry->resourcePath, nullptr };
		entry->icon = p_directory ? state.folderIcon : RetainEditorTexture(EDITOR_CONTEXT(editorResources)->GetFileIcon(entry->name));

		if (p_directory)
		{
			auto menu = std::make_unique<FolderContextualMenu>(entry->path.string(), entry->engine);
			menu->CreateList();
			Wire(*menu, true, false);
			entry->menu = std::move(menu);
		}
		else
		{
			auto menu = MakeFileMenu(entry->type, entry->path.string(), entry->engine);
			menu->CreateList();
			Wire(*menu, false, entry->type == EFileType::SCRIPT);
			entry->menu = std::move(menu);
		}

		state.entries.push_back(std::move(entry));
	};

	const auto addEmbedded = [&](const std::string& p_resourcePath, EFileType p_type)
	{
		const auto embeddedPath = OvRendering::Resources::Parsers::ParseEmbeddedAssetPath(p_resourcePath);
		if (!embeddedPath) return;

		auto entry = std::make_unique<Entry>();
		entry->name = embeddedPath->assetName;
		entry->resourcePath = p_resourcePath;
		entry->type = p_type;
		entry->embedded = true;
		entry->engine = !p_resourcePath.empty() && p_resourcePath.front() == ':';
		entry->payload = { p_resourcePath, nullptr };
		entry->icon = RetainEditorTexture(EDITOR_CONTEXT(editorResources)->GetFileIcon(entry->name));
		auto menu = std::make_unique<EmbeddedFileContextualMenu>(p_resourcePath);
		menu->CreateList();
		entry->menu = std::move(menu);
		state.entries.push_back(std::move(entry));
	};

	const auto addModelContent = [&](const Entry& p_model)
	{
		const auto* model = OVSERVICE(OvCore::ResourceManagement::ModelManager).GetResource(p_model.resourcePath);
		if (!model) return;

		for (size_t index = 0; index < model->GetEmbeddedMaterials().size(); ++index)
			addEmbedded(OvRendering::Resources::Parsers::MakeEmbeddedMaterialPath(p_model.resourcePath, static_cast<uint32_t>(index)), EFileType::MATERIAL);

		const auto& textures = model->GetEmbeddedTextures();
		for (size_t index = 0; index < textures.size(); ++index)
		{
			using ESourceType = OvRendering::Resources::EmbeddedTextureData::ESourceType;
			if (textures[index].sourceType == ESourceType::EXTERNAL_FILE) continue;
			const std::string extension = textures[index].extension.empty() ? "bin" : textures[index].extension;
			addEmbedded(OvRendering::Resources::Parsers::MakeEmbeddedTexturePath(p_model.resourcePath, static_cast<uint32_t>(index), extension), EFileType::TEXTURE);
		}
	};

	std::vector<std::filesystem::path> folders, files;
	const std::string query = ToLower(state.search.data());
	const auto consider = [&](const std::filesystem::directory_entry& p_item)
	{
		std::error_code itemError;
		const bool directory = p_item.is_directory(itemError);
		if (!directory && OvTools::Utils::PathParser::GetFileType(p_item.path().filename().string()) == EFileType::UNKNOWN) return;
		if (!query.empty() && ToLower(p_item.path().filename().string()).find(query) == std::string::npos) return;
		(directory ? folders : files).push_back(p_item.path());
	};

	if (query.empty())
	{
		for (std::filesystem::directory_iterator iterator(state.current, error), end; !error && iterator != end; iterator.increment(error))
			consider(*iterator);
	}
	else
	{
		// Like Unity, searching covers the whole root (Assets or Engine) of the current folder.
		const auto& root = IsEngine(state.current) ? state.engineRoot : state.projectRoot;
		for (std::filesystem::recursive_directory_iterator iterator(root, error), end; !error && iterator != end && folders.size() + files.size() < 1000; iterator.increment(error))
			consider(*iterator);
	}

	const auto byName = [](const auto& a, const auto& b) { return ToLower(a.filename().string()) < ToLower(b.filename().string()); };
	std::sort(folders.begin(), folders.end(), byName);
	std::sort(files.begin(), files.end(), byName);

	for (const auto& folder : folders) addEntry(folder, true);
	for (const auto& file : files)
	{
		addEntry(file, false);
		const auto& model = *state.entries.back();
		if (model.type == EFileType::MODEL && state.expandedModels.contains(model.Key()))
			addModelContent(model);
	}
}

void OvEditor::Panels::AssetBrowser::OnRenamed(const std::filesystem::path& p_previous, const std::filesystem::path& p_next, bool p_directory, bool p_script)
{
	using namespace OvWindowing::Dialogs;

	if (p_previous == p_next) return;

	if (std::filesystem::exists(p_next))
	{
		MessageBox errorMessage(
			p_directory ? "Folder already exists" : "File already exists",
			std::string("You can't rename this ") + (p_directory ? "folder" : "file") + " because the given name is already taken",
			MessageBox::EMessageType::ERROR,
			MessageBox::EButtonLayout::OK
		);
		return;
	}

	RenameAsset(p_previous, p_next);

	if (p_directory)
	{
		EDITOR_EXEC(PropagateFolderRename(p_previous.string(), p_next.string()));

		// Keep navigation state on the renamed folder.
		const auto remap = [&](const std::filesystem::path& p_path) -> std::filesystem::path
		{
			if (p_path != p_previous && !IsPathSameOrDescendant(p_path, p_previous)) return p_path;
			return (p_next / p_path.lexically_relative(p_previous)).lexically_normal();
		};
		m_state->current = remap(m_state->current);
		std::set<std::filesystem::path> expanded;
		for (const auto& path : m_state->expanded) expanded.insert(remap(path));
		m_state->expanded = std::move(expanded);
	}
	else
	{
		EDITOR_EXEC(PropagateFileRename(p_previous.string(), p_next.string()));

		if (!p_script && EDITOR_CONTEXT(sceneManager).GetCurrentSceneSourcePath() == p_previous)
			EDITOR_CONTEXT(sceneManager).StoreCurrentSceneSourcePath(p_next.string());
	}

	Select(p_next);
	Refresh();
}

void OvEditor::Panels::AssetBrowser::OnDestroyed(const std::filesystem::path& p_path)
{
	if (EDITOR_CONTEXT(sceneManager).GetCurrentSceneSourcePath() == p_path)
		EDITOR_CONTEXT(sceneManager).ForgetCurrentSceneSourcePath();

	if (m_state->current == p_path || IsPathSameOrDescendant(m_state->current, p_path))
		m_state->current = p_path.parent_path();

	m_state->selected.clear();
	Refresh();
}

void OvEditor::Panels::AssetBrowser::MoveInto(const std::filesystem::path& p_folder, const std::string& p_resourcePath, bool p_directory)
{
	using namespace OvWindowing::Dialogs;

	if (p_resourcePath.empty() || IsEngine(p_folder) || !ValidateFolderPath(p_folder, "Move"))
		return;

	const std::filesystem::path previous = std::filesystem::path{ EDITOR_EXEC(GetRealPath(p_resourcePath)) }.lexically_normal();
	const std::filesystem::path next = p_folder / previous.filename();
	const bool fromEngine = p_resourcePath.front() == ':';

	std::error_code error;
	if (previous.parent_path() == p_folder || std::filesystem::equivalent(previous, p_folder, error))
		return;

	if (p_directory && IsPathSameOrDescendant(p_folder, previous))
	{
		OVLOG_WARNING(std::format("Cannot move folder \"{}\" to \"{}\" because the destination is inside the source folder.", previous.string(), p_folder.string()));
		return;
	}

	if (std::filesystem::exists(next))
	{
		MessageBox errorMessage(
			p_directory ? "Folder already exists" : "File already exists",
			std::string("You can't move this ") + (p_directory ? "folder" : "file") + " to this location because the name is already taken",
			MessageBox::EMessageType::ERROR,
			MessageBox::EButtonLayout::OK
		);
		return;
	}

	// Engine assets are copied into the project; project assets are moved and references updated.
	if (fromEngine)
	{
		if (p_directory) std::filesystem::copy(previous, next, std::filesystem::copy_options::recursive);
		else std::filesystem::copy_file(previous, next);
	}
	else
	{
		RenameAsset(previous, next);
		if (p_directory) EDITOR_EXEC(PropagateFolderRename(previous.string(), next.string()));
		else EDITOR_EXEC(PropagateFileRename(previous.string(), next.string()));

		if (!p_directory && EDITOR_CONTEXT(sceneManager).GetCurrentSceneSourcePath() == previous)
			EDITOR_CONTEXT(sceneManager).StoreCurrentSceneSourcePath(next.string());
	}

	Select(next);
	Refresh();
}

void OvEditor::Panels::AssetBrowser::AcceptDrops(const std::filesystem::path& p_folder)
{
	if (IsEngine(p_folder) || !ImGui::BeginDragDropTarget())
		return;

	AcceptPayloads(p_folder);
	ImGui::EndDragDropTarget();
}

void OvEditor::Panels::AssetBrowser::AcceptPayloads(const std::filesystem::path& p_folder)
{
	if (const auto* payload = ImGui::AcceptDragDropPayload("File"))
		MoveInto(p_folder, static_cast<const AssetPayload*>(payload->Data)->first, false);
	else if (const auto* folderPayload = ImGui::AcceptDragDropPayload("Folder"))
		MoveInto(p_folder, static_cast<const AssetPayload*>(folderPayload->Data)->first, true);
	else if (const auto* actorPayload = ImGui::AcceptDragDropPayload("Actor"))
	{
		// Dropping an entity from the Hierarchy creates a prefab, as in Unity.
		if (auto* actor = static_cast<const ActorPayload*>(actorPayload->Data)->first; actor && ValidateFolderPath(p_folder, "Create prefab"))
		{
			const std::string actorName = actor->GetName().empty() ? "Prefab" : actor->GetName();
			const auto prefabPath = FindAvailableFilePath(p_folder / (actorName + ".ovprefab"));
			EDITOR_EXEC(SaveActorAsPrefab(*actor, prefabPath.string()));
			Select(prefabPath);
			Refresh();
		}
	}
}


OvUI::Plugins::ContextualMenu& OvEditor::Panels::AssetBrowser::FolderMenu(const std::filesystem::path& p_folder)
{
	auto& menu = m_state->folderMenus[p_folder];
	if (!menu)
	{
		menu = std::make_unique<FolderContextualMenu>(p_folder.string(), IsEngine(p_folder));
		menu->removable = p_folder != m_state->projectRoot && p_folder != m_state->engineRoot;
		menu->CreateList();
		Wire(*menu, true, false);
	}
	return *menu;
}

void OvEditor::Panels::AssetBrowser::Wire(OvUI::Plugins::ContextualMenu& p_menu, bool p_directory, bool p_script)
{
	auto* menu = dynamic_cast<BrowserItemContextualMenu*>(&p_menu);
	if (!menu) return;

	menu->DestroyedEvent += [this](std::filesystem::path p_path) { OnDestroyed(p_path); };
	menu->RenamedEvent += [this, p_directory, p_script](std::filesystem::path p_previous, std::filesystem::path p_next)
	{
		OnRenamed(p_previous, p_next, p_directory, p_script);
	};
	if (auto* file = dynamic_cast<FileContextualMenu*>(menu))
		file->DuplicateEvent += [this](std::filesystem::path p_path) { Refresh(); Select(p_path); };
	if (auto* folder = dynamic_cast<FolderContextualMenu*>(menu))
		folder->ItemAddedEvent += [this](std::filesystem::path p_path)
		{
			// Items created from the tree reveal their folder, like Unity.
			const auto added = p_path.lexically_normal();
			if (std::filesystem::is_directory(added) && added.parent_path() != m_state->current) ShowFolder(added.parent_path());
			else if (!std::filesystem::is_directory(added) && added.parent_path() != m_state->current) ShowFolder(added.parent_path());
			Refresh();
			Select(added);
		};
}

void OvEditor::Panels::AssetBrowser::DrawBrowser()
{
	auto& state = *m_state;
	// Never rebuild while a drag is in flight: payloads reference entry strings.
	if (state.dirty && !ImGui::GetDragDropPayload())
		Rebuild();

	const float scale = OVUI_SCALE;
	DrawToolbar();

	const float footer = ImGui::GetFrameHeightWithSpacing();
	const float height = std::max(ImGui::GetContentRegionAvail().y - footer, 40.0f * scale);
	const float available = ImGui::GetContentRegionAvail().x;
	// Clamp only the drawn width: a narrow layout must not shrink the preferred width for good.
	const float treeWidth = std::clamp(state.treeWidth, 100.0f * scale, std::max(100.0f * scale, available - 160.0f * scale));

	ImGui::BeginChild("##FolderTree", { treeWidth, height });
	DrawFolderTree();
	ImGui::EndChild();

	// Draggable splitter between the folder tree and the content area.
	ImGui::SameLine(0.0f, 0.0f);
	ImGui::InvisibleButton("##Splitter", { 6.0f * scale, height });
	const bool splitterActive = ImGui::IsItemActive();
	if (ImGui::IsItemHovered() || splitterActive) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
	if (splitterActive) state.treeWidth = treeWidth + ImGui::GetIO().MouseDelta.x;
	{
		const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
		const float x = (min.x + max.x) * 0.5f;
		ImGui::GetWindowDrawList()->AddLine({ x, min.y }, { x, max.y },
			ImGui::GetColorU32(splitterActive ? ImGuiCol_SeparatorActive : ImGui::IsItemHovered() ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator), scale);
	}

	ImGui::SameLine(0.0f, 0.0f);
	ImGui::BeginChild("##Content", { 0.0f, height });
	DrawContent();
	ImGui::EndChild();

	DrawFooter();
}

void OvEditor::Panels::AssetBrowser::DrawToolbar()
{
	auto& state = *m_state;
	const float scale = OVUI_SCALE;
	const bool engine = IsEngine(state.current);

	if (ImGui::Button("+##Create", { ImGui::GetFrameHeight() * 1.4f, 0.0f })) ImGui::OpenPopup("##CreateMenu");
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip(engine ? "Engine folders are read-only" : "Create assets in the current folder");
	if (ImGui::BeginPopup("##CreateMenu"))
	{
		FolderMenu(state.current).DrawWidgets();
		ImGui::EndPopup();
	}

	// Breadcrumbs: every segment navigates and accepts drops, like Unity's path bar.
	const auto& root = engine ? state.engineRoot : state.projectRoot;
	std::vector<std::filesystem::path> crumbs;
	for (auto folder = state.current; IsPathSameOrDescendant(folder, root); folder = folder.parent_path())
	{
		crumbs.push_back(folder);
		if (folder == root) break;
	}
	std::reverse(crumbs.begin(), crumbs.end());

	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f });
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
	for (size_t index = 0; index < crumbs.size(); ++index)
	{
		ImGui::SameLine(0.0f, index == 0 ? 8.0f * scale : 0.0f);
		if (index > 0)
		{
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled(">");
			ImGui::SameLine(0.0f, 0.0f);
		}

		const std::string label = (crumbs[index] == root ? std::string(engine ? "Engine" : "Assets") : crumbs[index].filename().string()) + "##Crumb" + std::to_string(index);
		if (ImGui::Button(label.c_str())) ShowFolder(crumbs[index]);
		AcceptDrops(crumbs[index]);
	}
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();

	const float searchWidth = std::min(220.0f * scale, ImGui::GetContentRegionAvail().x * 0.5f);
	ImGui::SameLine();
	ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - searchWidth));
	ImGui::SetNextItemWidth(searchWidth);
	if (ImGui::InputTextWithHint("##Search", engine ? "Search Engine" : "Search Assets", state.search.data(), state.search.size()))
		Refresh();
}

void OvEditor::Panels::AssetBrowser::DrawFolderTree()
{
	DrawFolderNode(m_state->projectRoot, "Assets");
	DrawFolderNode(m_state->engineRoot, "Engine");
}

void OvEditor::Panels::AssetBrowser::DrawFolderNode(const std::filesystem::path& p_folder, const std::string& p_label)
{
	auto& state = *m_state;
	const auto& children = state.Subfolders(p_folder);

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
	if (state.current == p_folder) flags |= ImGuiTreeNodeFlags_Selected;

	ImGui::SetNextItemOpen(state.expanded.contains(p_folder), ImGuiCond_Always);
	const bool open = ImGui::TreeNodeEx(p_folder.string().c_str(), flags, "%s", "");
	const bool toggled = ImGui::IsItemToggledOpen();
	if (toggled)
	{
		if (open) state.expanded.insert(p_folder);
		else state.expanded.erase(p_folder);
	}

	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !toggled && state.current != p_folder)
	{
		state.current = p_folder;
		state.search.fill('\0');
		state.selected.clear();
		Refresh();
	}

	FolderMenu(p_folder).Execute(OvUI::Plugins::EPluginExecutionContext::WIDGET);
	AcceptDrops(p_folder);

	if (p_folder != state.projectRoot && p_folder != state.engineRoot && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover | ImGuiDragDropFlags_SourceNoHoldToOpenOthers))
	{
		state.dragPayload = { EDITOR_EXEC(GetResourcePath(p_folder.string(), IsEngine(p_folder))), nullptr };
		ImGui::SetDragDropPayload("Folder", &state.dragPayload, sizeof(state.dragPayload));
		ImGui::TextUnformatted(OvTools::Utils::PathParser::GetFriendlyPath(state.dragPayload.first).c_str());
		ImGui::EndDragDropSource();
	}

	// Icon and label after the arrow.
	const ImVec2 min = ImGui::GetItemRectMin();
	const float lineHeight = ImGui::GetTextLineHeight();
	const float x = min.x + ImGui::GetTreeNodeToLabelSpacing();
	auto* drawList = ImGui::GetWindowDrawList();
	state.folderIcon.RetainForCapture();
	drawList->AddImage(state.folderIcon.id, { x, min.y }, { x + lineHeight, min.y + lineHeight }, { 0.0f, 1.0f }, { 1.0f, 0.0f });
	drawList->AddText({ x + lineHeight + 4.0f * OVUI_SCALE, min.y }, ImGui::GetColorU32(ImGuiCol_Text), p_label.c_str());

	if (open)
	{
		for (const auto& child : children)
			DrawFolderNode(child, child.filename().string());
		ImGui::TreePop();
	}
}

void OvEditor::Panels::AssetBrowser::DrawContent()
{
	auto& state = *m_state;
	const float scale = OVUI_SCALE;
	const bool listMode = state.tileSize <= kListModeTileSize;
	auto* drawList = ImGui::GetWindowDrawList();

	const auto activate = [this](Entry& p_entry)
	{
		if (p_entry.directory) ShowFolder(p_entry.path);
		else OvCore::Helpers::GUIHelpers::Open(p_entry.resourcePath);
	};

	const auto handleItem = [&](Entry& p_entry, bool p_hovered)
	{
		if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right))
		{
			state.selected = p_entry.Key();
			if (!p_entry.directory)
				EDITOR_PANEL(OvEditor::Panels::Inspector, "Inspector").FocusAsset(p_entry.resourcePath);
		}
		if (p_hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) activate(p_entry);
		if (state.scrollToSelection && state.selected == p_entry.Key())
		{
			ImGui::SetScrollHereY(0.5f);
			state.scrollToSelection = false;
		}

		p_entry.menu->Execute(OvUI::Plugins::EPluginExecutionContext::WIDGET);

		if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover | ImGuiDragDropFlags_SourceNoHoldToOpenOthers))
		{
			ImGui::SetDragDropPayload(p_entry.directory ? "Folder" : "File", &p_entry.payload, sizeof(p_entry.payload));
			ImGui::TextUnformatted(OvTools::Utils::PathParser::GetFriendlyPath(p_entry.resourcePath).c_str());
			ImGui::EndDragDropSource();
		}

		if (p_entry.directory) AcceptDrops(p_entry.path);

		if (p_hovered && !ImGui::GetDragDropPayload() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
			ImGui::SetTooltip("%s\n%s", p_entry.name.c_str(), OvTools::Utils::PathParser::GetFriendlyPath(p_entry.resourcePath).c_str());
	};

	// Texture assets show their own pixels, other assets their type icon.
	const auto image = [&](Entry& p_entry, ImVec2 p_min, ImVec2 p_max, bool p_thumbnail)
	{
		using EFileType = OvTools::Utils::PathParser::EFileType;
		if (p_thumbnail && p_entry.type == EFileType::TEXTURE && !p_entry.thumbnailRequested && ImGui::IsRectVisible(p_min, p_max))
		{
			p_entry.thumbnailRequested = true;
			p_entry.thumbnail = OVSERVICE(OvCore::ResourceManagement::TextureManager)[p_entry.resourcePath];
		}

		OvUI::Internal::TextureID texture = p_entry.icon;
		float aspect = 1.0f;
		if (p_thumbnail && p_entry.thumbnail)
		{
			texture = OvUI::Internal::TextureID{ p_entry.thumbnail->RetainTexture() };
			const auto& desc = p_entry.thumbnail->GetTexture().GetDesc();
			if (desc.width > 0 && desc.height > 0) aspect = static_cast<float>(desc.width) / static_cast<float>(desc.height);
		}
		else if (p_thumbnail)
		{
			// Type icons read better a little smaller than the thumbnail frame.
			const float inset = (p_max.x - p_min.x) * 0.18f;
			p_min = { p_min.x + inset, p_min.y + inset };
			p_max = { p_max.x - inset, p_max.y - inset };
		}

		ImVec2 size{ p_max.x - p_min.x, p_max.y - p_min.y };
		if (aspect > 1.0f) size.y /= aspect; else size.x *= aspect;
		const ImVec2 center{ (p_min.x + p_max.x) * 0.5f, (p_min.y + p_max.y) * 0.5f };
		texture.RetainForCapture();
		drawList->AddImage(texture.id, { center.x - size.x * 0.5f, center.y - size.y * 0.5f }, { center.x + size.x * 0.5f, center.y + size.y * 0.5f }, { 0.0f, 1.0f }, { 1.0f, 0.0f });
	};

	const auto modelToggle = [&](Entry& p_entry, ImVec2 p_position)
	{
		if (p_entry.type != OvTools::Utils::PathParser::EFileType::MODEL || p_entry.embedded) return;
		const bool expanded = state.expandedModels.contains(p_entry.Key());
		ImGui::SetCursorScreenPos(p_position);
		if (ImGui::ArrowButton("##SubAssets", expanded ? ImGuiDir_Left : ImGuiDir_Right))
		{
			if (expanded) state.expandedModels.erase(p_entry.Key());
			else state.expandedModels.insert(p_entry.Key());
			Refresh();
		}
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip(expanded ? "Hide embedded materials and textures" : "Show embedded materials and textures");
	};

	if (state.entries.empty())
	{
		const char* message = state.search[0] ? "No assets match the search" : "This folder is empty";
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 12.0f * scale);
		ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(message).x) * 0.5f);
		ImGui::TextDisabled("%s", message);
	}
	else if (listMode)
	{
		const float rowHeight = ImGui::GetTextLineHeight() + 4.0f * scale;
		for (auto& entry : state.entries)
		{
			ImGui::PushID(entry->Key().c_str());
			const ImVec2 min = ImGui::GetCursorScreenPos();
			const float width = ImGui::GetContentRegionAvail().x;
			const bool selected = state.selected == entry->Key();
			ImGui::SetNextItemAllowOverlap();
			ImGui::InvisibleButton("##Row", { width, rowHeight }, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
			const bool hovered = ImGui::IsItemHovered();
			handleItem(*entry, hovered);

			const ImVec2 max{ min.x + width, min.y + rowHeight };
			if (selected || hovered) drawList->AddRectFilled(min, max, ImGui::GetColorU32(selected ? ImGuiCol_Header : ImGuiCol_HeaderHovered));
			const float indent = entry->embedded ? 18.0f * scale : 4.0f * scale;
			const float icon = ImGui::GetTextLineHeight();
			image(*entry, { min.x + indent, min.y + 2.0f * scale }, { min.x + indent + icon, min.y + 2.0f * scale + icon }, false);
			drawList->AddText({ min.x + indent + icon + 6.0f * scale, min.y + 2.0f * scale }, ImGui::GetColorU32(ImGuiCol_Text), entry->name.c_str());
			modelToggle(*entry, { max.x - ImGui::GetFrameHeight() - 2.0f * scale, min.y + (rowHeight - ImGui::GetFrameHeight()) * 0.5f });

			ImGui::SetCursorScreenPos(min);
			ImGui::Dummy({ width, rowHeight });
			ImGui::PopID();
		}
	}
	else
	{
		const float tile = state.tileSize * scale;
		const float spacing = 8.0f * scale;
		const float lineHeight = ImGui::GetTextLineHeight();
		const ImVec2 size{ tile, tile + lineHeight + 6.0f * scale };
		const int columns = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + spacing) / (tile + spacing)));

		ImGui::Dummy({ 0.0f, 2.0f * scale });
		for (size_t index = 0; index < state.entries.size(); ++index)
		{
			auto& entry = *state.entries[index];
			if (index % columns != 0) ImGui::SameLine(0.0f, spacing);
			else ImGui::SetCursorPosX(ImGui::GetCursorPosX() + spacing * 0.5f);

			ImGui::PushID(entry.Key().c_str());
			const ImVec2 min = ImGui::GetCursorScreenPos();
			const ImVec2 max{ min.x + size.x, min.y + size.y };
			const bool selected = state.selected == entry.Key();
			ImGui::SetNextItemAllowOverlap();
			ImGui::InvisibleButton("##Tile", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
			const bool hovered = ImGui::IsItemHovered();
			handleItem(entry, hovered);

			const float rounding = 4.0f * scale;
			const ImVec2 imageMax{ max.x, min.y + tile };
			if (entry.embedded) drawList->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_FrameBg), rounding);
			if (selected) drawList->AddRectFilled(min, imageMax, ImGui::GetColorU32(ImGuiCol_Header, 0.45f), rounding);
			else if (hovered) drawList->AddRectFilled(min, imageMax, ImGui::GetColorU32(ImGuiCol_Text, 0.06f), rounding);
			image(entry, { min.x + 6.0f * scale, min.y + 6.0f * scale }, { imageMax.x - 6.0f * scale, imageMax.y - 4.0f * scale }, true);

			// Name below the thumbnail: centered when it fits, otherwise elided; selection highlights it.
			const float labelY = min.y + tile + 2.0f * scale;
			const float textWidth = ImGui::CalcTextSize(entry.name.c_str()).x;
			const float labelWidth = std::min(textWidth, size.x - 4.0f * scale);
			const float labelX = min.x + (size.x - labelWidth) * 0.5f;
			if (selected) drawList->AddRectFilled({ labelX - 3.0f * scale, labelY - 1.0f * scale }, { labelX + labelWidth + 3.0f * scale, labelY + lineHeight + 1.0f * scale }, ImGui::GetColorU32(ImGuiCol_Header), rounding);
			if (textWidth <= labelWidth) drawList->AddText({ labelX, labelY }, ImGui::GetColorU32(ImGuiCol_Text), entry.name.c_str());
			else ImGui::RenderTextEllipsis(drawList, { labelX, labelY }, { labelX + labelWidth, labelY + lineHeight }, labelX + labelWidth, entry.name.c_str(), nullptr, nullptr);

			modelToggle(entry, { imageMax.x - ImGui::GetFrameHeight(), min.y + (tile - ImGui::GetFrameHeight()) * 0.5f });

			ImGui::SetCursorScreenPos(min);
			ImGui::Dummy(size);
			ImGui::PopID();
		}
		ImGui::Dummy({ 0.0f, spacing });
	}

	// Empty space: click clears the selection, right-click opens the folder menu, drops move here.
	const bool backgroundHovered = ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered();
	if (backgroundHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) state.selected.clear();
	if (backgroundHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) ImGui::OpenPopup("##FolderContext");
	if (ImGui::BeginPopup("##FolderContext"))
	{
		FolderMenu(state.current).DrawWidgets();
		ImGui::EndPopup();
	}

	if (auto* window = ImGui::GetCurrentWindow(); !IsEngine(state.current) && ImGui::BeginDragDropTargetCustom(window->InnerRect, ImGui::GetID("##ContentDrop")))
	{
		AcceptPayloads(state.current);
		ImGui::EndDragDropTarget();
	}

	HandleShortcuts();
}

void OvEditor::Panels::AssetBrowser::HandleShortcuts()
{
	auto& state = *m_state;
	if (!ImGui::IsWindowFocused() || ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive())
		return;

	if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false) && state.current != state.projectRoot && state.current != state.engineRoot)
	{
		const auto previous = state.current;
		ShowFolder(state.current.parent_path());
		Select(previous);
		return;
	}

	const auto found = std::find_if(state.entries.begin(), state.entries.end(), [&](const auto& p_entry) { return p_entry->Key() == state.selected; });
	if (found == state.entries.end())
		return;

	auto& entry = **found;
	if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))
	{
		if (entry.directory) ShowFolder(entry.path);
		else OvCore::Helpers::GUIHelpers::Open(entry.resourcePath);
	}
	else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && !entry.engine && !entry.embedded)
	{
		if (auto* menu = dynamic_cast<BrowserItemContextualMenu*>(entry.menu.get()))
			menu->DeleteItem();
	}
}

void OvEditor::Panels::AssetBrowser::DrawFooter()
{
	auto& state = *m_state;
	const float scale = OVUI_SCALE;
	const float sliderWidth = 110.0f * scale;

	const auto found = std::find_if(state.entries.begin(), state.entries.end(), [&](const auto& p_entry) { return p_entry->Key() == state.selected; });
	const std::string status = found != state.entries.end()
		? OvTools::Utils::PathParser::GetFriendlyPath((*found)->resourcePath)
		: std::to_string(state.entries.size()) + (state.search[0] ? " results" : " items");

	ImGui::Dummy({ 0.0f, 0.0f });
	const ImVec2 start = ImGui::GetCursorScreenPos();
	const float textEnd = start.x + ImGui::GetContentRegionAvail().x - sliderWidth - 12.0f * scale;
	const float textY = start.y + ImGui::GetStyle().FramePadding.y;
	ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
	ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), { start.x, textY }, { textEnd, textY + ImGui::GetTextLineHeight() }, textEnd, status.c_str(), nullptr, nullptr);
	ImGui::PopStyleColor();

	ImGui::SetCursorScreenPos({ start.x + ImGui::GetContentRegionAvail().x - sliderWidth, start.y });
	ImGui::SetNextItemWidth(sliderWidth);
	ImGui::SliderFloat("##Zoom", &state.tileSize, kListModeTileSize, 128.0f, "");
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Icon size (smallest shows a list)");
}
