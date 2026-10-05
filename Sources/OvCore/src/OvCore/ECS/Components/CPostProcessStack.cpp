/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <OvCore/ECS/Components/CPostProcessStack.h>

OvCore::ECS::Components::CPostProcessStack::CPostProcessStack(ECS::Actor& p_owner) : AComponent(p_owner)
{
}

std::string OvCore::ECS::Components::CPostProcessStack::GetName()
{
	return "Post Process Stack";
}

std::string OvCore::ECS::Components::CPostProcessStack::GetTypeName()
{
	return std::string{ComponentTraits<CPostProcessStack>::Name};
}

const OvCore::Rendering::PostProcess::PostProcessStack& OvCore::ECS::Components::CPostProcessStack::GetStack() const
{
	return m_settings;
}

const OvCore::Rendering::PostProcess::BloomSettings& OvCore::ECS::Components::CPostProcessStack::GetBloomSettings() const
{
	return m_settings.Get<Rendering::PostProcess::BloomEffect, Rendering::PostProcess::BloomSettings>();
}

const OvCore::Rendering::PostProcess::AutoExposureSettings& OvCore::ECS::Components::CPostProcessStack::GetAutoExposureSettings() const
{
	return m_settings.Get<Rendering::PostProcess::AutoExposureEffect, Rendering::PostProcess::AutoExposureSettings>();
}

const OvCore::Rendering::PostProcess::TonemappingSettings& OvCore::ECS::Components::CPostProcessStack::GetTonemappingSettings() const
{
	return m_settings.Get<Rendering::PostProcess::TonemappingEffect, Rendering::PostProcess::TonemappingSettings>();
}

const OvCore::Rendering::PostProcess::FXAASettings& OvCore::ECS::Components::CPostProcessStack::GetFXAASettings() const
{
	return m_settings.Get<Rendering::PostProcess::FXAAEffect, Rendering::PostProcess::FXAASettings>();
}

void OvCore::ECS::Components::CPostProcessStack::SetBloomSettings(const OvCore::Rendering::PostProcess::BloomSettings& p_settings)
{
	m_settings.Set<Rendering::PostProcess::BloomEffect>(p_settings);
}

void OvCore::ECS::Components::CPostProcessStack::SetAutoExposureSettings(const OvCore::Rendering::PostProcess::AutoExposureSettings& p_settings)
{
	m_settings.Set<Rendering::PostProcess::AutoExposureEffect>(p_settings);
}

void OvCore::ECS::Components::CPostProcessStack::SetTonemappingSettings(const OvCore::Rendering::PostProcess::TonemappingSettings& p_settings)
{
	m_settings.Set<Rendering::PostProcess::TonemappingEffect>(p_settings);
}

void OvCore::ECS::Components::CPostProcessStack::SetFXAASettings(const OvCore::Rendering::PostProcess::FXAASettings& p_settings)
{
	m_settings.Set<Rendering::PostProcess::FXAAEffect>(p_settings);
}
