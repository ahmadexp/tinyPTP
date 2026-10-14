# Standalone grandmaster and follower firmware

The two programmed UNOs start their assigned roles automatically after reset
or power-up. They do not need the Python pair launcher, serial commands, or a
Linux PTP process. Keep each shield/TimeStick powered and connect their Ethernet
ports directly. The driver negotiates 1 Gb/s full duplex.

| Board on PTPBox | Role | Attached TimeStick |
| --- | --- | --- |
| UNO USB serial `1453230343235190E071`, currently `/dev/ttyACM0` | Grandmaster | `9C:69:D3:2B:A1:8D` |
| UNO USB serial `14533303731351B0C090`, currently `/dev/ttyACM1` | Follower | `9C:69:D3:2B:A1:2F` |

Both use domain 42 and hardware Ethernet timestamps. The grandmaster sends
Sync/Follow_Up at 1 Hz, responds to Delay_Req with Delay_Resp, and sends Announce
at 0.5 Hz. Its Announce identifies its own clock as grandmaster, with priority
128, clock class 248, unknown accuracy/variance, zero steps removed, and an
internal oscillator time source. UTC-valid, traceability, and PTP-timescale
flags are clear. This is a free-running arbitrary timescale, not a UTC source.
There is no BMCA election or automatic selection among multiple grandmasters;
the follower binds to the first valid two-step Sync source on this direct link.

## Grandmaster serial monitor

Open the grandmaster UNO, USB serial ending `E071`, at **115200 baud, 8N1**.
It reports its clock identity, the transition to MASTER, and `assuming the
grandmaster role`. When it receives the first valid Delay_Req it reports the
follower's clock identity. Every second it prints a status line such as:

```text
tinyptp[21.813]: port 1: MASTER sync 10 delay_resp 10 freq +0 timeouts 0 missing_rx 0 discarded 0
```

- `sync`: cumulative completed Sync/Follow_Up pairs, with a matching hardware
  TX timestamp. A send failure or missing TX timestamp does not increment it.
- `delay_resp`: cumulative Delay_Resp packets sent for the bound follower.
  An increasing count shows requests are arriving; it does not prove follower lock.
- `freq`: applied hardware-clock frequency correction in ppb. It is `+0` for
  this free-running grandmaster, which has no external reference.
- `timeouts`, `missing_rx`, `discarded`: cumulative error counters since the
  role started. A Sync TX timestamp timeout also produces a timestamped event.

This uses the same `tinyptp[uptime]:` prefix as the follower. Offset and path
estimates are computed and displayed by the follower; the grandmaster has no
upstream master to measure them against. These are ptp4l-style messages from
this project's firmware, not LinuxPTP running on AVR.

## Follower serial monitor

Open the follower port at **115200 baud, 8 data bits, no parity, 1 stop bit**,
with no flow control. Arduino IDE Serial Monitor or another serial terminal
works. Opening the UNO serial port may reset it; the follower initializes and
rejoins automatically. Only one program should own the port at a time.

The output follows this format:

```text
tinyptp[seconds.milliseconds]: master offset OFFSET sSTATE freq FREQUENCY path delay DELAY sample NUMBER seq SEQUENCE
```

- `master offset`: follower minus grandmaster, in nanoseconds.
- `freq`: the newly applied TimeStick frequency correction, in parts per billion.
  Positive speeds up the follower clock; negative slows it down. This differs
  from the older diagnostic format, which printed the previous correction.
- `path delay`: estimated mean one-way delay in nanoseconds.
- The bracketed time is Arduino uptime, not UTC or the hardware PTP clock.
- `s0`: observation without clock discipline.
- `s1`: a coarse clock step was requested for the sample.
- `s2`: the frequency servo is tracking, including initial acquisition.
- `s3`: the last eight accepted offsets were each within +/-1000 ns. This
  indicator clears on an out-of-range sample, exchange timeout, rejected path,
  link loss, or restart. It is not an independent accuracy measurement.

