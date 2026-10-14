# Fixed-point PLL/FLL experiment

`SERVO=pllfll` (or `TINYPTP_SERVO=4`) selects a hybrid phase/frequency controller
for the follower. Fixed PI remains the default. Only the follower needs this
image; the grandmaster has no upstream reference to discipline it.

## Controller

The phase loop uses the newest raw PTP offset `e`, in nanoseconds. The
frequency loop estimates the intrinsic oscillator error `f`, in ppb, once
every eight accepted intervals. It subtracts the known effect of the actual
commands applied during that window:

```text
measured_frequency = (e_now - e_anchor - sum(previous_command * dt)) / sum(dt)
first_window: f = measured_frequency
later_windows: f = f + (measured_frequency - f) / 2
command = clamp(-f - e_now / 4 seconds, -500000, +500000) ppb
```

Here `dt` is in seconds and `ppb` is numerically equivalent to ns/s. The source
uses master timestamp intervals in milliseconds, a milli-ppb frequency state,
and a 64-bit accumulated correction in ns * 1000. It never converts an
absolute clock epoch to floating point. There is no heap, sample ring buffer,
or floating-point arithmetic in this servo.

The eight intervals are non-overlapping and normally span eight seconds.
Only the two endpoint offsets enter each frequency estimate; every intervening
applied frequency command contributes to the correction integral. The first
window initializes frequency directly. Subsequent windows use a fixed gain of
1/2, reducing noise at the cost of tracking lag. The phase loop runs on every
accepted sample. These parameters are fixed before the hardware experiment;
there is no optimization against its measurement window.

This follows the general idea of combining phase and frequency loops,
described in the [NTP clock-discipline documentation](https://www.ntp.org/documentation/4.2.8-series/discipline/),
but is an independent, much smaller design for 1 Hz PTP. It does not implement
NTP's polling rules, mode selection, startup state machine, or gain adaptation.
It is also distinct from the existing eight-point ordinary-least-squares fit.

## Gaps and limits

The first sample holds the current bounded hardware command and anchors a new
window, initializing the frequency estimate to the negative of that command.
A duplicate timestamp, interval outside 0.5 to 1.5 seconds, explicit history
invalidation, rejected path, or lost exchange discards the window. The next
measurement holds the command and starts a new window. Timestamp subtraction
handles unsigned 32-bit millisecond wrap.

Both the frequency estimate and output are limited to +/-500,000 ppb. There
is no phase integrator to wind up. Offsets outside +/-500 ms clear history;
the caller retains its existing coarse-step behavior. A smaller unannounced
phase discontinuity can contaminate an endpoint frequency estimate until
subsequent windows replace it. The host tests include recovery from such a step.
There is no additional outlier rejection or compensation for USB actuation
latency, cable asymmetry, or temperature.

## Resource use and checks

The follower compiles to **29,674 flash bytes** and **1,016 static SRAM bytes**
on the UNO, leaving 2,582 flash bytes and 1,032 bytes beyond static allocation.
Maximum runtime stack and worst-case execution time remain unmeasured. The
fixed-PI rebuild is byte-identical to the previously installed PI image.

`tests/ptp_pllfll.cpp` checks known intrinsic frequency under independently
changing applied commands, both drift signs, nonuniform intervals, wrap,
acquisition, phase-step recovery, gaps, integer bounds, and saturation. Host
address/undefined-behavior sanitizer checks pass, as do the existing tests and
protocol tests compiled for all five servo selections.

Three deterministic 1,800-sample models give the following **simulated
true-phase RMS** after excluding the initial 201 samples:

| Model | Fixed PI | PLL/FLL |
| --- | ---: | ---: |
| Constant frequency, small timestamp noise | 5.10 ns | 3.25 ns |
| Frequency ramp, small timestamp noise | 19.42 ns | 33.77 ns |
| Constant frequency, larger timestamp noise | 37.24 ns | 27.80 ns |

This exposes the noise/lag tradeoff, not a general performance improvement.
These are synthetic plant results, not measured TimeStick PPS accuracy.
Hardware comparisons use the original raw PTP offset, not a smoothed estimate.

```sh
SERVO=pllfll BUILD_DIR="$PWD/build/pllfll" sh tools/build_roles.sh
```

Upload the follower using the [standalone procedure](standalone-roles.md).
