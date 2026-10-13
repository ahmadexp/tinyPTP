# Direct two-UNO PTP experiment

**The installed firmware now starts automatically.** Use the
[standalone role guide](standalone-roles.md) for current operation. The manual
commands and pair launcher below describe the earlier diagnostic workflow.

Status on 2026-10-13: both UNOs are programmed, both attached TimeSticks are
supported AX88279 units, and the direct pair exchanges hardware-timestamped
PTP. The new [hardware report](two-uno-test-2026-10-13.md) records the test and
remaining measurement limits.

## Hardware and roles

Connect each TimeStick to its own UNO's MAX3421E shield. Connect the two
TimeSticks directly by Ethernet. Both UNO USB-B ports connect to PTPBox for
power, upload, and serial control. Linux does not participate as a PTP master
or follower in this topology.

The current driver supports AX88279, chip code 7. The original stick tested on
2026-10-12 is AX88279A, chip code 15, and requires a different driver. Neither
of the two sticks attached for the successful pair test is that AX88279A unit. Identify
both attached sticks with the probe before initialization. USB `0B95:1790`
alone cannot distinguish them. Back up the new UNO's existing flash before its
first upload, as was done for the first UNO.

Both UNOs use the same sketch. After Ethernet initialization:

- `m` starts the fixed master: multicast two-step Sync at 1 Hz, hardware TX
  timestamp followed by Follow_Up, hardware RX timestamps in Delay_Resp.
- `f` starts the direct follower with the integer PI servo. It binds to the
  first valid hardware-timestamped two-step Sync source in domain 42, then
  rejects other source identities until restarted.
- `x` stops the active role and restores nominal clock frequency.
- `l` reports PHY registers for link diagnostics after vendor identification.
- Existing `p` and `s` retain the pinned PTPBox master identity for the earlier
  observation and servo experiments.

The tested port mapping on PTPBox is:

| Role | UNO USB serial | TimeStick MAC |
| --- | --- | --- |
| Master | `1453230343235190E071` (`ttyACM0` during test) | `9C:69:D3:2B:A1:8D` |
| Follower | `14533303731351B0C090` (`ttyACM1` during test) | `9C:69:D3:2B:A1:2F` |

Use stable `/dev/serial/by-id/` paths when launching. Both sticks report
firmware `1.3.16.7`, silicon revision 1, and USB device revision `0400`.
PHY initialization now advertises only 1 Gb/s full duplex. In this pair,
disabling 2.5G advertisement and removing 10/100 advertisement resolved the
startup failure seen with the earlier Linux-peer initialization sequence.

The master also binds to the first valid Delay_Req requester. This is a fixed
one-master/one-follower direct-link experiment: no Announce generation, BMCA,
master election, failover, or IEEE profile compliance is implemented. The
master's TimeStick free-runs from its initialized clock. Synchronization is
relative to it; the clock has no established UTC epoch or traceability.

## Deployment procedure

1. List `/dev/serial/by-id/` on PTPBox and map each stable serial identifier to
   its UNO. Do not assume `/dev/ttyACM0` retains the same board after reconnect.
2. Capture both descriptor and vendor-control probes. Confirm MAX3421E
   initialization, distinct TimeStick MACs, and supported AX88279 chip codes.
3. Compile and upload the sketch to each selected serial port using the
   [existing toolchain instructions](setup-and-design.md#toolchain-and-firmware).
4. Start both serial sessions together with queued commands `em` and `ef`.
   Both PHYs must initialize while each waits for the direct Ethernet link;
   completing initialization on one board before starting the other may time out.
5. Verify hardware Sync TX reports and Delay_Resp counts at the master, then
   complete four-timestamp exchanges, acquisition, and settled offsets at the
   follower. A transmitted Sync alone does not establish synchronization.

For a bounded capture, set `MASTER_PORT` and `FOLLOWER_PORT` to the two actual,
different `/dev/serial/by-id/...` paths and use fresh log filenames. In a shell
on PTPBox, start the captures concurrently:

```sh
python3 /home/user/tinyPTP/tools/capture_serial.py \
  "$MASTER_PORT" /home/user/tinyPTP/test-results/uno-master-new.log \
  --reset --command em --seconds 240 --end-command x &
master_capture=$!
python3 /home/user/tinyPTP/tools/capture_serial.py \
  "$FOLLOWER_PORT" /home/user/tinyPTP/test-results/uno-follower-new.log \
  --reset --command ef --seconds 240 --end-command x &
follower_capture=$!
wait "$master_capture"
wait "$follower_capture"
```

Both commands use the existing logger's exclusive port access and output-file
creation. Their terminal output can interleave, but their log files are
separate. Opening another serial session can reset an UNO. A normal capture
ends with `x`, so this procedure deliberately stops both roles after testing.
It does not install a persistent service.

## Software verification

The protocol tests cover existing follower behavior plus master wire framing,
1 Hz scheduling, sequence rollover, waiting for the matching Sync hardware TX
report, Follow_Up timestamp encoding, Delay_Resp requester/sequence echo and
hardware RX time, peer binding, missing TX timestamp recovery, and transfer
failure. Address and undefined-behavior sanitizers pass. These tests exercise
protocol logic with a simulated USB transport; actual timestamp delivery on
both attached devices has also been observed in the hardware run.

```sh
c++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  tests/ptp_protocol.cpp -o /tmp/tinyptp-protocol-test
/tmp/tinyptp-protocol-test
```

The final installed sketch uses 25408 bytes flash and 960 bytes static SRAM,
leaving 1088 bytes beyond static allocation, with AVR core 1.8.8 and USB Host
Shield Library 2.0 version 1.7.0 on PTPBox. An earlier local check of the role
implementation, before the link-negotiation fix, also compiled with native
QMK AVR GCC 15.2.0 on macOS ARM64. Compiler and source changes affect image
sizes. Neither build establishes a measured runtime stack margin.

## Continuous operation

`tools/run_uno_pair.py` opens both serial ports exclusively, resets the UNOs
together, sends `em`/`ef`, and keeps separate `master.log` and `follower.log`
files in a newly created log directory. It does not compute timestamps,
exchange PTP packets, or run the servo. Both Arduino/TimeStick pairs do that.
The default duration is unlimited; `--seconds 240` runs a bounded test.
SIGTERM or Ctrl-C sends `x` to both boards before closing their sessions.
SIGUSR1 retries initialization on both, which restarts acquisition.

For the tested hardware, start from PTPBox:

```sh
python3 /home/user/tinyPTP/tools/run_uno_pair.py \
  --master-port /dev/serial/by-id/usb-Arduino__www.arduino.cc__0043_1453230343235190E071-if00 \
  --follower-port /dev/serial/by-id/usb-Arduino__www.arduino.cc__0043_14533303731351B0C090-if00 \
  --log-dir /home/user/tinyPTP/test-results/pair-new
```

The log directory must not already exist. The runner records its process ID in
`pid` there while active. Stop it with `kill -TERM` and that verified process
ID. A background launch with `nohup` keeps it running after SSH closes; this
is an ordinary user process, not a service that restarts on reboot. Logs grow
while it runs. Closing another program on the same serial ports is not a
substitute for stopping the runner cleanly.
