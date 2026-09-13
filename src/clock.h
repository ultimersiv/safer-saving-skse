#pragma once

namespace SaferSaving::Clock
{
// Real milliseconds since the game launched: one pointer dereference, no syscall, no allocation,
// and immune to both the SGTM time multiplier and freezeTime. The delta the update hook receives
// and GetSecondsSinceLastFrame are scaled game time, so neither measures real time spent playing.
inline std::uint32_t Now()
{
    return RE::GetDurationOfApplicationRunTime();
}

// The counter wraps after roughly 49 days of uptime, so deadlines are compared as differences
// rather than as values
inline bool Reached(std::uint32_t a_now, std::uint32_t a_deadline)
{
    return static_cast<std::int32_t>(a_now - a_deadline) >= 0;
}
} // namespace SaferSaving::Clock
