/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <OvCore/Rendering/PostProcess/AEffect.h>
#include <OvRendering/Data/Material.h>

namespace OvCore::Rendering::PostProcess
{
	namespace BloomConstants
	{
		constexpr uint32_t kMinPassCount = 1;
		constexpr uint32_t kMaxPassCount = 10;
		constexpr float kMaxBloomIntensity = 1.0f / 0.04f; // 0.04f being the default internal interpolation factor.
	}

	/**
	* Settings for the BloomEffect
	*/
	struct OV_TYPE(Record) BloomSettings : public EffectSettings
	{
		OV_FIELD(Serialize, Inspect, Name = "intensity", DisplayName = "Intensity", Order = 1, Step = 0.1, Min = 0, Max = 25)
		float intensity = 1.0f;
		OV_FIELD(Serialize, Inspect, Name = "passes", DisplayName = "Passes", Order = 2, Step = 1, Min = 1, Max = 10)
		int passes = 6;
	};

	/**
	* Bloom post-processing effect:
	* This effect will make the bright parts of the image glow
	*/
	class BloomEffect : public AEffect
	{
	public:
		/**
		* Constructor of the BloomEffect class
		* @param p_renderer
		*/
        BloomEffect();

		/**
		* Returns true if the effect is applicable with the given settings.
		* If the effect is not applicable, it will be skipped by the post processing render pass
		* @param p_settings
		*/
		virtual bool IsApplicable(const EffectSettings& p_settings) const override;

		/**
		* Render the bloom effect
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
		OvRendering::Data::Material m_downsamplingMaterial;
		OvRendering::Data::Material m_upsamplingMaterial;
		OvRendering::Data::Material m_bloomMaterial;
		OvRendering::Data::Material m_blitMaterial;
	};
}
