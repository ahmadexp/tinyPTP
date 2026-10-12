# tinyPTP

Bring-up work for two classic Arduino UNO R3 boards, each using a MAX3421E USB
Host Shield and a TimeStick v2, with an Ethernet cable between the TimeSticks.
TimeStick v2 is the target. The first test host identifies the Arduino as an
UNO R3; the shield model and its MAX3421E wiring still need confirmation.

**Status: experimental fixed-peer PTP client on the UNO, tested against
PTPBox using hardware timestamps. This is not a general-purpose PTP stack.**

The [first hardware tests](docs/bringup-2026-10-12.md) now pass shield
initialization, TimeStick full-speed USB enumeration, vendor configuration
selection, chip/MAC reads, and hardware-clock register reads. The chip identifies
as AX88279 revision `0x01`; endpoint `0x84` is present. The replacement stick
(MAC `9C:69:D3:2B:A1:2F`, firmware `1.3.16.7`) now has a running hardware clock
and a confirmed 1 Gb/s full-duplex Ethernet link. Clock access initially stalls
on this stick until native-mode/PHY setup and link establishment complete.
Ten of ten 60-byte test frames completed a verified round trip through the
UNO, shield, TimeStick, and PTPBox. The fixed-peer client now exchanges Sync,
Follow_Up, Delay_Req, and Delay_Resp with Linux `ptp4l`, using hardware RX/TX
timestamps. See the [PTP test report](docs/ptp-test-2026-10-12.md).

## Running the PTP experiment

On PTPBox, start the dedicated, time-limited master in one terminal:

```sh
sudo /home/user/tinyPTP/tools/run_ptp_master.sh 180
```

Then run the UNO client in another terminal, using a fresh log filename:

```sh
python3 /home/user/tinyPTP/tools/capture_serial.py \
  /dev/ttyACM0 /home/user/tinyPTP/test-results/ptp-new.log \
  --reset --command es --seconds 160 --end-command x
```

`e` initializes Ethernet; `s` starts the fixed-peer servo. Use `ep` instead to
observe exchanges without disciplining the clock. `x` stops the client and
restores nominal clock frequency. Opening serial resets the UNO, so keep
initialization and PTP in the same session.

The test uses L2 PTPv2, domain 42, two-step Sync, E2E delay, and a 1 Hz master.
The master is pinned to clock identity `0c9d92.fffe.c58332`, port 1. The supplied
host configuration uses only `enp179s0f1` and `free_running 1`; the UNO adjusts
the TimeStick. Existing host PTP services and the host system clock are not
reconfigured. This experiment establishes relative synchronization to the NIC,
not independently verified UTC accuracy.

Current limitations: AX88279 only, 1 Gb/s full duplex, one outstanding exchange,
no BMCA or master failover, a bounded 256-byte aggregate buffer, and a 32-bit
seconds epoch ending in 2106. Oversized aggregates are drained and discarded.
No profile compliance, calibrated PHY asymmetry, or PPS accuracy is claimed.
The original stick identifies as AX88279A and needs a different driver path.

## Feasibility

The AX88279-based test unit now demonstrates this architecture with a Linux
PTP peer. Two-UNO operation and the AX88279A variant remain untested. ASIX documents USB full-speed support for both
the AX88179B and AX88279. This matches the MAX3421E's 12 Mbit/s host capability.
An Ethernet PHY operating at 100 Mbit/s or faster does not require the USB host
to sustain that rate for a sparse PTP exchange.

The UNO would run the protocol state machine and clock servo. The TimeStick
would maintain the precision hardware clock, timestamp Ethernet ingress and
egress, and generate PPS. USB delivery latency does not become the packet
timestamp error when timestamps are captured at the Ethernet interface. It
still affects update delay, buffering, and the ability to service timestamp
queues before overflow. Never substitute `micros()` or USB completion times for
hardware packet timestamps.

The target is TimeStick v2, with the AX88279 driver path as the starting point:
the TimeStick2 directory contains AX88279 firmware and its manufacturing archive
contains a `06_TIME_STICK_279` bill of materials. The top-level TimeStick README
describes AX88179B and should not be treated as the v2 programming reference.
Firmware, silicon revision, and the full-speed USB interface layout still need
verification on the actual sticks. VID/PID alone may not distinguish all ASIX
revisions.

## Why flexPTP cannot be used unchanged

The inspected flexPTP source has 16 receive and 16 transmit message-pool entries.
Each contains a 128-byte payload. Those payloads alone require 4096 bytes, before
message metadata, protocol state, queues, network buffers, USB state, and stack.
The ATmega328P has only 2048 bytes of SRAM and 32 KB of flash.

