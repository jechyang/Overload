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
	* Represent a physical object with a capsule hape
	*/
	class OV_TYPE(Component, Factory = AddComponent, Alias = "class OvCore::ECS::Components::CPhysicalCapsule", DisplayName = "Physical Capsule", Order = 4) CPhysicalCapsule : public CPhysicalObject
	{
	public:
		/**
		* Constructor
		* @param p_owner
		*/
		CPhysicalCapsule(ECS::Actor& p_owner);

		/**
		* Returns the name of the component
		*/
		std::string GetName() override;

		/**
		* Returns the type name of the component
		*/
		virtual std::string GetTypeName() override;

		/**
		* Defines the radius of the capsule shape
		* @param p_radius
		*/
		void SetRadius(float p_radius);

		/**
		* Defines the height of the capsule shape
		* @param p_height
		*/
		void SetHeight(float p_height);

		/**
		* Returns the radius of the capsule shape
		*/
		OV_PROPERTY(Setter = SetRadius, Name = "radius", Serialize, Thread = Main, Order = 8, Missing = Default, Default = "0")
		float GetRadius() const;

		/**
		* Returns the height of the capsule shape
		*/
		OV_PROPERTY(Setter = SetHeight, Name = "height", Serialize, Thread = Main, Order = 9, Missing = Default, Default = "0")
		float GetHeight() const;

		/**
		* Defines how the component should be drawn in the inspector
		* @param p_root
		*/
		virtual void OnInspector(OvUI::Internal::WidgetContainer& p_root) override;
	};

	template<>
	struct ComponentTraits<OvCore::ECS::Components::CPhysicalCapsule>
	{
		static constexpr std::string_view Name = "class OvCore::ECS::Components::CPhysicalCapsule";
	};
}