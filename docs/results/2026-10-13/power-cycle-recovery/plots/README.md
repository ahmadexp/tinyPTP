# Follower measurement graphs

Source: [the 180-second follower capture](../pps-recovery-follower-20261013-01.log)
after the October 13, 2026 power cycle. The plots contain 166 accepted exchanges,
starting at the last boot marker in that file. Buffered output preceding that
boot is excluded. The first and last samples are approximately 165.1 seconds
apart; startup occupies part of the 180-second capture.

- [Acquisition plot, PNG](follower-acquisition.png) or [SVG](follower-acquisition.svg): offset, frequency correction, and estimated path delay over the full run.
- [Offset detail, PNG](follower-offset-detail.png) or [SVG](follower-offset-detail.svg): final 60 samples, numbered 107 through 166.
- [Extracted data, CSV](follower-samples.csv): sample number, sequence, UNO uptime, elapsed time, servo state, offset, frequency correction, and path delay, with units in the column names.

The elapsed-time origin is the first accepted sample, not boot time or UTC.
The acquisition plot displays offset in milliseconds; the detail uses
nanoseconds. Both preserve the original values without smoothing or decimation.
Lines connect observations to aid reading; no measurements between samples are
implied. The final 60-sample window spans approximately 59 seconds.

The final-window offset RMS is 10.37 ns, mean is -6 ns, and range is -25 to
+10 ns. These agree with the [validation record](../validation.json).
All plotted values are internal PTP estimates, not calibrated measurements of
the physical PPS edge separation or UTC accuracy.

## Reproduce from the repository root

Use Python 3 with Matplotlib installed. The committed figures were generated
with Matplotlib 3.10.8.

```sh
python3 tools/plot_follower.py \
  docs/results/2026-10-13/power-cycle-recovery/pps-recovery-follower-20261013-01.log \
  --output-dir docs/results/2026-10-13/power-cycle-recovery/plots \
  --tail 60
```

The script writes both PNG and SVG figures, the extracted CSV, and
`SHA256SUMS`. The same command can plot a new follower capture by changing
the input path and output directory. Source logs are never modified.
