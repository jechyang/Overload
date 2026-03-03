/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/

#pragma once

#include <OvRendering/Settings/EAccessSpecifier.h>
#include <OvRendering/Settings/EBlendingEquation.h>
#include <OvRendering/Settings/EBlendingFactor.h>
#include <OvRendering/Settings/EBufferType.h>
#include <OvRendering/Settings/EComparaisonAlgorithm.h>
#include <OvRendering/Settings/ECullFace.h>
#include <OvRendering/Settings/EDataType.h>
#include <OvRendering/Settings/EFormat.h>
#include <OvRendering/Settings/EFramebufferAttachment.h>
#include <OvRendering/Settings/EInternalFormat.h>
#include <OvRendering/Settings/EOperation.h>
#include <OvRendering/Settings/EPrimitiveMode.h>
#include <OvRendering/Settings/ERasterizationMode.h>
#include <OvRendering/Settings/ERenderingCapability.h>
#include <OvRendering/Settings/EShaderType.h>
#include <OvRendering/Settings/ETextureFilteringMode.h>
#include <OvRendering/Settings/ETextureType.h>
#include <OvRendering/Settings/ETextureWrapMode.h>
#include <OvRendering/Settings/EUniformType.h>
#include <OvRendering/Settings/EPixelDataFormat.h>
#include <OvRendering/Settings/EPixelDataType.h>
#include <OvRendering/Settings/EGraphicsBackend.h>
#include <OvTools/Utils/EnumMapper.h>

// DX12 Headers - include DX12Prerequisites.h for all DX12 types
#include <OvRendering/HAL/DirectX12/DX12Prerequisites.h>

namespace OvRendering::HAL
{
	// Helper functions for enum conversion
	template <typename ValueType, typename EnumType>
	constexpr ValueType EnumToValue(EnumType enumValue)
	{
		return OvTools::Utils::ToValueImpl<EnumType, ValueType>(enumValue);
	}

	template <typename EnumType, typename ValueType>
	constexpr EnumType ValueToEnum(ValueType value)
	{
		return OvTools::Utils::FromValueImpl<EnumType, ValueType>(value);
	}
}

// ========================================================================
// Template specializations must be in global namespace
// ========================================================================

// EComparaisonAlgorithm -> D3D12_COMPARISON_FUNC
template <>
struct OvTools::Utils::MappingFor<OvRendering::Settings::EComparaisonAlgorithm, D3D12_COMPARISON_FUNC>
{
	using EnumType = OvRendering::Settings::EComparaisonAlgorithm;
	using type = std::tuple<
		EnumValuePair<EnumType::NEVER, D3D12_COMPARISON_FUNC_NEVER>,
		EnumValuePair<EnumType::LESS, D3D12_COMPARISON_FUNC_LESS>,
		EnumValuePair<EnumType::EQUAL, D3D12_COMPARISON_FUNC_EQUAL>,
		EnumValuePair<EnumType::LESS_EQUAL, D3D12_COMPARISON_FUNC_LESS_EQUAL>,
		EnumValuePair<EnumType::GREATER, D3D12_COMPARISON_FUNC_GREATER>,
		EnumValuePair<EnumType::NOTEQUAL, D3D12_COMPARISON_FUNC_NOT_EQUAL>,
		EnumValuePair<EnumType::GREATER_EQUAL, D3D12_COMPARISON_FUNC_GREATER_EQUAL>,
		EnumValuePair<EnumType::ALWAYS, D3D12_COMPARISON_FUNC_ALWAYS>
	>;
};

