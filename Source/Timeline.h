#pragma once
#include <JuceHeader.h>
#include "Dsp/Recipes.h"
#include <algorithm>
#include <cmath>

namespace entropy::timeline
{
inline constexpr int mappingVersion = 1;
inline constexpr juce::int64 second = 1000;
inline constexpr juce::int64 day = 86400000;
inline constexpr juce::int64 year = 31556952000LL;
inline constexpr juce::int64 earliest = -2208988800000LL;
inline constexpr juce::int64 latest = 253402214399000LL;

inline juce::int64 spanMs(int carrier) noexcept
{
    constexpr std::array<juce::int64, 4> spans { 60 * year, 100 * year, day, 40 * year };
    return spans[static_cast<size_t>(std::clamp(carrier, 0, 3))];
}

inline const char* spanLabel(int carrier) noexcept
{
    constexpr std::array<const char*, 4> labels { "60 YEARS", "100 YEARS", "24 HOURS", "40 YEARS" };
    return labels[static_cast<size_t>(std::clamp(carrier, 0, 3))];
}

inline float amountAt(juce::int64 selected, juce::int64 now, int carrier) noexcept
{
    return static_cast<float>(std::clamp((static_cast<double>(now) - static_cast<double>(selected))
        / static_cast<double>(spanMs(carrier)), 0.0, 1.0));
}

inline juce::int64 dateAt(float amount, juce::int64 now, int carrier) noexcept
{
    const auto age = static_cast<juce::int64>(std::llround(static_cast<double>(finiteClamp(amount, 0.0f, 1.0f))
        * static_cast<double>(spanMs(carrier))));
    return std::clamp(now - age, earliest, latest);
}

juce::String localDate(juce::int64 milliseconds);
juce::String tickLabel(juce::int64 milliseconds, bool includeTime);
bool parseLocalDate(const juce::String& text, juce::int64& result);

struct Snapshot
{
    juce::int64 now = 0, selected = 0;
    int carrier = 0;
    float amount() const noexcept { return amountAt(selected, now, carrier); }
    double position() const noexcept { return 1.0 - static_cast<double>(amount()); }
};

class Clock final : private juce::Thread
{
public:
    explicit Clock(const std::atomic<juce::int64>* injected = nullptr)
        : juce::Thread("Entropy wall clock"), external(injected), cached(juce::Time::currentTimeMillis())
    {
        static_assert(std::atomic<juce::int64>::is_always_lock_free);
        if (external == nullptr) startThread();
    }
    ~Clock() override { stopThread(1000); }
    juce::int64 now() const noexcept
    {
        return std::clamp((external != nullptr ? external : &cached)->load(std::memory_order_relaxed), earliest, latest);
    }
private:
    void run() override
    {
        while (!threadShouldExit())
        {
            cached.store(juce::Time::currentTimeMillis(), std::memory_order_relaxed);
            wait(100);
        }
    }
    const std::atomic<juce::int64>* external;
    std::atomic<juce::int64> cached;
};
}
