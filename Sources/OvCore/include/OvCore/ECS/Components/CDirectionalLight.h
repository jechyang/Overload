/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <OvReflection/Annotations.h>

#include "OvCore/ECS/Components/CLight.h"

namespace OvCore::ECS { class Actor; }

namespace OvCore::ECS::Components
{
	/**
	* A simple light that has no attenuation and that has a direction
	*/
	class OV_TYPE(Component, Factory = AddComponent, Alias = "class OvCore::ECS::Components::CDirectionalLight", DisplayName = "Directional Light", Order = 6) CDirectionalLight : public CLight
	{
	public:
		/**
		* Constructor
		* @param p_owner
		*/
		CDirectionalLight(ECS::Actor& p_owner);

		/**
		* Returns the name of the component
		*/
		std::string GetName() override;

		/**
		* Returns the type name of the component
		*/
		virtual std::string GetTypeName() override;

		/**
		* Set if the light should cast shadows
		* @param p_enabled
		*/
		void SetCastShadows(bool p_enabled);

		/**
		* Returns true if the light should cast shadows
		*/
		OV_PROPERTY(Setter = SetCastShadows, Name = "cast_shadows", Serialize, Thread = Main, Order = 2, Missing = Default, Default = "false")
		bool GetCastShadows() const;

		/**
		* Defines the area size of the shadow
		* @param p_shadowAreaSize
		*/
		void SetShadowAreaSize(float p_shadowAreaSize);

		/**
		* Returns the area size of the shadow
		*/
		OV_PROPERTY(Setter = SetShadowAreaSize, Name = "shadow_area_size", Serialize, Thread = Main, Order = 3, Missing = Default, Default = "0")
		float GetShadowAreaSize() const;

		/**
		* Defines whether or not the light position should snap to the camera position
		* @param p_enabled
		*/
		void SetShadowFollowCamera(bool p_enabled);

		/**
		* Returns true if the light position should snap to the camera position
		*/
		OV_PROPERTY(Setter = SetShadowFollowCamera, Name = "shadow_follow_camera", Serialize, Thread = Main, Order = 4, Missing = Default, Default = "false")
		bool GetShadowFollowCamera() const;

		/**
		* Sets the shadow map resolution
		* @note The resolution should be a power of 2 for better results
		* @param p_resolution
		*/
		void SetShadowMapResolution(uint32_t p_resolution);

		/**
		* Returns the shadow map resolution
		*/
		OV_PROPERTY(Setter = SetShadowMapResolution, Name = "shadow_map_resolution", Serialize, Thread = Main, Order = 5, Missing = Default, Default = "0")
		uint32_t GetShadowMapResolution() const;

		/**
		* Defines how the component should be drawn in the inspector
		* @param p_root
		*/
		virtual void OnInspector(OvUI::Internal::WidgetContainer& p_root) override;
	};

	template<>
	struct ComponentTraits<OvCore::ECS::Components::CDirectionalLight>
	{
		static constexpr std::string_view Name = "class OvCore::ECS::Components::CDirectionalLight";
	};
}