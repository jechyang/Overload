/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <OvRendering/Core/ABaseRenderer.h>
#include <OvRendering/Data/Describable.h>
#include <OvRendering/FrameGraph/FrameGraph.h>
#include <OvRendering/HAL/UniformBuffer.h>
#include <OvMaths/FMatrix4.h>

namespace OvRendering::Core
{
	/**
	* A renderer driven by a FrameGraph. Subclasses implement BuildFrameGraph()
	* to declare passes and resource dependencies each frame.
	*
	* @deprecated preDrawEntityEvent/postDrawEntityEvent are deprecated and will be removed
	* in a future version. Use FrameGraph passes directly instead.
	*/
	class CompositeRenderer : public ABaseRenderer, public Data::Describable
	{
	public:
		// TODO: Remove event system in future version when all features are migrated to FrameGraph
		OvTools::Eventing::Event<OvRendering::Data::PipelineState&, const Entities::Drawable&> preDrawEntityEvent;
		OvTools::Eventing::Event<const Entities::Drawable&> postDrawEntityEvent;

		/**
		* Constructor
		* @param p_driver
		*/
		CompositeRenderer(Context::Driver& p_driver);

		/**
		* Begin Frame
		* @param p_frameDescriptor
		*/
		virtual void BeginFrame(const Data::FrameDescriptor& p_frameDescriptor) override;

		/**
		* Resets the FrameGraph, calls BuildFrameGraph, compiles and executes.
		*/
		virtual void DrawFrame() final;

		/**
		* End Frame
		*/
		virtual void EndFrame() override;

		/**
		* Draw a drawable entity, firing pre/post draw events.
		* @param p_pso
		* @param p_drawable
		*/
		virtual void DrawEntity(
			OvRendering::Data::PipelineState p_pso,
			const Entities::Drawable& p_drawable
		) override;

		/**
		* Upload model/user matrices to engine UBO and draw a drawable.
		* Helper method to avoid code duplication in DrawModelWithSingleMaterial implementations.
		* @param p_pso Pipeline state
		* @param p_drawable Drawable to draw
		* @param p_engineUBO Engine uniform buffer
		* @param p_modelMatrix Model matrix (world space, will be transposed for UBO)
		* @param p_userMatrix User matrix (defaults to identity)
		* @param p_uboSize Size of the engine UBO (for calculating user matrix offset)
		*/
		void UploadMatricesAndDraw(
			OvRendering::Data::PipelineState p_pso,
			const Entities::Drawable& p_drawable,
			OvRendering::HAL::UniformBuffer& p_engineUBO,
			const OvMaths::FMatrix4& p_modelMatrix,
			const OvMaths::FMatrix4& p_userMatrix = OvMaths::FMatrix4::Identity,
			size_t p_uboSize = 4 * sizeof(OvMaths::FMatrix4) + sizeof(OvMaths::FVector3) + sizeof(float)
		);

	protected:
		/**
		* Subclasses implement this to register FrameGraph passes each frame.
		* Called between Reset() and Compile() inside DrawFrame().
		*/
		virtual void BuildFrameGraph(FrameGraph::FrameGraph& p_fg) = 0;

		FrameGraph::FrameGraph m_frameGraph;
	};
}
