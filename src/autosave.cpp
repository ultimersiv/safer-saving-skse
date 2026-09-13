#include "autosave.h"

#include "config.h"

namespace
{
namespace Config = SaferSaving::Config;

constexpr auto kDisableSaving = RE::PlayerCharacter::ByCharGenFlag::kDisableSaving;
constexpr std::string_view kSlotPrefix{"SaferSave_"};
constexpr const char* kNotification{"Autosaving..."};
// anything longer is a pause, a load or a stall
constexpr std::uint32_t kMaxStepMS{250};
// two digits in the file name
constexpr std::int32_t kMinSlots{1};
constexpr std::int32_t kMaxSlots{99};
// keeps the interval in milliseconds inside 32 bits
constexpr std::int32_t kMinMinutes{1};
constexpr std::int32_t kMaxMinutes{1440};

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
std::atomic<bool> g_complained{false};

static_assert(decltype(g_lastTickMS)::is_always_lock_free);
static_assert(decltype(g_pending)::is_always_lock_free);

std::string SlotName(std::uint32_t a_slot)
{
    return std::format("{}{:02}", kSlotPrefix, a_slot);
}

std::uint32_t TakeNextSlot()
{
    const auto next = (g_slot.load(std::memory_order_relaxed) % g_slots) + 1;
    g_slot.store(next, std::memory_order_relaxed);
    return next;
}

// newest slot of ours on the save list, or zero
std::uint32_t FindNewestSlot()
{
    const auto* manager = RE::BGSSaveLoadManager::GetSingleton();
    if (!manager)
    {
        return 0;
    }

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
        if (!name.starts_with(kSlotPrefix))
        {
            continue;
        }

        // lenient, in case the name carries the extension
        const auto digits  = name.substr(kSlotPrefix.size());
        std::uint32_t slot = 0;
        if (std::from_chars(digits.data(), digits.data() + digits.size(), slot).ec != std::errc{} || slot == 0)
        {
            continue;
        }

        highestSlot = (std::max)(highestSlot, slot);

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
    // another mod or chargen may own the flag, and AllowSaving never clears those
    if (a_player->GetGameStatsData().byCharGenFlag.any(kDisableSaving))
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

    const auto name = SlotName(TakeNextSlot());
    RE::SendHUDMessage::ShowHUDMessage(kNotification);
    manager->Save(name.c_str());
    logs::info("autosaved to {}", name);
}
} // namespace

void SaferSaving::AutoSave::Init()
{
    const auto minutes = Config::autoSaveMinutes.GetValue();
    if (minutes <= 0)
    {
        logs::info("autosave is off");
        return;
    }

    const auto useMinutes = std::clamp(minutes, kMinMinutes, kMaxMinutes);
    const auto useSlots   = std::clamp(Config::autoSaveSlots.GetValue(), kMinSlots, kMaxSlots);

    g_intervalMS = static_cast<std::uint32_t>(useMinutes) * 60u * 1000u;
    g_slots      = static_cast<std::uint32_t>(useSlots);

    logs::info("autosave every {} minutes of play, rotating {} slots", useMinutes, useSlots);
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
    g_complained.store(false, std::memory_order_relaxed);
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

void SaferSaving::AutoSave::Tick(RE::PlayerCharacter* a_player, std::uint32_t a_now, bool a_blocked)
{
    if (g_intervalMS == 0)
    {
        return;
    }

    // a save landed, ours or the player's, so the interval restarts
    if (const auto rearm = g_rearm.load(std::memory_order_relaxed); rearm != g_rearmSeen)
    {
        g_rearmSeen = rearm;
        g_elapsedMS.store(0, std::memory_order_relaxed);
        g_pending.store(false, std::memory_order_relaxed);
        g_complained.store(false, std::memory_order_relaxed);
    }

    // load and store rather than exchange: only Tick writes these, and this is every frame
    const auto lastMS = g_lastTickMS.load(std::memory_order_relaxed);
    g_lastTickMS.store(a_now, std::memory_order_relaxed);

    auto elapsed = g_elapsedMS.load(std::memory_order_relaxed);
    if (lastMS != 0)
    {
        // unsigned, so a wrapped counter costs one clamped step
        elapsed += (std::min)(a_now - lastMS, kMaxStepMS);
        g_elapsedMS.store(elapsed, std::memory_order_relaxed);
    }

    if (elapsed >= g_intervalMS)
    {
        g_pending.store(true, std::memory_order_relaxed);
    }

    // a blocked interval waits rather than skips
    if (!g_pending.load(std::memory_order_relaxed) || a_blocked || g_dispatched.load(std::memory_order_relaxed))
    {
        return;
    }

    if (!CanDispatch(a_player))
    {
        if (!g_complained.exchange(true, std::memory_order_relaxed))
        {
            logs::info("autosave is due, but something else is blocking saving; still waiting");
        }
        return;
    }

    g_complained.store(false, std::memory_order_relaxed);
    g_dispatched.store(true, std::memory_order_relaxed);
    g_pending.store(false, std::memory_order_relaxed);
    g_elapsedMS.store(0, std::memory_order_relaxed);

    // never save from inside an actor update; the task queue drains at the end of the frame
    const auto generation = g_generation.load(std::memory_order_relaxed);
    SKSE::GetTaskInterface()->AddTask([generation] { Write(generation); });
}
