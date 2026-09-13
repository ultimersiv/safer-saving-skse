#include "save-guard.h"

#include "autosave.h"
#include "clock.h"
#include "config.h"

namespace
{
namespace Clock  = SaferSaving::Clock;
namespace Config = SaferSaving::Config;

constexpr auto kDisableSaving = RE::PlayerCharacter::ByCharGenFlag::kDisableSaving;

std::atomic<std::uint32_t> g_settleUntil{0};
std::atomic<bool> g_ownsFlag{false};

static_assert(decltype(g_settleUntil)::is_always_lock_free);
static_assert(decltype(g_ownsFlag)::is_always_lock_free);

struct Context
{
    RE::PlayerCharacter& player;
    const RE::ActorState& state;
};

struct Check
{
    const REX::INI::Bool<>* toggle;
    bool (*test)(const Context&);
};

// grouped as in the ini, cheapest first within each group
const Check kChecks[]{
    // [Combat] also covers the draw and sheathe animations, not just a fully drawn weapon or spell
    {&Config::weaponDrawn, [](const Context& c) { return c.state.GetWeaponState() != RE::WEAPON_STATE::kSheathed; }},
    {&Config::attacking, [](const Context& c) { return c.state.GetAttackState() != RE::ATTACK_STATE_ENUM::kNone; }},
    {&Config::killmove, [](const Context& c) { return c.player.IsInKillMove(); }},
    {&Config::inCombat, [](const Context& c) { return c.player.IsInCombat(); }},
    // [Movement], moving last because it is true for most of the others too
    {&Config::sprinting, [](const Context& c) { return c.state.IsSprinting(); }},
    {&Config::sneaking, [](const Context& c) { return c.state.IsSneaking(); }},
    {&Config::swimming, [](const Context& c) { return c.state.IsSwimming(); }},
    {&Config::flying, [](const Context& c) { return c.state.IsFlying(); }},
    {&Config::midair, [](const Context& c) { return c.player.IsInMidair(); }},
    {&Config::mounted, [](const Context& c) { return c.player.IsOnMount() || c.player.IsBeingRidden(); }},
    {&Config::moving, [](const Context& c) { return c.player.IsMoving(); }},
    // [State], IsAnimationDriven last: it is a graph variable lookup, the dearest check here
    {&Config::notAlive, [](const Context& c) { return c.state.GetLifeState() != RE::ACTOR_LIFE_STATE::kAlive; }},
    {&Config::bleedingOut, [](const Context& c) { return c.state.IsBleedingOut(); }},
    {&Config::knockedDown, [](const Context& c) { return c.state.GetKnockState() != RE::KNOCK_STATE_ENUM::kNormal; }},
    {&Config::staggered, [](const Context& c) { return c.state.IsStaggered(); }},
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
     }},
    // menus use input contexts, so an open menu does not trip this
    {&Config::controlsDisabled,
     [](const Context&)
     {
         const auto* controls = RE::ControlMap::GetSingleton();
         return controls && !controls->IsMovementControlsEnabled();
     }},
    {&Config::grabbing, [](const Context& c) { return c.player.IsGrabbing(); }},
    // also true for furniture idles, so sitting counts as busy
    {&Config::animationDriven, [](const Context& c) { return c.player.IsAnimationDriven(); }},
};

template <class Fn>
bool MatchBlockingMenu(Fn&& a_pred)
{
    const auto* strings = RE::InterfaceStrings::GetSingleton();
    if (!strings)
    {
        return false;
    }

    // no journal or save/load menu here, or there would be no way to save on purpose
    const std::pair<const RE::BSFixedString*, const REX::INI::Bool<>*> menus[]{
        {&strings->barterMenu, &Config::itemMenus},
        {&strings->containerMenu, &Config::itemMenus},
        {&strings->favoritesMenu, &Config::itemMenus},
        {&strings->giftMenu, &Config::itemMenus},
        {&strings->inventoryMenu, &Config::itemMenus},
        {&strings->magicMenu, &Config::itemMenus},
        {&strings->tweenMenu, &Config::itemMenus},
        {&strings->dialogueMenu, &Config::dialogue},
        {&strings->bookMenu, &Config::book},
        {&strings->craftingMenu, &Config::crafting},
        {&strings->lockpickingMenu, &Config::lockpicking},
        {&strings->levelUpMenu, &Config::levelUpTraining},
        {&strings->trainingMenu, &Config::levelUpTraining},
        {&strings->sleepWaitMenu, &Config::sleepWait},
        {&strings->loadingMenu, &Config::loading},
        {&strings->mistMenu, &Config::loading},
        {&strings->raceSexMenu, &Config::characterCreation},
    };

    for (const auto& [name, toggle] : menus)
    {
        if (toggle->GetValue() && a_pred(*name))
        {
            return true;
        }
    }

    return false;
}

