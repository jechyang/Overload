/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include "OvRendering/Core/CompositeRenderer.h"
#include "OvRendering/Features/ARenderFeature.h"
#include "OvRendering/Data/FrameInfo.h"
#include "OvRendering/Entities/Light.h"
#include "OvRendering/Data/Frustum.h"

#include <baregl/Buffer.h>

namespace OvRendering::Features
{
	class LightingRenderFeature : public ARenderFeature
	{
	public:
		bool SupportsDrawRecording() const override { return typeid(*this) == typeid(LightingRenderFeature); }

		// Light upload preparation copies values; other descriptor consumers (e.g.
		// shadow passes) still borrow these lights for the active frame.
		using LightSet = std::vector<std::reference_wrapper<OvRendering::Entities::Light>>;

		struct LightingDescriptor
		{
			LightSet lights;
			OvTools::Utils::OptRef<const OvRendering::Data::Frustum> frustumOverride;
		};

		/**
		* Constructor
		* @param p_renderer
		* @param p_executionPolicy
		* @param p_bufferBindingPoint
		*/
		LightingRenderFeature(
			OvRendering::Core::CompositeRenderer& p_renderer,
			OvRendering::Features::EFeatureExecutionPolicy p_executionPolicy,
			uint32_t p_bufferBindingPoint = 0
		);

		/**
		* Bind the light buffer
		*/
		void Bind() const;

		/**
		* Returns the binding point of the light buffer
		*/
		uint32_t GetBufferBindingPoint() const;

		struct LightingUploadCommand
		{
			std::vector<OvMaths::FMatrix4> matrices;
		};
		static LightingUploadCommand PrepareLightingUpload(const LightingDescriptor& p_lights, const Entities::Camera& p_camera);
		void ExecuteLightingUpload(const LightingUploadCommand& p_command);

		// Same-draw binding ownership. Buffer contents must stay stable through execution.
		// Acquire, execute and finally release on the GL owner before context shutdown.
		struct LightBindingCommand
		{
			std::shared_ptr<baregl::Buffer> buffer;
			uint32_t bindingPoint;
		};
		LightBindingCommand RecordLightBinding() const;
		static void ExecuteLightBinding(const LightBindingCommand& p_command);
		// Explicit override used by editor debug passes; Bind() restores scene lighting.
		void BindOverride(std::shared_ptr<baregl::Buffer> p_buffer);

	protected:
		virtual void OnBeginFrame(const Data::FrameDescriptor& p_frameDescriptor) override;
		virtual void OnEndFrame() override;
		void OnBeforeDraw(Data::PipelineState& p_pso, const Entities::Drawable& p_drawable) override;

	private:
		uint32_t m_bufferBindingPoint;
		std::shared_ptr<baregl::Buffer> m_lightBuffer;
		mutable std::shared_ptr<baregl::Buffer> m_boundLightBuffer;
	};
}
