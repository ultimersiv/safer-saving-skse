#pragma once

namespace SaferSaving::AutoSave
{
// applies the current settings; call again whenever one changes
void Configure();

// restarts the interval and reseeds the slot
void OnGameLoaded();

// any save re-arms the interval, and one of ours clears the old file from its slot
void OnSaved(std::string_view a_name);

// every frame, so only time the game is actually running counts toward the interval
void Tick();
} // namespace SaferSaving::AutoSave
