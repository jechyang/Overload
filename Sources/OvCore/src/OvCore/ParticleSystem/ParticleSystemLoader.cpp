/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <tinyxml2.h>

#include <OvDebug/Logger.h>

#include "OvCore/ParticleSystem/ParticleSystemLoader.h"
#include "OvCore/ParticleSystem/CParticleSystem.h"
#include "OvCore/ParticleSystem/AParticleSystemEmitter.h"

bool OvCore::ParticleSystem::ParticleSystemLoader::Load(
	OvCore::ECS::Components::CParticleSystem& p_target,
	const std::string& p_path)
{
	tinyxml2::XMLDocument doc;
	if (doc.LoadFile(p_path.c_str()) != tinyxml2::XML_SUCCESS)
	{
		OVLOG_ERROR("[PARTICLE] Failed to load \"" + p_path + "\": " + doc.ErrorStr());
		return false;
	}

	tinyxml2::XMLNode* root = doc.FirstChild();
	if (!root)
	{
		OVLOG_ERROR("[PARTICLE] Empty document: " + p_path);
		return false;
	}

	p_target.OnDeserialize(doc, root);
	OVLOG_INFO("[PARTICLE] Loaded \"" + p_path + "\"");
	return true;
}

void OvCore::ParticleSystem::ParticleSystemLoader::Save(
	OvCore::ECS::Components::CParticleSystem& p_target,
	const std::string& p_path)
{
	tinyxml2::XMLDocument doc;
	tinyxml2::XMLNode* root = doc.NewElement("root");
	doc.InsertFirstChild(root);

	p_target.OnSerialize(doc, root);

	if (doc.SaveFile(p_path.c_str()) == tinyxml2::XML_SUCCESS)
		OVLOG_INFO("[PARTICLE] Saved \"" + p_path + "\"");
	else
		OVLOG_ERROR("[PARTICLE] Failed to save \"" + p_path + "\"");
}

void OvCore::ParticleSystem::ParticleSystemLoader::CreateDefault(const std::string& p_path)
{
	tinyxml2::XMLDocument doc;
	tinyxml2::XMLNode* root = doc.NewElement("root");
	doc.InsertFirstChild(root);

	// Default: PointEmitter with sensible values
	tinyxml2::XMLElement* emitterElem = doc.NewElement("Emitter");
	emitterElem->SetAttribute("type", "Point");
	root->InsertEndChild(emitterElem);

	auto addFloat = [&](tinyxml2::XMLElement* parent, const char* name, float value) {
		tinyxml2::XMLElement* elem = doc.NewElement(name);
		elem->SetText(value);
		parent->InsertEndChild(elem);
	};

	addFloat(emitterElem, "emissionRate", 10.0f);
	addFloat(emitterElem, "lifetime",     2.0f);
	addFloat(emitterElem, "initialSpeed", 1.0f);
	addFloat(emitterElem, "size",         0.1f);
	addFloat(emitterElem, "spread",       0.5f);

	// Empty affectors list
	root->InsertEndChild(doc.NewElement("Affectors"));

	if (doc.SaveFile(p_path.c_str()) == tinyxml2::XML_SUCCESS)
		OVLOG_INFO("[PARTICLE] Created \"" + p_path + "\"");
	else
		OVLOG_ERROR("[PARTICLE] Failed to create \"" + p_path + "\"");
}

void OvCore::ParticleSystem::ParticleSystemLoader::CreateDefaultCircleEmitter(const std::string& p_path)
{
	tinyxml2::XMLDocument doc;
	tinyxml2::XMLNode* root = doc.NewElement("root");
	doc.InsertFirstChild(root);

	// Default: CircleEmitter with sensible values
	tinyxml2::XMLElement* emitterElem = doc.NewElement("Emitter");
	emitterElem->SetAttribute("type", "Circle");
	root->InsertEndChild(emitterElem);

	auto addFloat = [&](tinyxml2::XMLElement* parent, const char* name, float value) {
		tinyxml2::XMLElement* elem = doc.NewElement(name);
		elem->SetText(value);
		parent->InsertEndChild(elem);
	};

	addFloat(emitterElem, "emissionRate", 10.0f);
	addFloat(emitterElem, "lifetime",     2.0f);
	addFloat(emitterElem, "initialSpeed", 1.0f);
	addFloat(emitterElem, "size",         0.1f);
	addFloat(emitterElem, "radius",       1.0f);
	addFloat(emitterElem, "spread",       0.5f);
	Helpers::Serializer::SerializeVec3(doc, emitterElem, "direction", OvMaths::FVector3{ 0.0f, 1.0f, 0.0f });

	// Empty affectors list
	root->InsertEndChild(doc.NewElement("Affectors"));

	if (doc.SaveFile(p_path.c_str()) == tinyxml2::XML_SUCCESS)
		OVLOG_INFO("[PARTICLE] Created circle emitter \"" + p_path + "\"");
	else
		OVLOG_ERROR("[PARTICLE] Failed to create \"" + p_path + "\"");
}
