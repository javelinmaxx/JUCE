#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace zao
{
// ISO 226:2003 normal equal-loudness contours, fixed at 80 phons. The standard
// supplies these 29 frequencies and af/Lu/Tf coefficients for its SPL equation.
// Filter gains use SPL(f)-SPL(1 kHz), with the requested +3 dB ceiling.
class EqualLoudness80
{
public:
    static constexpr std::array<double, 29> frequencies {
        20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500,
        630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500 };

    static double splAtIndex (size_t index) noexcept
    {
        constexpr std::array<double, 29> af {
            .532, .506, .480, .455, .432, .409, .387, .367, .349, .330, .315, .301, .288, .276, .267,
            .259, .253, .250, .246, .244, .243, .243, .243, .242, .242, .245, .254, .271, .301 };
        constexpr std::array<double, 29> lu {
            -31.6, -27.2, -23, -19.1, -15.9, -13, -10.3, -8.1, -6.2, -4.5, -3.1, -2, -1.1, -.4, 0,
            .3, .5, 0, -2.7, -4.1, -1, .1, 1.2, 1.6, .5, -2.6, -7.1, -11.2, -10.7 };
        constexpr std::array<double, 29> tf {
            78.5, 68.7, 59.5, 51.1, 44, 37.5, 31.5, 26.5, 22.1, 17.9, 14.4, 11.4, 8.6, 6.2, 4.4,
            3, 2.2, 2.4, 3.5, 1.7, -1.3, -4.2, -6, -5.4, -1.5, 6, 12.6, 13.9, 12.3 };
        const auto a = 4.47e-3*(std::pow (10.0, .025*80)-1.15)
            + std::pow (.4*std::pow (10.0, (tf[index]+lu[index])/10-9), af[index]);
        return 10/af[index]*std::log10 (a)-lu[index]+94;
    }

    static double contourDb (double frequency) noexcept
    {
        // The ISO table ends at 20 Hz and 12.5 kHz. Hold those endpoints outside
        // its range rather than inventing an extension of the standard.
        const auto hz = std::clamp (frequency, frequencies.front(), frequencies.back());
        static const auto spl = []
        {
            std::array<double, 29> result {};
            for (size_t i = 0; i < result.size(); ++i) result[i] = splAtIndex (i);
            return result;
        }();
        const auto upper = std::upper_bound (frequencies.begin(), frequencies.end(), hz);
        if (upper == frequencies.end()) return spl.back()-spl[17];
        const auto high = static_cast<size_t> (upper-frequencies.begin()), low = high-1;
        const auto t = std::log (hz/frequencies[low])/std::log (frequencies[high]/frequencies[low]);
        return spl[low]+t*(spl[high]-spl[low])-spl[17];
    }
    static double gainDb (double frequency) noexcept
    {
        return std::min (3.0, contourDb (frequency));
    }
};
}
