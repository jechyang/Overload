/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <memory>
#include <vector>

#include <OvMaths/FVector3.h>
#include <OvTools/Eventing/Event.h>

#include "OvCore/ECS/Components/AComponent.h"
#include "OvCore/ParticleSystem/AParticleSystemEmitter.h"
#include "OvCore/ParticleSystem/ParticleSystemAffector.h"
#include "OvCore/ParticleSystem/ParticleSystemParticle.h"
#include "OvCore/ParticleSystem/ParticleMesh.h"
#include "OvCore/Resources/Material.h"

namespace OvCore::ECS { class Actor; }

namespace OvCore::ECS::Components
{
	/**
	* Component that manages a CPU-simulated billboard particle system.
	* Attach an emitter and optional affectors, then assign a material.
	*/
	class CParticleSystem : public AComponent
	{
	public:
		CParticleSystem(ECS::Actor& p_owner);

		std::string GetName() override;
		virtual std::string GetTypeName() override;

		virtual void OnAwake() override;
		virtual void OnUpdate(float p_deltaTime) override;

		virtual void OnSerialize(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node) override;
		virtual void OnDeserialize(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node) override;
		virtual void OnInspector(OvUI::Internal::WidgetContainer& p_root) override;

		/**
		* Replace the current emitter.
		*/
		void SetEmitter(std::unique_ptr<ParticleSystem::AParticleEmitter> p_emitter);

		/**
		* Add an affector that will be applied to every live particle each frame.
		*/
		void AddAffector(std::unique_ptr<ParticleSystem::AParticleAffector> p_affector);

		/**
		* Reset to a default PointEmitter, no affectors, no material.
		*/
		void Reset();

		/**
		* Material used to render the particle quads.
		*/
		OvCore::Resources::Material* material = nullptr;

		/**
		* Rebuild the billboard mesh using the given camera right/up vectors.
		* Called by the SceneRenderer during ParseScene.
		* @param p_cameraRight  Camera's right vector in world space
		* @param p_cameraUp     Camera's up vector in world space
		*/
		void RebuildMesh(
			const OvMaths::FVector3& p_cameraRight,
			const OvMaths::FVector3& p_cameraUp
		);

		/**
		* Returns the dynamic mesh (may be empty if no live particles).
		*/
		ParticleSystem::ParticleMesh& GetMesh();

		/**
		* Returns the number of currently live particles.
		*/
		uint32_t GetParticleCount() const;

		/**
		* Returns the raw emitter pointer (may be null).
		*/
		ParticleSystem::AParticleEmitter* GetEmitter();

		/**
		* Returns the first affector of type T, or nullptr.
		*/
		template<typename T>
		T* GetAffectorAs()
		{
			for (auto& a : m_affectors)
				if (auto* p = dynamic_cast<T*>(a.get()))
					return p;
			return nullptr;
		}

		/**
		* Fired when the inspector "Open in Particle Editor" button is clicked.
		*/
		static OvTools::Eventing::Event<CParticleSystem&> OpenInEditorRequestEvent;

	private:
		std::unique_ptr<ParticleSystem::AParticleEmitter>              m_emitter;
		std::vector<std::unique_ptr<ParticleSystem::AParticleAffector>> m_affectors;
		std::vector<ParticleSystem::ParticleSystemParticle>             m_particles;
		ParticleSystem::ParticleMesh                                    m_mesh;
	};

	template<>
	struct ComponentTraits<OvCore::ECS::Components::CParticleSystem>
	{
		static constexpr std::string_view Name = "class OvCore::ECS::Components::CParticleSystem";
	};
}
