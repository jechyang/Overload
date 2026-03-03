/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <OvRendering/HAL/OpenGL/GLUniformBuffer.h>
#include <OvRendering/HAL/OpenGL/GLTypes.h>

template<>
OvRendering::HAL::GLUniformBuffer::TUniformBuffer() : GLBuffer(Settings::EBufferType::UNIFORM)
{
}
