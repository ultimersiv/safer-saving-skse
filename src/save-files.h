#pragma once

namespace SaferSaving::SaveFiles
{
// empty until the game can resolve it
const std::filesystem::path& Directory();

// the _<id>_ every save of the current character has in its name
std::string OwnId(const RE::BGSSaveLoadManager& a_manager);

// false if any file could not be moved
bool Recycle(const std::vector<std::filesystem::path>& a_paths);

// nullopt for anything beyond ASCII, which path::string() throws on when the code page lacks it
std::optional<std::string> AsciiName(const std::filesystem::path& a_path);

// stops at the first error, where the range-for increment would throw
template <class Fn>
void ForEachEntry(const std::filesystem::path& a_directory, Fn&& a_fn)
{
    std::error_code ec;
    for (std::filesystem::directory_iterator it{a_directory, ec}, end; !ec && it != end; it.increment(ec))
    {
        a_fn(*it);
    }
}
} // namespace SaferSaving::SaveFiles
