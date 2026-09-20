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

// the engine's own autosave gates; nothing here touches saves a script asks for
constexpr Override kOverrides[]{
    {"bDisableAutoSave", true}, // master switch, may be vestigial
    {"bSaveOnPause", false},    // these four are the ones in Settings > Gameplay
    {"bSaveOnTravel", false},   //
    {"bSaveOnWait", false},     //
    {"bSaveOnRest", false},     //
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

std::array<RE::Setting*, kCount> ResolveAll()
{
    std::array<RE::Setting*, kCount> resolved{};

    for (std::size_t i = 0; i < kCount; ++i)
    {
        auto* setting = Resolve(kOverrides[i].key);
        if (!setting)
        {
            logs::warn("autosave setting {} not found; leaving it alone", kOverrides[i].key);
            continue;
        }

        // SetBool writes the union blind, so a mistyped setting would corrupt whatever is in there
        if (setting->GetType() != RE::Setting::Type::kBool)
        {
            logs::warn("autosave setting {} is not a bool; leaving it alone", setting->GetName());
            continue;
        }

        resolved[i] = setting;
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
