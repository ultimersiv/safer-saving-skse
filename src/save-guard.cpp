#include "save-guard.h"

#include "config.h"
#include "notification.h"

namespace
{
namespace Config = SaferSaving::Config;

constexpr auto kDisableSaving = RE::PlayerCharacter::ByCharGenFlag::kDisableSaving;

std::atomic<std::chrono::steady_clock::time_point> g_settleUntil{};
std::atomic<bool> g_ownsFlag{false};

static_assert(decltype(g_settleUntil)::is_always_lock_free);
static_assert(decltype(g_ownsFlag)::is_always_lock_free);

// how stale the flag may get on the sweep path; every player-facing save point forces a refresh
constexpr auto kSweepInterval = std::chrono::milliseconds{250};

// frame hook only, so no atomic
std::chrono::steady_clock::time_point g_lastSweep{};

struct Context
{
    RE::PlayerCharacter& player;
    const RE::ActorState& state;
};

struct Check
{
    const REX::INI::Bool<>* toggle;
    bool (*test)(const Context&);
    const char* reason;
};

// grouped as in the ini, cheapest first within each group; order also picks the reason shown
const Check kChecks[]{
    // [Combat] also covers the draw and sheathe animations, not just a fully drawn weapon or spell
    {&Config::weaponDrawn, [](const Context& c) { return c.state.GetWeaponState() != RE::WEAPON_STATE::kSheathed; },
     "Weapon or spell drawn."},
    {&Config::attacking, [](const Context& c) { return c.state.GetAttackState() != RE::ATTACK_STATE_ENUM::kNone; },
     "Mid-attack."},
    {&Config::killmove, [](const Context& c) { return c.player.IsInKillMove(); }, "In a killmove."},
    {&Config::inCombat, [](const Context& c) { return c.player.IsInCombat(); }, "In combat."},
    // [Movement], moving last because it is true for most of the others too
    {&Config::sprinting, [](const Context& c) { return c.state.IsSprinting(); }, "Sprinting."},
    {&Config::sneaking, [](const Context& c) { return c.state.IsSneaking(); }, "Sneaking."},
    {&Config::swimming, [](const Context& c) { return c.state.IsSwimming(); }, "Swimming."},
    {&Config::flying, [](const Context& c) { return c.state.IsFlying(); }, "Flying."},
    {&Config::midair, [](const Context& c) { return c.player.IsInMidair(); }, "In midair."},
    {&Config::mounted, [](const Context& c) { return c.player.IsOnMount() || c.player.IsBeingRidden(); }, "Mounted."},
    {&Config::moving, [](const Context& c) { return c.player.IsMoving(); }, "Moving."},
    // [State]
    {&Config::notAlive, [](const Context& c) { return c.state.GetLifeState() != RE::ACTOR_LIFE_STATE::kAlive; },
     "Dead or unconscious."},
    {&Config::bleedingOut, [](const Context& c) { return c.state.IsBleedingOut(); }, "Bleeding out."},
    {&Config::knockedDown, [](const Context& c) { return c.state.GetKnockState() != RE::KNOCK_STATE_ENUM::kNormal; },
     "Knocked down."},
    {&Config::staggered, [](const Context& c) { return c.state.IsStaggered(); }, "Staggered."},
    {&Config::sitSleepTransition,
     [](const Context& c)
     {
         switch (c.state.GetSitSleepState())
         {
             case RE::SIT_SLEEP_STATE::kNormal:
             case RE::SIT_SLEEP_STATE::kIsSitting:
             case RE::SIT_SLEEP_STATE::kIsSleeping:
                 return false;
             default:
                 return true;
         }
     },
     "Sitting down or standing up."},
    // menus use input contexts, so an open menu does not trip this
    {&Config::controlsDisabled,
     [](const Context&)
     {
         const auto* controls = RE::ControlMap::GetSingleton();
         return controls && !controls->IsMovementControlsEnabled();
     },
     "Controls disabled."},
    {&Config::grabbing, [](const Context& c) { return c.player.IsGrabbing(); }, "Holding an object."},
    // a guard or an owner is about to run a scene at you
    {&Config::trespassing, [](const Context& c) { return c.player.IsTrespassing(); }, "Trespassing."},
    // dearest two, out of their ini groups and last so that any cheaper check short-circuits them
    // also true for furniture idles, so sitting counts as busy
    {&Config::animationDriven, [](const Context& c) { return c.player.IsAnimationDriven(); }, "In an animation."},
    // walks the high process list; trips before IsInCombat does, when something has noticed you
    {&Config::enemiesNearby,
     [](const Context&)
     {
         auto* processes = RE::ProcessLists::GetSingleton();
         if (!processes)
         {
             return false;
         }

         // the out param is unused, but passing nullptr is not known to be safe
         RE::BSScrapArray<RE::ActorHandle> hostiles;
         return processes->AreHostileActorsNear(&hostiles);
     },
     "Enemies nearby."},
};

struct MenuCheck
{
    const RE::BSFixedString* name;
    const REX::INI::Bool<>* toggle;
    const char* reason;
};

// nullptr when nothing matches
template <class Fn>
const char* MatchBlockingMenu(Fn&& a_pred)
{
    const auto* strings = RE::InterfaceStrings::GetSingleton();
    if (!strings)
    {
        return nullptr;
    }

    // no journal or save/load menu here, or there would be no way to save on purpose
    const MenuCheck menus[]{
        {&strings->barterMenu, &Config::itemMenus, "Trading."},
        {&strings->containerMenu, &Config::itemMenus, "Container open."},
        {&strings->favoritesMenu, &Config::itemMenus, "Favourites open."},
        {&strings->giftMenu, &Config::itemMenus, "Giving a gift."},
        {&strings->inventoryMenu, &Config::itemMenus, "Inventory open."},
        {&strings->magicMenu, &Config::itemMenus, "Magic menu open."},
        {&strings->tweenMenu, &Config::itemMenus, "Pause wheel open."},
        {&strings->dialogueMenu, &Config::dialogue, "In a conversation."},
        {&strings->bookMenu, &Config::book, "Reading."},
        {&strings->craftingMenu, &Config::crafting, "Crafting."},
        {&strings->lockpickingMenu, &Config::lockpicking, "Lockpicking."},
        {&strings->levelUpMenu, &Config::levelUpTraining, "Levelling up."},
        {&strings->trainingMenu, &Config::levelUpTraining, "Training."},
        {&strings->sleepWaitMenu, &Config::sleepWait, "Sleeping or waiting."},
        {&strings->loadingMenu, &Config::loading, "The game is loading."},
        {&strings->mistMenu, &Config::loading, "The game is loading."},
        {&strings->raceSexMenu, &Config::characterCreation, "In character creation."},
    };

    for (const auto& [name, toggle, reason] : menus)
    {
        if (toggle->GetValue() && a_pred(*name))
        {
            return reason;
        }
    }

    return nullptr;
}

const char* BlockingMenuOpen()
{
    auto* ui = RE::UI::GetSingleton();
    if (!ui)
    {
        return nullptr;
    }

    return MatchBlockingMenu([ui](const RE::BSFixedString& a_name) { return ui->IsMenuOpen(a_name); });
}

// nullptr when saving is allowed
const char* GetBlockReason(RE::PlayerCharacter* a_player)
{
    if (std::chrono::steady_clock::now() < g_settleUntil.load(std::memory_order_relaxed))
    {
        return "The game is still settling down.";
    }

    // the one [State] check outside the table, since it runs before AsActorState
    if (Config::notLoaded.GetValue() && !a_player->Is3DLoaded())
    {
        return "Surroundings are still loading in.";
    }

    const auto* state = a_player->AsActorState();
    if (!state)
    {
        return "Player state unavailable.";
    }

    const Context context{*a_player, *state};
    for (const auto& check : kChecks)
    {
        if (check.toggle->GetValue() && check.test(context))
        {
            return check.reason;
        }
    }

    return BlockingMenuOpen();
}

void BlockSaving(RE::PlayerCharacter* a_player)
{
    auto& flags = a_player->GetPlayerRuntimeData().byCharGenFlag;
    if (flags.any(kDisableSaving))
    {
        return;
    }

    flags.set(kDisableSaving);
    g_ownsFlag.store(true, std::memory_order_relaxed);
}

void AllowSaving(RE::PlayerCharacter* a_player)
{
    auto& flags = a_player->GetPlayerRuntimeData().byCharGenFlag;
    if (!flags.any(kDisableSaving) || !g_ownsFlag.load(std::memory_order_relaxed))
    {
        return;
    }

    flags.reset(kDisableSaving);
    g_ownsFlag.store(false, std::memory_order_relaxed);
}
} // namespace

