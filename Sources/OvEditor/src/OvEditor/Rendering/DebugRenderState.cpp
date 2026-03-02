/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include "OvEditor/Rendering/DebugRenderState.h"

OvEditor::Rendering::DebugRenderStateSetup::DebugRenderStateSetup(
	OvCore::Rendering::SceneRenderer& p_renderer,
	const OvRendering::Entities::Camera& p_camera
) :
	m_renderer(p_renderer)
{
	// Set up camera matrices in engine UBO
	m_renderer._SetCameraUBO(p_camera);
}
