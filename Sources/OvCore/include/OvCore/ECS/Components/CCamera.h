/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <OvReflection/Annotations.h>

#include "OvRendering/Entities/Camera.h"
#include "AComponent.h"

namespace OvCore::ECS { class Actor; }

namespace OvCore::ECS::Components
{
	/**
	* Represents a camera entity. Its position will determine its view matrix
	*/
	class OV_TYPE(Component, Factory = AddComponent, Alias = "class OvCore::ECS::Components::CCamera", DisplayName = "Camera", Order = 1) CCamera : public AComponent
	{
	public:
		/**
		* Constructor
		* @param p_owner
		*/
		CCamera(ECS::Actor& p_owner);

		/**
		* Destructor
		*/
		~CCamera() = default;

		/**
		* Returns the name of the component
		*/
		std::string GetName() override;

		/**
		* Returns the type name of the component
		*/
		virtual std::string GetTypeName() override;

		/**
		* Sets the fov of the camera to the given value
		* @param p_value
		*/
		void SetFov(float p_value);

        /**
        * Sets the size of the camera to the given value
        * @param p_value
        */
        void SetSize(float p_value);

		/**
		* Sets the near of the camera to the given value
		* @param p_value
		*/
		void SetNear(float p_value);

		/**
		* Sets the far of the camera to the given value
		* @param p_value
		*/
		void SetFar(float p_value);

		/**
		* Sets the clear color of the camera to the given value
		* @param p_value
		*/
		void SetClearColor(const OvMaths::FVector3& p_clearColor);

		/**
		* Defines if the camera should apply frustum geometry culling in rendering
		* @param p_enable
		*/
		void SetFrustumGeometryCulling(bool p_enable);

		/**
		* Defines if the camera should apply frustum light culling in rendering
		* @param p_enable
		*/
		void SetFrustumLightCulling(bool p_enable);

        /**
        * Defines the projection mode the camera should adopt
        * @param p_projectionMode
        */
        void SetProjectionMode(OvRendering::Settings::EProjectionMode p_projectionMode);

		/**
		* Returns the fov of the camera
		*/
		OV_PROPERTY(Setter = SetFov, Name = "fov", Serialize, Thread = Main, Order = 0, Missing = Default, Default = "0")
		float GetFov() const;

        /**
        * Returns the size of the camera
        */
		OV_PROPERTY(Setter = SetSize, Name = "size", Serialize, Thread = Main, Order = 1, Missing = Default, Default = "0")
        float GetSize() const;

		/**
		* Returns the near of the camera
		*/
		OV_PROPERTY(Setter = SetNear, Name = "near", Serialize, Thread = Main, Order = 2, Missing = Default, Default = "0")
		float GetNear() const;

		/**
		* Returns the far of the camera
		*/
		OV_PROPERTY(Setter = SetFar, Name = "far", Serialize, Thread = Main, Order = 3, Missing = Default, Default = "0")
		float GetFar() const;

		/**
		* Returns the clear color of the camera
		*/
		OV_PROPERTY(Setter = SetClearColor, Name = "clear_color", Serialize, Thread = Main, Order = 4, Missing = Default, Default = "0,0,0")
		const OvMaths::FVector3& GetClearColor() const;

		/**
		* Returns true if the frustum geometry culling is enabled
		*/
		OV_PROPERTY(Setter = SetFrustumGeometryCulling, Name = "frustum_geometry_culling", Serialize, Thread = Main, Order = 5, Missing = Default, Default = "false")
		bool HasFrustumGeometryCulling() const;

		/**
		* Returns true if the frustum light culling is enabled
		*/
		OV_PROPERTY(Setter = SetFrustumLightCulling, Name = "frustum_light_culling", Serialize, Thread = Main, Order = 6, Missing = Default, Default = "false")
		bool HasFrustumLightCulling() const;

        /**
        * Returns the current projection mode
        */
		OV_PROPERTY(Setter = SetProjectionMode, Name = "projection_mode", Serialize, Thread = Main, Order = 7)
        OvRendering::Settings::EProjectionMode GetProjectionMode() const;

		/**
		* Returns the OvRendering camera instance attached to this component
		*/
		OvRendering::Entities::Camera& GetCamera();
		const OvRendering::Entities::Camera& GetCamera() const;

		/**
		* Defines how the component should be drawn in the inspector
		* @param p_root
		*/
		virtual void OnInspector(OvUI::Internal::WidgetContainer& p_root) override;

	private:
		OvRendering::Entities::Camera m_camera;
	};

	template<>
	struct ComponentTraits<OvCore::ECS::Components::CCamera>
	{
		static constexpr std::string_view Name = "class OvCore::ECS::Components::CCamera";
	};
}
