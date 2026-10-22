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

// The bypass-capacitor node couples only to the emitter. Eliminate it first,
// retaining partial pivoting for the remaining three transistor nodes.
inline bool solveBjtSystem (const std::array<std::array<double, 4>, 4>& a,
                            const std::array<double, 4>& b, std::array<double, 4>& x) noexcept
{
    if (std::abs (a[3][3]) < 1.0e-20) return solve4 (a, b, x);
    std::array<std::array<double, 3>, 3> m;
    std::array<double, 3> rhs { b[0], b[1], b[2] };
    for (size_t row = 0; row < 3; ++row)
        for (size_t col = 0; col < 3; ++col) m[row][col] = a[row][col];
    const auto coupling = a[1][3] / a[3][3];
    m[1][1] -= coupling * a[3][1]; rhs[1] -= coupling * b[3];
    for (size_t col = 0; col < 3; ++col)
    {
        auto pivot = col;
        for (size_t row = col+1; row < 3; ++row)
            if (std::abs (m[row][col]) > std::abs (m[pivot][col])) pivot = row;
        if (std::abs (m[pivot][col]) < 1.0e-20) return solve4 (a, b, x);
        std::swap (m[col], m[pivot]); std::swap (rhs[col], rhs[pivot]);
        for (size_t row = col+1; row < 3; ++row)
        {
            const auto scale = m[row][col] / m[col][col];
            for (size_t k = col+1; k < 3; ++k) m[row][k] -= scale*m[col][k];
            rhs[row] -= scale*rhs[col];
        }
    }
    x[2] = rhs[2]/m[2][2];
    x[1] = (rhs[1]-m[1][2]*x[2])/m[1][1];
    x[0] = (rhs[0]-m[0][1]*x[1]-m[0][2]*x[2])/m[0][0];
    x[3] = (b[3]-a[3][1]*x[1])/a[3][3];
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

    // Shift the divider's Thevenin voltage while keeping its source resistance.
    void setBiasVolts (double offset) noexcept { biasOffset = std::clamp (offset, -0.8, 0.8); }
    double getBiasVoltage() const noexcept { return supply * bottomResistance / (topResistance + bottomResistance) + biasOffset; }

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
        // The final residual evaluation already calculated these charges at
        // the accepted nodes, including when the iteration budget is reached.
        const auto newBeCharge = evaluatedBeCharge;
        const auto newBcCharge = evaluatedBcCharge;
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
    static Charge charge (double voltage, double capacitance, double transitTime, Exponential e) noexcept
    {
        // SPICE-style depletion charge with a continuous forward-bias
        // continuation, plus diffusion charge Q = transit-time * junction I.
        constexpr double potential = 0.75, grading = 0.33, forward = 0.5;
        const auto boundary = potential * forward;
        double q = 0, c = 0;
        if (voltage < boundary)
        {
            const auto t = 1.0 - voltage / potential;
            const auto power = std::pow (t, -grading);
            q = capacitance * potential / (1.0 - grading) * (1.0 - t*power);
            c = capacitance * power;
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
        return { q + transitTime * saturationCurrent * (e.value - 1.0),
                 c + transitTime * saturationCurrent * e.derivative };
    }
    static Charge charge (double voltage, double capacitance, double transitTime) noexcept
    {
        return charge (voltage, capacitance, transitTime, junctionExponential (voltage, thermalVoltage));
    }

    void equations (const std::array<double, 4>& v, double input, bool dc,
                    std::array<double, 4>& f, std::array<std::array<double, 4>, 4>& j) noexcept
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
        f = { (v[0] - supply) / topResistance + v[0] / bottomResistance
                  - biasOffset * (1.0 / topResistance + 1.0 / bottomResistance) + ib,
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
        const auto stampCharge = [&] (size_t to, Charge q,
                                      double previousCharge, double previousCurrent)
        {
            const auto current = 2.0 * rate * (q.value - previousCharge) - previousCurrent;
            const auto conductance = 2.0 * rate * q.derivative;
            f[0] += current; f[to] -= current;
            j[0][0] += conductance; j[0][to] -= conductance;
            j[to][0] -= conductance; j[to][to] += conductance;
        };
        const auto be = charge (v[0]-v[1], 25.0e-12, 0.4e-9, ef);
        const auto bc = charge (v[0]-v[2], 4.0e-12, 25.0e-9, er);
        evaluatedBeCharge = be.value; evaluatedBcCharge = bc.value;
        stampCharge (1, be, beCharge, beCurrent);
        stampCharge (2, bc, bcCharge, bcCurrent);
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
            if (! solveBjtSystem (j, f, step)) return;
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

    double rate = 192000, inputConductance = 0, bypassConductance = 0, biasOffset = 0;
    std::array<double, 4> nodes {}, operatingPoint {};
    double inputVoltage = 0, inputCurrent = 0, bypassVoltage = 0, bypassCurrent = 0;
    double beCharge = 0, bcCharge = 0, beCurrent = 0, bcCurrent = 0;
    double evaluatedBeCharge = 0, evaluatedBcCharge = 0;
    double dcBlockCoefficient = 0, dcInput = 0, dcOutput = 0, residual = 0;
};

struct DiodeCurrent { double value, derivative; };

// Nominal Schottky junctions plus explicit series ballast: the lower forward
// voltage alone is not used as a claim of softness. Solve the junction voltage
// and include the ballast drop and its analytic derivative in the circuit KCL.
struct SchottkyPair
{
    static constexpr double saturation = 200.0e-9, thermal = 1.2 * 0.02585;
    static constexpr double seriesResistance = 220.0;
    static DiodeCurrent current (double junctionVoltage) noexcept
    {
        // Antiparallel junctions share an exponential: exp(-x) = 1/exp(x).
        // Preserve the original continuation and reverse limit at overload.
        const auto x = std::abs (junctionVoltage) / thermal;
        const auto e = std::exp (std::min (x, 40.0));
        const auto reverse = x <= 40.0 ? 1.0/e : std::exp (-std::min (x, 80.0));
        const auto forward = x > 40.0 ? e*(1.0+x-40.0) : e;
        return { std::copysign (saturation*(forward-reverse), junctionVoltage),
                 saturation*(e+reverse)/thermal };
    }
};

// Non-inverting feedback circuit with finite op-amp output resistance. The
// internal amplifier must supply both feedback current and receiver loading.
class OpAmpFeedbackClipper
{
public:
    static constexpr double groundResistance = 4700, feedbackResistance = 47000;
    static constexpr double diodeIs = SchottkyPair::saturation, diodeThermalVoltage = SchottkyPair::thermal;
    static constexpr double feedbackCapacitance = 51.0e-12;
    static constexpr double openLoopGain = 100000, gainBandwidth = 3.0e6;
    static constexpr double rail = 4.0, slewRate = 1.0e6;
    static constexpr double outputResistance = 100, loadResistance = 100000;
    void prepare (double rate) noexcept
    {
        const auto poleStep = 2.0 * pi * gainBandwidth / (openLoopGain * rate);
        loopGain = poleStep * openLoopGain / (1.0 + poleStep);
        memory = 1.0 / (1.0 + poleStep);
        capacitorConductance = 2.0 * rate * feedbackCapacitance;
        slewStep = slewRate / rate;
        reset();
    }
    void reset() noexcept { output = internalOutput = feedbackVoltage = junctionVoltage = capacitorCurrent = residual = 0; }
    double process (double input) noexcept
    {
        const auto history = -capacitorConductance * feedbackVoltage - capacitorCurrent;
        const auto reference = memory * internalOutput + loopGain * std::clamp (input, -12.0, 12.0);
        const auto evaluate = [&] (double junction)
        {
            const auto diode = SchottkyPair::current (junction);
            const auto v = junction + SchottkyPair::seriesResistance * diode.value;
            const auto dv = 1.0 + SchottkyPair::seriesResistance * diode.derivative;
            const auto current = v / feedbackResistance + diode.value + capacitorConductance * v + history;
            const auto derivative = dv * (1.0 / feedbackResistance + capacitorConductance) + diode.derivative;
            const auto external = v + groundResistance * current;
            const auto internal = external + outputResistance * (current + external / loadResistance);
            const auto slope = dv * (1.0 + outputResistance / loadResistance)
                + derivative * (groundResistance * (1.0 + outputResistance / loadResistance) + outputResistance);
            return std::array<double, 6> { v, current, derivative, external, internal, slope };
        };
        std::array<double, 6> accepted {};
        const auto solve = [&] (double feedbackGain, double target)
        {
            auto junction = junctionVoltage;
            for (int iteration = 0; iteration < 24; ++iteration)
            {
                accepted = evaluate (junction);
                const auto& e = accepted;
                const auto f = e[4] + feedbackGain * groundResistance * e[1] - target;
                if (std::abs (f) < 1.0e-10) return junction;
                junction -= std::clamp (f / (e[5] + feedbackGain * groundResistance * e[2]), -0.10, 0.10);
            }
            accepted = evaluate (junction);
            return junction;
        };
        auto junction = solve (loopGain, reference);
        auto e = accepted;
        const auto lower = std::max (-rail, internalOutput - slewStep);
        const auto upper = std::min (rail, internalOutput + slewStep);
        const auto limited = std::clamp (e[4], lower, upper);
        if (std::abs (limited - e[4]) > 1.0e-10)
        {
            junction = solve (0, limited); e = accepted;
            residual = std::abs (e[4] - limited);
        }
        else residual = std::abs (e[4] + loopGain * groundResistance * e[1] - reference);
        capacitorCurrent = capacitorConductance * (e[0] - feedbackVoltage) - capacitorCurrent;
        feedbackVoltage = e[0]; junctionVoltage = junction;
        output = e[3]; internalOutput = e[4];
        return output / 4.0;
    }
    double getResidual() const noexcept { return residual; }
    double getOutputVolts() const noexcept { return output; }
    double getInternalVolts() const noexcept { return internalOutput; }
    double getFeedbackVoltage() const noexcept { return feedbackVoltage; }
private:
    double loopGain = 0, memory = 0, capacitorConductance = 0, slewStep = 0;
    double output = 0, internalOutput = 0, feedbackVoltage = 0, junctionVoltage = 0, capacitorCurrent = 0, residual = 0;
};

struct DriverParameters
{
    double resistance = 2200, currentLimit = 0.0031622776601683794, coefficient = 0;
    static DriverParameters fromKnee (double rate, double knee) noexcept
    {
        knee = std::clamp (knee, 0.0, 1.0);
        return { 220.0 * std::pow (100.0, knee), 0.01 * std::pow (0.1, knee),
                 1.0 - std::exp (-2.0 * pi * (45000.0 * std::pow (1.0 / 9.0, knee)) / rate) };
    }
};

// A loaded Thevenin driver, not an ideal gain feeding a hard voltage clamp.
// The current limit is part of each receiving circuit's nonlinear KCL solve.
class DiodeDriver
{
public:
    static constexpr double rail = 9.0;
    void prepare (double sampleRate) noexcept { rate = sampleRate; setKnee (0.5); reset(); }
    void reset() noexcept { voltage = 0; }
    void setKnee (double knee) noexcept { parameters = DriverParameters::fromKnee (rate, knee); }
    void setParameters (DriverParameters p) noexcept { parameters = p; }
    double process (double input) noexcept
    {
        const auto target = rail * std::tanh (input / rail);
        voltage += parameters.coefficient * (target - voltage);
        return voltage;
    }
    DiodeCurrent current (double receivingVoltage, double networkResistance) const noexcept
    {
        const auto resistance = parameters.resistance + networkResistance;
        const auto t = std::tanh ((voltage - receivingVoltage) / (resistance * parameters.currentLimit));
        // derivative is the positive conductance with respect to the voltage drop.
        return { parameters.currentLimit * t, (1.0 - t * t) / resistance };
    }
    double getVoltage() const noexcept { return voltage; }
    DriverParameters getParameters() const noexcept { return parameters; }
private:
    DriverParameters parameters;
    double rate = 192000, voltage = 0;
};

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
        if (std::abs (f.value) < 1.0e-12) return voltage;
        if (f.value > 0) upper = voltage; else lower = voltage;
        const auto next = voltage - f.value / f.derivative;
        voltage = next > lower && next < upper ? next : (lower + upper) * 0.5;
    }
    // Keep the equation's cached physical node values at the returned root,
    // even when the final iteration took a step without converging.
    (void) equation (voltage);
    return voltage;
}