flexPTP does support OS-less operation, so an RTOS is not the obstacle. A port
would require reducing message pools and queues, supplying a USB Ethernet
network adapter and hardware-clock port, auditing AVR integer widths and
floating-point behavior, and measuring the complete linked image. Its stock
memory layout is unsuitable; this is not proof that a carefully reduced fork
could never fit. No full flexPTP AVR build has been attempted here.

For the first experiment, a smaller fixed-role implementation is the recommended
engineering direction. Use flexPTP as a reference or evaluate a reduced fork
after the USB driver proves viable.

## Proposed first PTP experiment

- One fixed master and one fixed slave, domain 0, direct cable, unique identities.
- IEEE 1588v2 over Ethernet, EtherType `0x88F7`, multicast `01:1B:19:00:00:00`.
- End-to-end delay measurement, two-step Sync, initially one exchange per second.
- Sync, Follow_Up, Delay_Req and Delay_Resp, plus appropriate Announce messages.
- Static allocation, one outstanding exchange, bounded retries and timeouts.
- Integer timestamp calculations and a bounded fixed-point frequency servo.
- Discipline the slave TimeStick clock and compare the two PPS outputs.

This intentionally limited experiment would not be a complete general-purpose
PTP implementation: no BMCA, P2P/gPTP, management, arbitrary TLVs, or profile
interoperability claim in the first version. Raw Ethernet avoids needing IP,
UDP, DHCP, or ARP on the UNO.

For a zero-correction, symmetric-link example, collect master transmit `t1`,
slave receive `t2`, slave transmit `t3`, and master receive `t4`:

```
offset (slave minus master) = ((t2 - t1) - (t4 - t3)) / 2
mean path delay            = ((t2 - t1) + (t4 - t3)) / 2
```

The implementation must apply correction fields, match domain/source identity/
sequence IDs, reject stale or incomplete exchanges, and reset outstanding
samples after clock steps or link loss. Use separate seconds/nanoseconds fields
and bounded signed differences; do not flatten the entire 48-bit seconds field
into signed 64-bit nanoseconds. Absolute UTC needs an external reference; two
free-running sticks alone can establish relative synchronization.

## The main implementation work

1. Enumerate at USB full speed and identify interfaces, alternate settings,
   endpoint types and packet sizes. Verify power and stable attachment.
2. Implement the correct ASIX initialization and Ethernet TX/RX transport.
   Generic CDC Ethernet connectivity does not establish hardware timestamp
   support. The supplied Linux driver is the protocol reference, not code that
   can simply be compiled on AVR.
3. Read, set, and adjust the TimeStick hardware clock; verify PPS operation.
4. Extract RX timestamps and correlate asynchronous TX timestamps with packets.
   The supplied driver has different timestamp paths for different chips,
   including control-transfer and endpoint-based mechanisms.
5. Bound USB aggregation and memory use. Read USB packets incrementally, drain
   discarded data, and handle metadata even when it arrives after frame data.
   A small PTP frame buffer alone does not solve ASIX receive aggregation.
6. Add the PTP exchange and servo, then measure PPS offset, jitter, drift,
   packet loss recovery, and reconnect behavior on the actual pair.

For AX88279 specifically, `ax88279_submit_ts()` reads transmit timestamp reports
from bulk IN endpoint 4. The report is 131 bytes: two banks of five 13-byte
records and a final bank selector. At full speed this must be assembled across
USB packets, with the selector interpreted only after the report is complete.
The device descriptors and selected USB mode must expose the corresponding
endpoint before using this path. The driver also supplies a full-speed bulk-IN
configuration in `AX88279_BULKIN_SIZE`; this is useful evidence of a full-speed
transport path, but does not by itself verify timestamp delivery on the shield.

Do not allocate a normal 1536-byte Ethernet receive buffer on the UNO. Start
with a small bounded PTP frame buffer, USB packet scratch space, and minimal
state, then verify the entire application's SRAM with a linker map and runtime
stack high-water measurement. Reserve stack headroom. No final application
memory budget or synchronization accuracy has been measured yet.

If the UNO's own code needs aligned events, additionally bring a suitably
conditioned TimeStick PPS signal into a timer input and map the AVR timer to
that reference. Synchronizing the dongles does not automatically synchronize
the two AVR timers.

## First hardware check

