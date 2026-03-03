/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*
* @note: Minimal CD3DX12 helper classes for DX12 development
* This is a simplified version. For full implementation, use
* Microsoft's official d3dx12.h from:
* https://github.com/microsoft/DirectX-Graphics-Samples
*/

#pragma once

#include <d3d12.h>
#include <dxgiformat.h>
#include <d3dcommon.h>

// CD3DX12_RESOURCE_BARRIER - simplified
struct CD3DX12_RESOURCE_BARRIER
{
    D3D12_RESOURCE_BARRIER barrier;

    CD3DX12_RESOURCE_BARRIER() { memset(&barrier, 0, sizeof(barrier)); }

    static inline CD3DX12_RESOURCE_BARRIER Transition(
        ID3D12Resource* pResource,
        D3D12_RESOURCE_STATES stateBefore,
        D3D12_RESOURCE_STATES stateAfter,
        D3D12_RESOURCE_BARRIER_FLAGS flags = D3D12_RESOURCE_BARRIER_FLAG_NONE
    )
    {
        CD3DX12_RESOURCE_BARRIER result = {};
        result.barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        result.barrier.Flags = flags;
        result.barrier.Transition.pResource = pResource;
        result.barrier.Transition.StateBefore = stateBefore;
        result.barrier.Transition.StateAfter = stateAfter;
        result.barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        return result;
    }

    static inline CD3DX12_RESOURCE_BARRIER UAV(ID3D12Resource* pResource = nullptr)
    {
        CD3DX12_RESOURCE_BARRIER result = {};
        result.barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        result.barrier.UAV.pResource = pResource;
        return result;
    }

    // Note: Alias barrier removed for SDK compatibility

    operator const D3D12_RESOURCE_BARRIER& () const { return barrier; }
};

// CD3DX12_HEAP_PROPERTIES - simplified
struct CD3DX12_HEAP_PROPERTIES : public D3D12_HEAP_PROPERTIES
{
    CD3DX12_HEAP_PROPERTIES() = default;

    explicit CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE type)
    {
        Type = type;
        CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        CreationNodeMask = 1;
        VisibleNodeMask = 1;
    }
};

// CD3DX12_RESOURCE_DESC - simplified
struct CD3DX12_RESOURCE_DESC : public D3D12_RESOURCE_DESC
{
    CD3DX12_RESOURCE_DESC() = default;

    static inline CD3DX12_RESOURCE_DESC Buffer(
        UINT64 width,
        D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
        UINT64 alignment = 0
    )
    {
        CD3DX12_RESOURCE_DESC result = {};
        result.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        result.Width = width;
        result.Height = 1;
        result.DepthOrArraySize = 1;
        result.MipLevels = 1;
        result.Format = DXGI_FORMAT_UNKNOWN;
        result.SampleDesc.Count = 1;
        result.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        result.Flags = flags;
        result.Alignment = alignment;
        return result;
    }

    static inline CD3DX12_RESOURCE_DESC Tex2D(
        DXGI_FORMAT format,
        UINT64 width,
        UINT32 height,
        UINT16 arraySize = 1,
        UINT16 mipLevels = 0,
        UINT sampleCount = 1,
        UINT sampleQuality = 0,
        D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
        D3D12_TEXTURE_LAYOUT layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
        UINT64 alignment = 0
    )
    {
        CD3DX12_RESOURCE_DESC result = {};
        result.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        result.Width = width;
        result.Height = height;
        result.DepthOrArraySize = arraySize;
        result.MipLevels = mipLevels;
        result.Format = format;
        result.SampleDesc.Count = sampleCount;
        result.SampleDesc.Quality = sampleQuality;
        result.Layout = layout;
        result.Flags = flags;
        result.Alignment = alignment;
        return result;
    }
};

// CD3DX12_RENDER_TARGET_VIEW_DESC - simplified
struct CD3DX12_RENDER_TARGET_VIEW_DESC : public D3D12_RENDER_TARGET_VIEW_DESC
{
    CD3DX12_RENDER_TARGET_VIEW_DESC() = default;
};

