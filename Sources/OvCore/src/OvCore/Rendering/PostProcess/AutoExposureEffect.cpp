/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/


#include <OvCore/Global/ServiceLocator.h>
#include <OvCore/Rendering/PostProcess/AutoExposureEffect.h>
#include <OvCore/Rendering/FramebufferUtil.h>
#include <OvCore/ResourceManagement/ShaderManager.h>
#include <OvRendering/Utils/Profiling.h>

constexpr uint32_t kLuminanceBufferResolution = 1024;
constexpr uint32_t kExposureBufferResolution = 1;

OvCore::Rendering::PostProcess::AutoExposureEffect::AutoExposureEffect() :
	m_exposurePingPongBuffer{ "Exposure" }
{
	for (auto& buffer : m_exposurePingPongBuffer.GetFramebuffers())
	{
		FramebufferUtil::SetupFramebuffer(
			buffer,
			kExposureBufferResolution,
			kExposureBufferResolution,
			false, false, false
		);
	}

	m_luminanceMaterial.SetShader(OVSERVICE(OvCore::ResourceManagement::ShaderManager)[":Shaders\\PostProcess\\Luminance.ovfx"]);
	m_exposureMaterial.SetShader(OVSERVICE(OvCore::ResourceManagement::ShaderManager)[":Shaders\\PostProcess\\AutoExposure.ovfx"]);
	m_compensationMaterial.SetShader(OVSERVICE(OvCore::ResourceManagement::ShaderManager)[":Shaders\\PostProcess\\ApplyExposure.ovfx"]);
}

void OvCore::Rendering::PostProcess::AutoExposureEffect::Draw(
    OvRendering::Core::FullscreenCommandRecorder& recorder,
	OvRendering::Data::PipelineState p_pso,
	OvRendering::Data::ResourceHandle p_src,
	OvRendering::Data::ResourceHandle p_dst,
	const EffectSettings& p_settings
)
{
	ZoneScoped;
	OV_GPU_ZONE(recorder, "AutoExposureEffect");

	const auto& autoExposureSettings = static_cast<const AutoExposureSettings&>(p_settings);

    if (recorder.UseRenderHistory(m_history))
    {
        m_firstFrame = true;
        m_previousTime.reset();
        m_exposurePingPongBuffer.Reset();
    }

	const auto luminance=recorder.CreateTransient("Exposure.Luminance",kLuminanceBufferResolution,kLuminanceBufferResolution,true);

	// Luminance calculation
	m_luminanceMaterial.SetProperty("_CenterWeightBias", autoExposureSettings.centerWeightBias, true);
    recorder.SetPassName("Exposure.Luminance");
	recorder.Blit(p_pso, p_src, luminance, m_luminanceMaterial,
		OvRendering::Settings::EBlitFlags::DEFAULT & ~OvRendering::Settings::EBlitFlags::RESIZE_DST_TO_MATCH_SRC);
    recorder.SetPassName("Exposure.LuminanceMipmaps");recorder.GenerateMipmaps(luminance);

	float elapsedTime = 1.0f;
	auto currentTime = std::chrono::high_resolution_clock::now();
	if (m_previousTime.has_value())
	{
		elapsedTime = std::chrono::duration_cast<std::chrono::duration<float>>(
			currentTime - m_previousTime.value()
		).count();
	}
	m_previousTime = currentTime;

	const auto previousExposure = recorder.ImportTarget(m_exposurePingPongBuffer[0],OvRendering::Data::ResourceOrigin::History,!m_firstFrame);
	const auto currentExposure = recorder.ImportTarget(m_exposurePingPongBuffer[1],OvRendering::Data::ResourceOrigin::History,false);
    if(m_firstFrame) {recorder.SetPassName("Exposure.InitializeHistory");recorder.ClearColor(previousExposure,{1,1,1,1});}
	++m_exposurePingPongBuffer;

	// Exposure adaptation
	m_exposureMaterial.SetProperty("_MinLuminanceEV", autoExposureSettings.minLuminanceEV, true);
	m_exposureMaterial.SetProperty("_MaxLuminanceEV", autoExposureSettings.maxLuminanceEV, true);
	m_exposureMaterial.SetProperty("_ExposureCompensationEV", autoExposureSettings.exposureCompensationEV, true);
	m_exposureMaterial.SetProperty("_ElapsedTime", elapsedTime, true);
	m_exposureMaterial.SetProperty("_Progressive", static_cast<int>(autoExposureSettings.progressive && !m_firstFrame), true);
	m_exposureMaterial.SetProperty("_SpeedUp", autoExposureSettings.speedUp, true);
	m_exposureMaterial.SetProperty("_SpeedDown", autoExposureSettings.speedDown, true);
	const std::array luminanceBindings{OvRendering::Core::FullscreenCommandRecorder::SamplerBinding{"_LuminanceTexture",luminance,true}};
    recorder.SetPassName("Exposure.Adaptation");recorder.Blit(p_pso, previousExposure, currentExposure, m_exposureMaterial,
		OvRendering::Settings::EBlitFlags::DEFAULT, luminanceBindings);

	// Apply the exposure to the final image
	const std::array exposureBindings{OvRendering::Core::FullscreenCommandRecorder::SamplerBinding{"_ExposureTexture",currentExposure,true}};
    recorder.SetPassName("Exposure.Apply");recorder.Blit(p_pso, p_src, p_dst, m_compensationMaterial,
		OvRendering::Settings::EBlitFlags::DEFAULT, exposureBindings);

	m_firstFrame = false;
}
