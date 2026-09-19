#include "autosave.h"

#include "pch.h"

#include "config.h"
#include "notification.h"

namespace
{
namespace Config = SaferSaving::Config;

constexpr auto kDisableSaving = RE::PlayerCharacter::ByCharGenFlag::kDisableSaving;
constexpr const char* kNotification{"Autosaving..."};

// the load menu parses the whole vanilla shape out of the file name, so every field has to carry a
// real value or every entry renders with the same location, level, play time and date:
// <Kind><N>_<id>_<modded>_<name hex>_<location>_<play minutes>_<stamp>_<level>_1
constexpr std::string_view kNamePrefix{"Autosave"};
// vanilla rotation stays inside iAutoSaveCount, so slots this high are ours alone
constexpr std::uint32_t kSlotBase{1000};
// no cell or worldspace yet, which happens before the first load
constexpr const char* kUnknownLocation{"Skyrim"};

// anything longer is a pause, a load or a stall
constexpr std::uint32_t kMaxStepMS{250};
// two digits in the file name
constexpr std::int32_t kMinSlots{1};
constexpr std::int32_t kMaxSlots{99};
// keeps the interval in milliseconds inside 32 bits
constexpr std::int32_t kMinMinutes{1};
constexpr std::int32_t kMaxMinutes{1440};
// six digits in the file name
constexpr std::int64_t kMaxPlayMinutes{999999};

// set once by Init
std::uint32_t g_intervalMS{0};
std::uint32_t g_slots{1};

// only Tick touches this
std::uint32_t g_rearmSeen{0};

std::atomic<std::uint32_t> g_lastTickMS{0};
std::atomic<std::uint32_t> g_elapsedMS{0};
std::atomic<std::uint32_t> g_slot{0};
std::atomic<std::uint32_t> g_generation{0};
std::atomic<std::uint32_t> g_rearm{0};
std::atomic<bool> g_pending{false};
std::atomic<bool> g_dispatched{false};

static_assert(decltype(g_lastTickMS)::is_always_lock_free);
static_assert(decltype(g_pending)::is_always_lock_free);

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

// the cell editor id inside, the worldspace editor id outside, the way vanilla writes it
const char* LocationName(RE::PlayerCharacter& a_player)
{
    const auto* cell = a_player.GetParentCell();
    if (!cell)
    {
        return kUnknownLocation;
    }

    const char* name = nullptr;
    if (cell->IsInteriorCell())
    {
        name = cell->GetFormEditorID();
    }
    else if (const auto* world = cell->GetRuntimeData().worldSpace)
    {
        name = world->GetFormEditorID();
    }

    return (name && *name) ? name : kUnknownLocation;
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

std::uint32_t TakeNextSlot()
{
    const auto next = (g_slot.load(std::memory_order_relaxed) % g_slots) + 1;
    g_slot.store(next, std::memory_order_relaxed);
    return next;
}

// newest slot of ours for this character on the save list, or zero
std::uint32_t FindNewestSlot()
{
    const auto* manager = RE::BGSSaveLoadManager::GetSingleton();
    if (!manager)
    {
        return 0;
    }

    const auto ownId = std::format("_{:08X}_", manager->currentCharacterID);

    std::uint32_t newestSlot  = 0;
    std::uint32_t highestSlot = 0;
    std::uint64_t newestTime  = 0;
    for (const auto* entry : manager->saveGameList)
    {
        if (!entry)
        {
            continue;
        }

        const std::string_view name{entry->fileName};
        if (!name.starts_with(kNamePrefix))
        {
            continue;
        }

        const auto digits    = name.substr(kNamePrefix.size());
        std::uint32_t number = 0;
        const auto parsed    = std::from_chars(digits.data(), digits.data() + digits.size(), number);
        if (parsed.ec != std::errc{} || number <= kSlotBase || number > kSlotBase + g_slots)
        {
            continue;
        }

        // the id is what ties a slot to this character
        if (!std::string_view{parsed.ptr, digits.data() + digits.size()}.starts_with(ownId))
        {
            continue;
        }

        const auto slot = number - kSlotBase;
        highestSlot     = (std::max)(highestSlot, slot);

        const std::uint64_t saved = entry->saveTime;
        if (saved > newestTime)
        {
            newestTime = saved;
            newestSlot = slot;
        }
    }

    // unread headers report no time, so fall back to the highest index
    return newestTime != 0 ? newestSlot : highestSlot;
}

// whether the engine itself would take a save; Save_Impl fails silently otherwise
bool CanDispatch(RE::PlayerCharacter* a_player)
{
    // the guard verdict already materialised, and it catches flags we do not own too
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

    // false skips the VR ESL probe, a clock read per call on VR
    const auto* data = RE::TESDataHandler::GetSingleton(false);
    return !data || !data->GetGeometryRuntimeData().blockSave;
}

// runs from the task queue, a frame after Tick decided
void Write(std::uint32_t a_generation)
{
    g_dispatched.store(false, std::memory_order_relaxed);

    // a load in between means this save belongs to a session that is gone
    if (g_generation.load(std::memory_order_relaxed) != a_generation)
    {
        return;
    }

    auto* manager = RE::BGSSaveLoadManager::GetSingleton();
    auto* player  = RE::PlayerCharacter::GetSingleton();
    if (!manager || !player)
    {
        return;
    }

    // a frame has passed, and the guard may have set the flag since
    if (!CanDispatch(player))
    {
        g_pending.store(true, std::memory_order_relaxed);
        return;
    }

    const auto name = SlotName(TakeNextSlot(), *manager, *player);
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

    // a task queued by the old session must not run
    g_generation.fetch_add(1, std::memory_order_relaxed);
    // and if it never runs at all, this must not stay set
    g_dispatched.store(false, std::memory_order_relaxed);
    g_lastTickMS.store(0, std::memory_order_relaxed);
    g_elapsedMS.store(0, std::memory_order_relaxed);
    g_pending.store(false, std::memory_order_relaxed);
    g_slot.store(FindNewestSlot(), std::memory_order_relaxed);
}

void SaferSaving::AutoSave::OnSaved()
{
    if (g_intervalMS == 0)
    {
        return;
    }

    // just a counter: kSaveGame may arrive off the main thread, and Tick owns the timer
    g_rearm.fetch_add(1, std::memory_order_relaxed);
}

void SaferSaving::AutoSave::Tick()
{
    if (g_intervalMS == 0)
    {
        return;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player)
    {
        return;
    }

    // a save landed, ours or the player's, so the interval restarts
    if (const auto rearm = g_rearm.load(std::memory_order_relaxed); rearm != g_rearmSeen)
    {
        g_rearmSeen = rearm;
        g_elapsedMS.store(0, std::memory_order_relaxed);
        g_pending.store(false, std::memory_order_relaxed);
    }

    // real milliseconds since launch; unlike the update delta it ignores the time multiplier
    const auto now = RE::GetDurationOfApplicationRunTime();

    // load and store rather than exchange: only Tick writes these, and this is every frame
    const auto lastMS = g_lastTickMS.load(std::memory_order_relaxed);
    g_lastTickMS.store(now, std::memory_order_relaxed);

    auto elapsed = g_elapsedMS.load(std::memory_order_relaxed);
    if (lastMS != 0)
    {
        // a wrapped counter costs one clamped step, and so does a pause, which is what keeps menu
        // and loading time out of the interval
        elapsed += (std::min)(now - lastMS, kMaxStepMS);
        g_elapsedMS.store(elapsed, std::memory_order_relaxed);
    }

    if (elapsed >= g_intervalMS)
    {
        g_pending.store(true, std::memory_order_relaxed);
    }

    // a blocked interval waits rather than skips
    if (!g_pending.load(std::memory_order_relaxed) || g_dispatched.load(std::memory_order_relaxed) ||
        !CanDispatch(player))
    {
        return;
    }

    g_dispatched.store(true, std::memory_order_relaxed);
    g_pending.store(false, std::memory_order_relaxed);
    g_elapsedMS.store(0, std::memory_order_relaxed);

    // never save from inside an actor update; the task queue drains at the end of the frame
    const auto generation = g_generation.load(std::memory_order_relaxed);
    SKSE::GetTaskInterface()->AddTask([generation] { Write(generation); });
}
