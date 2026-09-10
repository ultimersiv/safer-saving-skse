#pragma once

namespace SaferSaving
{
bool IsMenuInBlockList(const RE::BSFixedString& a_menuName);

void BlockForMenu();

void BeginSettle();

void OnGameLoaded();

void Reevaluate();
} // namespace SaferSaving
