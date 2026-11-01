#!/bin/bash
# Offset rootfs flash driver: flashes the 112 off_* sparse chunks (rootfs image
# placed at 5 GiB into userdata, entirely above the UFS wedge zone).
# Same self-healing pattern as flash_driver.sh: getvar liveness preflight before
# every chunk, progress file, idempotent retries.
W="/media/nicklas/New Volume1/stuff/Carmelian hacks/work"
FB="$W/tools/fastboot"
DIR="$W/chunks_offset5G"
PROG="$DIR/flash-progress.txt"
LOG="$W/flash-driver.log"
touch "$PROG"
total=112

# wait until all chunks are generated
while [ "$(ls "$DIR"/off_*.img.sparse 2>/dev/null | wc -l)" -lt "$total" ]; do sleep 10; done
echo "=== offset driver started $(date '+%H:%M:%S') total=$total" >> "$LOG"

fails=0
while true; do
  done_count=$(wc -l < "$PROG")
  [ "$done_count" -ge "$total" ] && { echo "=== ALL $total OFFSET CHUNKS DONE $(date '+%H:%M:%S')" >> "$LOG"; exit 0; }

  next=""
  for f in "$DIR"/off_*.img.sparse; do
    grep -q "^$(basename "$f")$" "$PROG" || { next="$f"; break; }
  done
  [ -z "$next" ] && { echo "=== ALL OFFSET CHUNKS DONE $(date '+%H:%M:%S')" >> "$LOG"; exit 0; }

  if ! timeout 5 "$FB" getvar product >/dev/null 2>&1; then sleep 2; continue; fi

  n=$(basename "$next")
  echo "[$(date '+%H:%M:%S')] flashing $n ($done_count/$total)" >> "$LOG"
  out=$(timeout 90 "$FB" flash userdata "$next" 2>&1); rc=$?
  if [ $rc -eq 0 ] && echo "$out" | grep -q "OKAY"; then
    echo "$n" >> "$PROG"; fails=0
    echo "[$(date '+%H:%M:%S')] $n OKAY" >> "$LOG"
  else
    fails=$((fails+1))
    echo "[$(date '+%H:%M:%S')] $n FAILED rc=$rc (consecutive fails: $fails)" >> "$LOG"
    echo "$out" | tail -2 >> "$LOG"
    [ "$fails" -ge 10 ] && { echo "!!! 10 consecutive failures - stopping $(date '+%H:%M:%S')" >> "$LOG"; exit 1; }
  fi
done
