/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <chrono>
#include <optional>
#include <map>
#include <stack>

#include <baregl/Buffer.h>

#include <OvRendering/Features/ARenderFeature.h>
#include <OvRendering/Entities/Camera.h>

namespace OvCore::Rendering
{
	/**
	* Render feature handling engine buffer (UBO) updates
	*/
	class EngineBufferRenderFeature : public OvRendering::Features::ARenderFeature
	{
	public:
		/**
		* Constructor
		* @param p_renderer
		* @param p_executionPolicy
		*/
		EngineBufferRenderFeature(
			OvRendering::Core::CompositeRenderer& p_renderer,
			OvRendering::Features::EFeatureExecutionPolicy p_executionPolicy
		);

		/**
		* Replace the current camera data in the engine buffer by the provided camera
		* @param p_camera
		*/
		void SetCamera(const OvRendering::Entities::Camera& p_camera);

		// CPU-only, owned per-draw payload. View/camera/time and target remain external.
		struct DrawUploadCommand
		{
			OvMaths::FMatrix4 modelMatrix;
			OvMaths::FMatrix4 userMatrix;
		};
		static std::optional<DrawUploadCommand> PrepareDrawUpload(const OvRendering::Entities::Drawable& p_drawable);
		// Execute on this feature's GL owner within its active frame.
		void ExecuteDrawUpload(const DrawUploadCommand& p_command);

	protected:
		virtual void OnBeginFrame(const OvRendering::Data::FrameDescriptor& p_frameDescriptor) override;
		virtual void OnEndFrame() override;
		virtual void OnBeforeDraw(OvRendering::Data::PipelineState& p_pso, const OvRendering::Entities::Drawable& p_drawable) override;

	protected:
		std::chrono::high_resolution_clock::time_point m_startTime;
		std::unique_ptr<baregl::Buffer> m_engineBuffer;
	};
}
