# Zao Drive circuit specification

## BJT common emitter

The supply is 9 V. A 100 kΩ/22 kΩ divider biases the base; the source sees a
100 nF coupling capacitor and 10 kΩ series resistance. The collector connects
to supply through 4.7 kΩ. The emitter connects to ground through 220 Ω + 820 Ω,
with 4.7 µF bypassing the 820 Ω resistor. The four solved nodes are base B,
emitter E, collector C, and emitter-bypass node X.

| Device parameter | Value |
| --- | --- |
| Saturation current Is | 6.734 fA |
| Forward / reverse beta | 200 / 4 |
| Thermal voltage | 25.85 mV |
| Forward Early voltage | 100 V |
| BE / BC zero-bias junction capacitance | 25 pF / 4 pF |
| Junction potential / grading coefficient | 0.75 V / 0.33 |
| Forward depletion continuation | 0.5 × junction potential |
| Forward / reverse transit time | 0.4 ns / 25 ns |

Define F = Is(exp(Vbe/Vt) − 1), R = Is(exp(Vbc/Vt) − 1), and
A = 1 + Vce/VAF. Terminal currents leaving the base/collector/emitter are:

```
Ib = F / betaF + R / betaR
Ic = A * F - (1 + 1 / betaR) * R
Ie = -Ib - Ic
```

Thus the transistor conserves current, loses forward-active gain when the BC
junction conducts, and cuts off as the BE junction turns off. Its bias point is
solved with capacitors open, not assumed to be a fixed 0.7 V base-emitter drop.

For each junction, stored charge is depletion charge plus transit time times
the junction's exponential current. Depletion capacitance below 0.375 V is
`C0 / (1 - V / 0.75)^0.33`; a continuous charge/capacitance continuation handles
forward bias. Trapezoidal capacitor companions stamp their current and analytic
derivatives into the four-node Jacobian. Input/source series RC loading is
reduced to its exact trapezoidal companion conductance rather than treated as
an unloaded high-pass filter. The emitter bypass capacitor uses the same method.
An output coupling high-pass at 1.6 Hz removes the collector's bias voltage.

The solver uses partial-pivoted elimination, warm starts, and forward-junction
voltage limiting. Reverse BC voltage can recover freely from saturation. DC
initialization allows 80 iterations; audio steps allow 24 and terminate once
all KCL residuals are below 1e-10 A. The bounded solve avoids unbounded work on
the audio thread. The report tests its residual across several rates and input
voltages, including clipping and overload.

## Op-amp feedback clipper

The non-inverting input receives the driven signal. The inverting input has a
4.7 kΩ ground leg. A 47 kΩ resistor, 51 pF capacitor, and antiparallel silicon
diodes connect output to the inverting input. Each diode uses Is = 2 nA and
nVt = 49.115 mV. For feedback voltage D = Vout − Vminus:

```
Ifeedback(D) = D / 47000 + Is * (exp(D / nVt) - exp(-D / nVt)) + Icapacitor(D)
Vminus = 4700 * Ifeedback(D)
Vout = D + Vminus
```

An implicit dominant-pole equation supplies the remaining constraint with
open-loop gain 100,000 and unity-gain bandwidth 3 MHz. Newton solves this scalar,
monotonic feedback equation, including the capacitor's trapezoidal history.
When the solution exceeds ±4 V or 1 V/µs, an active output constraint is solved
again with the feedback KCL intact. Rail/slew limits do not replace the diode
feedback equation with a post-amplifier clip operation. The dominant pole uses
backward Euler integration; each scalar solve allows 18 iterations.

## Signal scaling and limitations

One full-scale digital input corresponds to 0.35 V at the circuit input before
Input and Drive gain. Four circuit-output volts correspond to one full-scale
digital output. BJT polarity is restored after solving the physical CE circuit.
The digital Tone control is a one-pole low-pass following circuit downsampling.

The circuit models omit semiconductor temperature drift, transistor avalanche,
high-current beta rolloff, noise, component tolerances, and a particular op-amp's
internal transistor network. The nominal op-amp's output impedance is zero
within its rail/slew constraints. Input voltage is bounded to ±12 V for overload
stability. Supply voltage and device parameters are fixed. The DC reference check
and residual tests establish consistency of the specified numerical models;
they do not constitute a bench measurement or full SPICE transient certification.

The output transformer is YinYang's existing behavioral magnetic model and is
shared by direct source inclusion. Its own documented limits still apply.
