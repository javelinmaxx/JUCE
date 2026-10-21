#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace zao
{
constexpr double pi = 3.14159265358979323846;
constexpr size_t circuitCount = 4;
constexpr std::array<const char*, circuitCount> circuitNames { "BJT CE", "OPAMP FB", "DIODE SHUNT", "CROSSOVER" };

// Linear continuation keeps the exponential and its derivative finite during
// Newton iterations without introducing a flat, nonphysical current clamp.
struct Exponential { double value, derivative; };
inline Exponential junctionExponential (double voltage, double thermalVoltage) noexcept
{
    const auto x = voltage / thermalVoltage;
    const auto e = std::exp (std::clamp (x, -80.0, 40.0));
    return { x > 40.0 ? e * (1.0 + x - 40.0) : e, e / thermalVoltage };
}

inline bool solve4 (std::array<std::array<double, 4>, 4> a,
                    std::array<double, 4> b, std::array<double, 4>& x) noexcept
{
    for (size_t col = 0; col < 4; ++col)
    {
        auto pivot = col;
        for (size_t row = col + 1; row < 4; ++row)
            if (std::abs (a[row][col]) > std::abs (a[pivot][col])) pivot = row;
        if (std::abs (a[pivot][col]) < 1.0e-20) return false;
        std::swap (a[col], a[pivot]);
        std::swap (b[col], b[pivot]);
        for (size_t row = col + 1; row < 4; ++row)
        {
            const auto scale = a[row][col] / a[col][col];
            for (size_t k = col; k < 4; ++k) a[row][k] -= scale * a[col][k];
            b[row] -= scale * b[col];
        }
    }
    for (int row = 3; row >= 0; --row)
    {
        const auto r = static_cast<size_t> (row);
        auto sum = b[r];
        for (size_t col = r + 1; col < 4; ++col) sum -= a[r][col] * x[col];
        x[r] = sum / a[r][r];
    }
    return true;
}

// A 9 V common-emitter circuit, solved in volts and amperes. The transistor is
// a charge-based Ebers-Moll model with finite forward/reverse beta and Early
// modulation. Both BE and BC conduction remain active in saturation/cutoff.
// It is an explicit nominal small-signal NPN design, not a measured 2N3904 fit.
class BjtCommonEmitter
{
public:
    static constexpr double supply = 9.0, saturationCurrent = 6.734e-15;
    static constexpr double betaForward = 200.0, betaReverse = 4.0;
    static constexpr double thermalVoltage = 0.02585, earlyVoltage = 100.0;
    static constexpr double sourceResistance = 10000.0, inputCapacitance = 100.0e-9;
    static constexpr double topResistance = 100000.0, bottomResistance = 22000.0;
    static constexpr double collectorResistance = 4700.0;
    static constexpr double emitterResistance = 220.0, bypassResistance = 820.0;
    static constexpr double bypassCapacitance = 4.7e-6;

    void prepare (double sampleRate) noexcept
    {
        rate = sampleRate;
        inputConductance = 1.0 / (sourceResistance + 1.0 / (2.0 * rate * inputCapacitance));
        bypassConductance = 2.0 * rate * bypassCapacitance;
        dcBlockCoefficient = std::exp (-2.0 * pi * 1.6 / rate);
        reset();
    }

    void reset() noexcept
    {
        nodes = { 1.45, 0.80, 5.4, 0.63 };
        solve (0.0, true, 80);
        operatingPoint = nodes;
        inputVoltage = -nodes[0]; inputCurrent = 0;
        bypassVoltage = nodes[3]; bypassCurrent = 0;
        beCharge = charge (nodes[0] - nodes[1], 25.0e-12, 0.4e-9).value;
        bcCharge = charge (nodes[0] - nodes[2], 4.0e-12, 25.0e-9).value;
        beCurrent = bcCurrent = dcInput = dcOutput = 0;
    }

    double process (double input) noexcept
    {
        const auto voltage = std::clamp (input, -12.0, 12.0);
        solve (voltage, false, 24);
        const auto inputHistory = inputVoltage + inputCurrent / (2.0 * rate * inputCapacitance);
        inputCurrent = inputConductance * (voltage - nodes[0] - inputHistory);
        inputVoltage = voltage - nodes[0] - sourceResistance * inputCurrent;
        const auto newBypassCurrent = bypassConductance * (nodes[3] - bypassVoltage) - bypassCurrent;
        bypassVoltage = nodes[3]; bypassCurrent = newBypassCurrent;
        const auto newBeCharge = charge (nodes[0] - nodes[1], 25.0e-12, 0.4e-9).value;
        const auto newBcCharge = charge (nodes[0] - nodes[2], 4.0e-12, 25.0e-9).value;
        beCurrent = 2.0 * rate * (newBeCharge - beCharge) - beCurrent;
        bcCurrent = 2.0 * rate * (newBcCharge - bcCharge) - bcCurrent;
        beCharge = newBeCharge; bcCharge = newBcCharge;
        // Restore input polarity after the physical CE inversion; the output
        // coupling capacitor removes the collector's DC operating voltage.
        const auto centred = (operatingPoint[2] - nodes[2]) / 4.0;
        const auto output = centred - dcInput + dcBlockCoefficient * dcOutput;
        dcInput = centred; dcOutput = output;
        return output;
    }

    const std::array<double, 4>& getNodes() const noexcept { return nodes; }
    const std::array<double, 4>& getOperatingPoint() const noexcept { return operatingPoint; }
    double getResidual() const noexcept { return residual; }

private:
    struct Charge { double value, derivative; };
    static Charge charge (double voltage, double capacitance, double transitTime) noexcept
    {
        // SPICE-style depletion charge with a continuous forward-bias
        // continuation, plus diffusion charge Q = transit-time * junction I.
        constexpr double potential = 0.75, grading = 0.33, forward = 0.5;
        const auto boundary = potential * forward;
        double q = 0, c = 0;
        if (voltage < boundary)
        {
            const auto t = 1.0 - voltage / potential;
            q = capacitance * potential / (1.0 - grading) * (1.0 - std::pow (t, 1.0 - grading));
            c = capacitance * std::pow (t, -grading);
        }
        else
        {
            const auto cf = capacitance * std::pow (1.0 - forward, -grading);
            const auto slope = cf * grading / (potential * (1.0 - forward));
            const auto delta = voltage - boundary;
            q = capacitance * potential / (1.0 - grading)
                * (1.0 - std::pow (1.0 - forward, 1.0 - grading)) + cf * delta + 0.5 * slope * delta * delta;
            c = cf + slope * delta;
        }
        const auto e = junctionExponential (voltage, thermalVoltage);
        return { q + transitTime * saturationCurrent * (e.value - 1.0),
                 c + transitTime * saturationCurrent * e.derivative };
    }

    void equations (const std::array<double, 4>& v, double input, bool dc,
                    std::array<double, 4>& f, std::array<std::array<double, 4>, 4>& j) const noexcept
    {
        const auto ef = junctionExponential (v[0] - v[1], thermalVoltage);
        const auto er = junctionExponential (v[0] - v[2], thermalVoltage);
        const auto forward = saturationCurrent * (ef.value - 1.0);
        const auto reverse = saturationCurrent * (er.value - 1.0);
        const auto gm = saturationCurrent * ef.derivative, gr = saturationCurrent * er.derivative;
        const auto early = 1.0 + (v[2] - v[1]) / earlyVoltage;
        const auto ic = forward * early - (1.0 + 1.0 / betaReverse) * reverse;
        const auto ib = forward / betaForward + reverse / betaReverse;
        const std::array<double, 3> dic { gm * early - (1.0 + 1.0 / betaReverse) * gr,
            -gm * early - forward / earlyVoltage, forward / earlyVoltage + (1.0 + 1.0 / betaReverse) * gr };
        const std::array<double, 3> dib { gm / betaForward + gr / betaReverse,
            -gm / betaForward, -gr / betaReverse };
        f = { (v[0] - supply) / topResistance + v[0] / bottomResistance + ib,
              (v[1] - v[3]) / emitterResistance - ic - ib,
              (v[2] - supply) / collectorResistance + ic,
              (v[3] - v[1]) / emitterResistance + v[3] / bypassResistance };
        j = {};
        for (size_t k = 0; k < 3; ++k)
        {
            j[0][k] = dib[k]; j[1][k] = -dic[k] - dib[k]; j[2][k] = dic[k];
        }
        j[0][0] += 1.0 / topResistance + 1.0 / bottomResistance;
        j[1][1] += 1.0 / emitterResistance; j[1][3] = -1.0 / emitterResistance;
        j[2][2] += 1.0 / collectorResistance;
        j[3][1] = -1.0 / emitterResistance;
        j[3][3] = 1.0 / emitterResistance + 1.0 / bypassResistance;
        if (dc) return;
        f[0] += inputConductance * (v[0] - input + inputVoltage
                                     + inputCurrent / (2.0 * rate * inputCapacitance));
        j[0][0] += inputConductance;
        f[3] += bypassConductance * (v[3] - bypassVoltage) - bypassCurrent;
        j[3][3] += bypassConductance;
        const auto stampCharge = [&] (size_t to, double capacitance, double transitTime,
                                      double previousCharge, double previousCurrent)
        {
            const auto q = charge (v[0] - v[to], capacitance, transitTime);
            const auto current = 2.0 * rate * (q.value - previousCharge) - previousCurrent;
            const auto conductance = 2.0 * rate * q.derivative;
            f[0] += current; f[to] -= current;
            j[0][0] += conductance; j[0][to] -= conductance;
            j[to][0] -= conductance; j[to][to] += conductance;
        };
        stampCharge (1, 25.0e-12, 0.4e-9, beCharge, beCurrent);
        stampCharge (2, 4.0e-12, 25.0e-9, bcCharge, bcCurrent);
    }

    void solve (double input, bool dc, int iterations) noexcept
    {
        std::array<double, 4> f {}, step {};
        std::array<std::array<double, 4>, 4> j {};
        for (int iteration = 0; iteration < iterations; ++iteration)
        {
            equations (nodes, input, dc, f, j);
            residual = 0;
            for (auto x : f) residual = std::max (residual, std::abs (x));
            if (residual < 1.0e-10) return;
            if (! solve4 (j, f, step)) return;
            // Limit only movement into forward conduction. Reverse-biased BC
            // voltage can change by several volts at a clipping edge; limiting
            // that harmless movement to 100 mV would leave saturation recovery
            // unconverged within the real-time iteration budget.
            auto damping = 1.0;
            for (size_t to : { size_t (1), size_t (2) })
            {
                const auto forwardStep = step[to] - step[0];
                const auto allowed = std::max (0.10, 0.65 - (nodes[0] - nodes[to]));
                if (forwardStep > allowed) damping = std::min (damping, allowed / forwardStep);
            }
            for (auto x : step) if (std::abs (x) > 2.0) damping = std::min (damping, 2.0 / std::abs (x));
            for (size_t k = 0; k < 4; ++k) nodes[k] -= damping * step[k];
        }
        equations (nodes, input, dc, f, j);
        residual = 0;
        for (auto x : f) residual = std::max (residual, std::abs (x));
    }

    double rate = 192000, inputConductance = 0, bypassConductance = 0;
    std::array<double, 4> nodes {}, operatingPoint {};
    double inputVoltage = 0, inputCurrent = 0, bypassVoltage = 0, bypassCurrent = 0;
    double beCharge = 0, bcCharge = 0, beCurrent = 0, bcCurrent = 0;
    double dcBlockCoefficient = 0, dcInput = 0, dcOutput = 0, residual = 0;
};

// Non-inverting op-amp with antiparallel silicon diodes and a capacitor inside
// its feedback loop. Solve the feedback voltage, not a post-amplifier waveshaper.
class OpAmpFeedbackClipper
{
public:
    static constexpr double groundResistance = 4700, feedbackResistance = 47000;
    static constexpr double diodeIs = 2.0e-9, diodeThermalVoltage = 1.9 * 0.02585;
    static constexpr double feedbackCapacitance = 51.0e-12;
    static constexpr double openLoopGain = 100000, gainBandwidth = 3.0e6;
    static constexpr double rail = 4.0, slewRate = 1.0e6;

    void prepare (double rate) noexcept
    {
        const auto poleStep = 2.0 * pi * gainBandwidth / (openLoopGain * rate);
        loopGain = poleStep * openLoopGain / (1.0 + poleStep);
        memory = 1.0 / (1.0 + poleStep);
        capacitorConductance = 2.0 * rate * feedbackCapacitance;
        slewStep = slewRate / rate;
        reset();
    }
    void reset() noexcept { output = feedbackVoltage = capacitorCurrent = residual = 0; }

    double process (double input) noexcept
    {
        const auto history = -capacitorConductance * feedbackVoltage - capacitorCurrent;
        const auto oldOutput = output;
        const auto reference = memory * oldOutput + loopGain * std::clamp (input, -12.0, 12.0);
        const auto current = [&] (double v)
        {
            const auto positive = junctionExponential (v, diodeThermalVoltage);
            const auto negative = junctionExponential (-v, diodeThermalVoltage);
            return std::array<double, 2> {
                v / feedbackResistance + diodeIs * (positive.value - negative.value)
                    + capacitorConductance * v + history,
                1.0 / feedbackResistance + diodeIs * (positive.derivative + negative.derivative)
                    + capacitorConductance };
        };
        const auto solve = [&] (double multiplier, double target)
        {
            auto v = feedbackVoltage;
            for (int iteration = 0; iteration < 18; ++iteration)
            {
                const auto i = current (v);
                const auto f = v + multiplier * groundResistance * i[0] - target;
                if (std::abs (f) < 1.0e-10) break;
                v -= std::clamp (f / (1.0 + multiplier * groundResistance * i[1]), -0.15, 0.15);
            }
            return v;
        };
        auto v = solve (1.0 + loopGain, reference);
        auto i = current (v);
        auto nextOutput = v + groundResistance * i[0];
        const auto lower = std::max (-rail, oldOutput - slewStep);
        const auto upper = std::min (rail, oldOutput + slewStep);
        const auto limited = std::clamp (nextOutput, lower, upper);
        if (std::abs (limited - nextOutput) > 1.0e-10)
        {
            v = solve (1.0, limited);
            i = current (v);
            nextOutput = v + groundResistance * i[0];
            residual = std::abs (nextOutput - limited);
        }
        else
            residual = std::abs (v + (1.0 + loopGain) * groundResistance * i[0] - reference);
        capacitorCurrent = capacitorConductance * (v - feedbackVoltage) - capacitorCurrent;
        feedbackVoltage = v;
        output = nextOutput;
        return output / 4.0;
    }
    double getResidual() const noexcept { return residual; }
    double getOutputVolts() const noexcept { return output; }
    double getFeedbackVoltage() const noexcept { return feedbackVoltage; }
private:
    double loopGain = 0, memory = 0, capacitorConductance = 0, slewStep = 0;
    double output = 0, feedbackVoltage = 0, capacitorCurrent = 0, residual = 0;
};

struct DiodeCurrent { double value, derivative; };
inline DiodeCurrent antiparallelDiodes (double voltage) noexcept
{
    constexpr double saturation = 2.0e-9, thermal = 1.9 * 0.02585;
    const auto positive = junctionExponential (voltage, thermal);
    const auto negative = junctionExponential (-voltage, thermal);
    return { saturation * (positive.value - negative.value),
             saturation * (positive.derivative + negative.derivative) };
}

// Both passive networks reduce to a strictly increasing scalar KCL equation.
// Bracketing prevents an overload reversal from sending Newton outside the
// physically relevant diode-voltage range; no artificial diode knee is used.
template <typename Equation>
inline double solveDiodeVoltage (double previous, Equation equation) noexcept
{
    double lower = -1.5, upper = 1.5;
    auto voltage = std::clamp (previous, lower, upper);
    for (int iteration = 0; iteration < 40; ++iteration)
    {
        const auto f = equation (voltage);
        if (std::abs (f.value) < 1.0e-12) break;
        if (f.value > 0) upper = voltage; else lower = voltage;
        const auto next = voltage - f.value / f.derivative;
        voltage = next > lower && next < upper ? next : (lower + upper) * 0.5;
    }
    return voltage;
}

class DiodeShuntClipper
{
public:
    static constexpr double sourceResistance = 4700, loadResistance = 100000;
    static constexpr double capacitance = 100.0e-12;
    void prepare (double rate) noexcept { conductance = 2.0 * rate * capacitance; reset(); }
    void reset() noexcept { outputVoltage = capacitorCurrent = residual = 0; }
    double process (double input) noexcept
    {
        const auto source = std::clamp (input, -12.0, 12.0);
        const auto history = -conductance * outputVoltage - capacitorCurrent;
        const auto equation = [&] (double voltage)
        {
            const auto diode = antiparallelDiodes (voltage);
            return DiodeCurrent { (voltage - source) / sourceResistance + voltage / loadResistance
                                    + conductance * voltage + history + diode.value,
                                  1.0 / sourceResistance + 1.0 / loadResistance + conductance + diode.derivative };
        };
        const auto voltage = solveDiodeVoltage (outputVoltage, equation);
        residual = std::abs (equation (voltage).value);
        capacitorCurrent = conductance * (voltage - outputVoltage) - capacitorCurrent;
        outputVoltage = voltage;
        return voltage / 4.0;
    }
    double getResidual() const noexcept { return residual; }
    double getOutputVolts() const noexcept { return outputVoltage; }
private:
    double conductance = 0, outputVoltage = 0, capacitorCurrent = 0, residual = 0;
};

class SeriesDiodeCrossover
{
public:
    static constexpr double sourceResistance = 1000, loadResistance = 10000;
    static constexpr double loadCapacitance = 100.0e-12, junctionCapacitance = 4.0e-12;
    void prepare (double rate) noexcept
    {
        loadConductance = 2.0 * rate * loadCapacitance;
        junctionConductance = 2.0 * rate * junctionCapacitance;
        reset();
    }
    void reset() noexcept { outputVoltage = diodeVoltage = loadCurrent = junctionCurrent = residual = 0; }
    double process (double input) noexcept
    {
        const auto source = std::clamp (input, -12.0, 12.0);
        const auto loadHistory = -loadConductance * outputVoltage - loadCurrent;
        const auto diodeHistory = -junctionConductance * diodeVoltage - junctionCurrent;
        const auto load = 1.0 / loadResistance + loadConductance;
        const auto denominator = 1.0 + sourceResistance * load;
        const auto equation = [&] (double voltage)
        {
            const auto diode = antiparallelDiodes (voltage);
            return DiodeCurrent {
                diode.value + junctionConductance * voltage + diodeHistory
                    - (load * (source - voltage) + loadHistory) / denominator,
                diode.derivative + junctionConductance + load / denominator };
        };
        const auto voltage = solveDiodeVoltage (diodeVoltage, equation);
        const auto nextOutput = (source - voltage - sourceResistance * loadHistory) / denominator;
        residual = std::abs (equation (voltage).value);
        junctionCurrent = junctionConductance * (voltage - diodeVoltage) - junctionCurrent;
        loadCurrent = loadConductance * (nextOutput - outputVoltage) - loadCurrent;
        diodeVoltage = voltage; outputVoltage = nextOutput;
        return nextOutput / 4.0;
    }
    double getResidual() const noexcept { return residual; }
    double getOutputVolts() const noexcept { return outputVoltage; }
    double getDiodeVoltage() const noexcept { return diodeVoltage; }
private:
    double loadConductance = 0, junctionConductance = 0;
    double outputVoltage = 0, diodeVoltage = 0, loadCurrent = 0, junctionCurrent = 0, residual = 0;
};
}
