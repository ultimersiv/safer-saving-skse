#pragma once

namespace SaferSaving::SavePrune
{
// reads the ini once, before the first frame
void Init();

// a prune queued by the session that just ended must not run against this one
void OnGameLoaded();

// queues a prune, which goes ahead only if the save that landed was a manual one
void OnSaved(std::string_view a_name);
} // namespace SaferSaving::SavePrune
