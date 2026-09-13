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
	* Represent a physical object with a box shape
	*/
	class OV_TYPE(Component, Factory = AddComponent, Alias = "class OvCore::ECS::Components::CPhysicalBox", DisplayName = "Physical Box", Order = 2) CPhysicalBox : public CPhysicalObject
	{
	public:
		/**
		* Constructor
		* @param p_owner
		*/
		CPhysicalBox(ECS::Actor& p_owner);

		/**
		* Returns the name of the component
		*/
		std::string GetName() override;

		/**
		* Returns the type name of the component
		*/
		virtual std::string GetTypeName() override;

		/**
		* Defines the size of the box shape
		* @param p_size
		*/
		void SetSize(const OvMaths::FVector3& p_size);

		/**
		* Returns the size of the box shape
		*/
		OV_PROPERTY(Setter = SetSize, Name = "size", Serialize, Inspect, Script, Thread = Main, DisplayName = "Size", Step = 0.1, Min = 0, Max = 100000, Missing = Default, Default = "0,0,0")
		OvMaths::FVector3 GetSize() const;

		/**
		* Serialize the component
		* @param p_doc
		* @param p_node
		*/
		virtual void OnSerialize(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node) override;

		/**
		* Deserialize the component
		* @param p_doc
		* @param p_node
		*/
		virtual void OnDeserialize(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node) override;

		/**
		* Defines how the component should be drawn in the inspector
		* @param p_root
		*/
		virtual void OnInspector(OvUI::Internal::WidgetContainer& p_root) override;
	};

	template<>
	struct ComponentTraits<OvCore::ECS::Components::CPhysicalBox>
	{
		static constexpr std::string_view Name = "class OvCore::ECS::Components::CPhysicalBox";
	};
}