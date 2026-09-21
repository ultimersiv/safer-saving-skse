#include "save-prune.h"

#include "pch.h"

#include "config.h"
#include "save-files.h"

namespace
{
namespace Config    = SaferSaving::Config;
namespace SaveFiles = SaferSaving::SaveFiles;

// vanilla names a manual save Save<N>_; Quicksave, Autosave and our own Autosave1NN all miss it
constexpr std::string_view kNamePrefix{"Save"};
constexpr std::string_view kSaveExtension{".ess"};
// YYYYMMDDHHMMSS
constexpr std::size_t kStampDigits{14};
constexpr std::int32_t kMaxSaves{999};

// set once by Init; zero keeps every save
std::uint32_t g_max{0};

// kSaveGame can arrive off the main thread, so the generation is read there and checked on the main one
std::atomic<std::uint32_t> g_generation{0};
static_assert(decltype(g_generation)::is_always_lock_free);

struct Save
{
    std::uint64_t stamp;
    std::string name;
};

// kSaveGame hands over a name that may still carry a folder and the extension
std::string StemOf(std::string_view a_name)
{
    std::string stem = std::filesystem::path{a_name}.filename().string();
    if (stem.size() > kSaveExtension.size() && stem.ends_with(kSaveExtension))
    {
        stem.erase(stem.size() - kSaveExtension.size());
    }

    return stem;
}

// Save<digits>_<character id>_; the run of digits is what tells a manual save from every other kind
bool IsManualSave(std::string_view a_stem, std::string_view a_ownId)
{
    if (!a_stem.starts_with(kNamePrefix))
    {
        return false;
    }

    const auto digits    = a_stem.substr(kNamePrefix.size());
    std::uint32_t number = 0;
    const auto parsed    = std::from_chars(digits.data(), digits.data() + digits.size(), number);
    if (parsed.ec != std::errc{})
    {
        return false;
    }

    const std::string_view rest{parsed.ptr, digits.data() + digits.size()};
    return rest.starts_with(a_ownId);
}

// the engine closes a name with _<stamp>_<level>_1, so the stamp is found by counting back from the
// end and nothing in the variable middle of the name can shift it
std::optional<std::uint64_t> StampOf(std::string_view a_stem)
{
    const auto atTrailing = a_stem.find_last_of('_');
    if (atTrailing == std::string_view::npos || atTrailing == 0)
    {
        return std::nullopt;
    }

    const auto atLevel = a_stem.find_last_of('_', atTrailing - 1);
    if (atLevel == std::string_view::npos || atLevel == 0)
    {
        return std::nullopt;
    }

    const auto atStamp = a_stem.find_last_of('_', atLevel - 1);
    if (atStamp == std::string_view::npos)
    {
        return std::nullopt;
    }

    const auto field = a_stem.substr(atStamp + 1, atLevel - atStamp - 1);
    if (field.size() != kStampDigits)
    {
        return std::nullopt;
    }

    std::uint64_t stamp = 0;
    const auto parsed   = std::from_chars(field.data(), field.data() + field.size(), stamp);
    if (parsed.ec != std::errc{} || parsed.ptr != field.data() + field.size())
    {
        return std::nullopt;
    }

    return stamp;
}

// a name the engine did not write carries no stamp; its file time, put on the same scale, still
// leaves one order over the whole set
std::uint64_t StampFromFileTime(const std::filesystem::path& a_path)
{
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (!GetFileAttributesExW(a_path.c_str(), GetFileExInfoStandard, &attributes))
    {
        return 0;
    }

    // local time, to sit on the same scale as the stamp the engine writes into the name
    FILETIME local{};
    SYSTEMTIME time{};
    if (!FileTimeToLocalFileTime(&attributes.ftLastWriteTime, &local) || !FileTimeToSystemTime(&local, &time))
    {
        return 0;
    }

    std::uint64_t stamp = time.wYear;
    stamp               = stamp * 100 + time.wMonth;
    stamp               = stamp * 100 + time.wDay;
    stamp               = stamp * 100 + time.wHour;
    stamp               = stamp * 100 + time.wMinute;
    stamp               = stamp * 100 + time.wSecond;
    return stamp;
}

// newest first, and by name under a tie so two saves sharing a second still have one order
bool Newer(const Save& a_lhs, const Save& a_rhs)
{
    return a_lhs.stamp != a_rhs.stamp ? a_lhs.stamp > a_rhs.stamp : a_lhs.name > a_rhs.name;
}

// every manual save of this character on disk, newest first
std::vector<Save> Gather(const std::filesystem::path& a_directory, std::string_view a_ownId)
{
    std::vector<Save> saves;

    std::error_code ec;
    for (const auto& item : std::filesystem::directory_iterator{a_directory, ec})
    {
        // the ess is the save; the co-save and any bak follow it by name
        if (item.path().extension() != kSaveExtension)
        {
            continue;
        }

        auto name = item.path().stem().string();
        if (!IsManualSave(name, a_ownId))
        {
            continue;
        }

        const auto stamp = StampOf(name);
        saves.push_back(Save{stamp.value_or(StampFromFileTime(item.path())), std::move(name)});
    }

    std::ranges::sort(saves, Newer);
    return saves;
}

// the ess, the co-save and any bak all open with the save name and a dot
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

// from the task queue, the frame after kSaveGame, by which time the new save is on disk
void Prune(const std::string& a_name, std::uint32_t a_generation)
{
    // a load since then means this save belongs to a session that is gone
    if (g_generation.load(std::memory_order_relaxed) != a_generation)
    {
        return;
    }

    const auto* manager = RE::BGSSaveLoadManager::GetSingleton();
    if (!manager)
    {
        return;
    }

    const auto& directory = SaveFiles::Directory();
    if (directory.empty())
    {
        return;
    }

    const auto ownId = SaveFiles::OwnId(*manager);
    const auto stem  = StemOf(a_name);

    // only a manual save of the character being played moves the cap; every other kind is left alone
    if (!IsManualSave(stem, ownId))
    {
        return;
    }

    auto saves = Gather(directory, ownId);

    // the file may not have landed yet, so the save that triggered this is counted either way
    if (std::ranges::none_of(saves, [&stem](const Save& a_save) { return a_save.name == stem; }))
    {
        if (const auto stamp = StampOf(stem))
        {
            saves.push_back(Save{*stamp, stem});
            std::ranges::sort(saves, Newer);
        }
    }

    if (saves.size() <= g_max)
    {
        return;
    }

    std::vector<std::string> doomed;
    for (std::size_t i = g_max; i < saves.size(); ++i)
    {
        // a clock that went backwards could sort the new save last; it is never ours to take
        if (saves[i].name != stem)
        {
            doomed.push_back(saves[i].name);
        }
    }

    const auto files = FilesOf(directory, doomed);
    if (!SaveFiles::Recycle(files))
    {
        logs::warn("could not send {} old save file(s) to the recycle bin", files.size());
    }
}
} // namespace

void SaferSaving::SavePrune::Init()
{
    const auto max = Config::maxManualSaves.GetValue();
    if (max <= 0)
    {
        return;
    }

    g_max = static_cast<std::uint32_t>((std::min)(max, kMaxSaves));
}

void SaferSaving::SavePrune::OnGameLoaded()
{
    g_generation.fetch_add(1, std::memory_order_relaxed);
}

void SaferSaving::SavePrune::OnSaved(std::string_view a_name)
{
    if (g_max == 0 || a_name.empty())
    {
        return;
    }

    // kSaveGame lands before the file does and may not be the main thread; the task drains at the
    // end of the frame, on the main thread, by which time the new save is there to count
    const auto generation = g_generation.load(std::memory_order_relaxed);
    SKSE::GetTaskInterface()->AddTask([name = std::string{a_name}, generation] { Prune(name, generation); });
}