// EBlendingFactor -> D3D12_BLEND
template <>
struct OvTools::Utils::MappingFor<OvRendering::Settings::EBlendingFactor, D3D12_BLEND>
{
	using EnumType = OvRendering::Settings::EBlendingFactor;
	using type = std::tuple<
		EnumValuePair<EnumType::ZERO, D3D12_BLEND_ZERO>,
		EnumValuePair<EnumType::ONE, D3D12_BLEND_ONE>,
		EnumValuePair<EnumType::SRC_COLOR, D3D12_BLEND_SRC_COLOR>,
		EnumValuePair<EnumType::ONE_MINUS_SRC_COLOR, D3D12_BLEND_INV_SRC_COLOR>,
		EnumValuePair<EnumType::DST_COLOR, D3D12_BLEND_DEST_COLOR>,
		EnumValuePair<EnumType::ONE_MINUS_DST_COLOR, D3D12_BLEND_INV_DEST_COLOR>,
		EnumValuePair<EnumType::SRC_ALPHA, D3D12_BLEND_SRC_ALPHA>,
		EnumValuePair<EnumType::ONE_MINUS_SRC_ALPHA, D3D12_BLEND_INV_SRC_ALPHA>,
		EnumValuePair<EnumType::DST_ALPHA, D3D12_BLEND_DEST_ALPHA>,
		EnumValuePair<EnumType::ONE_MINUS_DST_ALPHA, D3D12_BLEND_INV_DEST_ALPHA>,
		EnumValuePair<EnumType::CONSTANT_COLOR, D3D12_BLEND_BLEND_FACTOR>,
		EnumValuePair<EnumType::ONE_MINUS_CONSTANT_COLOR, D3D12_BLEND_INV_BLEND_FACTOR>,
		EnumValuePair<EnumType::CONSTANT_ALPHA, D3D12_BLEND_BLEND_FACTOR>,
		EnumValuePair<EnumType::ONE_MINUS_CONSTANT_ALPHA, D3D12_BLEND_INV_BLEND_FACTOR>,
		EnumValuePair<EnumType::SRC_ALPHA_SATURATE, D3D12_BLEND_SRC_ALPHA_SAT>,
		EnumValuePair<EnumType::SRC1_COLOR, D3D12_BLEND_SRC1_COLOR>,
		EnumValuePair<EnumType::ONE_MINUS_SRC1_COLOR, D3D12_BLEND_INV_SRC1_COLOR>,
		EnumValuePair<EnumType::SRC1_ALPHA, D3D12_BLEND_SRC1_ALPHA>,
		EnumValuePair<EnumType::ONE_MINUS_SRC1_ALPHA, D3D12_BLEND_INV_SRC1_ALPHA>
	>;
};

// EBlendingEquation -> D3D12_BLEND_OP
template <>
struct OvTools::Utils::MappingFor<OvRendering::Settings::EBlendingEquation, D3D12_BLEND_OP>
{
	using EnumType = OvRendering::Settings::EBlendingEquation;
	using type = std::tuple<
		EnumValuePair<EnumType::FUNC_ADD, D3D12_BLEND_OP_ADD>,
		EnumValuePair<EnumType::FUNC_SUBTRACT, D3D12_BLEND_OP_SUBTRACT>,
		EnumValuePair<EnumType::FUNC_REVERSE_SUBTRACT, D3D12_BLEND_OP_REV_SUBTRACT>,
		EnumValuePair<EnumType::MIN, D3D12_BLEND_OP_MIN>,
		EnumValuePair<EnumType::MAX, D3D12_BLEND_OP_MAX>
	>;
};

// ERasterizationMode -> D3D12_FILL_MODE
template <>
struct OvTools::Utils::MappingFor<OvRendering::Settings::ERasterizationMode, D3D12_FILL_MODE>
{
	using EnumType = OvRendering::Settings::ERasterizationMode;
	using type = std::tuple<
		EnumValuePair<EnumType::POINT, D3D12_FILL_MODE_WIREFRAME>,
		EnumValuePair<EnumType::LINE, D3D12_FILL_MODE_WIREFRAME>,
		EnumValuePair<EnumType::FILL, D3D12_FILL_MODE_SOLID>
	>;
};

// ECullFace -> D3D12_CULL_MODE
template <>
struct OvTools::Utils::MappingFor<OvRendering::Settings::ECullFace, D3D12_CULL_MODE>
{
	using EnumType = OvRendering::Settings::ECullFace;
	using type = std::tuple<
		EnumValuePair<EnumType::FRONT, D3D12_CULL_MODE_BACK>,
		EnumValuePair<EnumType::BACK, D3D12_CULL_MODE_FRONT>,
		EnumValuePair<EnumType::FRONT_AND_BACK, D3D12_CULL_MODE_NONE>
	>;
};

