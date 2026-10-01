#pragma once
#include <memory>
#include <optional>
#include <OvMaths/FMatrix4.h>
#include <OvRendering/Data/DrawBindings.h>
#include <OvRendering/Data/Material.h>
#include <OvRendering/Data/PipelineState.h>
#include <OvRendering/Resources/IMesh.h>
#include <baregl/Buffer.h>
#include <baregl/Framebuffer.h>
namespace OvRendering::Data
{
    // Typed mesh draw payload. Workers capture owned values only; execution is
    // confined to the GL owner through ExecuteMeshDraw / Driver. Portable command
    // streams should grow from these payloads rather than opaque GL closures.
    struct MeshDrawCommand
    {
        std::shared_ptr<const Resources::IMesh> geometry;
        std::shared_ptr<const Material::Snapshot> material;
        PipelineState state;
        uint32_t instances = 1;
        EngineConstants constants;
        SkinningBindings palette;
        ReflectionConstants reflection;
        uint16_t width = 0, height = 0;
        std::optional<baregl::Framebuffer::Snapshot> target;
    };
    // View setup: bind target, set viewport, apply clear. No scene access.
    struct BeginViewCommand
    {
        uint16_t width = 0, height = 0;
        bool clearColor = true, clearDepth = true, clearStencil = true;
        OvMaths::FVector4 clearValue;
        std::optional<baregl::Framebuffer::Snapshot> target;
    };
    // Owned dynamic buffers written only on the executor.
    struct MeshDrawBuffers
    {
        std::shared_ptr<baregl::Buffer> engine, lights, reflection, skinning, identity;
    };
}
