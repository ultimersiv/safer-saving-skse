#pragma once

namespace SaferSaving
{
bool IsMenuInBlockList(const RE::BSFixedString& a_menuName);

void BlockForMenu();

void BeginSettle();

void OnGameLoaded();

void Tick();

void Reevaluate();

void NotifyBlockedSaveAttempt();
} // namespace SaferSaving
