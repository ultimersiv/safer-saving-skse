#include "autosave-blocker.h"

#include "pch.h"

#include "config.h"

namespace
{
namespace Config = SaferSaving::Config;

enum class Collection
{
    kINI, // Skyrim.ini
    kPref // SkyrimPrefs.ini, which the game writes back
};

struct Override
{
    Collection collection;
    const char* name; // key:section, the format the engine stores INI settings under
    bool value;
};

// the engine's own autosave gates; the bSaveOn ones are the four in Settings > Gameplay
constexpr Override kOverrides[]{
    {Collection::kINI, "bDisableAutoSave:SaveGame", true},
    {Collection::kINI, "bAllowScriptedAutosave:SaveGame", false},
    {Collection::kINI, "bAllowScriptedForceSave:SaveGame", false},
    {Collection::kPref, "bSaveOnPause:MAIN", false},
    {Collection::kPref, "bSaveOnTravel:MAIN", false},
    {Collection::kPref, "bSaveOnWait:MAIN", false},
    {Collection::kPref, "bSaveOnRest:MAIN", false},
};

constexpr auto kCount = std::size(kOverrides);

RE::Setting* Resolve(const Override& a_override)
{
    if (a_override.collection == Collection::kINI)
    {
        auto* collection = RE::INISettingCollection::GetSingleton();
        return collection ? collection->GetSetting(a_override.name) : nullptr;
    }

    auto* collection = RE::INIPrefSettingCollection::GetSingleton();
    return collection ? collection->GetSetting(a_override.name) : nullptr;
}

std::array<RE::Setting*, kCount> ResolveAll()
{
    std::array<RE::Setting*, kCount> resolved{};

    for (std::size_t i = 0; i < kCount; ++i)
    {
        auto* setting = Resolve(kOverrides[i]);
        if (!setting)
        {
            logs::warn("autosave setting {} not found; leaving it alone", kOverrides[i].name);
            continue;
        }

        // SetBool writes the union blind, so a mistyped setting would corrupt whatever is in there
        if (setting->GetType() != RE::Setting::Type::kBool)
        {
            logs::warn("autosave setting {} is not a bool; leaving it alone", kOverrides[i].name);
            continue;
        }

        resolved[i] = setting;

        // logged before the first write so the player can put their own values back by hand
        logs::info("autosave setting {} was {}", kOverrides[i].name, setting->GetBool());
    }

    return resolved;
}

// function-local static, so the lookup happens once on whichever thread gets here first
const std::array<RE::Setting*, kCount>& Settings()
{
    static const auto settings = ResolveAll();
    return settings;
}
} // namespace

void SaferSaving::ApplyAutosaveBlock()
{
    if (!Config::disableVanillaAutosaves.GetValue())
    {
        return;
    }

    const auto& settings = Settings();
    for (std::size_t i = 0; i < kCount; ++i)
    {
        if (settings[i])
        {
            settings[i]->SetBool(kOverrides[i].value);
        }
    }
}
