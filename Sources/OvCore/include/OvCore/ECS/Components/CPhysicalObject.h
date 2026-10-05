/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <OvReflection/Annotations.h>

#include <OvPhysics/Entities/PhysicalObject.h>

#include "OvCore/ECS/Components/AComponent.h"

namespace OvCore::ECS { class Actor; }

namespace OvCore::ECS::Components
{
	/**
	* Base class for any physical object
	*/
	class OV_TYPE(Component, Lookup = Existing, Alias = "class OvCore::ECS::Components::CPhysicalObject", DisplayName = "Physical Object", Order = 101) CPhysicalObject : public AComponent
	{
	public:
		/**
		* Constructor
		* @param p_owner
		*/
		CPhysicalObject(ECS::Actor& p_owner);

		/**
		* Add a force to the rigidbody
		* @param p_force
		*/
		void AddForce(const OvMaths::FVector3& p_force);

		/**
		* Add an impulse to the rigidbody
		* @param p_impulse
		*/
		void AddImpulse(const OvMaths::FVector3& p_impulse);

		/**
		* Clear forces
		*/
		void ClearForces();

		/**
		* Returns the mass of the physical object
		*/
		OV_PROPERTY(Setter = SetMass, Name = "mass", Serialize, Thread = Main, Order = 3, Missing = Default, Default = "0")
		float GetMass() const;

		/**
		* Returns the collision detection mode of the physical object
		*/
		OV_PROPERTY(Setter = SetCollisionDetectionMode, Name = "collision_mode", Serialize, Thread = Main, Order = 7, Missing = Default, Default = "0")
		OvPhysics::Entities::PhysicalObject::ECollisionDetectionMode GetCollisionDetectionMode() const;

		/**
		* Returns the bounciness of the physical object
		*/
		OV_PROPERTY(Setter = SetBounciness, Name = "bounciness", Serialize, Thread = Main, Order = 2, Missing = Default, Default = "0")
		float GetBounciness() const;

		/**
		* Returns the friction of the physical object
		*/
		OV_PROPERTY(Setter = SetFriction, Name = "friction", Serialize, Thread = Main, Order = 4, Missing = Default, Default = "0")
		float GetFriction()	const;

		/**
		* Returns the linear velocity of the physical object
		*/
		OvMaths::FVector3 GetLinearVelocity() const;

		/**
		* Returns the angular of the physical object
		*/
		OvMaths::FVector3 GetAngularVelocity() const;

		/**
		* Returns the linear factor of the physical object
		*/
		OV_PROPERTY(Setter = SetLinearFactor, Name = "linear_factor", Serialize, Thread = Main, Order = 5, Missing = Default, Default = "0,0,0")
		OvMaths::FVector3 GetLinearFactor()	const;

		/**
		* Returns the angular factor of the physical object
		*/
		OV_PROPERTY(Setter = SetAngularFactor, Name = "angular_factor", Serialize, Thread = Main, Order = 6, Missing = Default, Default = "0,0,0")
		OvMaths::FVector3 GetAngularFactor() const;

		/**
		* Returns true if the physical object is a trigger
		*/
		OV_PROPERTY(Setter = SetTrigger, Name = "is_trigger", Serialize, Thread = Main, Order = 0, Missing = Default, Default = "false")
		bool IsTrigger() const;

		/**
		* Returns true if the physical object is kinematic
		*/
		OV_PROPERTY(Setter = SetKinematic, Name = "is_kinematic", Serialize, Thread = Main, Order = 1, Missing = Default, Default = "false")
		bool IsKinematic() const;

		/**
		* Returns the current activation state
		*/
		OvPhysics::Entities::PhysicalObject::EActivationState GetActivationState() const;

		/**
		* Defines a new mass for the physical object
		* @param p_mass
		*/
		void SetMass(float p_mass);

		/**
		* Defines a new collision detection mode for the physical object
		* @param p_mode
		*/
		void SetCollisionDetectionMode(OvPhysics::Entities::PhysicalObject::ECollisionDetectionMode p_mode);

		/**
		* Defines a new bounciness for the physical object
		* @param p_bounciness
		*/
		void SetBounciness(float p_bounciness);

		/**
		* Defines a new friction for the physical object
		* @param p_mass
		*/
		void SetFriction(float p_friction);

		/**
		* Defines a new linear velocity for the physical object
		* @param p_linearVelocity
		*/
		void SetLinearVelocity(const OvMaths::FVector3& p_linearVelocity);

		/**
		* Defines a new angular velocity for the physical object
		* @param p_angularVelocity
		*/
		void SetAngularVelocity(const OvMaths::FVector3& p_angularVelocity);

		/**
		* Defines a new linear factor for the physical object
		* @param p_linearFactor
		*/
		void SetLinearFactor(const OvMaths::FVector3& p_linearFactor);

		/**
		* Defines a new angular factor for the physical object
		* @param p_angularFactor
		*/
		void SetAngularFactor(const OvMaths::FVector3& p_angularFactor);

		/**
		* Defines if the physical object should be a trigger or not
		* @param p_trigger
		*/
		void SetTrigger(bool p_trigger);

		/**
		* Defines if the physical object should be kinematic or not
		* @param p_kinematic
		*/
		void SetKinematic(bool p_kinematic);

		/**
		* Defines the new activation state for the physical object
		* @param p_activationState
		*/
		void SetActivationState(OvPhysics::Entities::PhysicalObject::EActivationState p_activationState);

		/**
		* Defines how the component should be drawn in the inspector
		* @param p_root
		*/
		virtual void OnInspector(OvUI::Internal::WidgetContainer& p_root) override;

	protected:
		template <typename T> T& GetPhysicalObjectAs() const { return *static_cast<T*>(m_physicalObject.get()); }
		void Init();
		void BindListener();

	private:
		virtual void OnEnable() override;
		virtual void OnDisable() override;

	public:
		OvTools::Eventing::Event<CPhysicalObject&> CollisionEnterEvent;
		OvTools::Eventing::Event<CPhysicalObject&> CollisionStayEvent;
		OvTools::Eventing::Event<CPhysicalObject&> CollisionExitEvent;

		OvTools::Eventing::Event<CPhysicalObject&> TriggerEnterEvent;
		OvTools::Eventing::Event<CPhysicalObject&> TriggerStayEvent;
		OvTools::Eventing::Event<CPhysicalObject&> TriggerExitEvent;

	protected:
		std::unique_ptr<OvPhysics::Entities::PhysicalObject> m_physicalObject;
	};

	template<>
	struct ComponentTraits<OvCore::ECS::Components::CPhysicalObject>
	{
		static constexpr std::string_view Name = "class OvCore::ECS::Components::CPhysicalObject";
	};
}