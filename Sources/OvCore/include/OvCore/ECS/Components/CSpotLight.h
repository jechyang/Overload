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
	* A light that is localized, has attenuation and has a direction
	*/
	class OV_TYPE(Component, Factory = AddComponent, Alias = "class OvCore::ECS::Components::CSpotLight", DisplayName = "Spot Light", Order = 7) CSpotLight : public CLight
	{
	public:
		/**
		* Constructor
		* @param p_owner
		*/
		CSpotLight(ECS::Actor& p_owner);

		/**
		* Returns the name of the component
		*/
		std::string GetName() override;

		/**
		* Returns the type name of the component
		*/
		virtual std::string GetTypeName() override;

		/**
		* Returns the light constant
		*/
		OV_PROPERTY(Setter = SetConstant, Name = "constant", Serialize, Thread = Main, Order = 2)
		float GetConstant() const;

		/**
		* Returns the light linear
		*/
		OV_PROPERTY(Setter = SetLinear, Name = "linear", Serialize, Thread = Main, Order = 3)
		float GetLinear() const;

		/**
		* Returns the light quadratic
		*/
		OV_PROPERTY(Setter = SetQuadratic, Name = "quadratic", Serialize, Thread = Main, Order = 4)
		float GetQuadratic() const;

		/**
		* Returns the light cutoff
		*/
		OV_PROPERTY(Setter = SetCutoff, Name = "cutoff", Serialize, Thread = Main, Order = 5)
		float GetCutoff() const;

		/**
		* Returns the light outercutoff
		*/
		OV_PROPERTY(Setter = SetOuterCutoff, Name = "outercutoff", Serialize, Thread = Main, Order = 6)
		float GetOuterCutoff() const;

		/**
		* Defines the light constant
		* @param p_constant
		*/
		void SetConstant(float p_constant);

		/**
		* Defines the light linear
		* @param p_linear
		*/
		void SetLinear(float p_linear);

		/**
		* Defines the light quadratic
		* @param p_quadratic
		*/
		void SetQuadratic(float p_quadratic);

		/**
		* Defines the light cutoff
		* @param p_cutoff
		*/
		void SetCutoff(float p_cutoff);

		/**
		* Defines the light outercutoff
		* @param p_cutoff
		*/
		void SetOuterCutoff(float p_outerCutoff);

		/**
		* Defines how the component should be drawn in the inspector
		* @param p_root
		*/
		virtual void OnInspector(OvUI::Internal::WidgetContainer& p_root) override;
	};

	template<>
	struct ComponentTraits<OvCore::ECS::Components::CSpotLight>
	{
		static constexpr std::string_view Name = "class OvCore::ECS::Components::CSpotLight";
	};
}