# UNO R4 Minima port and hardware experiment

Experiment date: October 14, 2026 Pacific (October 15 UTC for serial captures).
Two UNO R4 Minima boards replaced the UNO R3s. Each carries the existing
MAX3421E USB Host Shield and AX88279 TimeStick, with direct Ethernet between
the sticks. The upload/capture host is an Intel Mac (Darwin 25.6.0).

## Board and clock identities

| Role | R4 USB serial | Serial port during capture | TimeStick clock identity |
| --- | --- | --- | --- |
| Grandmaster | 64,096 | 9,080 | 65,016 | 8,848 |
| Follower | `320D072639313139112733354B57336E` | `/dev/cu.usbmodem3201` | `9c69d3.fffe.2ba18d` |

These TimeStick roles are reversed relative to the archived R3 pair. Do not
compare correction signs or identify boards using the old serial suffixes.
USB IDs are Arduino `2341:0069` in application mode and `2341:0369` in DFU mode.
Uploads select the full USB serial, not an ambiguous VID/PID or port number.

## Build and software validation

- Arduino CLI 1.5.1, Arduino UNO R4 Boards (`renesas_uno`) 1.6.0.
- USB Host Shield Library 2.0 1.7.0, pyserial 3.5 for capture.
- **Hardware trial images:** native Arm GNU Toolchain 13.3.Rel1 on Apple silicon,
  overriding only `compiler.path` in the official core configuration.
- **Additional compile validation:** all seven examples also build using the
  official bundled `arm-none-eabi-gcc 7-2017q4` on the Intel capture host. Those
  separate binaries are not the images used for the trial results.
- FPU flags: `-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard`.
  Kalman disassembly includes single-precision floating-point instructions.
- All seven classic UNO examples still compile with AVR core 1.8.8 and the
  native QMK AVR GCC 15.2.0 toolchain. R3 builds retain the 256-byte buffer,
  one-round polling budget, and disabled CPU profiling.
- Eight native C++ suites passed with address/undefined-behavior sanitizers.
  The protocol harness additionally passed with the R4 configuration for each
  of the five servo selections, including 20,000 randomized aggregates per run.
- Thirteen Python analysis tests passed, including nonzero boot timestamps,
  settling criteria, checksum rejection and retention of non-s3 steady samples.

Flash and static RAM are compiler reports, not measured stack high-water marks.
The R4 has 262,144 flash bytes and 32,768 RAM bytes. Heap and runtime stack
usage are not measured.

| Example | GCC 13 flash | GCC 13 static RAM | Bundled GCC 7 flash | Bundled GCC 7 static RAM |
| --- | ---: | ---: | ---: | ---: |
| FollowerAdaptivePI | 64,996 | 9,100 | 66,012 | 8,868 |
| FollowerKalman | 64,612 | 9,100 | 65,548 | 8,868 |
| FollowerLinreg | 64,656 | 9,224 | 65,640 | 9,000 |
| FollowerPI | 64,084 | 9,080 | 65,004 | 8,848 |
| FollowerPllFll | 64,424 | 9,104 | 65,420 | 8,880 |
| Grandmaster | 64,096 | 9,080 | 65,016 | 8,848 |
| TimeStickProbe | 63,860 | 9,076 | 64,820 | 8,848 |

[Full R3/R4 compile matrix](build-matrix.json)
| [Toolchain sizes and image hashes](builds/summary.json)
| [Bundled-toolchain build logs](builds)

## Bring-up and capture method

Initial boots enumerated the sticks and negotiated Ethernet, but the clock
request `0x13` returned USB STALL `0x05`. Automatic retries subsequently
recovered both clocks without an operator power cycle. This confirms recovery
for this attempt, not a general fix for the previously observed restart issue.
PPS gate `0x4000`, clock control `0x5F`, and active interval 500,000,000 ns were
read back on both sticks. No oscilloscope measurement was made in this run.

The dedicated capture sequence reuploads the grandmaster once, then captures
five 300-second follower trials in order: PI, linreg, adaptive PI, Kalman,
PLL/FLL. It ends with a fresh 900-second PI capture. Grandmaster serial is
recorded continuously across the sequence. Each follower capture starts after
its upload, includes its actual boot marker, and uses 115200-baud native USB
CDC. No invented boot markers or interpolated PTP samples are inserted.

The same 120-second cutoff after the first accepted exchange defines each
steady window. All samples after that cutoff remain, including any non-s3
states or excursions. Startup plots include coarse steps and saturation.
Settling requires a final uninterrupted in-band suffix lasting at least 30 s;
the finite record cannot prove indefinite lock.

The Cortex-M4F port uses a 4096-byte aggregate buffer and at most four receive/
TX-timestamp polling rounds per loop. Five servo algorithms and gains remain
the same as the R3 source. R4 first-exchange stepping changes from 500 ms to
1 ms; later exchanges retain the 500 ms threshold. Arduino uploads do not
establish a common initial TimeStick phase, so this avoids minutes of
saturated startup slewing for initial offsets below 500 ms. Tests exercise both signs,
the exact threshold, and the distinct first-exchange/runtime rules on both
architectures. `servo_last_us` measures only filter arithmetic;
`usb_poll_max_us` includes packet handling, callbacks and clock/serial I/O.
The periodic last-update median is sampled telemetry, not a median over every
individual servo execution.

Results will be added after the complete capture sequence is validated.

## Reproduce

See the [R4 guide](../../../uno-r4-minima.md) for compilation, installation and
`tools/run_r4_trials.py` arguments. Analyze the completed capture directory:

```sh
python3 tools/plot_r4_trials.py docs/results/2026-10-14/uno-r4-minima/trials \
  --output-dir docs/results/2026-10-14/uno-r4-minima/plots
python3 tools/plot_steady_state.py docs/results/2026-10-14/uno-r4-minima/trials/pi-steady.log \
  --output-dir docs/results/2026-10-14/uno-r4-minima/steady-state --after 120
```

These are sequential experiments with uncontrolled temperature and oscillator
drift. Comparing a historical R3 run with a current R4 run does not isolate
processor effects. Offset/path values are internal PTP estimates, not calibrated
PPS, UTC or end-to-end accuracy measurements. The grandmaster is free-running.
