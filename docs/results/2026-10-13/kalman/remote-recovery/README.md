# Remote recovery attempts

On October 13, 2026 (Pacific), recovery was attempted over SSH because nobody
was at the hardware. These are diagnostic captures, not servo benchmarks.
Kalman was not uploaded during these attempts.

PTPBox's USB 2 root hub reports `wHubCharacteristic 0x000a`, with **no power
switching**. The grandmaster is on physical USB port `1-6`, and the follower
on `1-5`. Only these two ports were disabled and re-enabled through the Linux
per-port `disable` attributes, with a five-second interval and restoration in
a `finally` block. Both serial devices re-enumerated. This confirms a USB
reconnect, not removal of 5 V from the assemblies. Both port attributes were
read back as `0` afterward. The other USB device and Ethernet host interface
were not reset.

| Capture suffix | Action | Observed outcome |
| --- | --- | --- |
| `02` | Stop both automatic retry loops; enable diagnostics; initialize both Ethernet interfaces together; request fixed roles | Both PHYs timed out establishing link; no accepted PTP exchange |
| `03` | Capture original firmware after the host USB-port reconnect | Grandmaster clock reads returned USB STALL (`0x05`); follower initialized briefly, then lost link; no accepted exchange |
| `04` | Temporary firmware adds standard Clause 22 BMCR software reset before link setup | No recovery; link timeouts and USB errors persisted |
| `05` | Temporary interactive firmware sends the vendor driver's device-reset command, then re-enumerates and attempts paired initialization | Reset transfers returned `0x0e`; devices enumerated again, but Ethernet initialization still timed out |
| `06` | Restore the saved original grandmaster and fixed-PI follower firmware; capture both for 65 seconds | PTP exchange recovered; 51 accepted samples, no initialization failures, and zero reported exchange counters |

The `02` capture begins with buffered bytes from the previous session before
its new boot marker. Opening the serial ports reset the UNOs despite setting
DTR false before opening. Do not treat those prefixed bytes as observations
from the subsequent manual initialization. Logs are preserved unedited.

The PHY-reset experiment wrote `0x8000` to Clause 22 register 0 and waited for
the reset bit to clear before continuing normal initialization. The device
reset used request `0x2a`, value `0xaa00`, index `0`, and one data byte `0x41`,
matching `ax88179a_sw_reset` in the [TimeStick vendor driver](https://github.com/Time-Appliances-Project/TimeStick/blob/master/TimeStick2/DRV/ax88179a_772d.c).
Neither experiment writes EEPROM, eFuse, or TimeStick firmware. Temporary
UNO diagnostic builds were removed by restoring the saved images.

## Firmware hashes

SHA-256 of the Intel HEX files used for uploads:

| Image | SHA-256 |
| --- | --- |
| Restored grandmaster | `93d38313c683fd9fae9aefb4f0e1c2043097f5706cb7f92a928fa5617a2c12b0` |
| Restored fixed-PI follower | `facc9dab08bb78738e3c5bfca7672bd7855578b6e000e3a17662ada07bf46900` |
| Temporary PHY-reset grandmaster | `c9c27995742caf6cb64122cfe1cfdce60acb872d6054f053e41aa10fcdc499bf` |
| Temporary PHY-reset follower | `e344ef821d1de8bec55e518b502c5c746c83bb9aadc07ea15583ea883fdbe697` |
| Temporary interactive device-reset image, both UNOs | `1a39178d8753a3d7312861ffe2330d0383183e9515a593bea3ac1d8f1d30f627` |

The USB host's power-switching limitation is consistent with the
[uhubctl documentation](https://github.com/mvp/uhubctl#what-is-usb-per-port-power-switching).
A software disconnect is not evidence of a cold power cycle. Recovery occurred
after the sequence of software-reset experiments and restoration of the original
firmware, without physical intervention. The individual action responsible for
recovery and the underlying startup fault have not been isolated.
