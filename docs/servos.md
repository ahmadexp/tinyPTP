# Follower clock servos

The follower offers fixed PI (the default), experimental integer linear
regression, adaptive PI (`auto_pi`), two-state Kalman, and PLL/FLL options. The grandmaster keeps
its free-running clock; it has no upstream source for a servo to track.
All follower options produce
the same 115200-baud ptp4l-style offset, frequency, and path-delay messages.
The startup message identifies the selected controller.

## Build selection

```sh
SERVO=pi BUILD_DIR="$PWD/build/pi" sh tools/build_roles.sh
SERVO=linreg BUILD_DIR="$PWD/build/linreg" sh tools/build_roles.sh
SERVO=auto_pi BUILD_DIR="$PWD/build/auto-pi" sh tools/build_roles.sh
SERVO=kalman BUILD_DIR="$PWD/build/kalman" sh tools/build_roles.sh
SERVO=pllfll BUILD_DIR="$PWD/build/pllfll" sh tools/build_roles.sh
```

Set `ARDUINO_CLI` and `ARDUINO_CONFIG` if the CLI or configuration is outside
the normal search path. Upload the desired `follower` build directory to the
follower UNO using the [standalone instructions](standalone-roles.md).
The grandmaster does not need reflashing to change the follower's servo.
For Arduino IDE, select `TINYPTP_SERVO` in `BuildConfig.h`: `0` for PI, `1`
for linreg, `2` for adaptive PI, `3` for Kalman, or `4` for PLL/FLL, with `TINYPTP_BOOT_ROLE=2` for
the follower. Selection is at compile time, not a serial command.

## Controllers

PI retains the existing one-second update law: proportional gain 1/2,
integral gain 1/20, integral limit +/-200,000 ppb, and conditional integration
to avoid winding up while the output saturates.

Linreg uses ordinary least squares over the latest eight accepted exchanges.
It first subtracts the accumulated effect of previously applied frequency
corrections from measured offset, reconstructing the free-running clock's
phase. Regressing raw controlled offsets alone would underestimate oscillator
drift once the loop has settled. The fitted slope estimates that drift; the
command cancels it and removes fitted current phase over four seconds.
At least four points are required before the first fitted correction.

Times come from the grandmaster's hardware Sync timestamps, in milliseconds,
with unsigned differences handling wrap. Integration retains a fractional
nanosecond remainder. Centered 64-bit integer sums keep the fixed window's
products bounded; there is no floating-point arithmetic, heap allocation,
or general matrix library. The previous correction is approximated as acting
throughout the interval between Sync timestamps. USB actuation latency is not
explicitly modeled.

Both controllers limit frequency commands to +/-500,000 ppb and use the
existing coarse clock step for offsets beyond +/-500 ms. Linreg discards its
history after rejected paths, exchange timeouts, zero or greater-than-2.5-second
sample intervals, or loss of Sync. It holds the current correction while
collecting a new fit. A role restart or coarse clock step also resets it.
The `s3` indication still means eight consecutive accepted offsets within
+/-1000 ns; it is independent of the regression window and is not an accuracy
certification.

