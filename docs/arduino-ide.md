# Install and upload with Arduino IDE

Use **Arduino IDE 2**, a **classic Arduino UNO R3 (ATmega328P, 16 MHz)**,
a MAX3421E USB Host Shield, and a supported **AX88279 TimeStick** per node.
UNO R4, UNO WiFi Rev2, and the AX88279A variant are not supported by this package.
The tested shield power jumpers must be connected; see the
[hardware photographs](https://github.com/ahmadexp/tinyPTP#usb-host-shield-power-jumpers).

## Install once

1. In **Boards Manager**, install **Arduino AVR Boards**. The recorded test
   configuration uses version **1.8.8**.
2. In **Library Manager**, search for **USB Host Shield Library 2.0** and install
   version **1.7.0**. This is the MAX3421E driver, not an Ethernet Shield library.
3. Download [tinyPTP-0.1.0.zip](https://github.com/ahmadexp/tinyPTP/releases/download/v0.1.0/tinyPTP-0.1.0.zip).
4. Select **Sketch → Include Library → Add .ZIP Library**, and select that ZIP.
5. Open **File → Examples → tinyPTP**. If the examples are not visible, restart
   the IDE. On some versions they appear under the custom-library section.

The repository's **Code → Download ZIP** archive is also installable; it
contains the same `library.properties`, `src`, and `examples` layout, plus the
larger measurement archive. The release ZIP contains only the library, examples,
and this guide. tinyPTP is distributed by ZIP and is **not currently listed in
Arduino Library Manager**. Install the USB host dependency explicitly before
using the ZIP workflow; do not assume Add .ZIP Library installs dependencies.

Arduino documents this [ZIP installation workflow](https://support.arduino.cc/hc/en-us/articles/5145457742236-Install-libraries-in-the-Arduino-IDE).
The package follows the [Arduino library layout](https://docs.arduino.cc/arduino-cli/library-specification),
including dependency metadata and examples under `examples/`.

## Upload the two boards

1. Open **File → Examples → tinyPTP → Grandmaster**.
2. Select **Arduino Uno** as the board and the first UNO's port. Click **Upload**.
3. Open **File → Examples → tinyPTP → FollowerPI**.
4. Select the second UNO's port and click **Upload**. The two boards have the
   same board type; confirm their ports before each upload. Connecting one UNO
   at a time can make identification easier.
5. Power both assemblies and connect the TimeSticks directly by Ethernet.
6. Open each board's **Serial Monitor at 115200 baud**. Each sketch starts its
   role automatically; no terminal script or serial start command is needed.

The first board reports `boot role GRANDMASTER`, then MASTER status with
`sync` and `delay_resp` counters. The second reports `boot role FOLLOWER` and
`servo PI`, then `master offset`, `freq`, and `path delay`. Follower `s3` means
eight consecutive offsets inside +/-1,000 ns. Wait for acquisition; it is not
instantaneous. Opening Serial Monitor can reset an UNO and restart acquisition.
Only one program should use a serial port at a time.

The grandmaster is free-running with an arbitrary timescale, not a UTC
reference. The firmware configures PPS on both TimeSticks, but follower PPS
continues during acquisition or loss of synchronization. Check serial state
and counters before interpreting it as a disciplined output.

## Choose an example

| Example (.ino) | Role | Purpose |
| --- | --- | --- |
| `Grandmaster` | Grandmaster | Automatic 1 Hz PTP master and status output |
| `FollowerPI` | Follower | Fixed PI, recommended starting point |
| `FollowerLinreg` | Follower | Experimental eight-point linear regression |
| `FollowerAdaptivePI` | Follower | Experimental bounded PI gain scheduling |
| `FollowerKalman` | Follower | Experimental two-state Kalman filter |
| `FollowerPllFll` | Follower | Experimental fixed-point PLL/FLL |
| `TimeStickProbe` | Diagnostic | Interactive USB, PHY, clock, PPS, and PTP diagnostics |

Switch servos by uploading a different follower example. The grandmaster
example stays the same. All examples set their role and servo in the `.ino`;
there is no need to edit an installed library header or supply compiler flags.
The diagnostic example does not start Ethernet/PTP automatically. Use the
[diagnostic command guide](https://github.com/ahmadexp/tinyPTP#first-hardware-check)
when investigating hardware rather than starting with it for normal PTP use.

Experimental servos are not guaranteed improvements. See the
[servo results](https://github.com/ahmadexp/tinyPTP#startup-transients-for-every-servo)
for measured acquisition and steady-state behavior.

## What an example contains

```cpp
#define TINYPTP_BOOT_ROLE 2  // 1 grandmaster, 2 follower, 0 diagnostic
#define TINYPTP_SERVO 0      // 0 PI, 1 linreg, 2 adaptive PI, 3 Kalman, 4 PLL/FLL
#include <TinyPTP.h>

void setup() {
  TinyPTP::begin();
}

void loop() {
  TinyPTP::poll();
}
```

`begin()` configures serial and the USB host. `poll()` services USB, automatic
startup, PTP exchanges, and serial commands. Call it frequently, without long
application delays. Startup and diagnostic operations can block. The library
owns the serial output, USB host, and shield SPI usage; it is not a general
Ethernet socket library or a scheduler for arbitrary real-time application code.

This first library package includes its implementation in the sketch's
translation unit, allowing role and servo settings above the include to remove
unused code at compile time. Include `TinyPTP.h` only from the main `.ino`, not
from additional `.cpp` files. The library has one global USB/PTP instance. The
internal headers in `src/detail/` are implementation details, not a stable API.
Memory is tight on an UNO, especially with linreg and Kalman; check the compiler's
flash/SRAM report before adding application code. Runtime stack use is unmeasured.

## Troubleshooting and updates

- **`TinyPTP.h: No such file or directory`**: install the tinyPTP ZIP and reopen
  the example. Copying an `.ino` alone does not install the library.
- **`Usb.h: No such file or directory`**: install USB Host Shield Library 2.0.
- **Wrong processor error**: select the classic **Arduino Uno**, not UNO R4.
- **Upload failure**: check board/port selection and close any other serial
  reader. A successful compile does not mean the correct board was uploaded.
- **Repeated USB/PHY/clock initialization errors**: check shield jumper wiring,
  power, cable, and chip identity. Restarting the serial monitor does not remove
  TimeStick power. Full assembly power cycling has recovered previous failures;
  recovery from arbitrary reset/upload order remains imperfect.
- **Updating an installed copy**: replace the old tinyPTP library folder in the
  Arduino sketchbook's `libraries` directory before installing a new ZIP if the
  IDE reports an existing installation. Keep only one installed copy to avoid
  compiling against an older version. Save customized sketches outside the
  installed library directory.

## Command-line equivalent for developers

The IDE is the normal installation path. A repository checkout can also be
compiled directly without copying it into a sketchbook:

```sh
arduino-cli compile --fqbn arduino:avr:uno --library "$PWD" examples/Grandmaster
arduino-cli compile --fqbn arduino:avr:uno --library "$PWD" examples/FollowerPI
python3 tools/package_library.py
```

`tools/build_roles.sh` retains the original two-role build workflow and accepts
`SERVO=pi`, `linreg`, `auto_pi`, `kalman`, or `pllfll`. Historical result logs and
firmware hashes describe the earlier source layout; packaging does not turn
compile checks into new hardware measurements.