class DiodeShuntClipper
{
public:
    static constexpr double sourceResistance = 4700, loadResistance = 100000;
    static constexpr double capacitance = 100.0e-12;
    void prepare (double rate) noexcept { conductance = 2.0 * rate * capacitance; driver.prepare (rate); reset(); }
    void setKnee (double knee) noexcept { driver.setKnee (knee); }
    void setDriverParameters (DriverParameters p) noexcept { driver.setParameters (p); }
    void reset() noexcept { outputVoltage = junctionVoltage = capacitorCurrent = residual = 0; driver.reset(); }
    double process (double input) noexcept
    {
        driver.process (input);
        const auto history = -conductance * outputVoltage - capacitorCurrent;
        double solvedNode = 0;
        const auto equation = [&] (double voltage)
        {
            const auto diode = SchottkyPair::current (voltage);
            const auto node = voltage + SchottkyPair::seriesResistance * diode.value;
            const auto slope = 1.0 + SchottkyPair::seriesResistance * diode.derivative;
            const auto source = driver.current (node, sourceResistance);
            const auto value = node / loadResistance + conductance * node + history + diode.value - source.value;
            solvedNode = node; residual = std::abs (value);
            return DiodeCurrent { value,
                                  slope * (1.0 / loadResistance + conductance + source.derivative) + diode.derivative };
        };
        const auto junction = solveDiodeVoltage (junctionVoltage, equation);
        const auto voltage = solvedNode;
        junctionVoltage = junction;
        capacitorCurrent = conductance * (voltage - outputVoltage) - capacitorCurrent;
        outputVoltage = voltage;
        return voltage / 4.0;
    }
    double getResidual() const noexcept { return residual; }
    double getOutputVolts() const noexcept { return outputVoltage; }
private:
    DiodeDriver driver;
    double conductance = 0, outputVoltage = 0, junctionVoltage = 0, capacitorCurrent = 0, residual = 0;
};

