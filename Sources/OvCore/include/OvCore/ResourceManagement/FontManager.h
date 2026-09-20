#pragma once
#include <OvCore/ResourceManagement/AResourceManager.h>
#include <OvCore/UI/Fonts/Font.h>

namespace OvCore::ResourceManagement
{
    // Uses the existing project/engine path and resource ownership conventions.
    class FontManager final : public AResourceManager<UI::Fonts::Font>
    {
    public:
        ~FontManager() { UnloadResources(); }
        using AResourceManager<UI::Fonts::Font>::ReloadResource;
        std::filesystem::path ResolvePath(const std::filesystem::path& path) const { return GetRealPath(path); }
        UI::Fonts::Font* CreateResource(const std::filesystem::path& path) override;
        void DestroyResource(UI::Fonts::Font* resource) override;
        void ReloadResource(UI::Fonts::Font* resource, const std::filesystem::path& path) override;
    };
}
