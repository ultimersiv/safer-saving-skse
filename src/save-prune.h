#pragma once

namespace SaferSaving::SavePrune
{
// applies the current settings; call again whenever one changes
void Configure();

// trims the oldest manual saves when a new one is made
void OnSaved(std::string_view a_name);
} // namespace SaferSaving::SavePrune
