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
	* Represents an ambient light with a sphere shape
	*/
	class OV_TYPE(Component, Factory = AddComponent, Alias = "class OvCore::ECS::Components::CAmbientSphereLight", DisplayName = "Ambient Sphere Light", Order = 9) CAmbientSphereLight : public CLight
	{
	public:
		/**
		* Constructor
		* @param p_owner
		*/
		CAmbientSphereLight(ECS::Actor& p_owner);

		/**
		* Returns the name of the component
		*/
		std::string GetName() override;

		/**
		* Returns the type name of the component
		*/
		virtual std::string GetTypeName() override;

		/**
		* Returns the radius of the sphere
		*/
		OV_PROPERTY(Setter = SetRadius, Name = "radius", Serialize, Thread = Main, Order = 2)
		float GetRadius() const;

		/**
		* Defines the radius of the sphere
		* @param p_radius
		*/
		void SetRadius(float p_radius);

		/**
		* Defines how the component should be drawn in the inspector
		* @param p_root
		*/
		virtual void OnInspector(OvUI::Internal::WidgetContainer& p_root) override;
	};

	template<>
	struct ComponentTraits<OvCore::ECS::Components::CAmbientSphereLight>
	{
		static constexpr std::string_view Name = "class OvCore::ECS::Components::CAmbientSphereLight";
	};
}