/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <cstdint>
#include <filesystem>
#include <variant>

#include <baregl/Buffer.h>
#include <baregl/ResourceExecutor.h>
#include <OvRendering/Threading/RenderThread.h>

#include <OvAudio/Core/AudioEngine.h>
#include <OvCore/ResourceManagement/MaterialManager.h>
#include <OvCore/ResourceManagement/ModelManager.h>
#include <OvCore/ResourceManagement/ShaderManager.h>
#include <OvCore/ResourceManagement/SoundManager.h>
#include <OvCore/ResourceManagement/TextureManager.h>
#include <OvCore/SceneSystem/SceneManager.h>
#include <OvCore/Scripting/ScriptEngine.h>
#include <OvCore/Scripting/CSharp/RuntimeHost.h>
#include <OvEditor/Core/EditorResources.h>
#include <OvPhysics/Core/PhysicsEngine.h>
#include <OvTools/Filesystem/IniFile.h>
#include <OvTools/Jobs/JobSystem.h>
#include <OvWindowing/Window.h>
#include <OvUI/Core/UIManager.h>
#include <OvWindowing/Context/Device.h>
#include <OvWindowing/Inputs/InputManager.h>

namespace OvEditor::Core
{
	/**
	* The Context handle the engine features setup
	*/
	class Context
	{
	public:
		struct ActorCopyBuffer
		{
			uint64_t guid = 0;
		};

		using CopyBuffer = std::variant<std::monostate, ActorCopyBuffer>;

		/**
		* Constructor
		* @param p_projectFolder (including the .ovproject file)
		*/
		Context(const std::filesystem::path& p_projectFolder);

		/**
		* Destructor
		*/
		virtual ~Context();

		/**
		* Reset project settings ini file
		*/
		void ResetProjectSettings();

		/**
		* Verify that project settings are complete (No missing key).
		* Returns true if the integrity is verified
		*/
		bool IsProjectSettingsIntegrityVerified();

		/**
		* Apply project settings to the ini file
		*/
		void ApplyProjectSettings();

	public:
		const std::filesystem::path projectFolder;
		const std::filesystem::path projectFile;
		const std::filesystem::path engineAssetsPath;
		const std::filesystem::path projectAssetsPath;
		const std::filesystem::path editorAssetsPath;

		/**
		* Job system used by the renderers for parallel CPU-side work.
		* Declared before every other service so that it outlives them
		* (members are destroyed in reverse declaration order)
		*/
		OvReflection::MainThread scriptingMain;
		std::unique_ptr<OvTools::Jobs::JobSystem> jobSystem;

		std::unique_ptr<OvWindowing::Context::Device> device;
		std::unique_ptr<OvWindowing::Window> window;
		std::unique_ptr<OvWindowing::Inputs::InputManager> inputManager;
		// Resources/UI die before the producer scope, Render owner and window.
        std::unique_ptr<OvRendering::Threading::RenderThread> renderThread;
        std::unique_ptr<baregl::ResourceExecutor> resourceExecutor;
        // Owning in legacy mode; RenderThread owns the Driver in threaded mode.
        std::shared_ptr<OvRendering::Context::Driver> driver;
		std::unique_ptr<OvUI::Core::UIManager> uiManager;
		std::unique_ptr<OvPhysics::Core::PhysicsEngine> physicsEngine;
		std::unique_ptr<OvAudio::Core::AudioEngine> audioEngine;
		std::unique_ptr<OvEditor::Core::EditorResources> editorResources;

		std::unique_ptr<OvCore::Scripting::ScriptEngine> scriptEngine;

		OvCore::SceneSystem::SceneManager sceneManager;

		OvCore::ResourceManagement::ModelManager modelManager;
		OvCore::ResourceManagement::TextureManager textureManager;
		OvCore::ResourceManagement::ShaderManager shaderManager;
		OvCore::ResourceManagement::MaterialManager materialManager;
		OvCore::ResourceManagement::SoundManager soundManager;

		OvWindowing::Settings::WindowSettings windowSettings;

		OvTools::Filesystem::IniFile projectSettings;

		CopyBuffer copyBuffer;
		// Declared last: also detaches before scene/services during constructor unwinding.
		std::unique_ptr<OvCore::Scripting::CSharp::RuntimeHost> csharpHost;
	};
}
