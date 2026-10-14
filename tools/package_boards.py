#!/usr/bin/env python3
"""Bundle the released library in a Boards Manager platform and build its index.

Pass the immutable tinyPTP 0.1.0 library ZIP, not a regenerated development ZIP.
The platform references the separately installed official Arduino AVR core.
"""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

LIBRARY_SHA256 = "97dcb6a4d9598edbd49fe5e853365d9abc7cf2ed020869cf8a5829f6258c002e"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("library_zip", type=Path)
    parser.add_argument("--output-dir", type=Path, default=Path("build/boards-manager"))
    parser.add_argument("--index", type=Path, default=Path("package_tinyptp_index.json"))
    args = parser.parse_args()
    if hashlib.sha256(args.library_zip.read_bytes()).hexdigest() != LIBRARY_SHA256:
        parser.error("Expected the published tinyPTP-0.1.0.zip (SHA-256 mismatch)")
    root = Path(__file__).resolve().parents[1]
    files = {p.name: p.read_bytes() for p in (root / "arduino/avr").glob("*.txt")}
    with zipfile.ZipFile(args.library_zip) as library:
        for name in library.namelist():
            files["libraries/" + name] = library.read(name)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    archive = args.output_dir / "tinyPTP-avr-0.1.0.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as output:
        for name, data in sorted(files.items()):
            info = zipfile.ZipInfo("tinyPTP-avr/" + name, date_time=(2026, 10, 14, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            output.writestr(info, data)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    index = {"packages": [{
        "name": "tinyptp", "maintainer": "ahmadexp", "email": "ahmadexp@gmail.com",
        "websiteURL": "https://github.com/ahmadexp/tinyPTP",
        "platforms": [{
            "name": "tinyPTP UNO", "architecture": "avr", "version": "0.1.0",
            "category": "Contributed",
            "url": "https://github.com/ahmadexp/tinyPTP/releases/download/boards-v0.1.0/" + archive.name,
            "archiveFileName": archive.name, "checksum": "SHA-256:" + digest,
            "size": str(archive.stat().st_size),
            "boards": [{"name": "tinyPTP UNO (ATmega328P)"}], "toolsDependencies": [],
            "help": {"online": "https://github.com/ahmadexp/tinyPTP/blob/main/docs/arduino-boards-manager.md"},
        }], "tools": [],
    }]}
    args.index.parent.mkdir(parents=True, exist_ok=True)
    args.index.write_text(json.dumps(index, indent=2) + "\n")
    archive.with_suffix(".zip.sha256").write_text(digest + "  " + archive.name + "\n")
    print(f"{archive}: {len(files)} files; {args.index}")


if __name__ == "__main__":
    main()
