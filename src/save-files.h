#pragma once

namespace SaferSaving::SaveFiles
{
// where the game writes saves, SLocalSavePath and any profile folder resolved; empty if it will not resolve
const std::filesystem::path& Directory();

// the _<character id>_ fragment every save of the character now being played carries in its name
std::string OwnId(const RE::BGSSaveLoadManager& a_manager);

// to the Recycle Bin, in one batch the player can undo in one go; false if Windows refused any of it
bool Recycle(const std::vector<std::filesystem::path>& a_paths);
} // namespace SaferSaving::SaveFiles
