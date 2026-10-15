# UNO R4 Minima

This development port runs the same fixed-role, layer-2 PTP implementation on
an Arduino UNO R4 Minima with a MAX3421E USB Host Shield and AX88279 TimeStick
per node. Connect the two TimeSticks directly by Ethernet. The host computer
uploads sketches and records serial data; each R4 runs its own PTP role and servo.

The R4 Minima has a 48 MHz Cortex-M4F, 256 KB flash and 32 KB SRAM, compared
with the classic UNO's 16 MHz ATmega328P, 32 KB flash and 2 KB SRAM. See
[Arduino's board documentation](https://docs.arduino.cc/hardware/uno-r4-minima).
UNO R4 WiFi is not enabled by this port.

## Using the additional resources

| Setting | UNO R3 | UNO R4 Minima |
| --- | ---: | ---: |
| Receive aggregate buffer | 256 bytes | 4096 bytes |
| Maximum USB polling rounds per main loop | 1 | 4 |
| Kalman single-precision arithmetic | Software | Cortex-M4F FPU |
| CPU timing reports | Disabled | Enabled |

Each polling round services the receive and TX-timestamp endpoints. Polling
stops early when both return no data, with a bounded budget to preserve main
loop progress. Aggregate validation checks every frame before delivering any
of them. The larger buffer allows more queued frames without increasing the
64-byte full-speed USB packet size.

The built Kalman image contains hardware `vmul.f32`, `vadd.f32`, and `vdiv.f32`
instructions. The existing five servo algorithms, their gains, eight-point
regression history, 1 Hz Sync rate, and PTP timestamps are unchanged.
One startup policy differs: on R4, the first accepted exchange steps errors
larger than 1 ms. Later exchanges retain the 500 ms step threshold. An Arduino
upload does not establish a common epoch for both TimeSticks. The old 500 ms
startup threshold can leave a subsecond error slewing at the 500,000 ppb limit
for minutes.
R3 retains its original 500 ms first-exchange threshold.
Extra CPU capacity alone does not establish better synchronization accuracy:
the TimeStick supplies the hardware clock and timestamps, and the MAX3421E
remains a full-speed USB host reached over SPI.

Override `TINYPTP_RX_BUFFER_SIZE`, `TINYPTP_USB_POLL_BUDGET`, or
`TINYPTP_CPU_PROFILE` before including `TinyPTP.h` if an application needs
different resource limits. Defaults are selected by board, not by example.
`TINYPTP_FIRST_STEP_THRESHOLD_NS` can select a first-exchange threshold from
1,000,000 to 500,000,000 ns; use 500,000,000 to reproduce the original policy.
This step is based on a complete, path-validated hardware timestamp exchange.
It is not applied to every small runtime excursion.

The serial report adds:

```text
CPU servo_last_us N servo_max_us N usb_poll_max_us N
```

`servo_last_us` times only the last servo update. The maxima cover the period
since the previous status report, one second on the master and ten seconds on
the follower. `usb_poll_max_us` includes packet parsing, callbacks, clock
updates, and any serial output inside that polling section. It is not pure
USB transfer time or total CPU utilization. Microsecond timer quantization
limits very short measurements.

## Install and upload

1. Install **Arduino UNO R4 Boards 1.6.0** in Boards Manager.
2. Install **USB Host Shield Library 2.0, version 1.7.0** in Library Manager.
3. Install the development library from this branch using **Sketch > Include
   Library > Add .ZIP Library**. From a checkout, `python3 tools/package_library.py`
   creates `build/tinyPTP-0.2.0-dev.zip`. Replace any older installed tinyPTP copy.
4. Select **Arduino UNO R4 Minima** and the intended board's port.
5. Upload `Grandmaster` to one board and `FollowerPI` to the other. The other
   follower examples select linreg, adaptive PI, Kalman, and PLL/FLL.
6. Open Serial Monitor at **115200 baud**. Confirm both roles, advancing
   exchange counters, and follower offsets before interpreting the PPS outputs.

The **v0.1.0 release ZIP and existing tinyPTP Boards Manager JSON are R3-only**.
Use the official R4 core plus the development library for this experiment;
do not select the custom `tinyPTP UNO (ATmega328P)` board profile.

Command-line builds use:

```sh
arduino-cli core install arduino:renesas_uno@1.6.0
arduino-cli lib install "USB Host Shield Library 2.0@1.7.0"
arduino-cli compile --fqbn arduino:renesas_uno:minima --library "$PWD" examples/Grandmaster
arduino-cli compile --fqbn arduino:renesas_uno:minima --library "$PWD" examples/FollowerKalman
FQBN=arduino:renesas_uno:minima SERVO=kalman tools/build_roles.sh
```

## Native USB and two-board uploads

R4 serial is native USB CDC. Opening a monitor or pulsing DTR is not equivalent
to the R3's USB-to-serial reset circuit. The sketch waits up to five seconds
for a monitor at boot and then starts unattended. Open promptly after upload
to record the genuine boot marker. Its timestamp can be nonzero.

The Minima uses DFU for upload. With two identical boards attached, select by
the actual USB serial identity. `tools/run_r4_trials.py` uses dfu-util's
`--serial` selector and waits for that same identity to return as a serial port.
Do not assume `/dev/cu.usbmodem*` or `/dev/ttyACM*` numbering remains fixed.
The script requires pyserial, Arduino's dfu-util, and prebuilt `.ino.bin` files
for Grandmaster and all five follower examples. It flashes the master once,
captures all follower starts, and leaves fixed PI running after the long run.
It gives the grandmaster a 30-second lead before the first follower upload
for startup and link negotiation. This does not reset the TimeStick counters
or impose identical initial phase offsets.
On macOS it holds a temporary keep-awake assertion while the capture runs;
it does not change persistent sleep settings.

```sh
python3 tools/run_r4_trials.py \
  --master-serial MASTER_USB_SERIAL --follower-serial FOLLOWER_USB_SERIAL \
  --dfu-util /path/to/arduino/dfu-util --firmware-dir /path/to/images \
  --output-dir /path/to/new-results --seconds 300 --steady-seconds 900
```

Use a new output directory each time. The manifest records firmware and log
SHA-256 hashes, UTC capture times, identities and sample counts. The ordinary
R3 capture script's `--reset` option is not an R4 reboot mechanism.

## Validation and comparison

All seven examples compile for both boards. Native protocol/servo tests pass
under address and undefined-behavior sanitizers, including R4-sized aggregates,
bounded polling and all five servos. The serial analysis tests cover nonzero
boot timestamps. See the [R4 experiment record](results/2026-10-14/uno-r4-minima/README.md)
for exact toolchains, resource counts and hardware results.

The hardware comparison retains every accepted sample, including coarse clock
steps, saturation and acquisition. Apply the same 120-second cutoff when
comparing steady windows; report settling separately. Sequential trials have
different starting offsets and oscillator drift, so lower RMS in one trial
does not establish a generally superior servo or an R4 speedup. Serial offsets
are internal PTP estimates, not physical PPS measurements. Scope confirmation
is separate from PPS register readback.