bool SaferSaving::IsMenuInBlockList(const RE::BSFixedString& a_menuName)
{
    return MatchBlockingMenu([&a_menuName](const RE::BSFixedString& a_name) { return a_name == a_menuName; }) !=
           nullptr;
}

void SaferSaving::BlockForMenu()
{
    if (auto* player = RE::PlayerCharacter::GetSingleton())
    {
        BlockSaving(player);
    }
}

void SaferSaving::BeginSettle()
{
    const auto seconds = Config::settleSeconds.GetValue();
    if (seconds <= 0)
    {
        return;
    }

    g_settleUntil.store(std::chrono::steady_clock::now() + std::chrono::seconds{seconds}, std::memory_order_relaxed);

    if (auto* player = RE::PlayerCharacter::GetSingleton())
    {
        BlockSaving(player);
    }
}

void SaferSaving::OnGameLoaded()
{
    // clear flag just in case
    if (auto* player = RE::PlayerCharacter::GetSingleton())
    {
        player->GetPlayerRuntimeData().byCharGenFlag.reset(kDisableSaving);
    }

    g_ownsFlag.store(false, std::memory_order_relaxed);

    BeginSettle();
}

void SaferSaving::Tick()
{
    // the sweep only has to be fresh enough for saves we cannot observe; the rest force a refresh
    const auto now = std::chrono::steady_clock::now();
    if (now - g_lastSweep < kSweepInterval)
    {
        return;
    }

    g_lastSweep = now;
    Reevaluate();
}

void SaferSaving::Reevaluate()
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player)
    {
        return;
    }

    if (GetBlockReason(player))
    {
        BlockSaving(player);
    }
    else
    {
        AllowSaving(player);
    }
}

void SaferSaving::NotifyBlockedSaveAttempt()
{
    if (!Config::notify.GetValue())
    {
        return;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player)
    {
        return;
    }

    // another mod's flag is not ours to explain
    const auto* reason = GetBlockReason(player);
    if (!reason)
    {
        return;
    }

    const auto prefix = Config::messagePrefix.GetValue();
    if (prefix.empty())
    {
        ShowNotification(reason);
    }
    else
    {
        ShowNotification(std::format("{} {}", prefix, reason).c_str());
    }
}
