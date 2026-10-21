#pragma once

namespace character
{
// The host stores one stable 1:1–20:1 ratio position for both modes.
// Yin projects that position onto its supported 1:1–2:1 range.
constexpr float hostToYinRatio (float hostRatio) noexcept
{
    return 1.0f + (hostRatio - 1.0f) / 19.0f;
}

constexpr float yinToHostRatio (float yinRatio) noexcept
{
    return 1.0f + (yinRatio - 1.0f) * 19.0f;
}
}
