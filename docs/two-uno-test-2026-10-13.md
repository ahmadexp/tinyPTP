# Two UNOs running hardware-timestamped PTP

Two Arduino UNO R3 boards run PTP across a direct Ethernet cable between two
AX88279 TimeSticks. One UNO generates the master protocol messages; the other
runs the follower protocol and integer PI servo. Each uses a MAX3421E USB Host
Shield at USB full speed. Linux provides USB power, firmware upload, and serial
logging only. No Linux PTP peer or host clock adjustment is part of this test.

## Equipment and deployment

| Field | Master | Follower |
| --- | --- | --- |
| UNO USB serial | `1453230343235190E071` | `14533303731351B0C090` |
| Linux port during this test | `/dev/ttyACM0` | `/dev/ttyACM1` |
| TimeStick MAC | `9C:69:D3:2B:A1:8D` | `9C:69:D3:2B:A1:2F` |
| PTP clock identity | `9c69d3.fffe.2ba18d` | `9c69d3.fffe.2ba12f` |
| PTP port number | 1 | 1 |
| ASIX chip | AX88279, code 7, revision 1 | AX88279, code 7, revision 1 |
| Firmware | `1.3.16.7` | `1.3.16.7` |
| USB device revision | `0400` | `0400` |
| MAX3421E revision | `0x13` | `0x13` |

Both sticks use vendor configuration 1 with five endpoints and 64-byte bulk
packets. Their exact PCB revisions remain unconfirmed. Neither is the earlier
AX88279A unit. Both UNOs had their 32768-byte flash contents backed up before
this session's uploads; those private backups remain on PTPBox. They are not
EEPROM or fuse backups. Both final uploads completed successfully.

The installed combined-role firmware uses **25408 bytes of flash** and
**960 bytes of static SRAM**, with **1088 bytes** left for stack and dynamic
use. Maximum stack usage has not been measured. This build uses Arduino AVR
core 1.8.8 and USB Host Shield Library 2.0 version 1.7.0 with PTPBox's standard
Arduino compiler. It supersedes the smaller original follower-only build.

## Direct-link initialization fix

The initial sequence copied from the earlier UNO-to-Linux experiment did not
bring up the two-stick link. Both PHYs advertised 2.5G (`AN MMD 7`, register 32,
value `0x0081`). One side exhausted the 15-second link wait; the other returned
USB control error `0x0D` while reading PHY status. The master-side diagnostics
showed partner advertisement, so absence of a cable was not established.

The updated sequence clears the 2.5G advertisement bit using ASIX Clause 45
request `0x27`, verifies the readback, removes 10/100 and half-duplex
advertisements, and advertises 1000BASE-T full duplex before restarting
autonegotiation. Both devices then reported link, passed clock progression
checks, and exchanged PTP. This establishes that constraining advertisement
resolved the observed failure. It does not independently establish the cause
of the earlier USB error or certify operation at 2.5 Gb/s.

Reference encodings are from the
[TimeStick ASIX driver](https://github.com/Time-Appliances-Project/TimeStick/tree/4c2935a1f723f8ec66fc3a3b4fb6a246b9de7c6e/TimeStick2/DRV)
(`ax_mmd_read`, `ax_mmd_write`, `AX88179A_PHY_CLAUSE45`) and the host's Linux
`mii.h`/`mdio.h` definitions. PHY settings are volatile. No TimeStick firmware,
EEPROM, or eFuse programming was performed.

## Protocol behavior

Both roles use L2 PTPv2, domain 42, two-step Sync, and end-to-end delay at 1 Hz.
The master timestamps Sync at Ethernet transmission and sends Follow_Up only
after the matching type/sequence hardware timestamp arrives through endpoint
`0x84`. Delay_Resp carries the master's hardware ingress timestamp of the
follower's Delay_Req, along with its requester identity and sequence number.

The follower binds to the first valid hardware-timestamped two-step Sync source
on the direct cable. It then rejects other source identities until restarted.
The master similarly binds to one Delay_Req requester. The follower uses four
hardware timestamps for offset and path delay. Neither serial arrival times
nor AVR `micros()` replace those timestamps.

The master TimeStick free-runs from its initialized clock. This establishes
relative synchronization; neither clock has an independently established UTC
epoch. No Announce generation, BMCA, automatic failover, or PTP profile
compliance is claimed. PPS routing and independent output comparison remain
untested. The bounded RX buffer, 32-bit seconds limit, and other experimental
constraints in the [earlier report](ptp-test-2026-10-12.md) still apply.

## Measured results

The completed 240-second run began at 22:00:20 UTC on 2026-10-13
(3:00:20 PM America/Los_Angeles). It includes initialization and 228 complete
four-timestamp follower exchanges. The last 60 samples are 169 through 228.

| Measurement | Result |
| --- | ---: |
| Complete exchanges | 228 |
| Mean offset, last 60 | +25.667 ns |
| RMS offset, last 60 | 67.305 ns |
| Minimum / maximum offset, last 60 | -85 / +150 ns |
| Mean path delay, last 60 | 735.5 ns |
| Path delay range, last 60 | 730 to 740 ns |
| Last logged frequency correction | -10165 ppb |
| Coarse clock steps | 0 |
| Follower exchange timeouts | 0 |
| Rejected path measurements | 0 |
| Missing RX timestamps, last status on each board | 0 |
| Discarded RX aggregates, last status on each board | 0 |
| Master Sync TX timestamp timeouts | 0 |

The initial offset was +13765455 ns. The follower initially saturated its
frequency correction at -500000 ppb, then converged through the existing PI
servo. No coarse step occurred because the phase error was below the 500 ms
step threshold. Startup is included in the raw CSV but excluded from the
last-60-sample statistics. The mean path estimate varies during acquisition
while the clocks have a substantial frequency difference.

These are internal PTP offset estimates, not independently measured PPS error,
calibrated link asymmetry, or absolute time accuracy. The last-window statistics
do not characterize startup or guarantee a bound for longer operation.
`freq_ppb` records the control in effect before the row's servo update.
Master status is printed every ten seconds, so its last recorded status is at
sample 220; the follower's per-sample records establish all 228 exchanges.

The [evidence archive](results/2026-10-13/README.md) contains the raw logs,
per-sample CSV, reproducible JSON summary, and checksums. The bounded runner
sent `x` to both boards at completion; nominal addend writes are visible at
the end of both logs. The pair was then restarted in a separate continuous
session, with logs under `/home/user/tinyPTP/test-results/pair-live-20261013/`.
That session's acquisition is separate from the completed dataset above.

## Reproduction

See the [two-UNO guide](two-uno-setup.md) for stable port paths, the concurrent
launcher, and clean stop/retry commands. Starting both PHYs together matters
because each waits for the other end of the direct link. The same firmware is
installed on both boards, selected with `em` for master and `ef` for follower.

The protocol tests pass with address and undefined-behavior sanitizers. They
cover master message encoding, hardware timestamp correlation, requester
identity, sequence rollover, timeout recovery, transmit failure, and the
existing follower checks. Hardware captures additionally demonstrate the
master Sync timestamp reports and completed follower exchanges.

The uploaded Intel HEX file has SHA-256
`0761a9097074b49225580e3f691a3c4b9a5480018c6e99968050d20764b1279f`.
The deployed source hashes match this checkout. The seven pre-existing Linux
`ptp4l` service processes retained their original PIDs throughout the test;
none was reconfigured or restarted.
