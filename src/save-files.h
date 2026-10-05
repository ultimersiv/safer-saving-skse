#pragma once

namespace SaferSaving::SaveFiles
{
// empty until the game can resolve it
const std::filesystem::path& Directory();

// the _<id>_ every save of the current character has in its name
std::string OwnId(const RE::BGSSaveLoadManager& a_manager);

// false if any file could not be moved
bool Recycle(const std::vector<std::filesystem::path>& a_paths);
} // namespace SaferSaving::SaveFiles
