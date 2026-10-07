#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace SaferSaving::IniOverrides
{
struct Override
{
    std::string_view section;
    std::string_view key;
    std::optional<std::string> value; // nullopt deletes the key
};

// the new file text, or nullopt when a_text does not parse
std::optional<std::string> Apply(std::string_view a_text, std::span<const Override> a_overrides);
} // namespace SaferSaving::IniOverrides
