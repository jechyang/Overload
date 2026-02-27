/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <vector>

#include <tinyxml2.h>

#include <OvRendering/Geometry/Vertex.h>
#include <OvUI/Widgets/Buttons/Button.h>

#include "OvCore/ECS/Actor.h"
#include "OvCore/Helpers/Serializer.h"
#include "OvCore/ParticleSystem/CParticleSystem.h"

OvTools::Eventing::Event<OvCore::ECS::Components::CParticleSystem&>
	OvCore::ECS::Components::CParticleSystem::OpenInEditorRequestEvent;

OvCore::ECS::Components::CParticleSystem::CParticleSystem(ECS::Actor& p_owner) :
	AComponent(p_owner)
{
}

std::string OvCore::ECS::Components::CParticleSystem::GetName()
{
	return "CParticleSystem";
}

std::string OvCore::ECS::Components::CParticleSystem::GetTypeName()
{
	return std::string{ ComponentTraits<CParticleSystem>::Name };
}

void OvCore::ECS::Components::CParticleSystem::OnAwake()
{
	AComponent::OnAwake();
}

void OvCore::ECS::Components::CParticleSystem::OnUpdate(float p_deltaTime)
{
	AComponent::OnUpdate(p_deltaTime);

	// Emit new particles
	if (m_emitter)
		m_emitter->Emit(m_pool, p_deltaTime);

	// Update live particles and release dead ones
	for (auto& p : m_pool.GetSlots())
	{
		if (!p.active)
			continue;

		// Apply affectors
		for (auto& affector : m_affectors)
			affector->Apply(p, p_deltaTime);

		// Integrate position
		p.position += p.velocity * p_deltaTime;

		// Fade alpha over lifetime
		const float lifeRatio = p.timeToLive / p.totalTimeToLive;
		p.color.w = lifeRatio;

		p.timeToLive -= p_deltaTime;

		if (p.timeToLive <= 0.0f)
			m_pool.Release(p);
	}
}

void OvCore::ECS::Components::CParticleSystem::SetEmitter(
	std::unique_ptr<ParticleSystem::AParticleEmitter> p_emitter)
{
	m_emitter = std::move(p_emitter);
}

void OvCore::ECS::Components::CParticleSystem::AddAffector(
	std::unique_ptr<ParticleSystem::AParticleAffector> p_affector)
{
	m_affectors.push_back(std::move(p_affector));
}

void OvCore::ECS::Components::CParticleSystem::Reset()
{
	m_emitter = std::make_unique<ParticleSystem::PointParticleEmitter>();
	m_affectors.clear();
	m_pool.Clear();
	material = nullptr;
}

void OvCore::ECS::Components::CParticleSystem::RebuildMesh(
	const OvMaths::FVector3& p_cameraRight,
	const OvMaths::FVector3& p_cameraUp)
{
	if (m_pool.GetActiveCount() == 0)
		return;

	const OvMaths::FVector3 worldPos = owner.transform.GetWorldPosition();

	std::vector<OvRendering::Geometry::Vertex> vertices;
	std::vector<uint32_t> indices;
	vertices.reserve(m_pool.GetActiveCount() * 4);
	indices.reserve(m_pool.GetActiveCount() * 6);

	for (const auto& p : m_pool.GetSlots())
	{
		if (!p.active)
			continue;

		const OvMaths::FVector3 center = worldPos + p.position;
		const float half = p.size * 0.5f;

		const OvMaths::FVector3 bl = center - p_cameraRight * half - p_cameraUp * half;
		const OvMaths::FVector3 br = center + p_cameraRight * half - p_cameraUp * half;
		const OvMaths::FVector3 tr = center + p_cameraRight * half + p_cameraUp * half;
		const OvMaths::FVector3 tl = center - p_cameraRight * half + p_cameraUp * half;

		const uint32_t base = static_cast<uint32_t>(vertices.size());

		auto makeVertex = [](const OvMaths::FVector3& pos, float u, float v) -> OvRendering::Geometry::Vertex {
			OvRendering::Geometry::Vertex vert{};
			vert.position[0] = pos.x; vert.position[1] = pos.y; vert.position[2] = pos.z;
			vert.texCoords[0] = u;    vert.texCoords[1] = v;
			return vert;
		};

		vertices.push_back(makeVertex(bl, p.lb_uv.x, p.lb_uv.y));
		vertices.push_back(makeVertex(br, p.rt_uv.x, p.lb_uv.y));
		vertices.push_back(makeVertex(tr, p.rt_uv.x, p.rt_uv.y));
		vertices.push_back(makeVertex(tl, p.lb_uv.x, p.rt_uv.y));

		indices.insert(indices.end(), {
			base, base + 1, base + 2,
			base, base + 2, base + 3
		});
	}

	m_mesh.Update(vertices, indices);
}

