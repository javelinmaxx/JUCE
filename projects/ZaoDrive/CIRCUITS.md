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

BJT Bias spans −100% to +100% and shifts the divider's open-circuit voltage by
−0.8 to +0.8 V around its nominal 1.62295 V. The divider resistance remains
`Rth = 100k || 22k`. The base KCL receives the term `−biasOffset/Rth`, so
changing Bias changes real base current, collector voltage, and asymmetric
headroom. Initialization solves that operating point before initializing the
coupling and junction-charge states. Bias automation is smoothed over 20 ms;
the output coupling filter rejects the resulting DC change.

For each junction, stored charge is depletion charge plus transit time times
the junction's exponential current. Depletion capacitance below 0.375 V is
`C0 / (1 - V / 0.75)^0.33`; a continuous charge/capacitance continuation handles
forward bias. Trapezoidal capacitor companions stamp their current and analytic
derivatives into the four-node Jacobian. Input/source series RC loading is
reduced to its exact trapezoidal companion conductance rather than treated as
an unloaded high-pass filter. The emitter bypass capacitor uses the same method.
An output coupling high-pass at 1.6 Hz removes the collector's bias voltage.

The solver uses partial-pivoted elimination, warm starts, and forward-junction
voltage limiting. Its passive bypass node is eliminated first, leaving a
three-node system with partial pivoting; the full four-node solver remains a
fallback. Junction exponentials are shared with their charge stamps, and the
accepted charges are reused when advancing capacitor history. Reverse BC
voltage can recover freely from saturation. DC
initialization allows 80 iterations; audio steps allow 24 and terminate once
all KCL residuals are below 1e-10 A. The bounded solve avoids unbounded work on
the audio thread. The report tests its residual across several rates and input
voltages, including clipping and overload.

## Schottky diode pair

The feedback, shunt and crossover circuits use nominal Schottky junctions with
`Is = 200 nA` and `nVt = 31.02 mV` (n = 1.2 at Vt = 25.85 mV). A 220 Ω
ballast resistor is in series with each antiparallel pair. Define internal
junction voltage J, pair current Ij and external terminal voltage D:

```
Ij(J) = Is * (exp(J/nVt) - exp(-J/nVt))
D(J) = J + 220 * Ij(J)
dD/dJ = 1 + 220 * dIj/dJ
```

Solving J directly avoids an extra nested diode solve. The ballast derivative
is included in every Jacobian. At 1 mA the pair's terminal voltage is about
0.484 V; its incremental resistance is about 251 Ω rather than the former
unballasted silicon pair's 49 Ω. Schottky junctions alone do not guarantee a
softer knee; the ballast and loaded amplifier provide the intended rounding.
These parameters specify a nominal model, not a measured part number.

## Op-amp feedback clipper

The non-inverting input receives the driven signal. The inverting input has a
4.7 kΩ ground leg. A 47 kΩ resistor, 51 pF capacitor and ballasted Schottky pair
connect output to the inverting input. The op-amp has 100 Ω output resistance
and supplies a 100 kΩ receiver as well as the feedback branch. For D = Vout − Vminus:

```
Ifb = D/47000 + Ij(J) + Icapacitor(D)
Vminus = 4700 * Ifb
Vout = D + Vminus
Vinternal = Vout + 100 * (Ifb + Vout/100000)
Vinternal + loopGain * Vminus = memory * previousVinternal + loopGain * Vin
```

The dominant-pole integration uses open-loop gain 100,000, unity-gain bandwidth
3 MHz and backward Euler. Newton solves in J with the complete chain-rule
Jacobian, including finite output impedance and the capacitor's trapezoidal
history. If the internal amplifier exceeds ±4 V or a 1 V/µs slew limit, the
active internal-voltage constraint is solved again with feedback/loading KCL
intact. Each scalar solve allows 24 iterations.

## Driver for the two external diode modes

The fixed ×4 conditioner now commands a finite amplifier. Knee k in [0,1]
controls its output impedance, current delivery and unloaded pole:

```
Rdriver = 220 * 100^k ohms
Ilimit = 0.01 * 0.1^k amps
fdriver = 45000 * (1/9)^k Hz
Vtarget = 9 * tanh(Vcommand/9)
Vdriver[n] = Vdriver[n-1] + (1 - exp(-2*pi*fdriver/fs)) * (Vtarget - Vdriver[n-1])
Idelivered(Vreceiver) = Ilimit * tanh((Vdriver - Vreceiver) / ((Rdriver + Rsource)*Ilimit))
```

Knee 50% is 2.2 kΩ, 3.162 mA and 15 kHz; the endpoints are 220 Ω/10 mA/45 kHz
and 22 kΩ/1 mA/5 kHz. This is a behavioral finite-current amplifier with a
physical Thevenin resistance and smooth overload, rather than a transistor-level
op-amp implementation. The delivered current and its analytic conductance enter
the receiving circuit's KCL. Increasing Knee changes both shape and signal level.
Driver parameters are computed once per host block and smoothed over 20 ms at
the oversampled rate, avoiding per-sample exponent/power work for the controls.

## Diode shunt clipper

The driver feeds a 4.7 kΩ source resistor into a 100 kΩ output load, a 100 pF
shunt capacitor, and the ballasted Schottky pair to ground. In J:

```
Vout = D(J)
Vout/100000 + Icapacitor(Vout) + Ij(J) - Idelivered(Vout) = 0
```

The pair's finite conducting resistance, driver bandwidth and limited source
current give rounded peaks at the receiver. The model does not impose a hard
diodeless clipping threshold on the output.

## Series-diode crossover distortion

The driver feeds a 1 kΩ source resistor and the ballasted pair in series with
a 10 kΩ load. A 100 pF capacitor loads the receiver; 4 pF is across the complete
pair. Let load/current companion histories be Hl and Hj:

```
I = Ij(J) + Gj*D(J) + Hj
Vout = (I - Hl) / (1/10000 + Gl)
I - Idelivered(D(J) + Vout) = 0
```

The exponential pair creates a softer central conduction region, with leakage
and capacitive feedthrough retained. Both external diode networks use a
bracketed Newton solve in J, capped at 40 iterations, targeting a 1e-12 A KCL
residual. Stiff capacitor transitions may exhaust that budget; tests verify a
1e-9 A bound across Knee settings, noise, overload and sample rates.

## Signal scaling and limitations

One full-scale digital input corresponds to 0.35 V at the circuit input before
Input and Drive gain. Four circuit-output volts correspond to one full-scale
digital output. BJT polarity is restored after solving the physical CE circuit.
The external diode modes retain ×4 input conditioning before the finite driver;
DC plots label the amplifier command before its voltage/current limits. Tone is
a one-pole low-pass following circuit downsampling.

The visible first knob uses independent BJT Bias and diode Knee parameters,
while Op-Amp mode retains Input. The legacy Input parameter remains available
to automation and stored presets and retains its gain behavior in every mode.
Missing Bias/Knee fields in old states default to 0%/50%, respectively.

The circuit models omit semiconductor temperature drift, transistor avalanche,
high-current beta rolloff, noise, component tolerances, and a particular op-amp's
internal transistor network. Diode capacitances are fixed; voltage-dependent
junction charge and reverse recovery are omitted. The BJT and feedback input
voltages are bounded to ±12 V; the external diode driver instead uses a smooth
±9 V swing. Supply voltages and device parameters other than Bias/Knee are fixed.
DC reference checks and residual tests establish consistency with these equations,
not bench calibration or full SPICE transient certification.

The output transformer is Yinyang's existing behavioral magnetic model, shared
by direct source inclusion; its documented limits still apply.
