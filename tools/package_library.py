#!/usr/bin/env python3
"""Build a small ZIP for Arduino IDE's Add .ZIP Library command."""
import argparse
from pathlib import Path
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    version = next(line.split('=', 1)[1] for line in
                   (root / 'library.properties').read_text().splitlines()
                   if line.startswith('version='))
    if args.output is None:
        args.output = root / 'build' / f'tinyPTP-{version}.zip'
    files = [root / "library.properties", root / "docs/arduino-ide.md",
             root / "docs/uno-r4-minima.md"]
    files += sorted((root / "src").rglob("*.h"))
    files += sorted((root / "examples").glob("*/*.ino"))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, "w", zipfile.ZIP_DEFLATED) as archive:
        for path in files:
            relative = "README.md" if path.name == "arduino-ide.md" else path.relative_to(root).as_posix()
            info = zipfile.ZipInfo("tinyPTP/" + relative, date_time=(2026, 10, 14, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, path.read_bytes())
    print(f"{args.output}: {len(files)} files, {args.output.stat().st_size} bytes")


if __name__ == "__main__":
    main()
