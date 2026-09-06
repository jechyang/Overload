/**
* @project: Overload
* @author: Overload Tech.
* @licence: MIT
*/
#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include <baregl/Texture.h>

namespace OvUI::Internal
{
    struct TextureID;

    // Scoped producer-only collection used while building an owned UI frame.
    class TextureCaptureScope
    {
        std::vector<TextureID>* m_previous;
        static thread_local std::vector<TextureID>* s_current;
        friend struct TextureID;
    public:
        explicit TextureCaptureScope(std::vector<TextureID>& textures);
        ~TextureCaptureScope();
        TextureCaptureScope(const TextureCaptureScope&) = delete;
        TextureCaptureScope& operator=(const TextureCaptureScope&) = delete;
    };

    struct TextureID
    {
        union { uint32_t id; void* raw; };
        std::shared_ptr<baregl::Texture> owner;
        baregl::Texture::Snapshot storage;
        TextureID(uint32_t value = 0) : id(value) {}
        TextureID(std::shared_ptr<baregl::Texture> texture) : id(texture ? texture->GetID() : 0), owner(std::move(texture)), storage(owner ? owner->RetainStorage() : baregl::Texture::Snapshot{}) {}
        TextureID(baregl::Texture::Snapshot texture) : id(texture.GetID()), storage(std::move(texture)) {}
        bool IsRetained() const { return id != 0 && storage.GetID() == id; }
        void RetainForCapture() const;
    };
}
