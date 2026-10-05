#include "autosave.h"

#include "pch.h"

#include "config.h"
#include "notification.h"
#include "save-files.h"
#include "save-guard.h"

namespace
{
namespace Config    = SaferSaving::Config;
namespace SaveFiles = SaferSaving::SaveFiles;

constexpr auto kDisableSaving = RE::PlayerCharacter::ByCharGenFlag::kDisableSaving;
constexpr const char* kNotification{"Autosaving..."};

// the load menu renders an entry from the file name alone, so every field has to be real:
// <Kind><N>_<id>_<modded>_<name hex>_<location>_<play minutes>_<stamp>_<level>_1
constexpr std::string_view kNamePrefix{"Autosave"};
// vanilla rotation stays inside iAutoSaveCount, so 101 upwards never collides with it
constexpr std::uint32_t kSlotBase{100};
constexpr const char* kUnknownLocation{"Skyrim"};

// a longer gap is a pause, a load or a stall, and must not count as play time
constexpr std::uint32_t kMaxStepMS{250};
constexpr std::int32_t kMinSlots{1};
constexpr std::int32_t kMaxSlots{99};
constexpr std::int32_t kMinMinutes{1};
constexpr std::int32_t kMaxMinutes{1440};
// six digits in the file name
constexpr std::int64_t kMaxPlayMinutes{999999};

// set once by Init
std::uint32_t g_intervalMS{0};
std::uint32_t g_slots{1};

// kSaveGame can arrive off the main thread; everything below it is main thread only
std::atomic<std::uint32_t> g_rearm{0};
static_assert(decltype(g_rearm)::is_always_lock_free);

std::uint32_t g_rearmSeen{0};
std::uint32_t g_lastTickMS{0};
std::uint32_t g_elapsedMS{0};
std::uint32_t g_slot{0};
std::uint32_t g_generation{0};
bool g_due{false};
bool g_dispatched{false};
bool g_slotSeeded{false};

std::string HexOf(std::string_view a_text)
{
    constexpr char kDigits[]{"0123456789ABCDEF"};

    std::string out;
    out.reserve(a_text.size() * 2);
    for (const unsigned char c : a_text)
    {
        out.push_back(kDigits[c >> 4]);
        out.push_back(kDigits[c & 0xF]);
    }

    return out;
}

// cell id inside, worldspace outside; underscores go because the menu splits the name on them
std::string LocationName(RE::PlayerCharacter& a_player)
{
    const char* name = nullptr;
    if (const auto* cell = a_player.GetParentCell())
    {
        if (cell->IsInteriorCell())
        {
            name = cell->GetFormEditorID();
        }
        else if (const auto* world = cell->GetRuntimeData().worldSpace)
        {
            name = world->GetFormEditorID();
        }
    }

    if (!name || !*name)
    {
        return kUnknownLocation;
    }

    std::string location{name};
    std::erase(location, '_');
    return location.empty() ? kUnknownLocation : location;
}

std::uint32_t PlayMinutes(RE::PlayerCharacter& a_player)
{
    const auto seconds = a_player.GetPlayerRuntimeData().totalPlayingTime;
    if (seconds <= 0)
    {
        return 0;
    }

    return static_cast<std::uint32_t>((std::min)(seconds / 60, kMaxPlayMinutes));
}

std::string TimeStamp()
{
    const auto now = std::time(nullptr);
    std::tm local{};
    if (localtime_s(&local, &now) != 0)
    {
        return "00000000000000";
    }

    return std::format("{:04}{:02}{:02}{:02}{:02}{:02}", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                       local.tm_hour, local.tm_min, local.tm_sec);
}

std::string SlotName(std::uint32_t a_slot, const RE::BGSSaveLoadManager& a_manager, RE::PlayerCharacter& a_player)
{
    return std::format("{}{}_{:08X}_{}_{}_{}_{:06}_{}_{}_1", kNamePrefix, kSlotBase + a_slot,
                       a_manager.currentCharacterID, a_manager.currentCharacterModded, HexOf(a_player.GetName()),
                       LocationName(a_player), PlayMinutes(a_player), TimeStamp(), a_player.GetLevel());
}

// which slot a file belongs to for this character, or zero; seeding and pruning share it so they
// can never disagree about what counts as ours
std::uint32_t SlotOf(std::string_view a_name, std::string_view a_ownId)
{
    if (!a_name.starts_with(kNamePrefix))
    {
        return 0;
    }

    const auto digits    = a_name.substr(kNamePrefix.size());
    std::uint32_t number = 0;
    const auto parsed    = std::from_chars(digits.data(), digits.data() + digits.size(), number);
    if (parsed.ec != std::errc{} || number <= kSlotBase || number > kSlotBase + g_slots)
    {
        return 0;
    }

    const std::string_view rest{parsed.ptr, digits.data() + digits.size()};
    return rest.starts_with(a_ownId) ? number - kSlotBase : 0;
}

// by file time, because the engine save list is not reliably populated when a game has loaded
std::uint32_t NewestSlotOnDisk(const RE::BGSSaveLoadManager& a_manager)
{
    const auto& directory = SaveFiles::Directory();
    if (directory.empty())
    {
        return 0;
    }

    const auto ownId = SaveFiles::OwnId(a_manager);

    std::uint32_t newestSlot = 0;
    std::filesystem::file_time_type newestTime{};

    std::error_code ec;
    for (const auto& item : std::filesystem::directory_iterator{directory, ec})
    {
        // the ess alone; the co-save shares its time and a bak is older by definition
        if (item.path().extension() != ".ess")
        {
            continue;
        }

        const auto slot = SlotOf(item.path().filename().string(), ownId);
        if (slot == 0)
        {
            continue;
        }

        const auto written = item.last_write_time(ec);
        if (!ec && written > newestTime)
        {
            newestTime = written;
            newestSlot = slot;
        }
        ec.clear();
    }

    return newestSlot;
}

// seeded here rather than on load: the save folder does not resolve at kPostLoadGame, but does by now
std::uint32_t TakeNextSlot(const RE::BGSSaveLoadManager& a_manager)
{
    if (!g_slotSeeded)
    {
        g_slot       = NewestSlotOnDisk(a_manager);
        g_slotSeeded = true;
    }

    g_slot = (g_slot % g_slots) + 1;
    return g_slot;
}

// the timestamp in the name means the engine never overwrites a slot, so old files are ours to clear
void PruneSlot(std::uint32_t a_slot, const RE::BGSSaveLoadManager& a_manager)
{
    const auto& directory = SaveFiles::Directory();
    if (directory.empty())
    {
        return;
    }

    const auto ownId = SaveFiles::OwnId(a_manager);

    std::error_code ec;
    for (const auto& item : std::filesystem::directory_iterator{directory, ec})
    {
        // the ess, the co-save and any bak all carry the same name
        if (SlotOf(item.path().filename().string(), ownId) == a_slot)
        {
            std::filesystem::remove(item.path(), ec);
        }
    }
}

// whether the engine would take a save at all; Save_Impl fails silently otherwise
bool CanDispatch(RE::PlayerCharacter* a_player)
{
    // the guard verdict already materialised, and flags we do not own too
    if (a_player->GetPlayerRuntimeData().byCharGenFlag.any(kDisableSaving))
    {
        return false;
    }

    if (const auto* saveLoad = RE::BGSSaveLoadGame::GetSingleton())
    {
        if (saveLoad->GetSaveGameSaving() || saveLoad->GetSaveGameLoading() ||
            saveLoad->GetPositioningPlayerCharacter())
        {
            return false;
        }
    }

    // false skips the VR ESL probe
    const auto* data = RE::TESDataHandler::GetSingleton(false);
    return !data || !data->GetGeometryRuntimeData().blockSave;
}

// from the task queue, a frame after Tick decided
void Write(std::uint32_t a_generation)
{
    g_dispatched = false;

    // a load since then means this save belongs to a session that is gone
    if (g_generation != a_generation)
    {
        return;
    }

    auto* manager = RE::BGSSaveLoadManager::GetSingleton();
    auto* player  = RE::PlayerCharacter::GetSingleton();
    if (!manager || !player)
    {
        return;
    }

    // the sweep may be a quarter second stale by now, so make this one verdict exact
    SaferSaving::Reevaluate();
    if (!CanDispatch(player))
    {
        // still due, so the next tick tries again
        return;
    }

    const auto slot = TakeNextSlot(*manager);
    const auto name = SlotName(slot, *manager, *player);

    PruneSlot(slot, *manager);

    SaferSaving::ShowNotification(kNotification);
    manager->Save(name.c_str());
}
} // namespace

