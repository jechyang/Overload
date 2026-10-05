/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <OvReflection/Annotations.h>
#include <OvCore/Resources/ResourceReference.h>

#include <OvRendering/Geometry/Vertex.h>
#include <OvRendering/Resources/Model.h>

#include "OvCore/ECS/Components/AComponent.h"

namespace OvCore::ECS { class Actor; }

namespace OvCore::ECS::Components
{
	/**
	* A ModelRenderer is necessary in combination with a MaterialRenderer to render a model in the world
	*/
	class OV_TYPE(Component, Factory = AddComponent, Alias = "class OvCore::ECS::Components::CModelRenderer", DisplayName = "Model Renderer", Order = 0) CModelRenderer : public AComponent
	{
	public:
		/**
		* Defines how the model renderer bounding sphere should be interpreted
		*/
		enum class OV_TYPE() EFrustumBehaviour
		{
			DISABLED = 0,
			DEPRECATED_MODEL_BOUNDS = 1, // This is not used anymore, but the enum value is kept for compatibility
			MESH_BOUNDS = 2,
			CUSTOM_BOUNDS = 3
		};

		/**
		* Constructor
		* @param p_owner
		*/
		CModelRenderer(ECS::Actor& p_owner);

		/**
		* Returns the name of the component
		*/
		std::string GetName() override;

		/**
		* Returns the type name of the component
		*/
		virtual std::string GetTypeName() override;

		/**
		* Defines the model to use
		* @param p_model
		*/
		void SetModel(OvRendering::Resources::Model* p_model);

		/**
		* Returns the current model
		*/
		OvRendering::Resources::Model* GetModel() const;

		/**
		* Sets a bounding mode
		* @param p_boundingMode
		*/
		void SetFrustumBehaviour(EFrustumBehaviour p_boundingMode);

		/**
		* Returns the current bounding mode
		*/
		OV_PROPERTY(Serialize, Name = "frustum_behaviour", Setter = SetFrustumBehaviour, Order = 1)
		EFrustumBehaviour GetFrustumBehaviour() const;

		/**
		* Returns the custom bounding sphere
		*/
		const OvRendering::Geometry::BoundingSphere& GetCustomBoundingSphere() const;

		/**
		* Sets the custom bounding sphere
		* @param p_boundingSphere
		*/
		void SetCustomBoundingSphere(const OvRendering::Geometry::BoundingSphere& p_boundingSphere);


		/**
		* Defines how the component should be drawn in the inspector
		* @param p_root
		*/
		virtual void OnInspector(OvUI::Internal::WidgetContainer& p_root) override;

	private:
        friend struct OvReflection::Access<CModelRenderer>;
        OV_PROPERTY(Serialize, Name = "model", Setter = SetSerializedModel, Missing = Default, Default = "?", Order = 0)
        Resources::ResourceReference<OvRendering::Resources::Model> GetSerializedModel() const;
        void SetSerializedModel(const Resources::ResourceReference<OvRendering::Resources::Model>& value);
        OV_PROPERTY(Serialize, Name = "custom_bounding_sphere_position", Setter = SetSerializedBoundsPosition, Order = 2)
        OvMaths::FVector3 GetSerializedBoundsPosition() const;
        void SetSerializedBoundsPosition(const OvMaths::FVector3& value);
        OV_PROPERTY(Serialize, Name = "custom_bounding_sphere_radius", Setter = SetSerializedBoundsRadius, Order = 3)
        float GetSerializedBoundsRadius() const;
        void SetSerializedBoundsRadius(float value);
        std::string m_modelPath;
		OvRendering::Resources::Model* m_model = nullptr;
		OvTools::Eventing::Event<> m_modelChangedEvent;
		OvRendering::Geometry::BoundingSphere m_customBoundingSphere = { {}, 1.0f };
		EFrustumBehaviour m_frustumBehaviour = EFrustumBehaviour::MESH_BOUNDS;
	};

	template<>
	struct ComponentTraits<OvCore::ECS::Components::CModelRenderer>
	{
		static constexpr std::string_view Name = "class OvCore::ECS::Components::CModelRenderer";
	};
}
