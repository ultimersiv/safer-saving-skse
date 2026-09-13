#include "pch.h"

#include "autosave.h"
#include "config.h"
#include "menu-listener.h"
#include "save-guard.h"

namespace
{
struct PlayerUpdate
{
    static void thunk(RE::PlayerCharacter* a_player, float a_delta)
    {
        func(a_player, a_delta);
        SaferSaving::OnFrame();
    }

    static inline REL::Relocation<decltype(thunk)> func;
};

void InstallHook()
{
    // Actor::Update is 0AD on SE/AE; VR adds a virtual earlier, shifting everything past 0x82 by one
    const std::size_t index = REL::Module::IsVR() ? 0xAE : 0xAD;

    REL::Relocation<std::uintptr_t> vtbl{RE::VTABLE_PlayerCharacter[0]};
    PlayerUpdate::func = vtbl.write_vfunc(index, PlayerUpdate::thunk);
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
            break;
        case SKSE::MessagingInterface::kPostLoadGame:
            SaferSaving::OnGameLoaded();
            SaferSaving::AutoSave::OnGameLoaded();
            break;
        case SKSE::MessagingInterface::kNewGame:
            SaferSaving::BeginSettle();
            SaferSaving::AutoSave::OnGameLoaded();
            break;
        case SKSE::MessagingInterface::kSaveGame:
            SaferSaving::AutoSave::OnSaved();
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
    SaferSaving::AutoSave::Init();
    InstallHook();
    if (!SKSE::GetMessagingInterface()->RegisterListener(OnMessage))
    {
        logs::error("failed to register messaging listener");
        return false;
    }
    return true;
}
