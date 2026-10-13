#!/bin/sh
# Build separate, automatically starting grandmaster and follower UNO images.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cli=${ARDUINO_CLI:-arduino-cli}
build_dir=${BUILD_DIR:-"$root/build/roles"}
set -- "$cli"
if [ -n "${ARDUINO_CONFIG:-}" ]; then
  set -- "$@" --config-file "$ARDUINO_CONFIG"
fi
for role in 1 2; do
  if [ "$role" = 1 ]; then name=grandmaster; else name=follower; fi
  "$@" compile --fqbn arduino:avr:uno \
    --build-property "compiler.cpp.extra_flags=-DTINYPTP_BOOT_ROLE=$role" \
    --build-path "$build_dir/$name" "$root/examples/TimeStickProbe"
done
