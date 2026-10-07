#pragma once

namespace SaferSaving::SavePrune
{
// reads the ini once, before the first frame
void Init();

// trims the oldest manual saves when a new one is made
void OnSaved(std::string_view a_name);
} // namespace SaferSaving::SavePrune
