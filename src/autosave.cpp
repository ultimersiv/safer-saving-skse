#include "autosave.h"

#include "config.h"

namespace
{
namespace Config = SaferSaving::Config;

constexpr auto kDisableSaving = RE::PlayerCharacter::ByCharGenFlag::kDisableSaving;
constexpr std::string_view kSlotPrefix{"SaferSave_"};
constexpr const char* kNotification{"Autosaving..."};
// a gap this long is a pause, a loading screen or a stall rather than play, so it is dropped
// instead of counted. this is what makes the interval measure time actually spent playing
constexpr std::uint32_t kMaxStepMS{250};
// two digits, so the file name width is fixed
constexpr std::int32_t kMinSlots{1};
constexpr std::int32_t kMaxSlots{99};
// keeps the interval in milliseconds comfortably inside 32 bits
constexpr std::int32_t kMinMinutes{1};
constexpr std::int32_t kMaxMinutes{1440};

// written once by Init, before the first frame, and only read afterwards
std::uint32_t g_intervalMS{0};
std::uint32_t g_slots{1};

// only ever touched by Tick, which keeps it off the atomics
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

// the slot holding the newest save of ours, so a restart carries on after it instead of
// overwriting it. zero when none of our saves are on the list
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

        // parsed leniently, so it does not matter whether the list holds the extension or not
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

    // entries whose headers the game has not read report no time, so fall back to the index
    return newestTime != 0 ? newestSlot : highestSlot;
}

// the block list says the player is somewhere safe; this says the engine will actually take the
// request. every one of these makes the game drop a save on the floor without a word
bool CanDispatch(RE::PlayerCharacter* a_player)
{
    // AllowSaving leaves the flag alone when another mod or character creation owns it, so a clear
    // verdict from our own checks does not mean the game will accept a save
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

    const auto* data = RE::TESDataHandler::GetSingleton();
    return !data || !data->GetGeometryRuntimeData().blockSave;
}

// runs on the main thread from the queued task, a frame or so after the decision was taken
void Write(std::uint32_t a_generation)
{
    g_dispatched.store(false, std::memory_order_relaxed);

    // a load in between would put this save in a different session than the one that asked for it
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

    // state can move between the decision and here, so ask again. the guard closes most of this
    // window itself: if the player started moving, the Apply call this frame already set the flag
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

// saving walks the whole game state, so it does not belong inside an actor update. the task
// interface runs it on the main thread at the frame drain point, where the engine does its own
void Dispatch()
{
    const auto* task = SKSE::GetTaskInterface();
    if (!task)
    {
        g_dispatched.store(false, std::memory_order_relaxed);
        return;
    }

    task->AddTask([generation = g_generation.load(std::memory_order_relaxed)] { Write(generation); });
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

    // a pending save described a session that no longer exists, so it goes with the old game
    g_generation.fetch_add(1, std::memory_order_relaxed);
    // if a queued task was dropped rather than run, this flag would otherwise stay set and kill
    // autosaving for the rest of the session
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

    // Fires for our own save too, which is the point: the interval always runs from the last save
    // on disk, whoever asked for it. This can arrive off the main thread, so it only nudges a
    // counter and lets Tick do the reset; storing the timer here could lose the reset to the
    // fetch_add running concurrently in Tick.
    g_rearm.fetch_add(1, std::memory_order_relaxed);
}

void SaferSaving::AutoSave::Tick(RE::PlayerCharacter* a_player, std::uint32_t a_now, bool a_blocked)
{
    if (g_intervalMS == 0)
    {
        return;
    }

    // a save landed since the last frame, from us or from the player, so the interval starts over
    if (const auto rearm = g_rearm.load(std::memory_order_relaxed); rearm != g_rearmSeen)
    {
        g_rearmSeen = rearm;
        g_elapsedMS.store(0, std::memory_order_relaxed);
        g_pending.store(false, std::memory_order_relaxed);
        g_complained.store(false, std::memory_order_relaxed);
    }

    const auto lastMS = g_lastTickMS.exchange(a_now, std::memory_order_relaxed);
    if (lastMS != 0)
    {
        // unsigned, so the counter wrapping costs one clamped step and nothing else
        g_elapsedMS.fetch_add((std::min)(a_now - lastMS, kMaxStepMS), std::memory_order_relaxed);
    }

    if (g_elapsedMS.load(std::memory_order_relaxed) >= g_intervalMS)
    {
        g_pending.store(true, std::memory_order_relaxed);
    }

    // the pending flag is the whole feature: a blocked interval becomes a deferral, not a miss
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
    Dispatch();
}
