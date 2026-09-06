/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include <OvMaths/FMatrix4.h>
#include <OvRendering/Features/ARenderFeature.h>
#include <baregl/Buffer.h>

namespace OvCore::Rendering
{
	/**
	* Render feature responsible for uploading and binding skinning matrices.
	*/
	class SkinningRenderFeature : public OvRendering::Features::ARenderFeature
	{
	public:
		bool SupportsCommandRecording() const override { return SupportsDrawRecording(); }
		bool SupportsDrawRecording() const override { return typeid(*this) == typeid(SkinningRenderFeature); }

		static constexpr uint32_t kDefaultBufferBindingPoint = 1;

		/**
		* Constructor
		* @param p_renderer
		* @param p_executionPolicy
		* @param p_bufferBindingPoint
		*/
		SkinningRenderFeature(
			OvRendering::Core::CompositeRenderer& p_renderer,
			OvRendering::Features::EFeatureExecutionPolicy p_executionPolicy,
			uint32_t p_bufferBindingPoint = kDefaultBufferBindingPoint
		);

		/**
		* Returns the skinning buffer binding point
		*/
		uint32_t GetBufferBindingPoint() const;

		// Owns already-transposed palette values; no component or Drawable reference.
		// Capture on a stable CPU input; execute/release on the GL owner in a frame.
		class PaletteSnapshot
		{
		public:
			PaletteSnapshot(PaletteSnapshot&&) noexcept = default;
			PaletteSnapshot& operator=(PaletteSnapshot&&) noexcept = default;
			PaletteSnapshot(const PaletteSnapshot&) = delete;
			PaletteSnapshot& operator=(const PaletteSnapshot&) = delete;
		private:
			friend class SkinningRenderFeature;
			PaletteSnapshot() = default;
			bool enabled = false;
			std::vector<OvMaths::FMatrix4> matrices;
		};
		static PaletteSnapshot RecordPalette(const OvRendering::Entities::Drawable& p_drawable);
		// Independent replay invalidates the immediate borrowed-input cache.
		void ExecutePaletteSnapshot(const PaletteSnapshot& p_snapshot);

	protected:
		virtual void OnBeginFrame(const OvRendering::Data::FrameDescriptor& p_frameDescriptor) override;
		virtual void OnEndFrame() override;
		virtual void OnBeforeDraw(OvRendering::Data::PipelineState& p_pso, const OvRendering::Entities::Drawable& p_drawable) override;

	private:
		static void ExecuteRetainedPalette(const PaletteSnapshot& p_snapshot, baregl::Buffer& p_buffer, baregl::Buffer& p_identity, uint32_t p_bindingPoint);
		void UploadPalette(const PaletteSnapshot& p_snapshot);
		void BindIdentityPalette() const;
		baregl::Buffer& GetCurrentSkinningBuffer() const;

	private:
		static constexpr uint32_t kSkinningBufferRingSize = 3;

		enum class EBoundPalette
		{
			NONE,
			IDENTITY,
			SKINNING
		};

		struct UploadedPaletteState
		{
			const OvMaths::FMatrix4* ptr = nullptr;
			uint32_t count = 0;
			uint64_t poseVersion = 0;
		};

		struct BoundPaletteState
		{
			EBoundPalette type = EBoundPalette::NONE;
			const OvMaths::FMatrix4* ptr = nullptr;
			uint32_t count = 0;
			uint64_t poseVersion = 0;
		};

		uint32_t m_bufferBindingPoint;
		mutable uint32_t m_skinningBufferIndex = kSkinningBufferRingSize - 1;
		std::array<std::shared_ptr<baregl::Buffer>, kSkinningBufferRingSize> m_skinningBuffers;
		std::shared_ptr<baregl::Buffer> m_identityBuffer;

		UploadedPaletteState m_lastUploaded;
		mutable BoundPaletteState m_bound;
	};
}
