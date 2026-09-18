#include "autosave-blocker.h"

#include "pch.h"

#include "config.h"

namespace
{
namespace Config = SaferSaving::Config;

struct Override
{
    const char* key;
    bool value;
};

// the engine's own autosave gates
constexpr Override kOverrides[]{
    {"bDisableAutoSave", true},         // master switch, may be vestigial
    {"bAllowScriptedAutosave", false},  // Game.RequestAutoSave()
    {"bAllowScriptedForceSave", false}, // Game.RequestSave()
    {"bSaveOnPause", false},            // the four below are the ones in Settings > Gameplay
    {"bSaveOnTravel", false},           //
    {"bSaveOnWait", false},             //
    {"bSaveOnRest", false},             //
};

constexpr auto kCount = std::size(kOverrides);

// GetSetting matches the whole "key:section" string, and the section a setting is filed under is
// not always the one the ini file writes it to, so match on the key and ignore the section
RE::Setting* FindByKey(RE::INISettingCollection* a_collection, std::string_view a_key)
{
    if (!a_collection)
    {
        return nullptr;
    }

    for (auto* setting : a_collection->settings)
    {
        const char* name = setting ? setting->GetName() : nullptr;
        if (!name)
        {
            continue;
        }

        const std::string_view full{name};
        if (full.size() < a_key.size() || _strnicmp(name, a_key.data(), a_key.size()) != 0)
        {
            continue;
        }

        if (full.size() == a_key.size() || full[a_key.size()] == ':')
        {
            return setting;
        }
    }

    return nullptr;
}

RE::Setting* Resolve(std::string_view a_key)
{
    if (auto* setting = FindByKey(RE::INIPrefSettingCollection::GetSingleton(), a_key))
    {
        return setting;
    }

    return FindByKey(RE::INISettingCollection::GetSingleton(), a_key);
}

std::size_t CountSettings(RE::INISettingCollection* a_collection)
{
    if (!a_collection)
    {
        return 0;
    }

    std::size_t count = 0;
    for ([[maybe_unused]] auto* setting : a_collection->settings)
    {
        ++count;
    }

    return count;
}

std::array<RE::Setting*, kCount> ResolveAll()
{
    std::array<RE::Setting*, kCount> resolved{};
    bool missing = false;

    for (std::size_t i = 0; i < kCount; ++i)
    {
        auto* setting = Resolve(kOverrides[i].key);
        if (!setting)
        {
            logs::warn("autosave setting {} not found; leaving it alone", kOverrides[i].key);
            missing = true;
            continue;
        }

        // SetBool writes the union blind, so a mistyped setting would corrupt whatever is in there
        if (setting->GetType() != RE::Setting::Type::kBool)
        {
            logs::warn("autosave setting {} is not a bool; leaving it alone", setting->GetName());
            missing = true;
            continue;
        }

        resolved[i] = setting;

        // the full name carries the section, and the value is logged before the first write so the
        // player can put their own back by hand
        logs::info("autosave setting {} was {}", setting->GetName(), setting->GetBool());
    }

    if (missing)
    {
        logs::warn("searched {} pref settings and {} ini settings",
                   CountSettings(RE::INIPrefSettingCollection::GetSingleton()),
                   CountSettings(RE::INISettingCollection::GetSingleton()));
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
