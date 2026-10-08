#pragma once

namespace SaferSaving::SavePrune
{
// reads the ini; call again whenever a setting changes
void Configure();

// trims the oldest manual saves when a new one is made
void OnSaved(std::string_view a_name);
} // namespace SaferSaving::SavePrune
