# Oscilloscope USB export, 2026-10-13

Retrieved from the FNIRSI 2D15P on October 13, 2026, at approximately
17:23 Pacific (October 14, 00:23 UTC). This is the retrieval time;
the original acquisition time is unknown. Only one saved screenshot was present.

![Exported scope screen with trigger menu and no resolved PPS transitions](scope-screen.png)

This screenshot shows the trigger menu over a flat yellow trace. Its displayed
settings include 20 ms/div, 625 kSa/s, automatic triggering from CH2,
CH1 at 1 V/div and CH2 at 10 mV/div, both with X1 probe scaling.
It does **not** show resolved PPS edges and cannot establish synchronization,
PPS offset, or long-term stability. It is separate from the earlier
[photograph showing both PPS rising edges](../../../images/two-timestick-pps-oscilloscope.png).

## Original files

| File | Scope source | Description |
| --- | --- | --- |
| [1.bmp](1.bmp) | `Screenshot file/1.bmp` | Unmodified 480 x 272, 16-bit BMP screenshot, 261174 bytes |
| [1.bin](1.bin) | `Screenshot simple file/1.bin` | Unmodified 805-byte companion file; format not decoded |
| [scope-screen.png](scope-screen.png) | Converted from `1.bmp` | PNG preview at original resolution, without cropping or annotations |

The companion `.bin` is not established to contain raw waveform samples.
SHA-256 hashes of both original files were checked against the copies on the
scope's mounted volume. `SHA256SUMS` also covers the PNG preview.

## Export procedure verified on this scope

1. Display the desired acquisition, close menus, and press **Save**.
2. Open **Menu > USB Sharing**.
3. Press the gray **ON** button beneath the tabs. Selecting the blue USB Sharing
   tab alone opens the settings but does not activate file sharing.
4. On the connected Mac, the device changes from **USB Virtual COM** to
   **Mass Storage**. In this test it mounted as `/Volumes/NO NAME`.
5. Copy the saved files from `Screenshot file` and preserve their originals.
6. Eject the mounted volume before leaving sharing mode or unplugging USB.

The [manufacturer's manual, section 3.4.1](https://cdn.shopify.com/s/files/1/0694/8310/2426/files/2D15P_Manual_New_ZH_EN.pdf?v=1761637874)
describes Save as a screenshot function and USB Sharing as image export.
This export does not demonstrate remote live acquisition or raw ADC data access.

To obtain useful PPS evidence, a new saved acquisition must show both physical
PPS transitions with the menus closed. Record the channel-to-role mapping and
any cable or channel delay calibration before interpreting edge separation.
