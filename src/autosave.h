#pragma once

namespace SaferSaving::AutoSave
{
// reads and range-checks the ini values once, before the first frame
void Init();

// a load or a new game restarts the interval and reseeds the rotation slot
void OnGameLoaded();

// any save at all re-arms the interval, so we never autosave moments after the player saved
void OnSaved();

// frame hook, sharing the clock read and the verdict the guard has already computed
void Tick(RE::PlayerCharacter* a_player, std::uint32_t a_now, bool a_blocked);
} // namespace SaferSaving::AutoSave
