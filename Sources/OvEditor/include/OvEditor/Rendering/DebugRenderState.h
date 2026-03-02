/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <OvCore/Rendering/SceneRenderer.h>
#include <OvRendering/Entities/Camera.h>

namespace OvEditor::Rendering
{
	/**
	* RAII helper for setting up camera UBO in debug render passes.
	* Automatically binds camera matrices to the engine UBO on construction.
	*
	* Usage:
	* @code
	* void DebugPass::Draw(PipelineState pso)
	* {
	*     DebugRenderStateSetup setup(renderer, camera);
	*     // ... render code, camera UBO is automatically set up ...
	* }
	* @endcode
	*/
	class DebugRenderStateSetup
	{
	public:
		/**
		* Constructor - automatically sets up camera UBO
		* @param p_renderer The renderer (must be or inherit from SceneRenderer)
		* @param p_camera The camera to use for matrix setup
		*/
		DebugRenderStateSetup(
			OvCore::Rendering::SceneRenderer& p_renderer,
			const OvRendering::Entities::Camera& p_camera
		);

		/**
		* Destructor - currently no cleanup needed (UBO state is overwritten by next pass)
		*/
		~DebugRenderStateSetup() = default;

		// Non-copyable to enforce RAII semantics
		DebugRenderStateSetup(const DebugRenderStateSetup&) = delete;
		DebugRenderStateSetup& operator=(const DebugRenderStateSetup&) = delete;

	private:
		OvCore::Rendering::SceneRenderer& m_renderer;
	};
}
