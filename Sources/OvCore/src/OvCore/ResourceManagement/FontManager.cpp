#include <OvCore/ResourceManagement/FontManager.h>

OvCore::UI::Fonts::Font* OvCore::ResourceManagement::FontManager::CreateResource(const std::filesystem::path& path)
{
    return new UI::Fonts::Font(GetRealPath(path));
}
void OvCore::ResourceManagement::FontManager::DestroyResource(UI::Fonts::Font* resource) { delete resource; }
void OvCore::ResourceManagement::FontManager::ReloadResource(UI::Fonts::Font* resource, const std::filesystem::path& path)
{
    if (resource) resource->Reload(GetRealPath(path));
}
