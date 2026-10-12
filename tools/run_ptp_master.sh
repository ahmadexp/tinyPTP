#!/bin/sh
# Bounded PTPBox test, separate from existing linuxptp services.
set -eu
seconds=${1:-180}
case "$seconds" in ''|*[!0-9]*) echo 'Duration must be positive integer seconds' >&2; exit 2;; esac
[ "$seconds" -gt 0 ] || exit 2
[ "$(id -u)" -eq 0 ] || { echo 'Run this script with sudo on PTPBox' >&2; exit 1; }
[ "$(cat /sys/class/net/enp179s0f1/address)" = '0c:9d:92:c5:83:32' ] || {
  echo 'Test NIC identity does not match PTPBox' >&2; exit 1;
}
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
install -d /run/timemaster
exec 9>/run/timemaster/tinyptp-master.lock
flock -n 9 || { echo 'Another tinyPTP master test is running' >&2; exit 1; }
# These paths fit the installed AppArmor profile. No security policy changes.
runtime_config=/run/timemaster/ptp4l.tinyptp.conf
install -m 644 "$script_dir/../configs/ptpbox-master.conf" "$runtime_config"
trap 'rm -f "$runtime_config"' EXIT
result=0
timeout --signal=TERM "$seconds" ptp4l -f "$runtime_config" -m -q || result=$?
[ "$result" -eq 124 ] && exit 0
exit "$result"
