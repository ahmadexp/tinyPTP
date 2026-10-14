# Kalman experiment status

The two-state Kalman servo is implemented, passes host checks, and compiles
for the UNO. **It has not been uploaded or measured on this hardware.**
The fixed-PI baseline failed before the comparison could begin.
The source default and installed follower image remain fixed PI.

The baseline attempt began on **2026-10-13 at 21:15:08 Pacific**
(October 14 04:15:08 UTC). Both serial ports were opened concurrently with
`--seconds 300 --reset` at 115200 baud. The grandmaster repeatedly failed
initialization. The follower briefly reported PPS enabled and entered
LISTENING, then reported link loss and initialization failures. No accepted
PTP exchange was recorded. The readers were stopped after roughly 200 seconds
using SIGTERM rather than treating this as a valid five-minute trial.

This resembles the unresolved startup failure recorded earlier in the project;
the precise hardware/driver cause remains undiagnosed. It occurred with the
existing firmware, before any Kalman upload. A full power cycle of both
UNO/shield/TimeStick assemblies was requested before retrying. No numerical
hardware comparison or Kalman PPS accuracy claim can be made from this attempt.

- [Grandmaster failed-baseline log](failed-baseline/kalman-baseline-20261013-01-master.log)
- [Follower failed-baseline log](failed-baseline/kalman-baseline-20261013-01-follower.log)
- [Failure counts and capture metadata](validation.json)
- [Build sizes and tested source/firmware hashes](firmware.json)
- [Filter model, provisional noise parameters, simulations, and build selection](../../../kalman-servo.md)

## Checks completed

Host checks passed with address, undefined-behavior, and float-cast-overflow
sanitizers: double-precision matrix-reference agreement, covariance positivity,
known correction input, nonuniform intervals, timestamp wrap, gaps, both signs
of oscillator error, phase-step recovery, and bounded commands. Existing math,
PI/linreg/adaptive-PI, PPS, serial-format, and log-summary tests also pass.
Protocol tests pass for all four servo selections.

Kalman follower and grandmaster-role builds use 31,426 and 31,428 flash bytes,
respectively, and 1,017 static SRAM bytes. Rebuilding fixed PI from the new
source produced exactly the installed PI HEX hash. No role was reflashed.

## Planned hardware comparison after recovery

Use new filenames to preserve this failed attempt. Capture fixed PI, Kalman,
and fixed PI again for five minutes each, with the same two-minute cutoff
and all accepted samples retained. The plotting tool accepts
`--candidate-label KALMAN` and `--reference-after PATH` for the repeated PI
reference. The `master offset` samples will remain raw measurements rather
than filtered phase estimates. Hardware results are deliberately absent here
until those trials actually complete.

Verify the archived evidence from this directory with
`sha256sum -c SHA256SUMS`.
