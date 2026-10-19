#pragma once

#include <algorithm>

namespace character
{
// Scalar play operator: a compact, causal hysteresis element. The state stays
// inside a coercive band around the applied field and follows a boundary when
// the input exits that band. A reversal therefore follows a different branch.
// This is an audio approximation, not a full Jiles-Atherton core model.
class MagneticHysteresis
{
public:
    static constexpr float coerciveFieldFraction = 0.04f;

    void reset() noexcept { magnetization = 0.0f; }

    float process (float appliedField, float drive) noexcept
    {
        const auto width = coerciveFieldFraction * drive;
        magnetization = std::clamp (magnetization, appliedField - width, appliedField + width);
        return magnetization;
    }

private:
    float magnetization = 0.0f;
};
} // namespace character
