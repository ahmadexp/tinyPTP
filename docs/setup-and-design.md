# Setup and implementation guide

This describes the successful 2026-10-12 PTPBox experiment. Hardware results
and their measurement limits are in the [test report](ptp-test-2026-10-12.md).

## Physical topology

```text
PTPBox USB -> UNO R3 USB-B (power, upload, serial)
                 |
            MAX3421E shield, SPI
                 |
            USB-A host port, 12 Mb/s
                 |
            AX88279 TimeStick
                 |
            direct Ethernet cable, 1 Gb/s full duplex
                 |
PTPBox enp179s0f1, ixgbe, /dev/ptp15 (PTP master)
```

PTPBox management uses `enp179s0f0` at `192.168.1.60`. The experiment uses
`enp179s0f1`, MAC `0c:9d:92:c5:83:32`. The launcher verifies that MAC before
starting. Interface names and `/dev/ptp15` are specific to this machine.
The TimeStick stays plugged into the shield, not directly into Linux USB.

The photographed shield is marked USB HostShield V5.60. Controller reads
confirmed MAX3421E revision `0x13`. The sketch uses standard library SPI
wiring, CS D10 and INT D9, and holds D4 high to deselect common shield SD
sockets. There is no USB hub driver. Initial all-zero controller reads cleared
after power-jumper work; the user reported connecting the 5 V jumper above
reset. This records the observed repair, not a universal soldering instruction
for every shield variant. Verify the board's own 3.3 V, 5 V, and VBUS routing.
The user supplied a [photograph of the three required jumpers on the tested
VS60 shield](../README.md#usb-host-shield-power-jumpers), including the 5 V
VBUS selection and two power-header jumpers.
USB power descriptors (450 mA replacement, 500 mA original) are declarations,
not measured consumption or evidence that the supply can deliver that current.

## Toolchain and firmware

The tested host has Arduino CLI 1.5.2-rc.1, AVR core 1.8.8, USB Host Shield
Library 2.0 version 1.7.0, pyserial, and LinuxPTP `ptp4l` 4.4. The test account
has `dialout` membership for `/dev/ttyACM0`. Close other serial monitors first.
The Arduino tools are isolated under `/home/user/.local/share/tinyptp/`.

From PTPBox, with the repository at `/home/user/tinyPTP`:

```sh
cli=/home/user/.local/share/tinyptp/bin/arduino-cli
cfg=/home/user/.local/share/tinyptp/arduino-cli.yaml
"$cli" --config-file "$cfg" compile --fqbn arduino:avr:uno \
  --build-path /home/user/tinyPTP/build/TimeStickProbe \
  /home/user/tinyPTP/examples/TimeStickProbe
"$cli" --config-file "$cfg" upload --fqbn arduino:avr:uno \
  --port /dev/ttyACM0 --input-dir /home/user/tinyPTP/build/TimeStickProbe \
  /home/user/tinyPTP/examples/TimeStickProbe
mkdir -p /home/user/tinyPTP/test-results
```

The upload replaces Arduino application flash. A 32768-byte backup was saved
before the first upload on this test unit; it is not an EEPROM or fuse backup.
No sketch command programs the TimeStick's flash, EEPROM, or eFuse.

## Run and stop

In the first terminal:

```sh
sudo /home/user/tinyPTP/tools/run_ptp_master.sh 180
```

In the second terminal, choose a new output filename:

```sh
python3 /home/user/tinyPTP/tools/capture_serial.py \
  /dev/ttyACM0 /home/user/tinyPTP/test-results/ptp-new.log \
  --reset --command es --seconds 160 --end-command x
```

The serial session uses 115200 baud. Opening the UNO serial port can reset it,
including without `--reset`. Keep initialization and the experiment in one
session. The capture refuses to overwrite an existing file and requests
exclusive serial access. `--end-command x` sends a stop command before closing;
the capture loop ends before this command, so its response is not recorded.
If a capture is interrupted, its normal end command may not run. Stop the
client with `x` in the active session or reset the UNO.

| Serial command | Action |
| --- | --- |
| `r` | Repeat descriptors |
| `v` | Select vendor configuration and read chip, MAC, firmware, clock |
| `i` | Volatile PHY/link and clock initialization |
| `e` | Initialize Ethernet and endpoints |
| `t` | Ten-frame echo test with `tools/link_peer.py` |
| `p` | Start PTP observation without servo |
| `s` | Start PTP with clock servo |
| `x` | Stop PTP and restore nominal frequency |

Use `ep` to observe or `es` to synchronize. Initialization commands are
sequential characters, not separate serial sessions. The master stops after
its time limit and removes its temporary config. Its lock and Unix sockets
use `/run/timemaster/`, which fits the installed AppArmor policy. No AppArmor
profile change was required. `free_running 1` leaves the master NIC clock
unadjusted by this test; the UNO adjusts only the TimeStick clock. Existing
host PTP services and system-clock configuration were left unchanged.

On a different host, update the interface and identity in the master config,
the launcher's NIC check, and the pinned source identity in `PtpClient.h`.
These scripts intentionally describe the tested fixed topology.

## Protocol and clock control

The transport is Ethernet PTPv2, EtherType `0x88F7`, multicast
`01:1B:19:00:00:00`, domain 42, two-step Sync and end-to-end delay at 1 Hz.
The client accepts source clock `0c9d92.fffe.c58332`, port 1. It derives its
own identity from its MAC. It correlates Sync/Follow_Up and
Delay_Req/Delay_Resp by identity and sequence, including a Delay_Resp that
arrives before the corresponding hardware TX report.

RX hardware timestamps come from ASIX aggregate metadata. TX timestamps come
from endpoint `0x84`: two banks of five 13-byte records and one selector byte,
131 bytes total. The selector is processed only after all three USB packets
arrive. A complete exchange uses master TX `t1`, client RX `t2`, client TX
`t3`, and master RX `t4`, with the corresponding PTP correction fields.
USB completion times and AVR `micros()` are not packet timestamps.

Initialization order is native mode and PHY, link wait and settling, MAC,
then complete PTP clock/report/parser setup. Clock progression is checked
after the final setup. USB control stalls, invalid nanoseconds, and invalid
register readback cause initialization to fail rather than feed the servo.

The integer servo assumes 1 Hz samples:

- If the absolute offset exceeds 500 ms, perform a coarse clock step, clear
  the integral, restore nominal frequency, and quarantine samples for 200 ms.
- Otherwise propose `integral - offset_ns / 20`, bounded to +/-200000 ppb.
- Compute `control = proposed_integral - offset_ns / 2`. Commit the integral
  only if control is within +/-500000 ppb, then clamp control to those bounds.
- Write addend `0xCCCCCCCC + 0xCCCCCCCC * control_ppb / 1000000000` using
  signed 64-bit intermediate arithmetic. Integer divisions truncate.

The frequency printed on each sample is the value before that sample's servo
update. It must not be interpreted as the new control output for that row.
The initial coarse step uses USB register access, so residual phase error is
expected and is removed gradually by subsequent packet-based measurements.

Only one exchange is outstanding, with a 2500 ms timeout. Paths outside
-1000 ns to 1 ms are rejected. Corrections are bounded, invalid timestamps
are rejected, and nonzero upper seconds bits are unsupported. The implementation
uses a 32-bit seconds epoch ending in 2106 and a 256-byte RX aggregate buffer.
These are explicit experimental constraints, not general IEEE 1588 compliance.

## Next experiments

The new selectable master and direct-follower roles are described in the
[two-UNO procedure](two-uno-setup.md). The first direct pair experiment is recorded in the
[two-UNO hardware report](two-uno-test-2026-10-13.md); both boards require explicit role selection after initialization. Identify the second stick before selecting its
driver. The original AX88279A requires separate support. Independent PPS
comparison, calibrated link asymmetry, stack high-water measurement, sustained
traffic, and disconnect/reconnect testing remain outstanding.
