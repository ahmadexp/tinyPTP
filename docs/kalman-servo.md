# Two-state Kalman follower experiment

`SERVO=kalman` (or `TINYPTP_SERVO=3`) builds an experimental Kalman-based
follower servo. It fits the classic UNO and passes host numerical and protocol
checks. It has also completed a five-minute hardware run after remote recovery
from the initial link/clock failure. With the provisional settings below, its
measured raw-offset RMS was 94.99 ns, compared with 48.30 ns for the preceding
PI run and 110.60 ns for PI repeated afterward. The Kalman result lies
between the PI references, so this comparison does not establish an advantage.
See the [experiment record](results/2026-10-13/kalman/README.md) for the
repeated PI reference, raw logs, and limitations. Fixed PI remains the default
and has been restored to the follower.

## What the filter estimates

The filter estimates two quantities: current follower-minus-master phase in
nanoseconds, and the intrinsic oscillator frequency error in ppb. A known
control input accounts for the correction already applied to the hardware
clock. Without that term, the filter would misinterpret its own corrections
as oscillator drift.

For an interval `dt` in seconds, the prediction is:

```text
phase_next = phase + (frequency_error + previous_correction) * dt
frequency_error_next = frequency_error
F = [[1, dt], [0, 1]]
H = [1, 0]
```

The observed phase is the existing raw four-timestamp PTP offset. The filter
predicts state and covariance, computes the innovation (observed minus
predicted phase), and updates both state estimates using Kalman gains. A
separate controller then requests:

```text
correction_ppb = clamp(-estimated_frequency - estimated_phase / 4,
                       -500000, +500000)
```

The phase-removal time constant is four seconds. The serial `master offset`
remains the original measured offset, not the filter's smoothed estimate.
Thus the usual comparison plots do not hide residual noise by plotting only
filtered data. The clock still uses the existing coarse step beyond +/-500 ms.

## Explicit noise assumptions

The initial experiment uses fixed, provisional parameters:

| Parameter | Value | Interpretation |
| --- | ---: | --- |
| Measurement variance R | 100 ns² | Assumed 10 ns measurement standard deviation |
| Frequency random-walk density qf | 4 ppb²/s | Unmodeled oscillator frequency changes |
| Independent phase random-walk density qp | 1 ns²/s | Additional phase uncertainty |
| Initial phase variance | 100 ns² | Starts from the first observed offset |
| Initial frequency variance | 1,000,000 ppb² | Provisional frequency uncertainty |

The interval-dependent process covariance is:

```text
Q = [[qf * dt^3 / 3 + qp * dt, qf * dt^2 / 2],
     [qf * dt^2 / 2,          qf * dt]]
```

These values are not fitted to a calibrated noise characterization. Kalman
gains evolve with covariance, but this implementation does not automatically
estimate R or Q. Assuming a Kalman filter is always better would be incorrect:
clock noise can change over time and differ from the assumed model, as
[NIST discusses for clock synchronization](https://www.nist.gov/publications/synchronizing-computer-clocks-use-kalman-filters).
Published PTP work also considers clock models informed by measured Allan
variance: [Giorgi and Narduzzi, 2011](https://www.research.unipd.it/handle/11577/2475880).
This small implementation is independent of those implementations.

## UNO implementation and boundaries

The filter uses five single-precision floats, a timestamp, and an initialized
flag. AVR executes floating-point arithmetic in software. There is no heap
allocation, matrix library, floating-point serial formatting, or conversion
of an absolute clock epoch to floating point. Large acquisition offsets have
coarser float resolution; nanosecond-scale resolution is available when the
residual phase is small. The Joseph-form covariance update reduces numerical
fragility; frequency variance has a 0.001 ppb² roundoff floor.

The intrinsic frequency estimate and the output command are each limited to
+/-500,000 ppb. These bounds make the implemented servo nonlinear during
saturation; covariance is not an accuracy guarantee in that regime.
Covariance bounds catch invalid prediction arithmetic and reinitialize the
filter while holding the current command.

The first sample, a rejected path, a timeout, or a Sync interval outside
0.5 to 1.5 seconds causes reinitialization at the next valid measurement.
Initialization sets phase to the observation and frequency to the negative
of the previous command, then holds that command for this first sample.
There is no extrapolation across a missing interval. This experiment targets
the current 1 Hz Sync rate. Role restart and coarse stepping clear history.
USB actuation delay and path asymmetry are not explicitly modeled.

The follower uses **31,426 flash bytes** (830 below the UNO limit) and
**1,017 static SRAM bytes** (1,031 remaining beyond static allocation).
The grandmaster-role build uses 31,428 flash bytes, but needs no Kalman filter
because it has no upstream reference. Maximum runtime stack and worst-case
AVR execution time have not been measured.

```sh
SERVO=kalman BUILD_DIR="$PWD/build/kalman" sh tools/build_roles.sh
```

Upload the `follower` image using the [standalone guide](standalone-roles.md).
Do not reflash the grandmaster merely to select a follower servo.

## Validation so far

`tests/ptp_kalman.cpp` checks the float implementation against an independent
double-precision 2x2 matrix reference, with tolerances that account for float
resolution at large phase. It checks covariance positivity, changing known
frequency corrections, timestamp wrap, duplicate/missing intervals, both
signs of oscillator error, phase-step recovery, and arithmetic/control limits.
Address, undefined-behavior, and float-cast-overflow sanitizers pass.

Three deterministic closed-loop models produced these **simulated true-phase
RMS values**, excluding the initial 201 samples:

| Model | Fixed PI | Kalman |
| --- | ---: | ---: |
| Constant drift, small timestamp noise | 5.10 ns | 3.80 ns |
| Frequency ramp, small timestamp noise | 19.42 ns | 10.71 ns |
| Constant drift, larger timestamp noise | 37.24 ns | 34.88 ns |

The control input sees noisy measurements; the score uses the simulated true
phase. These are synthetic scenarios with a simple plant and no USB delay,
not measured TimeStick performance or proof of superiority. The hardware
experiment did not establish this simulated advantage: Kalman raw-offset RMS
fell between the preceding and following PI references (48.30, 94.99, and
110.60 ns, in chronological order).
Noise parameters and the controller time constant need evaluation on separate,
repeated captures rather than tuning to a favorable segment of the test data.
