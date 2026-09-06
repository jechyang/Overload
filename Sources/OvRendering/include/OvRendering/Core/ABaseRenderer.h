/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <atomic>
#include <span>

#include <OvRendering/Core/IRenderer.h>
#include <OvRendering/Data/FrameInfo.h>
#include <OvRendering/Data/RenderCommandList.h>
#include <OvRendering/Resources/IMesh.h>
#include <OvRendering/Resources/Texture.h>
#include <OvRendering/Entities/Drawable.h>
#include <OvRendering/Settings/EBlitFlags.h>
#include <OvRendering/Context/Driver.h>

namespace OvRendering::Core
{
	/**
	* A simple base renderer that doesn't handle any object binding, but provide a strong base for other renderers
	* to implement their own logic.
	*/
	class ABaseRenderer : public IRenderer
	{
	public:
		/**
		* Constructor of the base renderer
		* @param p_driver
		*/
		ABaseRenderer(Context::Driver& p_driver);

		/**
		* Destructor of the base renderer
		*/
		virtual ~ABaseRenderer() = default;

		/**
		* Begin Frame
		* @param p_frameDescriptor
		*/
		virtual void BeginFrame(const Data::FrameDescriptor& p_frameDescriptor);

		/**
		* End Frame
		*/
		virtual void EndFrame();

		/**
		* Returns a reference to the current frame descriptor
		* @note Cannot be called outside of a frame drawing operation
		*/
		const Data::FrameDescriptor& GetFrameDescriptor() const;

		/**
		* Create a pipeline state object.
		* The settings are set with default settings provided by the renderer
		*/
		Data::PipelineState CreatePipelineState() const;

		/**
		* Returns true if the renderer is currently drawing a frame
		*/
		bool IsDrawing() const;

		/**
		* Set the viewport
		* @param p_x
		* @param p_y
		* @param p_width
		* @param p_height
		*/
		void SetViewport(uint32_t p_x, uint32_t p_y, uint32_t p_width, uint32_t p_height);

		/**
		* Clear the screen
		* @param p_colorBuffer
		* @param p_depthBuffer
		* @param p_stencilBuffer
		* @param p_color
		*/
		virtual void Clear(
			bool p_colorBuffer,
			bool p_depthBuffer,
			bool p_stencilBuffer,
			const OvMaths::FVector4& p_color = OvMaths::FVector4::Zero
		);

		/**
		* Draw a fullscreen quad with the given material
		* @param p_pso
		* @param p_src
		* @param p_dst
		* @param p_material
		* @param p_flags
		*/
		virtual void Blit(
			OvRendering::Data::PipelineState p_pso,
			baregl::Framebuffer& p_src,
			baregl::Framebuffer& p_dst,
			OvRendering::Data::Material& p_material,
			OvRendering::Settings::EBlitFlags p_flags = OvRendering::Settings::EBlitFlags::DEFAULT
		);

		// Own additional sampler inputs for the duration of this synchronous blit.
		// Attachment contents and the destination remain externally ordered.
		void BlitWithRetainedTextures(
			Data::PipelineState p_pso, baregl::Framebuffer& p_src, baregl::Framebuffer& p_dst,
			Data::Material& p_material, Settings::EBlitFlags p_flags,
			std::span<const std::shared_ptr<baregl::Texture>> p_textures);

		/**
		* Returns true if the entity is drawable. Should be used before submitting an entity.
		* @param p_drawable
		*/
		virtual bool IsDrawable(const Entities::Drawable& p_drawable) const;

		/**
		* Draw a drawable entity
		* @note Any submitted entity should be drawable (Use IsDrawable before)
		* @param p_pso
		* @param p_drawable
		*/
		virtual void DrawEntity(
			OvRendering::Data::PipelineState p_pso,
			const Entities::Drawable& p_drawable
		);

		struct ViewportCommand
		{
			uint32_t x = 0, y = 0, width = 0, height = 0;
		};
		struct PassStateCommand
		{
			std::optional<baregl::Framebuffer::Snapshot> target;
			ViewportCommand viewport;
		};
		struct ClearCommand
		{
			PassStateCommand state;
			bool colorBuffer, depthBuffer, stencilBuffer;
			OvMaths::FVector4 color;
		};
		// Select via this API while drawing. Recording copies CPU state only.
		// Execution and final resource release currently require the GL owner.
		void BindTarget(baregl::Framebuffer* p_target);
		PassStateCommand RecordPassState() const;
		void ExecutePassState(const PassStateCommand& p_command);
		ClearCommand PrepareClear(bool p_colorBuffer, bool p_depthBuffer, bool p_stencilBuffer,
			const OvMaths::FVector4& p_color = OvMaths::FVector4::Zero) const;
		void ExecuteClear(const ClearCommand& p_command);

		// Record one or more complete views before handing off the owned sequence.
		void BeginCommandRecording();
		Data::RenderCommandList FinishCommandRecording();
		void CancelCommandRecording();
        Data::RenderCommandList::Completion GetCommandCompletion() const
        { return m_commands ? m_commands->GetCompletion() : Data::RenderCommandList::Completion{}; }
		bool IsRecordingCommands() const { return m_commands != nullptr; }
        // Returns true if the effect must initialize its temporal history again.
        bool UseRenderHistory(Data::RenderCommandList::History& p_history)
        { return m_commands ? m_commands->UseHistory(p_history) : p_history.PrepareImmediate(); }
		void SubmitRenderOperation(Data::RenderCommandList::Operation p_operation);
		static void ExecutePassState(Context::Driver& p_driver, const PassStateCommand& p_command);

		struct RetainedTexturesDescriptor
		{
			std::vector<std::shared_ptr<baregl::Texture>> textures;
		};

	protected:
		// First command boundary: scalar geometry state is captured by value.
		// Standard Mesh GPU storage is retained. Custom IMesh implementations remain
		// borrowed and must stay stable. This is NOT a self-contained frame command:
		// shaders, textures, uniforms, palettes, viewport and targets are external.
		// Production consumes this in the same draw, after feature/material setup.
		// Retained geometry can outlive its source when all external bindings are
		// supplied again; borrowed geometry cannot. Worker GL execution is forbidden.
		struct GeometryDrawCommand
		{
			const Data::PipelineState pipelineState;
			const std::shared_ptr<const Resources::IMesh> retainedMesh;
			const Resources::IMesh& mesh;
			const baregl::types::EPrimitiveMode primitiveMode;
			const uint32_t instances;
		};

		// Pure CPU preparation; does not retain the Drawable or its Material.
		static GeometryDrawCommand PrepareGeometryDraw(Data::PipelineState p_pso, const Entities::Drawable& p_drawable);
		void ExecuteGeometryDraw(const GeometryDrawCommand& p_command);

		Data::FrameDescriptor m_frameDescriptor;
		Context::Driver& m_driver;
		baregl::Texture m_emptyTexture2D;
		baregl::Texture m_emptyTextureCube;
		OvRendering::Resources::Mesh m_unitQuad;
		OvRendering::Data::PipelineState m_basePipelineState;
		bool m_isDrawing;
		std::optional<OvRendering::Data::MaterialSignatureSet> m_previousMaterialSignature = std::nullopt;

		PassStateCommand m_passState;
		std::unique_ptr<Data::RenderCommandList> m_commands;

	private:
		static std::atomic_bool s_isDrawing;
	};
}
