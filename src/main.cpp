#include "pch.h"

#include "config.h"
#include "input-listener.h"
#include "menu-listener.h"
#include "save-guard.h"

namespace
{
struct PlayerUpdate
{
    static void thunk(RE::PlayerCharacter* a_player, float a_delta)
    {
        func(a_player, a_delta);
        SaferSaving::Tick();
    }

    static inline REL::Relocation<decltype(thunk)> func;
};

struct QuickSaveCanProcess
{
    static bool thunk(RE::MenuEventHandler* a_handler, RE::InputEvent* a_event)
    {
        // refresh before the original so it holds whether the flag is read here or in ProcessButton
        if (a_event && SaferSaving::IsQuicksavePress(*a_event))
        {
            SaferSaving::Reevaluate();
        }
        return func(a_handler, a_event);
    }

    static inline REL::Relocation<decltype(thunk)> func;
};

void InstallHooks()
{
    // Actor::Update is 0AD on SE/AE; VR adds a virtual earlier, shifting everything past 0x82 by one
    const std::size_t index = REL::Module::IsVR() ? 0xAE : 0xAD;

    REL::Relocation<std::uintptr_t> vtbl{RE::VTABLE_PlayerCharacter[0]};
    PlayerUpdate::func = vtbl.write_vfunc(index, PlayerUpdate::thunk);

    // MenuEventHandler::CanProcess is 01 everywhere; only ProcessButton shifts on AE and VR
    REL::Relocation<std::uintptr_t> quickSave{RE::VTABLE_QuickSaveLoadHandler[0]};
    QuickSaveCanProcess::func = quickSave.write_vfunc(0x1, QuickSaveCanProcess::thunk);
}

void OnMessage(SKSE::MessagingInterface::Message* a_message)
{
    if (!a_message)
    {
        return;
    }

    switch (a_message->type)
    {
        case SKSE::MessagingInterface::kDataLoaded:
            SaferSaving::RegisterMenuListener();
            SaferSaving::RegisterInputListener();
            break;
        case SKSE::MessagingInterface::kPostLoadGame:
            SaferSaving::OnGameLoaded();
            break;
        case SKSE::MessagingInterface::kNewGame:
            SaferSaving::BeginSettle();
            break;
        default:
            break;
    }
}
} // namespace

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse);
    SaferSaving::Config::Load();
    InstallHooks();
    if (!SKSE::GetMessagingInterface()->RegisterListener(OnMessage))
    {
        logs::error("failed to register messaging listener");
        return false;
    }
    return true;
}
