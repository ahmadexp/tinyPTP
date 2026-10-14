# Boards Manager package validation

The `tinyptp:avr@0.1.0` platform adds the `tinyptp:avr:uno` board profile.
Its core and variant reference the separately installed official Arduino AVR
platform; its uploader references Arduino's avrdude recipe. The board settings
retain the UNO's ATmega328P, 16 MHz clock, 32,256-byte sketch limit, 2,048-byte
SRAM limit, and 115200-baud serial upload protocol. This is an installation
profile for the existing hardware, not a new core or bootloader.

The platform contains the exact library files from the published tinyPTP
0.1.0 ZIP, including all seven examples. The USB host library remains a separate
Library Manager installation. No installer scripts or bundled third-party
toolchains are added.

## Isolated installation check

The test used fresh Arduino data and sketchbook directories on PTPBox with
Arduino CLI 1.5.2-rc.1. Existing downloaded archives were reused as a cache,
but no previously installed core, library, or board profile was inherited.
The official `arduino:avr@1.8.8` core and USB Host Shield Library 2.0 version
1.7.0 were installed first. The custom index was served temporarily on loopback
with only its archive URL changed; the platform bytes and SHA-256 matched the
public release artifact. Boards Manager installation and example discovery
succeeded. Each bundled example compiled without custom compiler flags or an
explicit library path.

| Example | Flash bytes | Static SRAM bytes |
| --- | ---: | ---: |
| Grandmaster | 28,558 | 995 |
| FollowerPI | 28,556 | 995 |
| FollowerLinreg | 31,174 | 1,105 |
| FollowerAdaptivePI | 30,364 | 1,017 |
| FollowerKalman | 31,426 | 1,017 |
| FollowerPllFll | 29,674 | 1,016 |
| TimeStickProbe | 27,966 | 995 |

The resulting HEX hashes match the corresponding seven builds from the
[ZIP-library installation](../arduino-library/README.md). Expanded board
properties resolve the official core, standard UNO variant, and installed
avrdude tool. No uploads or live serial reads were performed. Neither lab UNO
was reflashed, and the graphical IDE itself was not exercised remotely.
Burn Bootloader is outside this package's configuration and validation scope.

## Evidence and reproduction

- [Validation metadata and HEX hashes](validation.json)
- [Example discovery](examples.log)
- [Expanded board properties](board-details.log)
- [Package installation](install-tinyptp.log)
- [Checksums](SHA256SUMS)
- [User installation instructions](../../../arduino-boards-manager.md)

The remaining `.log` files retain prerequisite installation and each compile.
Paths in the logs refer to the isolated test directories. Loopback URLs in the
pre-publication logs are temporary test endpoints, not installation URLs.

To recreate the distribution from the repository root, download the original
`tinyPTP-0.1.0.zip` release asset and run:

```sh
python3 tools/package_boards.py /path/to/tinyPTP-0.1.0.zip
```

The script verifies the immutable library ZIP hash before bundling it. It
writes the board archive, its checksum, and `package_tinyptp_index.json` with
the release URL, byte size, and SHA-256. Fixed ZIP timestamps make the archive
reproducible. Use the actual released library ZIP; regenerating that ZIP from
later documentation changes is intentionally rejected.
