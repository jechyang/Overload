/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once
#include <OvReflection/Annotations.h>
#include <OvDebug/Assertion.h>


#include <OvRendering/Data/Material.h>
#include <OvRendering/Core/FullscreenCommandRecorder.h>

namespace OvCore::Rendering::PostProcess
{
	/**
	* Base effect settings structure
	*/
	struct OV_TYPE(Record) EffectSettings
	{
		OV_FIELD(Serialize, Inspect, DisplayName = "Enabled", Order = 0)
		bool enabled = true;
	};

	/**
	* Base class for any post-processing effect
	*/
	class AEffect
	{
	public:
		/**
		* Constructor of the effect class
		* @param p_renderer
		*/
		AEffect() = default;

		/**
		* Virtual destructor
		*/
		virtual ~AEffect() = default;

		/**
		* Returns true if the effect is applicable with the given settings.
		* If the effect is not applicable, it will be skipped by the post processing render pass
		* @param p_settings
		*/
		virtual bool IsApplicable(const EffectSettings& p_settings) const;
		
		/**
		* Draw the effect
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
		) = 0;

	};
}