class SeriesDiodeCrossover
{
public:
    static constexpr double sourceResistance = 1000, loadResistance = 10000;
    static constexpr double loadCapacitance = 100.0e-12, junctionCapacitance = 4.0e-12;
    void prepare (double rate) noexcept
    {
        driver.prepare (rate);
        loadConductance = 2.0 * rate * loadCapacitance;
        junctionConductance = 2.0 * rate * junctionCapacitance;
        reset();
    }
    void setKnee (double knee) noexcept { driver.setKnee (knee); }
    void setDriverParameters (DriverParameters p) noexcept { driver.setParameters (p); }
    void reset() noexcept { outputVoltage = diodeVoltage = junctionVoltage = loadCurrent = junctionCurrent = residual = 0; driver.reset(); }
    double process (double input) noexcept
    {
        driver.process (input);
        const auto loadHistory = -loadConductance * outputVoltage - loadCurrent;
        const auto diodeHistory = -junctionConductance * diodeVoltage - junctionCurrent;
        const auto load = 1.0 / loadResistance + loadConductance;
        double solvedDrop = 0, solvedOutput = 0;
        const auto equation = [&] (double voltage)
        {
            const auto diode = SchottkyPair::current (voltage);
            const auto drop = voltage + SchottkyPair::seriesResistance * diode.value;
            const auto dropSlope = 1.0 + SchottkyPair::seriesResistance * diode.derivative;
            const auto current = diode.value + junctionConductance * drop + diodeHistory;
            const auto slope = diode.derivative + junctionConductance * dropSlope;
            const auto nextOutput = (current - loadHistory) / load;
            const auto source = driver.current (drop + nextOutput, sourceResistance);
            const auto value = current-source.value;
            solvedDrop = drop; solvedOutput = nextOutput; residual = std::abs (value);
            return DiodeCurrent { value,
                                  slope + source.derivative * (dropSlope + slope / load) };
        };
        const auto junction = solveDiodeVoltage (junctionVoltage, equation);
        const auto voltage = solvedDrop;
        const auto nextOutput = solvedOutput;
        junctionVoltage = junction;
        junctionCurrent = junctionConductance * (voltage - diodeVoltage) - junctionCurrent;
        loadCurrent = loadConductance * (nextOutput - outputVoltage) - loadCurrent;
        diodeVoltage = voltage; outputVoltage = nextOutput;
        return nextOutput / 4.0;
    }
    double getResidual() const noexcept { return residual; }
    double getOutputVolts() const noexcept { return outputVoltage; }
    double getDiodeVoltage() const noexcept { return diodeVoltage; }
private:
    DiodeDriver driver;
    double loadConductance = 0, junctionConductance = 0;
    double outputVoltage = 0, diodeVoltage = 0, junctionVoltage = 0, loadCurrent = 0, junctionCurrent = 0, residual = 0;
};
}
