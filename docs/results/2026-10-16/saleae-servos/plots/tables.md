# Derived Saleae campaign tables

All timing values are in nanoseconds. Physical values use qualified rising PPS pairs after 120 s from capture start.

| Condition | PPS pairs | PPS mean | PPS SD | PPS RMS | PPS min / max | Internal RMS |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Fixed PI | 177 | -5.58 | 8.23 | 9.94 | -26 / +22 | 15.70 |
| Linear regression | 170 | +1.87 | 5.93 | 6.22 | -12 / +24 | 8.04 |
| Adaptive PI | 155 | +4.36 | 9.18 | 10.16 | -20 / +20 | 9.28 |
| Kalman | 159 | +4.96 | 10.43 | 11.55 | -14 / +36 | 9.80 |
| PLL/FLL | 176 | -7.98 | 15.87 | 17.77 | -36 / +30 | 23.81 |
| Fixed PI, longer run | 758 | -8.31 | 37.53 | 38.44 | -92 / +134 | 38.16 |

## Pulse qualification, entire capture

These are edge and segment counts, not counts of validated seconds. Boundary partial pulses and startup changes can also be excluded.

| Condition | CH0 raw rising | CH1 raw rising | CH0 qualified pulses | CH1 qualified pulses | CH0 / CH1 rejected complete segments | CH0 / CH1 unmatched qualified rises | CH0 / CH1 short highs after cutoff |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Fixed PI | 293 | 297 | 289 | 288 | 4 / 9 | 5 / 4 | 0 / 3 |
| Linear regression | 294 | 307 | 289 | 279 | 4 / 27 | 15 / 5 | 0 / 9 |
| Adaptive PI | 294 | 332 | 289 | 258 | 4 / 73 | 36 / 5 | 0 / 25 |
| Kalman | 294 | 321 | 289 | 266 | 4 / 54 | 28 / 5 | 0 / 21 |
| PLL/FLL | 293 | 299 | 289 | 286 | 4 / 13 | 8 / 5 | 0 / 4 |
| Fixed PI, longer run | 894 | 917 | 889 | 870 | 4 / 46 | 24 / 5 | 0 / 22 |

Short highs are recorded high segments below 1 microsecond, not a count of every possible disturbance. All rejected segments and edge timestamps are listed in each trial summary.

## Startup observations

Serial times below are from the first accepted exchange. Physical times are from Saleae capture start. A physical final suffix requires at least 30 s of in-band observations, no gap greater than 1.5 s, and observations through the capture end. ND means not demonstrated, including because pulse exclusions interrupt the observable record.

| Condition | Initial serial offset | Serial first s3 (s) | Physical final +/-1000 ns suffix start (s) | Physical final +/-100 ns suffix start (s) |
| --- | ---: | ---: | ---: | ---: |
| Fixed PI | -105707995 | 52.00 | ND | ND |
| Linear regression | -662142330 | 34.99 | ND | ND |
| Adaptive PI | -453633365 | 35.00 | ND | ND |
| Kalman | -483422655 | 31.99 | ND | ND |
| PLL/FLL | -365495325 | 33.99 | 267.08 | 267.08 |
| Fixed PI, longer run | -480040940 | 51.99 | ND | ND |
