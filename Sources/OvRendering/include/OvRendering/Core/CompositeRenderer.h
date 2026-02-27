/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <OvRendering/Core/ABaseRenderer.h>
#include <OvRendering/Data/Describable.h>
#include <OvRendering/FrameGraph/FrameGraph.h>

namespace OvRendering::Core
{
	/**
	* A renderer driven by a FrameGraph. Subclasses implement BuildFrameGraph()
	* to declare passes and resource dependencies each frame.
	*/
	class CompositeRenderer : public ABaseRenderer, public Data::Describable
	{
	public:
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

	protected:
		/**
		* Subclasses implement this to register FrameGraph passes each frame.
		* Called between Reset() and Compile() inside DrawFrame().
		*/
		virtual void BuildFrameGraph(FrameGraph::FrameGraph& p_fg) = 0;

		FrameGraph::FrameGraph m_frameGraph;
	};
}
