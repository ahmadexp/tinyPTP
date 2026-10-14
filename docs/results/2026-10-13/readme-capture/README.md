# README capture attempt, 2026-10-13

A new simultaneous serial capture was attempted at 23:26:54 UTC (16:26:54
Pacific), after the photographs were supplied. Opening both UNO serial ports
and pulsing DTR reset the boards. No firmware was uploaded during this attempt.

Both automatic roles repeatedly failed initialization, so these logs contain
no successful PTP exchange samples. The first capture was stopped early.
A second pair of captures began at 23:28:04 UTC, sending `d` to enable detailed
diagnostics. Those diagnostic captures ran for 55 seconds after sending the
command. Serial ports were released afterward.

The diagnostics show:

- Both devices still identify as AX88279, chip code 7, firmware 1.3.16.7,
  with the same MAC identities as the successful captures.
- Ethernet initially reaches link-up on both boards.
- Hardware-clock USB request `0x13` returns status `0x05` (STALL) on the
  grandmaster, and the follower also reports failed clock access. Some follower
  reads return the invalid `DEADBEEF` sentinel instead of a valid timestamp.
- Subsequent initialization retries also encounter PHY link timeouts.

A complete power cycle of both UNO/shield/TimeStick assemblies was requested.
A later power cycle at approximately 17:31 Pacific restored clock access and
PPS; see the [subsequent recovery record](../power-cycle-recovery/README.md).
The root cause of this restart failure remains unresolved. It is not proof of
failed PPS hardware or a defect in the photographed jumper setup.

The README's successful grandmaster and follower excerpts are explicitly
sourced from the earlier [22:44 UTC capture](../master-serial/README.md).
The photographs were taken separately; they are not claimed to document this
failed serial run. No passing summary is generated from these failed logs.

Files ending `debug-20261013-01.log` include verbose register diagnostics;
the other two files contain the initial quiet startup retries. `SHA256SUMS`
records the original log bytes, including serial CRLF endings.
