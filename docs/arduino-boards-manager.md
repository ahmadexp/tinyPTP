# Install tinyPTP using a Boards Manager JSON URL

The GitHub repository is currently **private**. Arduino Boards Manager cannot
use your browser's signed-in GitHub session, so the raw GitHub index and release
asset URLs return 404 to it. The package has been built and tested; use the
local method below until publicly accessible hosting is arranged.

## Private repository: localhost JSON method

1. While signed in to GitHub, download **tinyPTP-avr-0.1.0.zip** from the
   [board package release](https://github.com/ahmadexp/tinyPTP/releases/tag/boards-v0.1.0).
   This is the board ZIP, not the separate `tinyPTP-0.1.0.zip` library ZIP.
2. From a checkout of this repository, run:

   ```sh
   python3 tools/serve_boards.py /path/to/tinyPTP-avr-0.1.0.zip
   ```

3. Keep that terminal open and add this URL to **Additional Boards Manager URLs**:

   ```text
   http://127.0.0.1:8765/package_tinyptp_index.json
   ```

4. Follow the installation steps below. Stop the server with Ctrl-C after
   installation; installed examples compile without the server running.

The helper checks the archive hash and serves only the JSON and matching ZIP,
on the local computer only. It needs Python 3, but no external Python packages
or GitHub token. The IDE and server must run on the same computer. Use `--port`
if 8765 is occupied, and copy the URL it prints. Restart it for reinstalls or
updates; remove the localhost URL from IDE preferences when no longer needed
to avoid index-refresh errors while the server is stopped.

## Hosted index (requires public access)

The intended hosted URL is shown below, but **it will not work in Boards
Manager while the repository and its release assets remain private**. Both the
index and the archive it references must be accessible without authentication.
Do not paste GitHub access tokens into the URL.


```text
https://raw.githubusercontent.com/ahmadexp/tinyPTP/main/package_tinyptp_index.json
```

This installs a **tinyPTP UNO** board profile with the tinyPTP library and all
seven examples bundled inside it. It targets the same classic UNO R3 hardware;
it does not require a different board or a new bootloader. The separate
[ZIP-library method](arduino-ide.md) remains available.

## Install in Arduino IDE 2

1. In **Boards Manager**, install **Arduino AVR Boards**, version **1.8.8**.
   The tinyPTP profile uses that official core, standard UNO pin mapping, and
   uploader. Installing tinyPTP alone does not install the referenced core.
2. In **Library Manager**, install **USB Host Shield Library 2.0**, version
   **1.7.0**. The board package bundles tinyPTP, not the USB host dependency.
3. Open **File → Preferences** on Windows/Linux, or **Arduino IDE → Settings**
   on macOS. Add the **localhost URL** (or an accessible hosted index) to
   **Additional Boards Manager URLs**. Preserve
   any URLs already listed, then save the settings.
4. Open **Boards Manager**, search **tinyPTP UNO**, and install version **0.1.0**.
5. Select **Tools → Board → tinyPTP UNO → tinyPTP UNO (ATmega328P)** and choose
   the UNO's serial port. Select this profile manually even if automatic port
   detection suggests the ordinary Arduino Uno profile.
6. Open **File → Examples → tinyPTP → Grandmaster** and upload it to the first
   UNO. Then open **FollowerPI**, select the second UNO's port, and upload it.
7. Connect the TimeSticks directly by Ethernet and open Serial Monitor at
   **115200 baud**. The assigned roles start automatically.

Choose **tinyPTP UNO (ATmega328P)** before looking for the examples. The
bundled library is available for this board profile; selecting the ordinary
**Arduino Uno** profile uses the ZIP/Library Manager library locations instead.
Do not select UNO R4 or UNO WiFi Rev2.

The Arduino platform mechanism supports
[referencing an installed core and bundling libraries](https://arduino.github.io/arduino-cli/dev/platform-specification/#referencing-another-core-variant-or-tool).
The URL follows Arduino's
[Boards Manager index format](https://docs.arduino.cc/arduino-cli/package_index_json-specification).
It is a board-package index, not a custom Library Manager catalog.

## Available examples

| Example | Purpose |
| --- | --- |
| Grandmaster | Automatic free-running grandmaster |
| FollowerPI | Fixed PI follower, recommended starting point |
| FollowerLinreg | Experimental linear-regression follower |
| FollowerAdaptivePI | Experimental adaptive PI follower |
| FollowerKalman | Experimental Kalman follower |
| FollowerPllFll | Experimental PLL/FLL follower |
| TimeStickProbe | Interactive diagnostics, no automatic PTP startup |

The role and servo are set in each `.ino`. No edits to board configuration or
compiler flags are needed. The grandmaster is not a UTC source. See the
[installation and hardware guide](arduino-ide.md) for supported AX88279 sticks,
shield power jumpers, serial output, PPS behavior, and reset/recovery limitations.

## Updating and switching installation methods

Boards Manager checks the index for published platform versions. Updating
**tinyPTP UNO** updates its bundled tinyPTP library and examples together. It
does not update the firmware on the UNOs until you click Upload.

If tinyPTP is already installed from ZIP in the sketchbook's `libraries`
directory, Arduino may prefer that copy over the board-bundled library. Keep
customized sketches elsewhere, then remove the old ZIP-installed tinyPTP folder
when switching to this method. Keep USB Host Shield Library 2.0 installed. The
compiler's verbose output shows which library location was selected.

If the package is missing from search, check the full raw JSON URL and refresh
Boards Manager. If compilation cannot find `arduino:arduino`, install Arduino
AVR Boards. If it cannot find `Usb.h`, install the USB host library. If the
examples are missing, select the tinyPTP board profile and reopen the menu.

The package is intended for normal sketch upload using the UNO's existing
bootloader. Use the official Arduino Uno board profile for **Burn Bootloader**
or programmer-specific maintenance; those operations are not configured here.
No installer scripts run, and the package does not change other installed
Arduino board definitions.

## Arduino CLI equivalent

With the local server running:

```sh
arduino-cli core update-index --additional-urls \
  http://127.0.0.1:8765/package_tinyptp_index.json
arduino-cli core install arduino:avr@1.8.8
arduino-cli lib install 'USB Host Shield Library 2.0@1.7.0'
arduino-cli core install tinyptp:avr@0.1.0 --additional-urls \
  http://127.0.0.1:8765/package_tinyptp_index.json
arduino-cli lib examples --fqbn tinyptp:avr:uno tinyPTP
```

Compile a copied example with `arduino-cli compile --fqbn tinyptp:avr:uno
/path/to/Grandmaster`. The fully qualified board name differs from the
ZIP-library method's `arduino:avr:uno`; the processor, clock rate, pin mapping,
and serial upload speed are the same.

[Package index](../package_tinyptp_index.json)
| [Platform release](https://github.com/ahmadexp/tinyPTP/releases/tag/boards-v0.1.0)
| [Installation and build validation](results/2026-10-14/boards-manager/README.md)