// EOperation -> D3D12_STENCIL_OP
template <>
struct OvTools::Utils::MappingFor<OvRendering::Settings::EOperation, D3D12_STENCIL_OP>
{
	using EnumType = OvRendering::Settings::EOperation;
	using type = std::tuple<
		EnumValuePair<EnumType::KEEP, D3D12_STENCIL_OP_KEEP>,
		EnumValuePair<EnumType::ZERO, D3D12_STENCIL_OP_ZERO>,
		EnumValuePair<EnumType::REPLACE, D3D12_STENCIL_OP_REPLACE>,
		EnumValuePair<EnumType::INCREMENT, D3D12_STENCIL_OP_INCR_SAT>,
		EnumValuePair<EnumType::INCREMENT_WRAP, D3D12_STENCIL_OP_INCR>,
		EnumValuePair<EnumType::DECREMENT, D3D12_STENCIL_OP_DECR_SAT>,
		EnumValuePair<EnumType::DECREMENT_WRAP, D3D12_STENCIL_OP_DECR>,
		EnumValuePair<EnumType::INVERT, D3D12_STENCIL_OP_INVERT>
	>;
};

// EPrimitiveMode -> D3D12_PRIMITIVE_TOPOLOGY_TYPE
template <>
struct OvTools::Utils::MappingFor<OvRendering::Settings::EPrimitiveMode, D3D12_PRIMITIVE_TOPOLOGY_TYPE>
{
	using EnumType = OvRendering::Settings::EPrimitiveMode;
	using type = std::tuple<
		EnumValuePair<EnumType::POINTS, D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT>,
		EnumValuePair<EnumType::LINES, D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE>,
		EnumValuePair<EnumType::LINE_STRIP, D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE>,
		EnumValuePair<EnumType::LINE_LOOP, D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE>,
		EnumValuePair<EnumType::TRIANGLES, D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE>,
		EnumValuePair<EnumType::TRIANGLE_STRIP, D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE>,
		EnumValuePair<EnumType::TRIANGLE_FAN, D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE>,
		EnumValuePair<EnumType::LINES_ADJACENCY, D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE>,
		EnumValuePair<EnumType::LINE_STRIP_ADJACENCY, D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE>,
		EnumValuePair<EnumType::PATCHES, D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH>
	>;
};

// EFormat -> DXGI_FORMAT
template <>
struct OvTools::Utils::MappingFor<OvRendering::Settings::EFormat, DXGI_FORMAT>
{
	using EnumType = OvRendering::Settings::EFormat;
	using type = std::tuple<
		EnumValuePair<EnumType::RED, DXGI_FORMAT_R32_FLOAT>,
		EnumValuePair<EnumType::RG, DXGI_FORMAT_R32G32_FLOAT>,
		EnumValuePair<EnumType::RGB, DXGI_FORMAT_R8G8B8A8_UNORM>,
		EnumValuePair<EnumType::BGR, DXGI_FORMAT_B8G8R8A8_UNORM>,
		EnumValuePair<EnumType::RGBA, DXGI_FORMAT_R8G8B8A8_UNORM>,
		EnumValuePair<EnumType::BGRA, DXGI_FORMAT_B8G8R8A8_UNORM>,
		EnumValuePair<EnumType::RED_INTEGER, DXGI_FORMAT_R32_UINT>,
		EnumValuePair<EnumType::RG_INTEGER, DXGI_FORMAT_R32G32_UINT>,
		EnumValuePair<EnumType::RGB_INTEGER, DXGI_FORMAT_R8G8B8A8_UINT>,
		EnumValuePair<EnumType::BGR_INTEGER, DXGI_FORMAT_R8G8B8A8_UINT>,
		EnumValuePair<EnumType::RGBA_INTEGER, DXGI_FORMAT_R8G8B8A8_UINT>,
		EnumValuePair<EnumType::BGRA_INTEGER, DXGI_FORMAT_R8G8B8A8_UINT>,
		EnumValuePair<EnumType::STENCIL_INDEX, DXGI_FORMAT_D24_UNORM_S8_UINT>,
		EnumValuePair<EnumType::DEPTH_COMPONENT, DXGI_FORMAT_D32_FLOAT>,
		EnumValuePair<EnumType::DEPTH_STENCIL, DXGI_FORMAT_D32_FLOAT_S8X24_UINT>
	>;
};

