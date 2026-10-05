/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <chrono>

#include <OvCore/Rendering/PostProcess/AEffect.h>
#include <OvRendering/Data/Material.h>

namespace OvCore::Rendering::PostProcess
{
	/**
	* Enum of available tonemapping modes
	*/
	enum class OV_TYPE() ETonemappingMode
	{
		NEUTRAL = 0,
		REINHARD = 1,
		REINHARD_JODIE = 2,
		UNCHARTED2 = 3,
		UNCHARTED2_FILMIC = 4,
		ACES = 5
	};

	/**
	* Tonemapping effect settings
	*/
	struct OV_TYPE(Record) TonemappingSettings : public EffectSettings
	{
		OV_FIELD(Serialize, Inspect, Name = "exposure", DisplayName = "Exposure", Order = 1, Step = 0.05, Min = 0)
		float exposure = 1.0f;
		OV_FIELD(Serialize, Inspect, Name = "mode", DisplayName = "Mode", Order = 2)
		ETonemappingMode mode = ETonemappingMode::NEUTRAL;
		OV_FIELD(Serialize, Inspect, Name = "gamma_correction", DisplayName = "Gamma Correction", Order = 3)
		bool gammaCorrection = true;
	};

	/**
	* Post-processing effect that applies Tonemapping
	* Remap HDR values to the LDR range.
	*/
	class TonemappingEffect : public AEffect
	{
	public:
		/**
		* Constructor of the TonemappingEffect class
		* @param p_renderer
		*/
		TonemappingEffect();

		/**
		* Render the Tonemapping effect
		* @note: make sure the effect is applicable before calling this method
		* @param p_pso
		* @param p_src
		* @param p_dst
		* @param p_settings
		*/
		virtual void Draw(
            OvRendering::Core::FullscreenCommandRecorder& recorder,
			OvRendering::Data::PipelineState p_pso,
			baregl::Framebuffer& p_src,
			baregl::Framebuffer& p_dst,
			const EffectSettings& p_settings
		) override;

	private:
		OvRendering::Data::Material m_tonemappingMaterial;
	};
}
