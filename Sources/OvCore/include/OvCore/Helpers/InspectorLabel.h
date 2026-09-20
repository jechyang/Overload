#pragma once
#include <cctype>
#include <string>
#include <string_view>

namespace OvCore::Helpers
{
    inline std::string InspectorLabel(std::string_view name)
    {
        if (name.starts_with("m_")) name.remove_prefix(2);
        while (name.starts_with('_')) name.remove_prefix(1);
        std::string result;
        for (size_t i = 0; i < name.size(); ++i)
        {
            const auto c = static_cast<unsigned char>(name[i]);
            if (c == '_') { if (!result.empty() && result.back() != ' ') result += ' '; continue; }
            const bool upper = std::isupper(c) != 0;
            const bool previousLower = i && (std::islower(static_cast<unsigned char>(name[i-1])) || std::isdigit(static_cast<unsigned char>(name[i-1])));
            const bool wordAfterAcronym = i && upper && i+1 < name.size() && std::isupper(static_cast<unsigned char>(name[i-1])) && std::islower(static_cast<unsigned char>(name[i+1]));
            if (!result.empty() && result.back() != ' ' && upper && (previousLower || wordAfterAcronym)) result += ' ';
            result += result.empty() ? static_cast<char>(std::toupper(c)) : static_cast<char>(c);
        }
        return result;
    }
}
