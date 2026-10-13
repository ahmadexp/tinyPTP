# Standalone roles and PPS output validation, 2026-10-13

All captures used 115200 baud, 8N1 on the stable UNO USB paths in their headers.
The Python capture program only read output after pulsing reset. No PTP start
or stop commands were sent. Roles and the servo run on the UNOs.

## Initial standalone firmware

- `autoboot-master-20261013-01.log` and `autoboot-follower-20261013-01.log`:
  180-second simultaneous-reset experiment, 166 completed follower exchanges.
- `autoboot-summary.json`: final 60 samples, RMS 53.50 ns, mean -27.25 ns,
  range -125 to +45 ns, mean path delay 733.25 ns.
- `autoboot-follower-reopen-20261013-01.log`: follower-only serial reopen/reset,
  automatic reacquisition while the grandmaster continued running. One coarse
  clock step occurred, followed by convergence. No startup command was sent.

These images preceded the PPS gate fix. Their HEX SHA-256 values were:

```text
grandmaster b9356d8c5d4dd26465f30cfa8450959d7107e825805f1c1dab3edd662dbe26c6
follower    dc1349b8f786cd4dc526dd006bc7e1dd63b0cb42d9081433f3fdec8b101f968d
```

## PPS output-gate fix

The old setup enabled the clock-side PPS generator but omitted the physical
output gate. Readbacks on both AX88279 devices found gate `0x0000` before the
fix and `0x4000` after it. Both also reported clock control `0x5f` and active
pulse width `500000000` ns. The gate is a separate volatile register.

The first follower upload conflicted with an early-started serial capture.
That capture was stopped and the follower was successfully re-uploaded before
further tests. `pps-master-20261013-01.log` records the first master gate change
and startup retries while the peer was being uploaded/reset.
`pps-follower-20261013-02.log` records the staggered-start retry sequence.
Both captures were ended early, then the boards were reset together. This
exposed a startup limitation: repeated full PHY initialization during staggered
bring-up can disrupt the peer. Simultaneous reset recovered the direct link.

`pps-master-20261013-02.log` and `pps-follower-20261013-03.log` are the subsequent
180-second paired-reset validation with the PPS gate fix. The follower log
records its first gate change from zero. The master gate was already enabled
by the previous run. `pps-summary.json` summarizes the final 60 follower samples.

The final run completed 166 follower exchanges with zero timeouts or rejected
paths. Its last 60 offsets had RMS 25.76 ns, mean 21.17 ns, range -20 to +45 ns,
and mean path delay 737.17 ns. Final status counters reported no missing RX
timestamps or discarded aggregates. Both serial ports were released after
the captures; the automatic role firmware remains running.

Installed firmware HEX SHA-256 values:

```text
grandmaster 3d6215e635ad3feb88a3dbc518ebfee3bbf03103a61b335857ae6099ada31297
follower    5262123d14c46b1bb685c5344cc199401b9d0a8bfc817a6a09cb4d68cd29afff
```

AVR core 1.8.8 and USB Host Shield Library 2.0 version 1.7.0: grandmaster
28012 bytes flash, follower 28008 bytes flash; both 995 bytes static SRAM.
Maximum stack use is unmeasured.

Register readbacks confirm configuration only. Physical PPS presence, amplitude,
pulse width, and edge-to-edge offset have not been measured independently.
The offset statistics are the follower's hardware-timestamp estimates, not a
PPS measurement or UTC accuracy claim. See the [standalone guide](../../../standalone-roles.md)
for build instructions, serial fields, and the `q` PPS readback command.

## Reproduce the summaries and verify evidence

From the repository root:

```sh
python3 tools/summarize_ptp.py docs/results/2026-10-13/standalone/autoboot-follower-20261013-01.log --tail 60
python3 tools/summarize_ptp.py docs/results/2026-10-13/standalone/pps-follower-20261013-03.log --tail 60
```

`SHA256SUMS` contains checksums for the logs and JSON summaries in this folder.
Serial CRLF line endings are retained in the raw capture files.
