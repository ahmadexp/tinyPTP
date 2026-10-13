# Grandmaster serial output test, 2026-10-13

The grandmaster UNO (USB serial ending `E071`) was updated to print ptp4l-style
role events and one-second status lines at 115200 baud. The follower (`C090`)
retained its previously validated PPS-enabled firmware. Both were reset
together for this 100-second capture. Neither logger sent startup commands
or stopped PTP when it closed.

- `master-log-20261013-01.log`: grandmaster boot, PPS register readback, role
  transition, follower identity from its first valid Delay_Req, and periodic
  Sync/Follow_Up and Delay_Resp counters.
- `master-log-follower-20261013-01.log`: concurrent follower log to check that
  protocol exchanges and the follower servo continue during master logging.
- `validation.json`: measured report cadence, final master counters, and
  follower summary over the last 30 samples.
- `SHA256SUMS`: checksums of the raw logs and validation JSON. Original serial
  CRLF endings are retained.

The capture contains 87 master status lines, each separated by exactly
1000 ms in Arduino uptime. Final master counters were 86 completed Sync pairs
and 86 Delay_Resp packets, with zero timeouts, missing RX timestamps, or
discarded aggregates. The follower completed 86 exchanges without timeouts
and reached `s3`. Both serial ports were released afterward.

Installed grandmaster HEX SHA-256:

```text
93d38313c683fd9fae9aefb4f0e1c2043097f5706cb7f92a928fa5617a2c12b0
```

The grandmaster uses 28528 bytes flash and 995 bytes static SRAM with AVR core
1.8.8 and USB Host Shield Library 2.0 version 1.7.0. The unchanged follower HEX
hash is `5262123d14c46b1bb685c5344cc199401b9d0a8bfc817a6a09cb4d68cd29afff`.
The current shared source also compiles for the follower (28524 bytes flash),
but that newly built follower image was not uploaded in this test.

Native serial-format tests check all master fields, signed frequency, and
large counters. Master and follower protocol builds passed ASan/UBSan. The
log summary regression tests passed. The user had confirmed PPS presence on
both sticks before this logging change; independent edge-offset measurements
remain pending. Both clock/output-enable register readbacks are included here.

See the [serial guide](../../../standalone-roles.md#grandmaster-serial-monitor)
for field meanings. Increasing Delay_Resp counts confirm incoming follower
requests, not follower lock or an independent PPS accuracy measurement.
