#pragma once
#include <array>
#include <algorithm>
#include <cmath>

// UI samples use monotonic timestamps: timer delays must not stretch five seconds.
class GainReductionHistory
{
public:
    static constexpr double duration = 5.0;
    void clear() { next = count = 0; }
    void push (double time, float reduction)
    {
        if (! std::isfinite (time)) return;
        samples[next] = { time, std::isfinite (reduction) ? std::clamp (reduction, 0.0f, 30.0f) : 0.0f };
        next = (next + 1) % samples.size();
        count = std::min (count + 1, samples.size());
    }

    template <typename Visitor> void visit (double now, Visitor&& visitor) const
    {
        const auto first = (next + samples.size() - count) % samples.size();
        for (size_t i = 0; i < count; ++i)
        {
            const auto& sample = samples[(first + i) % samples.size()];
            const auto age = now - sample.time;
            if (age >= 0 && age <= duration)
                visitor (static_cast<float> (1.0 - age / duration), sample.reduction, sample.time);
        }
    }
private:
    struct Sample { double time = 0; float reduction = 0; };
    std::array<Sample, 256> samples {};
    size_t next = 0, count = 0;
};
