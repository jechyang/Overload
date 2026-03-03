/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#include <OvRendering/HAL/OpenGL/GLTypes.h>
#include <OvRendering/HAL/OpenGL/GLIndexBuffer.h>

template<>
OvRendering::HAL::GLIndexBuffer::TIndexBuffer() : TBuffer(Settings::EBufferType::INDEX)
{
}
