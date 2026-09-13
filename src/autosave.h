#pragma once

namespace SaferSaving::AutoSave
{
// reads the ini once, before the first frame
void Init();

// restarts the interval and reseeds the slot
void OnGameLoaded();

// any save re-arms the interval
void OnSaved();

// per frame, with the clock read and verdict the guard already has
void Tick(RE::PlayerCharacter* a_player, std::uint32_t a_now, bool a_blocked);
} // namespace SaferSaving::AutoSave
