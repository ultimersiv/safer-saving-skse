#pragma once

namespace SaferSaving::Config
{
// what the custom ini needs to know about a setting
class Entry
{
  public:
    Entry(const char* a_section, const char* a_key)
        : section(a_section),
          key(a_key)
    {
    }

    virtual ~Entry() = default;

    // nullopt when the value matches the base ini, so the key is dropped
    virtual std::optional<std::string> Override() const = 0;
    virtual void Reset()                                = 0;

    const char* section;
    const char* key;
};

// REX keeps section and key private, so they are kept again here
template <class T>
class Setting : public REX::INI::Setting<T>, public Entry
{
  public:
    Setting(const char* a_section, const char* a_key, T a_default)
        : REX::INI::Setting<T>(a_section, a_key, a_default),
          Entry(a_section, a_key)
    {
    }

    std::optional<std::string> Override() const override
    {
        const auto value = this->GetValue();
        if (value == this->GetValueDefault())
        {
            return std::nullopt;
        }

        // spelled the way REX writes them, so a saved file reads back the same
        if constexpr (std::is_same_v<T, bool>)
        {
            return value ? "true" : "false";
        }
        else if constexpr (std::is_same_v<T, std::string>)
        {
            return value;
        }
        else
        {
            return std::to_string(value);
        }
    }

    void Reset() override { this->SetValue(this->GetValueDefault()); }
};

using Bool = Setting<bool>;
using I32  = Setting<std::int32_t>;
using Str  = Setting<std::string>;

inline constexpr std::int32_t kMaxSettleSeconds{300};
inline constexpr std::int32_t kMaxIntervalMinutes{1440};
inline constexpr std::int32_t kMinSlots{1};
inline constexpr std::int32_t kMaxSlots{99};
inline constexpr std::int32_t kMaxManualSaves{999};

void Load();

// writes the overrides to the custom ini in one go; false when it could not
bool Persist(std::span<Entry* const> a_entries);
bool Persist(Entry& a_entry);

inline Bool notify{"Notification", "bEnabled", true};
// the ini trims spaces, so the separator is added in code
inline Str messagePrefix{"Notification", "sPrefix", "Cannot save:"};

inline I32 settleSeconds{"Load", "iSettleSeconds", 30}; // zero disables the wait

// on by default; the deferred autosave below replaces what it removes
inline Bool disableVanillaAutosaves{"Autosave", "bDisableVanilla", true};
inline I32 autoSaveMinutes{"Autosave", "iIntervalMinutes", 15}; // zero turns it off
inline I32 autoSaveSlots{"Autosave", "iSlots", 5};

// zero keeps every manual save
inline I32 maxManualSaves{"Saves", "iMaxManualSaves", 0};

inline Bool inCombat{"Combat", "bInCombat", true};
inline Bool attacking{"Combat", "bAttacking", true};
inline Bool weaponDrawn{"Combat", "bWeaponDrawn", true};
inline Bool killmove{"Combat", "bKillmove", true};
inline Bool enemiesNearby{"Combat", "bEnemiesNearby", true};

inline Bool moving{"Movement", "bMoving", true};
inline Bool sprinting{"Movement", "bSprinting", true};
inline Bool sneaking{"Movement", "bSneaking", true};
inline Bool swimming{"Movement", "bSwimming", true};
inline Bool flying{"Movement", "bFlying", true};
inline Bool midair{"Movement", "bMidair", true};
inline Bool mounted{"Movement", "bMounted", true};

inline Bool notAlive{"State", "bNotAlive", true};
inline Bool bleedingOut{"State", "bBleedingOut", true};
inline Bool knockedDown{"State", "bKnockedDown", true};
inline Bool staggered{"State", "bStaggered", true};
inline Bool sitSleepTransition{"State", "bSitSleepTransition", true};
inline Bool animationDriven{"State", "bAnimationDriven", true};
inline Bool grabbing{"State", "bGrabbing", true};
inline Bool controlsDisabled{"State", "bControlsDisabled", true};
inline Bool trespassing{"State", "bTrespassing", true};
inline Bool notLoaded{"State", "b3DNotLoaded", true};

inline Bool itemMenus{"Menus", "bItemMenus", true};
inline Bool dialogue{"Menus", "bDialogue", true};
inline Bool book{"Menus", "bBook", true};
inline Bool crafting{"Menus", "bCrafting", true};
inline Bool lockpicking{"Menus", "bLockpicking", true};
inline Bool levelUpTraining{"Menus", "bLevelUpTraining", true};
inline Bool sleepWait{"Menus", "bSleepWait", true};
inline Bool loading{"Menus", "bLoading", true};
inline Bool characterCreation{"Menus", "bCharacterCreation", true};
} // namespace SaferSaving::Config
