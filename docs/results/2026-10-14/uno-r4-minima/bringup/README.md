# Preliminary R4 bring-up attempts

These are retained development captures, **not the final servo comparison**.
They use the earlier R4 build with the original 500 ms first-exchange stepping
threshold. Their manifests contain those earlier firmware hashes; the final
build table does not describe these images.

| Attempt | PI samples captured | First offset | Last offset | Missing sample numbers | Reason stopped |
| --- | ---: | ---: | ---: | ---: | --- |
| trials-01 | 281 | -598,474,635 ns | 0 ns | 186 | Mac slept repeatedly during capture |
| trials-02 | 73 | -412,488,670 ns | -377,213,220 ns | 0 | Investigating slow saturated startup |
| trials-03 | 91 | -347,856,835 ns | -303,758,590 ns | 0 | Replaced by first-exchange threshold fix |

The first run reached s3 but cannot support an uninterrupted steady-state
claim. macOS power logs showed maintenance sleep at 17:31:24, 17:32:33 and
17:33:20 Pacific, with the last sleep lasting 150 seconds. The USB-powered
R4s continued generating samples while the sleeping host missed output. Python's
monotonic capture duration excluded sleep time, so board uptime spanned longer
than the requested capture duration. The final runner holds temporary system
and idle-sleep assertions and records to files independently of SSH.

The other two starts did not trigger the 500 ms coarse-step rule. The PI
output stayed at +500,000 ppb and removed only about 0.49 ms of offset per
second. Waiting 30 seconds before uploading the follower did not guarantee a
common initial TimeStick phase. The final R4 firmware instead uses a 1 ms
threshold on the first complete, path-validated exchange, with the 500 ms
threshold retained for subsequent exchanges. Servo equations and gains did
not change.

The raw manifests are preserved as written. Attempt 01 was interrupted with
SIGINT after it began linreg; the early runner did not mark KeyboardInterrupt
as failed. Attempt 02 inherited ignored SIGINT from its background shell and
was then stopped with SIGTERM. Attempt 03 was stopped with SIGTERM. These
unfinished manifests must not be interpreted as successful full sequences.
The current runner installs explicit signal handlers for graceful shutdown
and checks exclusive serial access before flashing either board.

[Parsed PI counts and endpoints](summary.json)