// CD3DX12_DEPTH_STENCIL_VIEW_DESC - simplified
struct CD3DX12_DEPTH_STENCIL_VIEW_DESC : public D3D12_DEPTH_STENCIL_VIEW_DESC
{
    CD3DX12_DEPTH_STENCIL_VIEW_DESC() = default;
};

// CD3DX12_SHADER_BYTECODE - simplified
struct CD3DX12_SHADER_BYTECODE
{
    const void* pShaderBytecode;
    SIZE_T BytecodeLength;

    CD3DX12_SHADER_BYTECODE() = default;

    CD3DX12_SHADER_BYTECODE(const void* pCode, SIZE_T length)
        : pShaderBytecode(pCode), BytecodeLength(length) {}
};

// CD3DX12_RASTERIZER_DESC - simplified
struct CD3DX12_RASTERIZER_DESC : public D3D12_RASTERIZER_DESC
{
    CD3DX12_RASTERIZER_DESC()
    {
        FillMode = D3D12_FILL_MODE_SOLID;
        CullMode = D3D12_CULL_MODE_BACK;
        FrontCounterClockwise = FALSE;
        DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
        DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
        SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
        DepthClipEnable = TRUE;
        MultisampleEnable = FALSE;
        AntialiasedLineEnable = FALSE;
        ForcedSampleCount = 0;
        ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    }

    explicit CD3DX12_RASTERIZER_DESC(int)
        : CD3DX12_RASTERIZER_DESC() {}

    static inline CD3DX12_RASTERIZER_DESC Default()
    {
        return CD3DX12_RASTERIZER_DESC(0);
    }
};

// CD3DX12_BLEND_DESC - simplified
struct CD3DX12_BLEND_DESC : public D3D12_BLEND_DESC
{
    CD3DX12_BLEND_DESC()
    {
        AlphaToCoverageEnable = FALSE;
        IndependentBlendEnable = FALSE;
        for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
        {
            RenderTarget[i].BlendEnable = FALSE;
            RenderTarget[i].LogicOpEnable = FALSE;
            RenderTarget[i].SrcBlend = D3D12_BLEND_ONE;
            RenderTarget[i].DestBlend = D3D12_BLEND_ZERO;
            RenderTarget[i].BlendOp = D3D12_BLEND_OP_ADD;
            RenderTarget[i].SrcBlendAlpha = D3D12_BLEND_ONE;
            RenderTarget[i].DestBlendAlpha = D3D12_BLEND_ZERO;
            RenderTarget[i].BlendOpAlpha = D3D12_BLEND_OP_ADD;
            RenderTarget[i].LogicOp = D3D12_LOGIC_OP_NOOP;
            RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        }
    }

    explicit CD3DX12_BLEND_DESC(int)
        : CD3DX12_BLEND_DESC() {}

    static inline CD3DX12_BLEND_DESC Default()
    {
        return CD3DX12_BLEND_DESC(0);
    }
};

// CD3DX12_DEPTH_STENCIL_DESC - simplified
struct CD3DX12_DEPTH_STENCIL_DESC : public D3D12_DEPTH_STENCIL_DESC
{
    CD3DX12_DEPTH_STENCIL_DESC()
    {
        DepthEnable = TRUE;
        DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        StencilEnable = FALSE;
        StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
        StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;

        const D3D12_DEPTH_STENCILOP_DESC defaultStencilOp =
        {
            D3D12_STENCIL_OP_KEEP,
            D3D12_STENCIL_OP_KEEP,
            D3D12_STENCIL_OP_KEEP,
            D3D12_COMPARISON_FUNC_ALWAYS
        };

        FrontFace = defaultStencilOp;
        BackFace = defaultStencilOp;
    }

    explicit CD3DX12_DEPTH_STENCIL_DESC(int)
        : CD3DX12_DEPTH_STENCIL_DESC() {}

    static inline CD3DX12_DEPTH_STENCIL_DESC Default()
    {
        return CD3DX12_DEPTH_STENCIL_DESC(0);
    }
};

// CD3DX12_INPUT_ELEMENT_DESC - simplified
struct CD3DX12_INPUT_ELEMENT_DESC : public D3D12_INPUT_ELEMENT_DESC
{
    CD3DX12_INPUT_ELEMENT_DESC() = default;

