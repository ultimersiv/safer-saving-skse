#include "save-prune.h"

#include "pch.h"

#include "config.h"
#include "save-files.h"

namespace
{
namespace Config    = SaferSaving::Config;
namespace SaveFiles = SaferSaving::SaveFiles;

constexpr std::string_view kPrefix{"Save"};
constexpr std::size_t kStampDigits{14};

std::uint32_t g_max{0};
std::mutex g_pruning;

struct Save
{
    std::filesystem::path path;
    std::uint64_t stamp;
};

// Save<N>_<id>_...; quicksaves and autosaves never start with Save and a digit
bool IsManualSave(std::string_view a_name, std::string_view a_ownId)
{
    if (!a_name.starts_with(kPrefix))
    {
        return false;
    }

    const auto rest = a_name.substr(kPrefix.size());
    const auto id   = rest.find_first_not_of("0123456789");
    return id != 0 && id != std::string_view::npos && rest.substr(id).starts_with(a_ownId);
}

// names end in _<YYYYMMDDHHMMSS>_<level>_1
std::optional<std::uint64_t> StampOf(std::string_view a_name)
{
    std::string_view field;
    for (int i = 0; i < 3; ++i)
    {
        const auto at = a_name.rfind('_');
        if (at == std::string_view::npos)
        {
            return std::nullopt;
        }

        field  = a_name.substr(at + 1);
        a_name = a_name.substr(0, at);
    }

    std::uint64_t stamp = 0;
    const auto parsed   = std::from_chars(field.data(), field.data() + field.size(), stamp);
    if (field.size() != kStampDigits || parsed.ec != std::errc{} || parsed.ptr != field.data() + field.size())
    {
        return std::nullopt;
    }

    return stamp;
}

// co-saves and backups share the save's name up to the first dot
std::vector<std::filesystem::path> FilesOf(const std::filesystem::path& a_directory,
                                           const std::vector<std::string>& a_names)
{
    std::vector<std::filesystem::path> files;

    std::error_code ec;
    for (const auto& item : std::filesystem::directory_iterator{a_directory, ec})
    {
        const auto name = item.path().filename().string();
        if (std::ranges::find(a_names, name.substr(0, name.find('.'))) != a_names.end())
        {
            files.push_back(item.path());
        }
    }

    return files;
}

// recycling a few hundred files can take seconds, so this runs on its own thread
void Prune(std::filesystem::path a_directory, std::string a_ownId, std::string a_newest)
{
    const std::scoped_lock lock{g_pruning};

    std::vector<Save> saves;

    std::error_code ec;
    for (const auto& item : std::filesystem::directory_iterator{a_directory, ec})
    {
        const auto& path = item.path();
        const auto name  = path.stem().string();
        // the new save may still be writing, and always keeps its place
        if (path.extension() != ".ess" || name == a_newest || !IsManualSave(name, a_ownId))
        {
            continue;
        }

        // a save that cannot be dated is never deleted
        if (const auto stamp = StampOf(name))
        {
            saves.push_back({path, *stamp});
        }
    }

    const std::size_t keep = g_max - 1;
    if (saves.size() <= keep)
    {
        return;
    }

    std::ranges::sort(saves, std::ranges::greater{}, &Save::stamp);

    std::vector<std::filesystem::path> old;
    std::vector<std::string> names;
    for (std::size_t i = keep; i < saves.size(); ++i)
    {
        old.push_back(saves[i].path);
        names.push_back(saves[i].path.stem().string());
    }

    // saves before co-saves, so quitting midway can never leave a save without its co-save
    if (!SaveFiles::Recycle(old) || !SaveFiles::Recycle(FilesOf(a_directory, names)))
    {
        logs::warn("could not move every old save to the recycle bin");
    }
}

void StartPrune(const std::string& a_name)
{
    const auto* manager   = RE::BGSSaveLoadManager::GetSingleton();
    const auto& directory = SaveFiles::Directory();
    if (!manager || directory.empty())
    {
        return;
    }

    auto ownId = SaveFiles::OwnId(*manager);
    auto name  = std::filesystem::path{a_name}.stem().string();
    if (IsManualSave(name, ownId))
    {
        std::thread{Prune, directory, std::move(ownId), std::move(name)}.detach();
    }
}
} // namespace

void SaferSaving::SavePrune::Init()
{
    const auto max = Config::maxManualSaves.GetValue();
    if (max > 0)
    {
        g_max = static_cast<std::uint32_t>((std::min)(max, Config::kMaxManualSaves));
    }
}

void SaferSaving::SavePrune::OnSaved(std::string_view a_name)
{
    if (g_max == 0 || a_name.empty())
    {
        return;
    }

    // kSaveGame can arrive off the main thread, and the engine is only safe to read from it
    SKSE::GetTaskInterface()->AddTask([name = std::string{a_name}] { StartPrune(name); });
}
