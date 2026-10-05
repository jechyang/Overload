/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <OvReflection/Annotations.h>

#include "OvCore/ECS/Components/AComponent.h"

#include "OvCore/ECS/Components/CPhysicalObject.h"

namespace OvCore::ECS { class Actor; }

namespace OvCore::ECS::Components
{
	/**
	* Represent a physical object with a sphere shape
	*/
	class OV_TYPE(Component, Factory = AddComponent, Alias = "class OvCore::ECS::Components::CPhysicalSphere", DisplayName = "Physical Sphere", Order = 3) CPhysicalSphere : public CPhysicalObject
	{
	public:
		/**
		* Constructor
		* @param p_owner
		*/
		CPhysicalSphere(ECS::Actor& p_owner);

		/**
		* Returns the name of the component
		*/
		std::string GetName() override;

		/**
		* Returns the type name of the component
		*/
		virtual std::string GetTypeName() override;

		/**
		* Defines the radius of the sphere shape
		* @param p_radius
		*/
		void SetRadius(float p_radius);

		/**
		* Returns the radius of the sphere shape
		*/
		OV_PROPERTY(Setter = SetRadius, Name = "radius", Serialize, Thread = Main, Order = 8, Missing = Default, Default = "0")
		float GetRadius() const;

		/**
		* Defines how the component should be drawn in the inspector
		* @param p_root
		*/
		virtual void OnInspector(OvUI::Internal::WidgetContainer& p_root) override;
	};

	template<>
	struct ComponentTraits<OvCore::ECS::Components::CPhysicalSphere>
	{
		static constexpr std::string_view Name = "class OvCore::ECS::Components::CPhysicalSphere";
	};
}