#pragma once
#include <any>
#include <functional>
#include <map>
#include <string>

namespace OvEditor::Panels::InspectorBindings
{
    // Clipboard/defaults own values, never component or widget references.
    struct PropertyClipboard
    {
        std::any value;
        std::map<int,std::string> choices;
    };
    using PropertyDefaults = std::map<std::string,std::any>;
    struct PropertyContext
    {
        PropertyClipboard* clipboard = nullptr;
        const PropertyDefaults* defaults = nullptr;
        std::function<void(std::function<void()>)> defer;
        std::function<void()> refresh;
    };
}
