/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <chrono>
#include <OvRendering/Data/RenderCommandList.h>

#include <OvCore/Rendering/PingPongFramebuffer.h>
#include <OvCore/Rendering/PostProcess/AEffect.h>
#include <OvRendering/Data/Material.h>

namespace OvCore::Rendering::PostProcess
{
	/**
	* Settings for the AutoExposure effect
	*/
	struct OV_TYPE(Record) AutoExposureSettings : public EffectSettings
	{
		OV_FIELD(Serialize, Inspect, Name = "center_weight_bias", DisplayName = "Center Weight Bias", Order = 1, Step = 0.1, Min = 0, Max = 1)
		float centerWeightBias = 0.5f;
		OV_FIELD(Serialize, Inspect, Name = "luminance_min", DisplayName = "Min Luminance (EV)", Order = 2, Alias = "min_luminance", Step = 0.1)
		float minLuminanceEV = -2.0f;
		OV_FIELD(Serialize, Inspect, Name = "luminance_max", DisplayName = "Max Luminance (EV)", Order = 3, Alias = "max_luminance", Step = 0.1)
		float maxLuminanceEV = 16.0f;
		OV_FIELD(Serialize, Inspect, Name = "exposure_compensation", DisplayName = "Exposure Compensation (EV)", Order = 4, Step = 0.1)
		float exposureCompensationEV = 0.0f;
		OV_FIELD(Serialize, Inspect, Name = "progressive", DisplayName = "Progressive", Order = 5)
		bool progressive = true;
		OV_FIELD(Serialize, Inspect, Name = "speed_up", DisplayName = "Speed Up", Order = 6, Step = 0.1)
		float speedUp = 1.0f;
		OV_FIELD(Serialize, Inspect, Name = "speed_down", DisplayName = "Speed Down", Order = 7, Step = 0.1)
		float speedDown = 1.0f;
	};

	/**
	* Post-processing effect that adapts the exposure based on the scene luminance.
	*/
	class AutoExposureEffect : public AEffect
	{
	public:
		/**
		* Constructor of the AutoExposureEffect class
		* @param p_renderer
		*/
		AutoExposureEffect();

		/**
		* Render the Auto-Exposure effect
		* @note: make sure the effect is applicable before calling this method
		* @param p_pso
		* @param p_src
		* @param p_dst
		* @param p_settings
		*/
		virtual void Draw(
            OvRendering::Core::FullscreenCommandRecorder& recorder,
			OvRendering::Data::PipelineState p_pso,
			OvRendering::Data::ResourceHandle p_src,
			OvRendering::Data::ResourceHandle p_dst,
			const EffectSettings& p_settings
		) override;

	private:
		std::optional<std::chrono::high_resolution_clock::time_point> m_previousTime;
		PingPongFramebuffer m_exposurePingPongBuffer;
		OvRendering::Data::Material m_luminanceMaterial;
		OvRendering::Data::Material m_exposureMaterial;
		OvRendering::Data::Material m_compensationMaterial;

		// Used to skip the first frame of the exposure adaptation
		bool m_firstFrame = true;
		OvRendering::Data::RenderCommandList::History m_history;
	};
}