The state numbering is inspired by [LinuxPTP's servo states](https://github.com/richardcochran/linuxptp/blob/master/servo.h).
The default servo is this project's integer PI implementation. An experimental
integer linear-regression option and adaptive PI are also available; see the
[servo guide](servos.md).
These controllers are not LinuxPTP servos ported to AVR. The full protocol/profile
and independent PPS measurement limitations in the earlier reports still apply.

Status lines show boot role, selected clock identity, port transitions, and
periodic error counters. Register dumps and per-update PBus writes are quiet
by default in the standalone builds. USB/PHY/clock startup failures retry
after five seconds. A detected Ethernet link-down event also triggers startup
retry. USB disconnects wait for enumeration again. One follower-only reset
reacquired successfully, but staggered uploads also produced repeated PHY
initialization and link interruptions. Resetting both boards together recovered
the pair. Recovery across arbitrary startup timing is not yet reliable.

Commands remain available:

| Command | Effect |
| --- | --- |
| `x` | Stop PTP, restore nominal frequency, and suspend automatic startup until reset or `a` |
| `a` | Resume the compiled automatic role |
| `d` | Toggle detailed diagnostic output |
| `q` | Read PPS output gate, clock control, and active pulse width |
| `r`, `v`, `l` | Descriptor, vendor, and PHY diagnostics |

The previous `e`, `m`, `f`, `p`, `s`, `i`, and `t` diagnostic commands also remain.
Normal operation needs none of them.

## Build and upload

`examples/TimeStickProbe/BuildConfig.h` defines `TINYPTP_BOOT_ROLE`:

- `0`: original interactive diagnostic, the source default.
- `1`: automatic grandmaster with ptp4l-style logs.
- `2`: automatic follower with ptp4l-style logs.

For Arduino IDE, change that value before compiling/uploading each board.
For Arduino CLI, `tools/build_roles.sh` builds both images with separate compiler
defines and output directories, without editing source between builds:

```sh
ARDUINO_CLI=/home/user/.local/share/tinyptp/bin/arduino-cli \
ARDUINO_CONFIG=/home/user/.local/share/tinyptp/arduino-cli.yaml \
  sh /home/user/tinyPTP/tools/build_roles.sh
```

The build outputs on PTPBox are `/home/user/tinyPTP/build/roles/grandmaster/`
and `/home/user/tinyPTP/build/roles/follower/`. Upload each directory to the
correct board using `arduino-cli upload --input-dir ...`, as in the existing
[setup instructions](setup-and-design.md#toolchain-and-firmware). Stable USB
paths are listed in the [two-UNO guide](two-uno-setup.md).

With AVR core 1.8.8 and USB Host Shield Library 2.0 version 1.7.0, the installed
grandmaster image uses **28528 bytes flash**. The original PPS-fix follower
used **28008 bytes**; the selectable-servo PI follower uses **28556 bytes**.
These images use **995 bytes static SRAM**, leaving **1053 bytes** beyond static
allocation. The experimental linreg follower uses **31174 bytes flash** and
**1105 bytes static SRAM**. Maximum runtime stack usage is unmeasured. See the
[servo comparison](results/2026-10-13/servo-comparison/README.md) for tested
firmware hashes and captured data.

The two standalone images were uploaded on 2026-10-13. The earlier Python
pair process was stopped. Validation captures send no startup commands and
no `x` at the end, so closing a logger leaves the firmware running. A capture
or monitor is only a reader of serial output, not part of the PTP control loop.

## Hardware validation, 2026-10-13

Both role images started automatically after a simultaneous reset, with no
serial startup commands. A 180-second capture recorded 166 complete follower
exchanges, no exchange timeouts, no rejected paths, and zero missing RX
hardware timestamps or discarded aggregates in the final status lines.
Over the final 60 samples, internal offset RMS was 53.50 ns, mean -27.25 ns,
range -125 to +45 ns, and mean path delay 733.25 ns. These estimates are not
an independent PPS accuracy measurement.

The initial standalone validation above preceded the PPS output-gate fix.
The PPS-fix firmware used for that test had these HEX hashes. The grandmaster
was subsequently updated for one-second status output, as recorded below:

```text
grandmaster 3d6215e635ad3feb88a3dbc518ebfee3bbf03103a61b335857ae6099ada31297
follower    5262123d14c46b1bb685c5344cc199401b9d0a8bfc817a6a09cb4d68cd29afff
```

The source tests cover PPS gate bit preservation, read/write failures,
readback mismatch, Announce field encoding and clock-quality flags,
hardware timestamp correlation, sequence numbers, and serial formatting,
including signed 64-bit values and uptime formatting. Both diagnostic and
ptp4l-style protocol test builds pass ASan/UBSan. The summary tool accepts
both log formats and reproduces the earlier archived results unchanged.

[Raw logs and summary](results/2026-10-13/standalone/README.md) are preserved
separately from the earlier interactive-firmware dataset.

## PPS output

The firmware enables the AX88279 PPS output gate at PBus `0x000c:0xf8c8`,
bit `0x4000`, using a read-modify-write to preserve other bits. This is separate
from the clock-side PPS enable at `0x0012:0x1000`, bit `0x10`. The active pulse
width at `0x0012:0x1040` is 500,000,000 ns, matching the vendor default.
The intended output is one pulse per hardware-clock second, active for 500 ms.

Earlier firmware enabled the clock-side bit but omitted the output gate.
The gate operation follows `ax88279_ptp_pps_ctrl` in the
[pinned TimeStick driver](https://github.com/Time-Appliances-Project/TimeStick/blob/4c2935a1f723f8ec66fc3a3b4fb6a246b9de7c6e/TimeStick2/DRV/ax_ptp.c#L326-L342).
Startup now reads the original gate, enables it, and checks the gate, clock
control, and pulse width. Failed reads, failed writes, or unexpected readback
prevent PTP startup and trigger the usual retry. These changes are volatile;
they do not program EEPROM, flash, or eFuse on the TimeStick.

The `q` serial command repeats those readbacks without changing registers.
The post-fix 180-second test completed 166 exchanges with no timeouts; the
last 60 internal offsets had RMS 25.76 ns and range -20 to +45 ns.
An enabled readback confirms configuration, not the waveform at a connector.
The user subsequently confirmed that PPS works on both TimeSticks. A
quantitative oscilloscope or time-interval-counter comparison of the two PPS
edges is still pending. The follower PPS is not suppressed
while acquiring or after PTP loss; use serial state and error counters to
determine whether its hardware clock is currently being disciplined.

## Host-side checks

Run from the repository root on a host with a C++11 compiler and Python 3:

```sh
for test in ptp_math ptp_protocol ptp_log pps_output ptp_servo ptp_auto_pi; do
  c++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    tests/$test.cpp -o /tmp/tinyptp-$test
  /tmp/tinyptp-$test
done
c++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -DTINYPTP_BOOT_ROLE=2 tests/ptp_protocol.cpp -o /tmp/tinyptp-protocol-modern
/tmp/tinyptp-protocol-modern
python3 tests/test_summarize_ptp.py
```

## Grandmaster logging update, 2026-10-13

The grandmaster was updated to the one-second status format documented above.
Its installed HEX SHA-256 is
`93d38313c683fd9fae9aefb4f0e1c2043097f5706cb7f92a928fa5617a2c12b0`.
The follower retains the PPS-fix image listed earlier. A concurrent 100-second
capture checks master reporting cadence and continuing follower exchanges;
[raw logs and validation](results/2026-10-13/master-serial/README.md) are archived.
