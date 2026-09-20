#pragma once

namespace SaferSaving::AutoSave
{
// reads the ini once, before the first frame
void Init();

// restarts the interval and reseeds the slot
void OnGameLoaded();

// any save re-arms the interval
void OnSaved();

// every frame, so only time the game is actually running counts toward the interval
void Tick();
} // namespace SaferSaving::AutoSave
