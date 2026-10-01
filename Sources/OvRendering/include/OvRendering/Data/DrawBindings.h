#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include <baregl/Texture.h>
#include <OvMaths/FMatrix4.h>

namespace OvRendering::Data
{
    struct FrameBindings { float elapsedTime = 0; };
    struct ViewBindings
    {
        OvMaths::FMatrix4 viewUpload, projectionUpload;
        OvMaths::FVector3 camera;
        std::vector<OvMaths::FMatrix4> lights;
    };
    struct LightInput
    {
        OvMaths::FVector3 position;
        float range;
        OvMaths::FMatrix4 matrix;
    };
    // std140 rounds the block to 16 bytes, including its trailing padding.
    struct alignas(16) ReflectionConstants
    {
        std::array<float, 4> position{};
        std::array<float, 16> rotation{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        std::array<float, 4> boxCenter{}, boxHalfExtents{};
        float brightness = 0;
        int32_t boxProjection = 0, local = 0, padding = 0;
    };
    static_assert(sizeof(ReflectionConstants) == 128 && offsetof(ReflectionConstants, brightness) == 112
        && offsetof(ReflectionConstants, boxProjection) == 116 && offsetof(ReflectionConstants, local) == 120);
    // Imported receiver data. A4 supplies producers and validates their content history.
    struct ShadowReceiverBindings
    {
        std::shared_ptr<baregl::Texture> map;
        OvMaths::FMatrix4 lightSpace = OvMaths::FMatrix4::Identity;
    };
    struct ReflectionReceiverBindings
    {
        std::shared_ptr<baregl::Texture> map;
        ReflectionConstants constants;
    };
    struct ReflectionProbeInput
    {
        ReflectionReceiverBindings receiver;
        OvMaths::FVector3 position, halfExtents;
        OvMaths::FMatrix4 inverseRotation;
        bool local = false;
    };
    struct SkinningBindings
    {
        bool enabled = false;
        uint64_t poseVersion = 0;
        // Shared per pose version across meshes/views; already transposed by the pose
        // publication boundary. Empty pointer means identity.
        std::shared_ptr<const std::vector<OvMaths::FMatrix4>> matrices;
    };
    struct EngineConstants
    {
        OvMaths::FMatrix4 model, view, projection;
        OvMaths::FVector3 camera;
        float elapsed;
        OvMaths::FMatrix4 user;
    };
    static_assert(sizeof(EngineConstants) == 272 && offsetof(EngineConstants, user) == 208);
}
