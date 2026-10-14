# Follower clock servos

The follower can be built with PI (the default) or an experimental integer
linear-regression controller. The grandmaster keeps its free-running clock;
it has no upstream source for a servo to track. Both follower options produce
the same 115200-baud ptp4l-style offset, frequency, and path-delay messages.
The startup message identifies the selected controller.

## Build selection

```sh
SERVO=pi BUILD_DIR="$PWD/build/pi" sh tools/build_roles.sh
SERVO=linreg BUILD_DIR="$PWD/build/linreg" sh tools/build_roles.sh
```

Set `ARDUINO_CLI` and `ARDUINO_CONFIG` if the CLI or configuration is outside
the normal search path. Upload the desired `follower` build directory to the
follower UNO using the [standalone instructions](standalone-roles.md).
The grandmaster does not need reflashing to change the follower's servo.
For Arduino IDE, select `TINYPTP_SERVO` in `BuildConfig.h`: `0` for PI or `1`
for linreg, together with `TINYPTP_BOOT_ROLE=2` for the follower. Selection is
at compile time, not a serial command.

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

## Resource use and validation

On AVR core 1.8.8 with USB Host Shield Library 2.0 version 1.7.0, the follower
builds use:

| Controller | Flash bytes (limit 32,256) | Static SRAM bytes (limit 2,048) |
| --- | ---: | ---: |
| PI | 28,556 | 995 |
| Linreg | 31,174 | 1,105 |

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
