/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <string>

namespace OvCore::ECS::Components { class CParticleSystem; }

namespace OvCore::ParticleSystem
{
	/**
	* Handles saving and loading particle system presets to/from .ovpart XML files.
	*/
	class ParticleSystemLoader
	{
	public:
		ParticleSystemLoader() = delete;

		/**
		* Apply settings from an .ovpart file to an existing component.
		* Returns true on success.
		*/
		static bool Load(OvCore::ECS::Components::CParticleSystem& p_target, const std::string& p_path);

		/**
		* Save the component's current settings to an .ovpart file.
		*/
		static void Save(OvCore::ECS::Components::CParticleSystem& p_target, const std::string& p_path);

		/**
		* Write a minimal default .ovpart file (PointEmitter, no affectors, no material).
		*/
		static void CreateDefault(const std::string& p_path);

		/**
		* Write a default .ovpart file with a CircleEmitter.
		*/
		static void CreateDefaultCircleEmitter(const std::string& p_path);
	};
}
