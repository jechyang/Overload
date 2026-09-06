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
    // Scoped producer-only collection used while building an owned UI frame.
    class TextureCaptureScope
    {
        std::vector<std::shared_ptr<baregl::Texture>>* m_previous;
        static thread_local std::vector<std::shared_ptr<baregl::Texture>>* s_current;
        friend struct TextureID;
    public:
        explicit TextureCaptureScope(std::vector<std::shared_ptr<baregl::Texture>>& textures);
        ~TextureCaptureScope();
        TextureCaptureScope(const TextureCaptureScope&) = delete;
        TextureCaptureScope& operator=(const TextureCaptureScope&) = delete;
    };

    struct TextureID
    {
        union { uint32_t id; void* raw; };
        std::shared_ptr<baregl::Texture> owner;
        TextureID(uint32_t value = 0) : id(value) {}
        TextureID(std::shared_ptr<baregl::Texture> texture) : id(texture ? texture->GetID() : 0), owner(std::move(texture)) {}
        void RetainForCapture() const;
    };
}
