#pragma once

namespace SaferSaving::Clock
{
// real milliseconds since launch, one load; unlike the update delta it ignores the time multiplier
inline std::uint32_t Now()
{
    return RE::GetDurationOfApplicationRunTime();
}

// wraps after ~49 days, so compare as a difference
inline bool Reached(std::uint32_t a_now, std::uint32_t a_deadline)
{
    return static_cast<std::int32_t>(a_now - a_deadline) >= 0;
}
} // namespace SaferSaving::Clock
