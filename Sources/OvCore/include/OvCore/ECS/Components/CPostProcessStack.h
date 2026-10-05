/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <OvReflection/Annotations.h>

#include "AComponent.h"

#include <OvCore/Rendering/PostProcess/PostProcessStack.h>
#include <OvCore/Rendering/PostProcess/BloomEffect.h>
#include <OvCore/Rendering/PostProcess/FXAAEffect.h>
#include <OvCore/Rendering/PostProcess/TonemappingEffect.h>
#include <OvCore/Rendering/PostProcess/AutoExposureEffect.h>

namespace OvCore::ECS { class Actor; }

namespace OvCore::ECS::Components
{
	/**
	* A component that handle a post process stack
	*/
	class OV_TYPE(Component, Factory = AddComponent, Alias = "class OvCore::ECS::Components::CPostProcessStack", DisplayName = "Post Process Stack", Order = 14) CPostProcessStack : public AComponent
	{
	public:
		/**
		* Constructor
		* @param p_owner
		*/
		CPostProcessStack(ECS::Actor& p_owner);

		/**
		* Returns the name of the component
		*/
		std::string GetName() override;

		/**
		* Returns the type name of the component
		*/
		virtual std::string GetTypeName() override;


		/**
		* Returns the post process setting stack
		*/
		const OvCore::Rendering::PostProcess::PostProcessStack& GetStack() const;

		/**
		* Returns the bloom settings
		*/
		OV_PROPERTY(Serialize, Inspect, Flatten, Prefix = "bloom_", Setter = SetBloomSettings, DisplayName = "Bloom", Order = 1)
		const OvCore::Rendering::PostProcess::BloomSettings& GetBloomSettings() const;

		/**
		* Returns the auto-exposure settings
		*/
		OV_PROPERTY(Serialize, Inspect, Flatten, Prefix = "auto_exposure_", Setter = SetAutoExposureSettings, DisplayName = "Auto Exposure", Order = 0)
		const OvCore::Rendering::PostProcess::AutoExposureSettings& GetAutoExposureSettings() const;

		/**
		* Returns the tonemapping settings
		*/
		OV_PROPERTY(Serialize, Inspect, Flatten, Prefix = "tonemapping_", Setter = SetTonemappingSettings, DisplayName = "Tonemapping", Order = 2)
		const OvCore::Rendering::PostProcess::TonemappingSettings& GetTonemappingSettings() const;

		/**
		* Returns the FXAA settings
		*/
		OV_PROPERTY(Serialize, Inspect, Flatten, Prefix = "fxaa_", Setter = SetFXAASettings, DisplayName = "FXAA", Order = 3)
		const OvCore::Rendering::PostProcess::FXAASettings& GetFXAASettings() const;

		/**
		* Sets the bloom settings
		* * @param p_settings
		*/
		void SetBloomSettings(const OvCore::Rendering::PostProcess::BloomSettings& p_settings);

		/**
		* Sets the auto exposure settings
		* * @param p_settings
		*/
		void SetAutoExposureSettings(const OvCore::Rendering::PostProcess::AutoExposureSettings& p_settings);

		/**
		* Sets the tonemapping settings
		* @param p_settings
		*/
		void SetTonemappingSettings(const OvCore::Rendering::PostProcess::TonemappingSettings& p_settings);

		/**
		* Sets the FXAA settings
		* @param p_settings
		*/
		void SetFXAASettings(const OvCore::Rendering::PostProcess::FXAASettings& p_settings);

	private:
		OvCore::Rendering::PostProcess::PostProcessStack m_settings;
	};

	template<>
	struct ComponentTraits<OvCore::ECS::Components::CPostProcessStack>
	{
		static constexpr std::string_view Name = "class OvCore::ECS::Components::CPostProcessStack";
	};
}