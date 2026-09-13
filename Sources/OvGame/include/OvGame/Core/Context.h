/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <OvPhysics/Core/PhysicsEngine.h>

#include <OvWindowing/Context/Device.h>
#include <OvWindowing/Inputs/InputManager.h>
#include <OvWindowing/Window.h>

#include <OvUI/Core/UIManager.h>

#include <OvCore/ResourceManagement/ModelManager.h>
#include <OvCore/ResourceManagement/TextureManager.h>
#include <OvCore/ResourceManagement/ShaderManager.h>
#include <OvCore/ResourceManagement/MaterialManager.h>
#include <OvCore/ResourceManagement/SoundManager.h>
#include <OvCore/SceneSystem/SceneManager.h>
#include <OvCore/Scripting/ScriptEngine.h>
#include <OvCore/Scripting/CSharp/RuntimeHost.h>

#include <OvAudio/Core/AudioEngine.h>

#include <OvTools/Filesystem/IniFile.h>
#include <OvTools/Jobs/JobSystem.h>
#include <OvRendering/Threading/RenderThread.h>
#include <baregl/ResourceExecutor.h>

namespace OvGame::Core
{
	/**
	* The Context handle the engine features setup
	*/
	class Context
	{
	public:
		/**
		* Constructor
		*/
		Context();

		/**
		* Destructor
		*/
		~Context();

	public:
		const std::filesystem::path engineAssetsPath;
		const std::filesystem::path projectAssetsPath;

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
		// Reverse destruction keeps the resource dispatcher and GL owner alive
		// through all services/resources, then destroys the window last.
		std::unique_ptr<OvRendering::Threading::RenderThread> renderThread;
		std::unique_ptr<baregl::ResourceExecutor> resourceExecutor;
		std::shared_ptr<OvRendering::Context::Driver> driver;
		std::unique_ptr<OvUI::Core::UIManager> uiManager;
		std::unique_ptr<OvPhysics::Core::PhysicsEngine> physicsEngine;
		std::unique_ptr<OvAudio::Core::AudioEngine> audioEngine;
		std::unique_ptr<OvCore::Scripting::ScriptEngine> scriptEngine;
		std::unique_ptr<baregl::Framebuffer> framebuffer;

		OvCore::SceneSystem::SceneManager sceneManager;

		OvCore::ResourceManagement::ModelManager modelManager;
		OvCore::ResourceManagement::TextureManager textureManager;
		OvCore::ResourceManagement::ShaderManager shaderManager;
		OvCore::ResourceManagement::MaterialManager materialManager;
		OvCore::ResourceManagement::SoundManager soundManager;
		
		OvTools::Filesystem::IniFile projectSettings;
		// Declared last: also detaches before scene/services during constructor unwinding.
		std::unique_ptr<OvCore::Scripting::CSharp::RuntimeHost> csharpHost;
	};
}
