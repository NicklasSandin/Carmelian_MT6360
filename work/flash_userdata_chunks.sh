#!/bin/bash
# Flash all rootfs sparse chunks (100 MiB each) to userdata.
# - Each chunk = one fastboot download round, no resparse.
# - Progress tracked in flash-progress.txt -> fully resumable.
# - On failure/wedge: "fastboot reboot bootloader" resets the phone's bootloader
#   (fresh USB/RAM state), then retries the same chunk (idempotent).
W="/media/nicklas/New Volume1/stuff/Carmelian hacks/work"
FB="$W/tools/fastboot"
DIR="$W/chunks100"
PROG="$DIR/flash-progress.txt"
LOG="$W/flash-chunks.log"
touch "$PROG"
total=$(ls "$DIR"/part_*.img.sparse 2>/dev/null | wc -l)
echo "=== chunk flash v2 started $(date '+%H:%M:%S') total=$total" | tee -a "$LOG"

wait_fb() {
  for w in $(seq 1 60); do
    "$FB" devices 2>/dev/null | grep -q fastboot && return 0
    sleep 3
  done
  return 1
}

done_count=0
for f in "$DIR"/part_*.img.sparse; do
  n=$(basename "$f")
  if grep -q "^$n$" "$PROG"; then done_count=$((done_count+1)); continue; fi
  ok=0
  for try in 1 2 3 4; do
    if ! wait_fb; then
      echo "!!! no fastboot device — plug the phone in fastboot mode; waiting $(date '+%H:%M:%S')" | tee -a "$LOG"
      wait_fb || true
    fi
    echo "=== $n attempt $try $(date '+%H:%M:%S') [$done_count/$total done]" | tee -a "$LOG"
    out=$(timeout 90 "$FB" flash userdata "$f" 2>&1); rc=$?
    [ -n "$out" ] && echo "$out" | tail -2 | tee -a "$LOG"
    if [ $rc -eq 0 ] && echo "$out" | grep -q "OKAY"; then
      echo "$n" >> "$PROG"; done_count=$((done_count+1)); ok=1; break
    fi
    echo "=== $n failed rc=$rc — resetting bootloader (reboot bootloader)" | tee -a "$LOG"
    timeout 30 "$FB" reboot bootloader 2>&1 | tail -1 | tee -a "$LOG"
    sleep 15
    wait_fb || true
  done
  if [ $ok -ne 1 ]; then
    echo "!!! $n exhausted retries — STOP. Power-cycle the phone into fastboot (hold Power, then Power+Vol-), rerun this script; it resumes automatically." | tee -a "$LOG"
    exit 1
  fi
done
echo "=== ALL $total CHUNKS FLASHED $(date '+%H:%M:%S')" | tee -a "$LOG"