OvCore::ParticleSystem::ParticleMesh& OvCore::ECS::Components::CParticleSystem::GetMesh()
{
	return m_mesh;
}

uint32_t OvCore::ECS::Components::CParticleSystem::GetParticleCount() const
{
	return m_pool.GetActiveCount();
}

void OvCore::ECS::Components::CParticleSystem::OnSerialize(
	tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node)
{
	// Emitter
	if (m_emitter)
	{
		tinyxml2::XMLElement* emitterElem = p_doc.NewElement("Emitter");
		p_node->InsertEndChild(emitterElem);

		if (auto* point = dynamic_cast<ParticleSystem::PointParticleEmitter*>(m_emitter.get()))
		{
			emitterElem->SetAttribute("type", "Point");
			Helpers::Serializer::SerializeFloat(p_doc, emitterElem, "emissionRate", point->emissionRate);
			Helpers::Serializer::SerializeFloat(p_doc, emitterElem, "lifetime",     point->lifetime);
			Helpers::Serializer::SerializeFloat(p_doc, emitterElem, "initialSpeed", point->initialSpeed);
			Helpers::Serializer::SerializeFloat(p_doc, emitterElem, "size",         point->size);
			Helpers::Serializer::SerializeFloat(p_doc, emitterElem, "spread",       point->spread);
		}
	}

	// Affectors
	tinyxml2::XMLElement* affectorsElem = p_doc.NewElement("Affectors");
	p_node->InsertEndChild(affectorsElem);

	for (auto& affector : m_affectors)
	{
		if (auto* gravity = dynamic_cast<ParticleSystem::GravityAffector*>(affector.get()))
		{
			tinyxml2::XMLElement* gravElem = p_doc.NewElement("Affector");
			affectorsElem->InsertEndChild(gravElem);
			gravElem->SetAttribute("type", "Gravity");
			Helpers::Serializer::SerializeFloat(p_doc, gravElem, "gravity", gravity->gravity);
		}
	}

	// Material
	Helpers::Serializer::SerializeMaterial(p_doc, p_node, "material", material);
}

void OvCore::ECS::Components::CParticleSystem::OnDeserialize(
	tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node)
{
	// Emitter
	if (tinyxml2::XMLElement* emitterElem = p_node->FirstChildElement("Emitter"))
	{
		const char* type = emitterElem->Attribute("type");
		if (type && std::string_view(type) == "Point")
		{
			auto emitter = std::make_unique<ParticleSystem::PointParticleEmitter>();
			Helpers::Serializer::DeserializeFloat(p_doc, emitterElem, "emissionRate", emitter->emissionRate);
			Helpers::Serializer::DeserializeFloat(p_doc, emitterElem, "lifetime",     emitter->lifetime);
			Helpers::Serializer::DeserializeFloat(p_doc, emitterElem, "initialSpeed", emitter->initialSpeed);
			Helpers::Serializer::DeserializeFloat(p_doc, emitterElem, "size",         emitter->size);
			Helpers::Serializer::DeserializeFloat(p_doc, emitterElem, "spread",       emitter->spread);
			m_emitter = std::move(emitter);
		}
	}

	// Affectors
	m_affectors.clear();
	if (tinyxml2::XMLElement* affectorsElem = p_node->FirstChildElement("Affectors"))
	{
		for (auto* affElem = affectorsElem->FirstChildElement("Affector");
			affElem; affElem = affElem->NextSiblingElement("Affector"))
		{
			const char* type = affElem->Attribute("type");
			if (type && std::string_view(type) == "Gravity")
			{
				auto aff = std::make_unique<ParticleSystem::GravityAffector>();
				Helpers::Serializer::DeserializeFloat(p_doc, affElem, "gravity", aff->gravity);
				m_affectors.push_back(std::move(aff));
			}
		}
	}

	// Material
	Helpers::Serializer::DeserializeMaterial(p_doc, p_node, "material", material);
}

OvCore::ParticleSystem::AParticleEmitter* OvCore::ECS::Components::CParticleSystem::GetEmitter()
{
	return m_emitter.get();
}

void OvCore::ECS::Components::CParticleSystem::OnInspector(
	OvUI::Internal::WidgetContainer& p_root)
{
	auto& btn = p_root.CreateWidget<OvUI::Widgets::Buttons::Button>("Open in Particle Editor");
	btn.idleBackgroundColor = { 0.1f, 0.4f, 0.7f };
	btn.ClickedEvent += [this] {
		OpenInEditorRequestEvent.Invoke(*this);
	};
}
