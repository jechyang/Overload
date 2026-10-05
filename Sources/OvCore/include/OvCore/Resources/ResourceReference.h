#pragma once
#include <string>
#include <utility>

namespace OvCore::Resources
{
    // Borrowed resource plus durable asset identity. An unavailable asset remains
    // identifiable across scene saves; ownership stays with its resource manager.
    template<class T> struct ResourceReference
    {
        T* resource = nullptr;
        std::string path;
        std::string Identity() const { return resource ? resource->path : path; }
        static ResourceReference From(T* resource, const std::string& unresolved = {})
        { return {resource, resource ? resource->path : unresolved}; }
    };
}