void SaferSaving::AutoSave::Init()
{
    const auto minutes = Config::autoSaveMinutes.GetValue();
    if (minutes <= 0)
    {
        return;
    }

    g_intervalMS = static_cast<std::uint32_t>(std::clamp(minutes, kMinMinutes, kMaxMinutes)) * 60u * 1000u;
    g_slots      = static_cast<std::uint32_t>(std::clamp(Config::autoSaveSlots.GetValue(), kMinSlots, kMaxSlots));
}

void SaferSaving::AutoSave::OnGameLoaded()
{
    if (g_intervalMS == 0)
    {
        return;
    }

    // a task queued by the old session must neither run nor leave dispatched set behind it
    ++g_generation;
    g_dispatched = false;
    g_lastTickMS = 0;
    g_elapsedMS  = 0;
    g_due        = false;
    g_slotSeeded = false;
}

void SaferSaving::AutoSave::OnSaved()
{
    if (g_intervalMS == 0)
    {
        return;
    }

    // a counter, because this may not be the main thread and Tick owns the timer
    g_rearm.fetch_add(1, std::memory_order_relaxed);
}

void SaferSaving::AutoSave::Tick()
{
    if (g_intervalMS == 0)
    {
        return;
    }

    // a save landed, ours or the player's, so the interval restarts
    if (const auto rearm = g_rearm.load(std::memory_order_relaxed); rearm != g_rearmSeen)
    {
        g_rearmSeen = rearm;
        g_elapsedMS = 0;
        g_due       = false;
    }

    // milliseconds since launch, so the time multiplier cannot skew the interval
    const auto now = RE::GetDurationOfApplicationRunTime();
    if (g_lastTickMS != 0)
    {
        g_elapsedMS += (std::min)(now - g_lastTickMS, kMaxStepMS);
    }
    g_lastTickMS = now;

    if (g_elapsedMS >= g_intervalMS)
    {
        g_due       = true;
        g_elapsedMS = 0;
    }

    // a blocked interval waits rather than skips: due stays set until a save actually lands
    if (!g_due || g_dispatched)
    {
        return;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player || !CanDispatch(player))
    {
        return;
    }

    g_dispatched = true;

    // never save from inside an actor update; the task queue drains at the end of the frame
    SKSE::GetTaskInterface()->AddTask([generation = g_generation] { Write(generation); });
}