// ETextureFilteringMode -> D3D12_FILTER
template <>
struct OvTools::Utils::MappingFor<OvRendering::Settings::ETextureFilteringMode, D3D12_FILTER>
{
	using EnumType = OvRendering::Settings::ETextureFilteringMode;
	using type = std::tuple<
		EnumValuePair<EnumType::NEAREST, D3D12_FILTER_MIN_MAG_MIP_POINT>,
		EnumValuePair<EnumType::LINEAR, D3D12_FILTER_MIN_MAG_MIP_LINEAR>,
		EnumValuePair<EnumType::NEAREST_MIPMAP_NEAREST, D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR>,
		EnumValuePair<EnumType::LINEAR_MIPMAP_LINEAR, D3D12_FILTER_MIN_MAG_MIP_LINEAR>,
		EnumValuePair<EnumType::LINEAR_MIPMAP_NEAREST, D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT>,
		EnumValuePair<EnumType::NEAREST_MIPMAP_LINEAR, D3D12_FILTER_MIN_POINT_MAG_MIP_LINEAR>
	>;
};

// ETextureWrapMode -> D3D12_TEXTURE_ADDRESS_MODE
template <>
struct OvTools::Utils::MappingFor<OvRendering::Settings::ETextureWrapMode, D3D12_TEXTURE_ADDRESS_MODE>
{
	using EnumType = OvRendering::Settings::ETextureWrapMode;
	using type = std::tuple<
		EnumValuePair<EnumType::REPEAT, D3D12_TEXTURE_ADDRESS_MODE_WRAP>,
		EnumValuePair<EnumType::CLAMP_TO_EDGE, D3D12_TEXTURE_ADDRESS_MODE_CLAMP>,
		EnumValuePair<EnumType::CLAMP_TO_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER>,
		EnumValuePair<EnumType::MIRRORED_REPEAT, D3D12_TEXTURE_ADDRESS_MODE_MIRROR>,
		EnumValuePair<EnumType::MIRROR_CLAMP_TO_EDGE, D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE>
	>;
};

// EPixelDataFormat -> DXGI_FORMAT
template <>
struct OvTools::Utils::MappingFor<OvRendering::Settings::EPixelDataFormat, DXGI_FORMAT>
{
	using EnumType = OvRendering::Settings::EPixelDataFormat;
	using type = std::tuple<
		EnumValuePair<EnumType::STENCIL_INDEX, DXGI_FORMAT_D24_UNORM_S8_UINT>,
		EnumValuePair<EnumType::DEPTH_COMPONENT, DXGI_FORMAT_D32_FLOAT>,
		EnumValuePair<EnumType::RED, DXGI_FORMAT_R32_FLOAT>,
		EnumValuePair<EnumType::GREEN, DXGI_FORMAT_R32G32_FLOAT>,
		EnumValuePair<EnumType::BLUE, DXGI_FORMAT_R32G32B32_FLOAT>,
		EnumValuePair<EnumType::ALPHA, DXGI_FORMAT_R32_FLOAT>,
		EnumValuePair<EnumType::RGB, DXGI_FORMAT_R8G8B8A8_UNORM>,
		EnumValuePair<EnumType::BGR, DXGI_FORMAT_B8G8R8A8_UNORM>,
		EnumValuePair<EnumType::RGBA, DXGI_FORMAT_R8G8B8A8_UNORM>,
		EnumValuePair<EnumType::BGRA, DXGI_FORMAT_B8G8R8A8_UNORM>
	>;
};

// EDataType -> DXGI_FORMAT
template <>
struct OvTools::Utils::MappingFor<OvRendering::Settings::EDataType, DXGI_FORMAT>
{
	using EnumType = OvRendering::Settings::EDataType;
	using type = std::tuple<
		EnumValuePair<EnumType::BYTE, DXGI_FORMAT_R8_SINT>,
		EnumValuePair<EnumType::UNSIGNED_BYTE, DXGI_FORMAT_R8_UINT>,
		EnumValuePair<EnumType::SHORT, DXGI_FORMAT_R16_SINT>,
		EnumValuePair<EnumType::UNSIGNED_SHORT, DXGI_FORMAT_R16_UINT>,
		EnumValuePair<EnumType::INT, DXGI_FORMAT_R32_SINT>,
		EnumValuePair<EnumType::UNSIGNED_INT, DXGI_FORMAT_R32_UINT>,
		EnumValuePair<EnumType::FLOAT, DXGI_FORMAT_R32_FLOAT>,
		EnumValuePair<EnumType::DOUBLE, DXGI_FORMAT_R32G32B32A32_FLOAT>
	>;
};