bool AnyBlockingMenuOpen()
{
    auto* ui = RE::UI::GetSingleton();
    if (!ui)
    {
        return false;
    }

    return MatchBlockingMenu([ui](const RE::BSFixedString& a_name) { return ui->IsMenuOpen(a_name); });
}

bool ShouldBlock(RE::PlayerCharacter* a_player, std::uint32_t a_now)
{
    if (const auto until = g_settleUntil.load(std::memory_order_relaxed); until != 0)
    {
        if (!Clock::Reached(a_now, until))
        {
            return true;
        }

        // the wait is over; clearing it keeps a wrapped counter from reading as a fresh deadline
        g_settleUntil.store(0, std::memory_order_relaxed);
    }

    // the one [State] check outside the table, since it runs before AsActorState
    if (Config::notLoaded.GetValue() && !a_player->Is3DLoaded())
    {
        return true;
    }

    const auto* state = a_player->AsActorState();
    if (!state)
    {
        return true;
    }

    const Context context{*a_player, *state};
    for (const auto& check : kChecks)
    {
        if (check.toggle->GetValue() && check.test(context))
        {
            return true;
        }
    }

    return AnyBlockingMenuOpen();
}

void BlockSaving(RE::PlayerCharacter* a_player)
{
    auto& flags = a_player->GetGameStatsData().byCharGenFlag;
    if (flags.any(kDisableSaving))
    {
        return;
    }

    flags.set(kDisableSaving);
    g_ownsFlag.store(true, std::memory_order_relaxed);
}

void AllowSaving(RE::PlayerCharacter* a_player)
{
    auto& flags = a_player->GetGameStatsData().byCharGenFlag;
    if (!flags.any(kDisableSaving) || !g_ownsFlag.load(std::memory_order_relaxed))
    {
        return;
    }

    flags.reset(kDisableSaving);
    g_ownsFlag.store(false, std::memory_order_relaxed);
}

// one decision, applied and handed back, so the caller never has to ask twice
bool Apply(RE::PlayerCharacter* a_player, std::uint32_t a_now)
{
    const bool blocked = ShouldBlock(a_player, a_now);
    if (blocked)
    {
        BlockSaving(a_player);
    }
    else
    {
        AllowSaving(a_player);
    }

    return blocked;
}
} // namespace

bool SaferSaving::IsMenuInBlockList(const RE::BSFixedString& a_menuName)
{
    return MatchBlockingMenu([&a_menuName](const RE::BSFixedString& a_name) { return a_name == a_menuName; });
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

    // a day is far past anything anyone means by settling, and it keeps the milliseconds in range
    auto until = Clock::Now() + (static_cast<std::uint32_t>(std::min(seconds, 86400)) * 1000u);
    if (until == 0)
    {
        // zero means "not settling", so step over it rather than cancel the wait
        until = 1;
    }

    g_settleUntil.store(until, std::memory_order_relaxed);

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
        player->GetGameStatsData().byCharGenFlag.reset(kDisableSaving);
    }

    g_ownsFlag.store(false, std::memory_order_relaxed);

    BeginSettle();
}

void SaferSaving::Reevaluate()
{
    // menu events only, so this deliberately does not drive the autosave timer: the frame hook
    // stays its single writer
    if (auto* player = RE::PlayerCharacter::GetSingleton())
    {
        Apply(player, Clock::Now());
    }
}

void SaferSaving::OnFrame()
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player)
    {
        return;
    }

    // one clock read and one verdict, shared: the autosave fires exactly when a manual save would
    // be allowed, and costs nothing the guard was not already paying
    const auto now     = Clock::Now();
    const bool blocked = Apply(player, now);

    AutoSave::Tick(player, now, blocked);
}
