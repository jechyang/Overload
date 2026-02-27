/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <OvCore/SceneSystem/Scene.h>
#include <OvCore/ParticleSystem/CParticleSystem.h>

#include "OvEditor/Panels/AViewControllable.h"

namespace OvEditor::Panels
{
	/**
	* Standalone particle editor: 3D preview viewport on the left,
	* property panel on the right. Not tied to any scene entity.
	*/
	class ParticleEditor : public AViewControllable
	{
	public:
		ParticleEditor(
			const std::string& p_title,
			bool p_opened,
			const OvUI::Settings::PanelWindowSettings& p_windowSettings
		);

		/**
		* Load a .ovpart file into the internal preview instance.
		*/
		void LoadFromFile(const std::string& p_path);

		/**
		* Copy settings from an existing CParticleSystem into the internal preview instance.
		*/
		void SetTarget(OvCore::ECS::Components::CParticleSystem& p_system);

		virtual OvCore::SceneSystem::Scene* GetScene() override;
		virtual void Update(float p_deltaTime) override;
		virtual const OvRendering::Data::FrameInfo& GetFrameInfo() const override;
		void Render();
		virtual void _Draw_Impl() override;

	private:
		void DrawProperties();

		OvCore::SceneSystem::Scene m_scene;
		OvCore::ECS::Actor* m_particleActor = nullptr;
		OvCore::ECS::Components::CParticleSystem* m_particleSystem = nullptr;

		bool  m_playing       = true;
		float m_viewportWidth  = 0.0f;
		float m_viewportHeight = 0.0f;
		std::string m_currentFilePath;

		static constexpr float kPropertiesWidth = 300.0f;
	};
}
