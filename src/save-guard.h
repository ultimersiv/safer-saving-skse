#pragma once

namespace SaferSaving
{
bool IsMenuInBlockList(const RE::BSFixedString& a_menuName);

void BlockForMenu();

void BeginSettle();

void OnGameLoaded();

// menu events: re-decide the flag only
void Reevaluate();

// frame hook: re-decide the flag and drive the autosave off the same clock read
void OnFrame();
} // namespace SaferSaving
