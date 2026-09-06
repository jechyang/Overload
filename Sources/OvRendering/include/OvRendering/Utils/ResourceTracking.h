#pragma once
#include <baregl/Texture.h>
#include <optional>
#include <vector>
namespace OvRendering::Utils::ResourceTracking
{
    // Immutable metadata values at query time, owning GPU storage, no wrapper
    // borrow. Pixels remain mutable; generation distinguishes reused native IDs.
    struct TextureRegistryEntryDesc
    {
        uint32_t id = 0;
        uint64_t generation = 0;
        std::string name;
        baregl::types::ETextureType type{};
        uint32_t width = 0, height = 0;
        bool allocated = false;
        baregl::Texture::Snapshot storage;
    };
    // Install once before starting resource producers. Do not replace baregl's
    // global event handler while tracking/producers are active.
    void TrackResources();
    // Managed storage may be retained by another thread. Legacy retention from
    // another thread is rejected; legacy copies must stay on their GL owner.
    std::optional<TextureRegistryEntryDesc> GetTexture(uint32_t id, uint64_t generation = 0);
    std::vector<TextureRegistryEntryDesc> GetTextures();
    std::vector<uint32_t> GetTextureIDs(); // CPU values, no borrowed span.
}
