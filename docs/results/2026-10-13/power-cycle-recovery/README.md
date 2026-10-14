# PPS recovery after a power cycle

On October 13, the scope showed no visible PPS pulses. Serial diagnostics at
17:30 Pacific (October 14, 00:30 UTC) found that both UNOs were failing TimeStick
hardware-clock initialization. Clock request `0x13` and PPS register request
`0x11` returned USB STALL (`0x05`). Ethernet link-up alone did not establish
that the clock or PPS output was working.

The owner then power-cycled both UNOs. Their USB devices reappeared at
17:31 Pacific. With the same installed firmware, the paired capture beginning
at 17:32:24 Pacific passed hardware-clock progression and PPS register checks
on both devices. PTP exchanges resumed. The owner subsequently confirmed that
both physical pulses were visible on the oscilloscope after leaving USB Sharing.

Both devices reported:

```text
PPS gate=0x4000 clock=0x5F active_ns=500000000 enabled (register readback)
```

This enables the external output gate and clock PPS function, with a configured
500 ms active pulse width. Register readback and the owner's scope observation
are separate evidence; neither alone supplies a calibrated edge-offset result.

## Evidence

- `pps-check-*.log`: failed checks immediately before the reported power cycle.
- `powercycle-*.log`: successful 35-second diagnostic captures after the cycle.
- `pps-recovery-*.log`: subsequent 180-second paired serial capture.
- `validation.json`: statistics derived from the 180-second capture.
- `SHA256SUMS`: hashes of the original logs and validation record.

Use the UTC headers inside the logs for capture times. Filename suffixes are
identifiers, not authoritative timestamps. The later capture requested DTR low
before opening the serial ports, but both logs still show a fresh UNO boot.
Both initialized successfully again. Opening the ports must therefore still be
treated as capable of resetting the UNOs.

The 180-second capture completed **166 exchanges**, with final grandmaster
counters reporting zero timeouts, missing receive timestamps, or discarded
frames. Over follower samples 107 through 166, the internal offset estimate had
**10.37 ns RMS**, mean **-6 ns**, and range **-25 to +10 ns**. Mean estimated
path delay was **739.33 ns**. The final sample was in state `s3` at **-15 ns**.
These are internal PTP estimates, not calibrated PPS or UTC measurements.
Analysis starts at the final boot marker in each file, excluding buffered
output from before that boot; the original log files retain those bytes.

The [follower graphs and extracted CSV](plots/README.md) show acquisition,
frequency correction, path delay, and the final offset window. Reproduction
instructions are included with the figures.

The installed grandmaster image has SHA-256
`93d38313c683fd9fae9aefb4f0e1c2043097f5706cb7f92a928fa5617a2c12b0`;
the follower image has SHA-256
`5262123d14c46b1bb685c5344cc199401b9d0a8bfc817a6a09cb4d68cd29afff`.
A different initialization order was compiled for investigation but was never
uploaded. Recovery in this record followed the power cycle, not a firmware change.

The underlying cause of the earlier persistent clock-access failure remains
unresolved. This recovery does not establish that every restart will succeed.
