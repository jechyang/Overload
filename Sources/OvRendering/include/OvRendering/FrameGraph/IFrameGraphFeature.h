/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <OvRendering/FrameGraph/FrameGraph.h>
#include <OvRendering/FrameGraph/FrameGraphBuilder.h>
#include <OvRendering/FrameGraph/FrameGraphResources.h>
#include <OvRendering/Data/FrameDescriptor.h>

namespace OvRendering::FrameGraph
{
	/**
	* Interface for render features integrated with FrameGraph.
	* Replaces ARenderFeature event-based system with explicit frame graph integration.
	*/
	class IFrameGraphFeature
	{
	public:
		/**
		* Virtual destructor
		*/
		virtual ~IFrameGraphFeature() = default;

		/**
		* Called during BuildFrameGraph to allow feature to register passes and resources.
		* @param p_frameGraph The frame graph being built
		* @param p_builder Resource builder for declaring dependencies
		*/
		virtual void OnBuildFrameGraph(FrameGraph& p_frameGraph, FrameGraphBuilder& p_builder) = 0;

		/**
		* Called during Execute to allow feature to perform rendering.
		* @param p_resources Resource accessor for bound resources
		*/
		virtual void OnExecute(const FrameGraphResources& p_resources) = 0;

		/**
		* Called when BeginFrame is called on the renderer.
		* @param p_frameDescriptor Frame descriptor
		*/
		virtual void OnBeginFrame(const Data::FrameDescriptor& p_frameDescriptor) {}

		/**
		* Called when EndFrame is called on the renderer.
		*/
		virtual void OnEndFrame() {}

		/**
		* Returns true if the feature is enabled.
		*/
		virtual bool IsEnabled() const { return true; }
	};
}
