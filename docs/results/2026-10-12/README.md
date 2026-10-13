# Test evidence, 2026-10-12

These are the recorded PTPBox and Arduino diagnostic outputs from the first
hardware session. Read the [bring-up history](../../bringup-2026-10-12.md) and
[PTP report](../../ptp-test-2026-10-12.md) for interpretation. Failure logs are
historical evidence, not the expected output of the final firmware.

## Successful PTP datasets

| Dataset | Raw log | Samples | Summary window | Derived data |
| --- | --- | ---: | --- | --- |
| Principal servo run | [ptp-servo-01.log](logs/ptp-servo-01.log) | 131 | Last 40, samples 92-131 | [CSV](ptp-servo-01-samples.csv), [JSON](ptp-servo-01-summary.json) |
| Final firmware restart | [ptp-final-check-02.log](logs/ptp-final-check-02.log) | 86 | Last 10, samples 77-86 | [CSV](ptp-final-check-02-samples.csv), [JSON](ptp-final-check-02-summary.json) |

The principal window has mean offset -6.375 ns, RMS 13.125 ns, and range
-29 to +22 ns. The shorter restart window has mean +39.6 ns, RMS 42.24 ns,
and range +13 to +59 ns. Both are internal PTP estimates; no independent PPS
or UTC measurement was performed. Windows were selected after acquisition.
The restart window was still finishing acquisition. Do not combine the two
runs as one continuous dataset or interpret these windows as startup bounds.

CSV columns preserve the integer values from complete `PTP sample=...`
records. `offset_ns` is client minus master; `path_ns` is the estimated mean
path delay. `logged_freq_ppb` is the correction in effect before the row's
servo update. Every complete servo sample, including initial acquisition and
coarse-step samples, is included in CSV. The JSON summary uses only the stated
tail window for offset/path statistics, but counts exchanges and diagnostic
events across the full capture. RMS is sqrt(mean(offset squared)), not the
standard deviation about the mean.

## Log inventory

| Files under [logs/](logs/) | Purpose |
| --- | --- |
| `probe-20261012.log`, `probe-registers-20261012.log` | Initial shield initialization and SPI register failure |
| `retest-02.log`, `retest-03.log` | Repeated all-zero controller registers |
| `retest-04.log` | Controller responds, device still detached |
| `retest-05.log`, `asix-controls-01.log` | Original stick enumeration and controls; historical chip-code mask error |
| `init-01.log` through `init-03.log` | Original stick initialization attempts |
| `replacement-init-01.log`, `replacement-controls-02.log`, `replacement-init-03.log` | Replacement identity, initial stalls, running clock |
| `replacement-ethernet-01.log`, `replacement-ethernet-02.log` | Early speed/duplex validation failures |
| `replacement-ethernet-03.log`, `replacement-peer-03.log` | Verified ten-frame exchange from both ends |
| `ptp-observe-01.log` through `ptp-observe-06.log` | Timestamp and initialization diagnosis |
| `ptp-observe-07.log` | Four-timestamp exchange without discipline, 14 samples |
| `ptp-master-02.log`, `ptp-master-03.log` | Linux master output during experiments |
| `ptp-servo-01.log` | Principal servo dataset |
| `ptp-final-check.log` | Restart rejected invalid `DEADBEEF` register readback |
| `ptp-final-check-02.log` | Successful final restart after settling check |
| `ptp-launcher-check.log` | Five-second bounded launcher smoke test |

The older logs label chip status `F0` as code 7 because that probe used mask
`0x70`. The corrected mask is `0xF0`, giving code 15, AX88279A. Raw logs have
not been rewritten to conceal that error. The working replacement is code 7,
AX88279, status `74`.

## Provenance and integrity

Logs are byte-for-byte copies of the captured working files, retaining CRLF
serial output and any partial final line caused by the capture deadline. Their
UTC headers record the capture start where present; `ptp4l` bracketed times
are its own log timestamps and must not be read as UTC capture headers.
The serial logger decodes non-ASCII bytes with escaped representations, so
these files are diagnostic text captures, not binary USB traces or packet
captures. Some `ptp4l` output may have been outside the redirected stream;
these files do not constitute a complete host audit.

Firmware evolved during the session. Final source and tests are recorded in
commit `0bf9e78`; the principal servo run preceded the final startup-settling
change, while `ptp-final-check-02.log` tests the final firmware. There is no
per-attempt binary/source snapshot for every intermediate run. Firmware size
in the report applies to the final uploaded image, not every historical probe.
The original Arduino application backup and its pre-probe serial output are
not part of this evidence archive. The empty first master log is also omitted.

[SHA256SUMS](SHA256SUMS) covers the 30 logs and four derived CSV/JSON files.
From this directory, verify with `sha256sum -c SHA256SUMS` on Linux or
`shasum -a 256 -c SHA256SUMS` on macOS.

From the repository root, reproduce the summaries:

```sh
python3 tools/summarize_ptp.py \
  docs/results/2026-10-12/logs/ptp-servo-01.log --tail 40
python3 tools/summarize_ptp.py \
  docs/results/2026-10-12/logs/ptp-final-check-02.log --tail 10
```

The CSVs can be reconstructed from each corresponding raw log with Python's
standard library, replacing `name` for the restart run:

```python
from pathlib import Path
import csv
import re

root = Path('docs/results/2026-10-12')
name = 'ptp-servo-01'
rows = re.findall(
    r'PTP sample=(\d+) seq=(\d+) offset_ns=(-?\d+) path_ns=(-?\d+) '
    r'freq_ppb=(-?\d+) servo=on',
    (root / 'logs' / (name + '.log')).read_text(),
)
with Path('/tmp/' + name + '-samples.csv').open('w', newline='') as output:
    writer = csv.writer(output)
    writer.writerow(['sample', 'sequence', 'offset_ns', 'path_ns', 'logged_freq_ppb'])
    writer.writerows(rows)
```
