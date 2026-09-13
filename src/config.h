#pragma once

namespace SaferSaving::Config
{
void Load();

inline REX::INI::I32<> settleSeconds{"Load", "iSettleSeconds", 30}; // zero disables the wait

// zero disables autosaving entirely; slots set how many files the rotation cycles through
inline REX::INI::I32<> autoSaveMinutes{"AutoSave", "iIntervalMinutes", 15};
inline REX::INI::I32<> autoSaveSlots{"AutoSave", "iSlots", 5};

inline REX::INI::Bool<> inCombat{"Combat", "bInCombat", true};
inline REX::INI::Bool<> attacking{"Combat", "bAttacking", true};
inline REX::INI::Bool<> weaponDrawn{"Combat", "bWeaponDrawn", true};
inline REX::INI::Bool<> killmove{"Combat", "bKillmove", true};

inline REX::INI::Bool<> moving{"Movement", "bMoving", true};
inline REX::INI::Bool<> sprinting{"Movement", "bSprinting", true};
inline REX::INI::Bool<> sneaking{"Movement", "bSneaking", true};
inline REX::INI::Bool<> swimming{"Movement", "bSwimming", true};
inline REX::INI::Bool<> flying{"Movement", "bFlying", true};
inline REX::INI::Bool<> midair{"Movement", "bMidair", true};
inline REX::INI::Bool<> mounted{"Movement", "bMounted", true};

inline REX::INI::Bool<> notAlive{"State", "bNotAlive", true};
inline REX::INI::Bool<> bleedingOut{"State", "bBleedingOut", true};
inline REX::INI::Bool<> knockedDown{"State", "bKnockedDown", true};
inline REX::INI::Bool<> staggered{"State", "bStaggered", true};
inline REX::INI::Bool<> sitSleepTransition{"State", "bSitSleepTransition", true};
inline REX::INI::Bool<> animationDriven{"State", "bAnimationDriven", true};
inline REX::INI::Bool<> grabbing{"State", "bGrabbing", true};
inline REX::INI::Bool<> controlsDisabled{"State", "bControlsDisabled", true};
inline REX::INI::Bool<> notLoaded{"State", "b3DNotLoaded", true};

inline REX::INI::Bool<> itemMenus{"Menus", "bItemMenus", true};
inline REX::INI::Bool<> dialogue{"Menus", "bDialogue", true};
inline REX::INI::Bool<> book{"Menus", "bBook", true};
inline REX::INI::Bool<> crafting{"Menus", "bCrafting", true};
inline REX::INI::Bool<> lockpicking{"Menus", "bLockpicking", true};
inline REX::INI::Bool<> levelUpTraining{"Menus", "bLevelUpTraining", true};
inline REX::INI::Bool<> sleepWait{"Menus", "bSleepWait", true};
inline REX::INI::Bool<> loading{"Menus", "bLoading", true};
inline REX::INI::Bool<> characterCreation{"Menus", "bCharacterCreation", true};
} // namespace SaferSaving::Config
