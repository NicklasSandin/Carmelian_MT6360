#!/bin/bash
# Continuation: flash part_044's range as 4x25MiB sub-chunks on the next live
# bootloader, then hand over to the main driver for part_045+.
W="/media/nicklas/New Volume1/stuff/Carmelian hacks/work"
FB="$W/tools/fastboot"
PROG="$W/chunks100/flash-progress.txt"
LOG="$W/flash-driver.log"

echo "=== continuation armed $(date '+%H:%M:%S'), waiting for live bootloader" >> "$LOG"
until timeout 5 "$FB" getvar product >/dev/null 2>&1; do sleep 3; done
echo "=== live $(date '+%H:%M:%S')" >> "$LOG"

for s in s44_0 s44_1 s44_2 s44_3; do
  out=$(timeout 90 "$FB" flash userdata "$W/subchunks/$s.img.sparse" 2>&1); rc=$?
  if [ $rc -eq 0 ] && echo "$out" | grep -q "OKAY"; then
    echo "[$(date '+%H:%M:%S')] $s OKAY" >> "$LOG"
  else
    echo "[$(date '+%H:%M:%S')] $s FAILED rc=$rc — region cursed at 25MiB too" >> "$LOG"
    echo "$out" | tail -2 >> "$LOG"
    exit 1
  fi
done
echo "part_044.img.sparse" >> "$PROG"
echo "[$(date '+%H:%M:%S')] part_044 covered by sub-chunks — resuming main driver" >> "$LOG"
exec bash "$W/flash_driver.sh"
