/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#if defined(GRAPHICS_API_DIRECTX12)
#include <OvRendering/HAL/DirectX12/DX12Backend.h>
#elif defined(GRAPHICS_API_OPENGL)
#include <OvRendering/HAL/OpenGL/GLBackend.h>
#else
#include <OvRendering/HAL/None/NoneBackend.h>
#endif

namespace OvRendering::HAL
{
#if defined(GRAPHICS_API_DIRECTX12)
	using Backend = DX12Backend;
#elif defined(GRAPHICS_API_OPENGL)
	using Backend = GLBackend;
#else
	using Backend = NoneBackend;
#endif
}