[LinuxPTP offers PI and adaptive linear-regression servos](https://www.linuxptp.org/documentation/default/).
This implementation is an independent, fixed-eight-point experiment for AVR,
not a port of LinuxPTP's adaptive `linreg` algorithm. Its gains and window are
currently fixed in `PtpServo.h`; other window sizes have not been validated.

## Adaptive PI

`SERVO=auto_pi` (or `TINYPTP_SERVO=2`) selects an experimental controller
that schedules bounded gains from recent measured offsets. It does not run
an optimization search or inject test disturbances into the clock. Its three
presets target the existing 1 Hz Sync rate:

| Serial mode | Name | Kp (1/s) | Ki (1/s²) |
| --- | --- | ---: | ---: |
| 0 | Low | 0.250 | 0.016 |
| 1 | Normal | 0.500 | 0.050 |
| 2 | Fast | 0.750 | 0.125 |

It starts in Fast. An offset beyond +/-1000 ns also selects Fast and clears
the tuning window. Otherwise it evaluates non-overlapping 16-sample windows:

- Increase one level if the sum of absolute offsets exceeds 160 ns and the
  absolute signed sum exceeds half that total, indicating persistent bias.
- Reduce one level after two consecutive windows that each have either a
  total absolute offset no larger than 160 ns, or at least nine strict sign
  reversals with absolute signed sum below one quarter of the absolute total.
- Hold the current level otherwise. The two-window requirement limits
  repeated switching when the measurements are near a threshold.

The integral retains 0.001 ppb units, avoiding the fixed PI controller's
per-update truncation for offsets below 20 ns. Integration uses the elapsed
master Sync time. A proportional-gain change is compensated in the integral
within its +/-200,000 ppb bound, reducing abrupt changes in the command.
Conditional integration prevents windup at the +/-500,000 ppb output limit.

After history is invalidated, the first sample holds the previous command and
reanchors the integral within its bounds. Intervals outside 0.5 to 1.5 seconds
also invalidate history and hold the command. This option is intended for the
current 1 Hz protocol; it is not tuned for arbitrary Sync rates. A coarse
clock step or role restart clears the integral and selects Fast again.

Serial gain messages use `kp_milli` and `ki_milli`, each 1000 times the
corresponding table value. A message is emitted at startup and whenever the
mode changes. Offset samples keep the existing ptp4l-style format.

This is a heuristic based on offset persistence and sign changes, not an
estimate that can uniquely separate oscillator wander from timestamp noise.
Its gains, thresholds, and window sizes remain experimental. It also changes
integral precision and interval handling, so a comparison against fixed PI
measures the complete adaptive implementation, not gain scheduling alone.
LinuxPTP's documented [automatic PI gains](https://www.linuxptp.org/documentation/ptp4l/)
are computed from the Sync interval; this offset-based schedule is a separate
implementation.

## Resource use and validation

On AVR core 1.8.8 with USB Host Shield Library 2.0 version 1.7.0, the follower
builds use:

| Controller | Flash bytes (limit 32,256) | Static SRAM bytes (limit 2,048) |
| --- | ---: | ---: |
| PI | 28,556 | 995 |
| Linreg | 31,174 | 1,105 |
| Adaptive PI | 30,364 | 1,017 |
| Kalman (hardware performance unvalidated) | 31,426 | 1,017 |

Linreg leaves 1,082 bytes of flash and 943 bytes beyond static SRAM allocation.
Maximum runtime stack use has not been measured. This is a tight flash budget
for additional protocol features.

`tests/ptp_servo.cpp` checks PI saturation/anti-windup, known drift recovery
under changing corrections, irregular intervals, timestamp wrap, gap recovery,
closed-loop acquisition with noise, and arithmetic bounds. Protocol tests also
run with each selection. These host checks complement the hardware trials;
they do not establish physical PPS accuracy.

The [paired five-minute hardware trials](results/2026-10-13/servo-comparison/README.md)
include acquisition and steady-state graphs, complete serial logs, extracted
samples, and firmware hashes. In this trial pair, post-cutoff offset RMS was
47.51 ns with PI and 32.06 ns with linreg. These sequential measurements do
not establish a general ranking of the controllers.

Adaptive PI leaves 1,892 flash bytes and 1,031 bytes beyond static SRAM.
`tests/ptp_auto_pi.cpp` checks both signs of oscillator error, nonuniform
intervals, saturation recovery, phase steps, wrap, gap handling, and gain
hysteresis. In three deterministic closed-loop models it reduces true-phase
RMS versus fixed PI. Those modeled results do not predict hardware accuracy.
See the [hardware adaptive-PI trial](results/2026-10-13/auto-pi/README.md) for
measured results, gain telemetry, and reproduction instructions.

In the hardware sequence, fixed PI before / adaptive PI / fixed PI after
measured 12.12 / 22.20 / 19.85 ns offset RMS in equal fixed-cutoff windows.
Adaptive PI did not improve hardware RMS in that experiment. Fixed PI remains
the default and installed follower image; the adaptive option remains
experimental despite passing the functional and simulated checks.

The [Kalman experiment](kalman-servo.md) estimates phase and intrinsic frequency
with a two-state filter, then steers from those estimates. It uses software
floating point and provisional fixed noise parameters. Numerical checks and
UNO builds pass, but the hardware comparison is pending recovery of an
initialization failure encountered with the existing fixed-PI firmware.

## PLL/FLL

`SERVO=pllfll` selects a fixed-point hybrid with a four-second phase correction
and an eight-interval frequency estimator. The frequency estimate removes the
known effect of prior corrections, then smooths successive window estimates
with gain 1/2. Raw serial offsets remain unchanged. This is an independent
experiment, not a port of NTP's clock discipline or an automatic bandwidth tuner.

[Equations, limitations, tests, and resources](pllfll-servo.md)
