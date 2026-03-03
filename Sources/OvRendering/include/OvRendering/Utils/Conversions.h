/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <cstdint>

#include <OvRendering/Settings/EPrimitiveMode.h>

#ifdef _WIN32
#include <d3d12.h>
#endif

namespace OvRendering::Utils::Conversions
{
    constexpr uint32_t _log2(float n)
    {
        return ((n < 2) ? 1 : 1 + _log2(n / 2));
    }

    constexpr float Pow2toFloat(uint8_t p_value)
    {
        return static_cast<float>(1U << p_value);
    }

    constexpr uint8_t FloatToPow2(float p_value)
    {
        return static_cast<uint8_t>(_log2(p_value) - 1);
    }

#ifdef _WIN32
    /**
    * Convert EPrimitiveMode to D3D_PRIMITIVE_TOPOLOGY
    */
    inline D3D_PRIMITIVE_TOPOLOGY ToD3D12PrimitiveTopology(Settings::EPrimitiveMode p_mode)
    {
        switch (p_mode)
        {
        case Settings::EPrimitiveMode::POINTS:
            return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
        case Settings::EPrimitiveMode::LINES:
            return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
        case Settings::EPrimitiveMode::LINE_STRIP:
            return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
        case Settings::EPrimitiveMode::LINE_LOOP:
            return D3D_PRIMITIVE_TOPOLOGY_LINELIST; // No direct equivalent
        case Settings::EPrimitiveMode::TRIANGLES:
            return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        case Settings::EPrimitiveMode::TRIANGLE_STRIP:
            return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        case Settings::EPrimitiveMode::TRIANGLE_FAN:
            return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP; // Close enough
        case Settings::EPrimitiveMode::LINES_ADJACENCY:
            return D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ;
        case Settings::EPrimitiveMode::LINE_STRIP_ADJACENCY:
            return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ;
        case Settings::EPrimitiveMode::PATCHES:
            return D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST;
        default:
            return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        }
    }
#endif
}
