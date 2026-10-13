# Two-UNO test evidence, 2026-10-13

Read the [hardware report](../../two-uno-test-2026-10-13.md) for equipment,
protocol details, measurements, and limits.

- [Master log](logs/pair-master-04.log) and [follower log](logs/pair-follower-04.log):
  the completed 240-second run, including initialization and 228 complete
  exchanges. Both show nominal-frequency writes at the clean stop.
- [Per-sample CSV](pair-follower-04-samples.csv): all 228 complete servo records,
  including acquisition. `logged_freq_ppb` is the control value before that
  sample's servo update.
- [JSON summary](pair-follower-04-summary.json): offset/path statistics for the
  final 60 samples, plus full-capture exchange and event counts.
- [Diagnostic logs](logs/): identification of both sticks and the first three
  failed attempts before constraining PHY advertisement to 1 Gb/s full duplex.
  Those files record earlier firmware stages, not the final operating behavior.
- [SHA-256 checksums](SHA256SUMS): integrity of all ten logs and both derived
  data files. Verify from this directory with `sha256sum -c SHA256SUMS` on Linux
  or `shasum -a 256 -c SHA256SUMS` on macOS.

The logs are byte-for-byte copies of the saved diagnostic text, preserving
CRLF serial output and any partial final lines. They are not packet captures
or binary USB traces. Intermediate attempts were not each preserved as a
separate source/binary revision. The final report records the uploaded image
hash and build sizes. Original Arduino flash backups remain private on the
test host and local working checkout.

From the repository root, reproduce the numeric summary:

```sh
python3 tools/summarize_ptp.py \
  docs/results/2026-10-13/logs/pair-follower-04.log --tail 60
```

The continuous session started after this bounded run is logged separately
on PTPBox. It does not append to the archived dataset. Internal PTP offset
estimates are not independent PPS or UTC accuracy measurements.
