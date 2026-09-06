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
		bool SupportsDrawRecording() const override { return typeid(*this) == typeid(EngineBufferRenderFeature); }

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

		// Capture already cached camera matrices without retaining the Camera/Transform.
		struct CameraUploadCommand
		{
			OvMaths::FMatrix4 viewMatrix;
			OvMaths::FMatrix4 projectionMatrix;
			OvMaths::FVector3 cameraPosition;
		};
		struct FrameUploadCommand
		{
			CameraUploadCommand camera;
			float elapsedTime;
		};
		static CameraUploadCommand PrepareCameraUpload(const OvRendering::Entities::Camera& p_camera);
		static FrameUploadCommand PrepareFrameUpload(const OvRendering::Entities::Camera& p_camera, float p_elapsedTime);
		// GPU-only replay; does not change the camera/time selected for future draws.
		void ExecuteCameraUpload(const CameraUploadCommand& p_command);
		void ExecuteFrameUpload(const FrameUploadCommand& p_command);

		// CPU-only, owned per-draw payload. Target/pass state remains external.
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
		std::optional<FrameUploadCommand> m_frameUpload;
	};
}