// EInternalFormat -> DXGI_FORMAT
template <>
struct OvTools::Utils::MappingFor<OvRendering::Settings::EInternalFormat, DXGI_FORMAT>
{
	using EnumType = OvRendering::Settings::EInternalFormat;
	using type = std::tuple<
		EnumValuePair<EnumType::DEPTH_COMPONENT, DXGI_FORMAT_D32_FLOAT>,
		EnumValuePair<EnumType::DEPTH_STENCIL, DXGI_FORMAT_D32_FLOAT_S8X24_UINT>,
		EnumValuePair<EnumType::RED, DXGI_FORMAT_R32_FLOAT>,
		EnumValuePair<EnumType::RG, DXGI_FORMAT_R32G32_FLOAT>,
		EnumValuePair<EnumType::RGB, DXGI_FORMAT_R8G8B8A8_UNORM>,
		EnumValuePair<EnumType::RGBA, DXGI_FORMAT_R8G8B8A8_UNORM>,
		EnumValuePair<EnumType::R8, DXGI_FORMAT_R8_UNORM>,
		EnumValuePair<EnumType::R8_SNORM, DXGI_FORMAT_R8_SNORM>,
		EnumValuePair<EnumType::R16, DXGI_FORMAT_R16_UNORM>,
		EnumValuePair<EnumType::R16_SNORM, DXGI_FORMAT_R16_SNORM>,
		EnumValuePair<EnumType::RG8, DXGI_FORMAT_R8G8_UNORM>,
		EnumValuePair<EnumType::RG8_SNORM, DXGI_FORMAT_R8G8_SNORM>,
		EnumValuePair<EnumType::RG16, DXGI_FORMAT_R16G16_UNORM>,
		EnumValuePair<EnumType::RG16_SNORM, DXGI_FORMAT_R16G16_SNORM>,
		EnumValuePair<EnumType::RGB8, DXGI_FORMAT_R8G8B8A8_UNORM>,
		EnumValuePair<EnumType::RGB8_SNORM, DXGI_FORMAT_R8G8B8A8_SNORM>,
		EnumValuePair<EnumType::RGBA8, DXGI_FORMAT_R8G8B8A8_UNORM>,
		EnumValuePair<EnumType::RGBA8_SNORM, DXGI_FORMAT_R8G8B8A8_SNORM>,
		EnumValuePair<EnumType::RGBA16, DXGI_FORMAT_R16G16B16A16_UNORM>,
		EnumValuePair<EnumType::R16F, DXGI_FORMAT_R16_FLOAT>,
		EnumValuePair<EnumType::RG16F, DXGI_FORMAT_R16G16_FLOAT>,
		EnumValuePair<EnumType::RGB16F, DXGI_FORMAT_R16G16B16A16_FLOAT>,
		EnumValuePair<EnumType::RGBA16F, DXGI_FORMAT_R16G16B16A16_FLOAT>,
		EnumValuePair<EnumType::R32F, DXGI_FORMAT_R32_FLOAT>,
		EnumValuePair<EnumType::RG32F, DXGI_FORMAT_R32G32_FLOAT>,
		EnumValuePair<EnumType::RGB32F, DXGI_FORMAT_R32G32B32_FLOAT>,
		EnumValuePair<EnumType::RGBA32F, DXGI_FORMAT_R32G32B32A32_FLOAT>,
		EnumValuePair<EnumType::R8I, DXGI_FORMAT_R8_SINT>,
		EnumValuePair<EnumType::R8UI, DXGI_FORMAT_R8_UINT>,
		EnumValuePair<EnumType::R16I, DXGI_FORMAT_R16_SINT>,
		EnumValuePair<EnumType::R16UI, DXGI_FORMAT_R16_UINT>,
		EnumValuePair<EnumType::R32I, DXGI_FORMAT_R32_SINT>,
		EnumValuePair<EnumType::R32UI, DXGI_FORMAT_R32_UINT>,
		EnumValuePair<EnumType::SRGB8, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB>,
		EnumValuePair<EnumType::SRGB8_ALPHA8, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB>
	>;
};
