/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <OvCore/Global/ServiceLocator.h>
#include <OvCore/Rendering/PostProcess/FXAAEffect.h>
#include <OvCore/ResourceManagement/ShaderManager.h>
#include <OvRendering/Utils/Profiling.h>

OvCore::Rendering::PostProcess::FXAAEffect::FXAAEffect()
{
	m_material.SetShader(OVSERVICE(OvCore::ResourceManagement::ShaderManager)[":Shaders\\PostProcess\\FXAA.ovfx"]);
}

void OvCore::Rendering::PostProcess::FXAAEffect::Draw(
    OvRendering::Core::FullscreenCommandRecorder& recorder,
	OvRendering::Data::PipelineState p_pso,
	OvRendering::Data::ResourceHandle p_src,
	OvRendering::Data::ResourceHandle p_dst,
	const EffectSettings& p_settings
)
{
	ZoneScoped;
	OV_GPU_ZONE(recorder, "FXAAEffect");

    recorder.SetPassName("FXAA");recorder.Blit(p_pso, p_src, p_dst, m_material);
}