    CD3DX12_INPUT_ELEMENT_DESC(
        const char* semanticName,
        UINT semanticIndex,
        DXGI_FORMAT format,
        UINT inputSlot,
        UINT alignedByteOffset,
        D3D12_INPUT_CLASSIFICATION slotClass,
        UINT instanceDataStepRate
    )
    {
        SemanticName = semanticName;
        SemanticIndex = semanticIndex;
        Format = format;
        InputSlot = inputSlot;
        AlignedByteOffset = alignedByteOffset;
        InputSlotClass = slotClass;
        InstanceDataStepRate = instanceDataStepRate;
    }
};

// CD3DX12_VIEWPORT - simplified
struct CD3DX12_VIEWPORT : public D3D12_VIEWPORT
{
    CD3DX12_VIEWPORT() = default;

    explicit CD3DX12_VIEWPORT(float width, float height, float minDepth = 0.0f, float maxDepth = 1.0f)
    {
        TopLeftX = 0;
        TopLeftY = 0;
        Width = width;
        Height = height;
        MinDepth = minDepth;
        MaxDepth = maxDepth;
    }
};

// CD3DX12_RECT - simplified
struct CD3DX12_RECT : public D3D12_RECT
{
    CD3DX12_RECT() = default;

    CD3DX12_RECT(LONG left, LONG top, LONG right, LONG bottom)
    {
        this->left = left;
        this->top = top;
        this->right = right;
        this->bottom = bottom;
    }
};

// CD3DX12_RANGE - simplified
struct CD3DX12_RANGE : public D3D12_RANGE
{
    CD3DX12_RANGE() = default;

    CD3DX12_RANGE(SIZE_T begin, SIZE_T end)
    {
        Begin = begin;
        End = end;
    }
};

// CD3DX12_TEXTURE_COPY_LOCATION - simplified
struct CD3DX12_TEXTURE_COPY_LOCATION : public D3D12_TEXTURE_COPY_LOCATION
{
    CD3DX12_TEXTURE_COPY_LOCATION() = default;

    explicit CD3DX12_TEXTURE_COPY_LOCATION(ID3D12Resource* pRes)
    {
        pResource = pRes;
        Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        PlacedFootprint = {};
    }

    explicit CD3DX12_TEXTURE_COPY_LOCATION(ID3D12Resource* pRes, D3D12_PLACED_SUBRESOURCE_FOOTPRINT foot)
    {
        pResource = pRes;
        Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        PlacedFootprint = foot;
    }
};

// CD3DX12_CPU_DESCRIPTOR_HANDLE - simplified
struct CD3DX12_CPU_DESCRIPTOR_HANDLE : public D3D12_CPU_DESCRIPTOR_HANDLE
{
    CD3DX12_CPU_DESCRIPTOR_HANDLE() = default;

    explicit CD3DX12_CPU_DESCRIPTOR_HANDLE(D3D12_CPU_DESCRIPTOR_HANDLE other)
    {
        ptr = other.ptr;
    }

    inline void Offset(INT offsetScaledByIncrementSize)
    {
        ptr += offsetScaledByIncrementSize;
    }

    inline void Offset(INT offsetInDescriptors, UINT descriptorIncrementSize)
    {
        ptr += offsetInDescriptors * descriptorIncrementSize;
    }
};

// CD3DX12_GPU_DESCRIPTOR_HANDLE - simplified
struct CD3DX12_GPU_DESCRIPTOR_HANDLE : public D3D12_GPU_DESCRIPTOR_HANDLE
{
    CD3DX12_GPU_DESCRIPTOR_HANDLE() = default;

    explicit CD3DX12_GPU_DESCRIPTOR_HANDLE(D3D12_GPU_DESCRIPTOR_HANDLE other)
    {
        ptr = other.ptr;
    }

    inline void Offset(INT offsetScaledByIncrementSize)
    {
        ptr += offsetScaledByIncrementSize;
    }

    inline void Offset(INT offsetInDescriptors, UINT descriptorIncrementSize)
    {
        ptr += offsetInDescriptors * descriptorIncrementSize;
    }
};

// Helper macro for IID_PPV_ARGS
#ifndef IID_PPV_ARGS
#define IID_PPV_ARGS(ppType) __uuidof(**(ppType)), reinterpret_cast<void**>(ppType)
#endif
