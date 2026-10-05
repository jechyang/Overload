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
	* Represents an ambient light with a box shape
	*/
	class OV_TYPE(Component, Factory = AddComponent, Alias = "class OvCore::ECS::Components::CAmbientBoxLight", DisplayName = "Ambient Box Light", Order = 8) CAmbientBoxLight : public CLight
	{
	public:
		/**
		* Constructor
		* @param p_owner
		*/
		CAmbientBoxLight(ECS::Actor& p_owner);

		/**
		* Returns the name of the component
		*/
		std::string GetName() override;

		/**
		* Returns the type name of the component
		*/
		virtual std::string GetTypeName() override;

		/**
		* Returns the size of the box
		*/
		OV_PROPERTY(Setter = SetSize, Name = "size", Serialize, Thread = Main, Order = 2, Missing = Default, Default = "0,0,0")
		OvMaths::FVector3 GetSize() const;

		/**
		* Defines the size of the box
		* @param p_size
		*/
		void SetSize(const OvMaths::FVector3& p_size);

		/**
		* Defines how the component should be drawn in the inspector
		* @param p_root
		*/
		virtual void OnInspector(OvUI::Internal::WidgetContainer& p_root) override;
	};

	template<>
	struct ComponentTraits<OvCore::ECS::Components::CAmbientBoxLight>
	{
		static constexpr std::string_view Name = "class OvCore::ECS::Components::CAmbientBoxLight";
	};
}