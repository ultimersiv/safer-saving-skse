#pragma once

namespace SaferSaving::AutoSave
{
// reads the ini; call again whenever a setting changes
void Configure();

// restarts the interval and reseeds the slot
void OnGameLoaded();

// any save re-arms the interval
void OnSaved();

// every frame, so only time the game is actually running counts toward the interval
void Tick();
} // namespace SaferSaving::AutoSave