The sketch at `examples/TimeStickProbe/TimeStickProbe.ino` reads USB device and
configuration descriptors at startup. It prints VID/PID, device revision, interface class,
alternate settings, endpoint addresses/types/sizes, and advertised power. It
streams descriptors instead of storing the full configuration tree. It uses
ordinary USB enumeration and descriptor requests by default. Optional serial
command `v` selects the observed ASIX vendor configuration 1, reads chip and MAC
registers and firmware version, and reads three clock samples if chip code 7 is confirmed. Read
lengths and USB status are checked. This does not initialize Ethernet, start or
adjust the precision clock, or write firmware/EEPROM. Configuration 1 remains
selected until reset or reconfiguration. The clock values are diagnostic only.

Command `i` applies volatile PHY and clock initialization, waits for link,
checks PTP register readback, and checks clock progression. Command `e` also
sets up the MAC and USB bulk endpoints for a 1 Gb/s full-duplex link. Command
`t` sends ten experimental EtherType `0x88B5` frames and checks matching echoes
from `tools/link_peer.py` running on the attached Linux NIC. Receive storage is
256 bytes; oversized and multi-frame aggregates are drained and discarded.
This echo command is a limited transport test, not a general Ethernet receive driver.
The PTP path additionally parses bounded multi-frame aggregates and hardware
timestamps. No command programs the TimeStick's flash, EEPROM, or eFuse.

1. Install **USB Host Shield Library 2.0** by the USB Host Shield team in Arduino
   Library Manager. Select **Arduino Uno** with the AVR core.
2. Stack the shield, attach one TimeStick directly, and provide a stable supply
   adequate for the UNO, shield, and dongle. Check the shield's VBUS wiring and
   power rating. A USB connector by itself does not guarantee adequate power.
3. Upload the sketch and open Serial Monitor at **115200 baud**. Reset the UNO
   if the initial output was missed.
4. Save the entire output for both sticks along with their PCB revision/chip
   markings and shield model. Send `r` to repeat descriptor reads. Send `v` for
   the optional vendor configuration and control-read test.

The probe assumes the library's standard MAX3421E SPI wiring and a directly
connected device. No USB hub driver is instantiated. D4 is held high to deselect
the SD socket on common shields. `USB_STATE_RUNNING` in this probe only means
the generic host enumeration path finished, not that an Ethernet interface is
configured. Descriptor success and clock reads do not prove packet timestamp
delivery or a running, disciplined clock.

Optional CLI build after installing the AVR core and library:

```
arduino-cli compile --fqbn arduino:avr:uno examples/TimeStickProbe
```

Build verification on 2026-10-08: Arduino AVR core 1.8.8, USB Host Shield Library
2.0 version 1.7.0 at the revision below, and QMK's native macOS ARM64 AVR GCC
15.2.0 toolchain produced a linked UNO image using **7934 bytes of flash and
332 bytes of static SRAM**. The installed Arduino GCC and ctags executables
were Intel-only and could not run on this host, so the check used a temporary
native compiler and locally built Arduino ctags. The Arduino installation was
not modified. Exact sizes may differ with the standard Arduino compiler.
That was the original descriptor-only build. Current hardware results and
standard Arduino compiler sizes are recorded in the bring-up report. Runtime
stack headroom has not yet been measured.

## Source references

Inspected on 2026-10-08:

- [TimeStick](https://github.com/Time-Appliances-Project/TimeStick), revision
  `4c2935a1f723f8ec66fc3a3b4fb6a246b9de7c6e`.
  See `TimeStick2/DRV/ax_ptp.c`, `ax_ptp.h`, `ax_main.c`, and
  `ax88179a_772d.c` for the hardware-clock, timestamp, and USB framing paths.
- [flexPTP](https://github.com/epagris/flexPTP), revision
  `3b33b895b230020bbd281b492b6adb9a253234f0`.
  See `src/flexptp/task_ptp.c` and `ptp_types.h` for message pools and payloads.
- [USB Host Shield Library 2.0](https://github.com/felis/USB_Host_Shield_2.0),
  revision `e5bc9950ba5389b67ad99175c312154af53df71d`.
- [ASIX AX88179B](https://www.asix.com.tw/en/product/USBEthernet/Super-Speed_USB_Ethernet/AX88179B)
  and [AX88279](https://www.asix.com.tw/en/product/USBEthernet/Super-Speed_USB_Ethernet/AX88279)
  product specifications.
- [MAX3421E](https://www.analog.com/en/products/max3421e.html) host specifications.
- [Arduino UNO R3](https://docs.arduino.cc/hardware/uno-rev3/) specifications.

No upstream implementation is vendored here. flexPTP is MIT licensed. The ASIX
driver source contains GPL notices, while the TimeStick repository has a
separate hardware/project license. Preserve the applicable notices and review
the relevant file licenses when incorporating upstream code.
