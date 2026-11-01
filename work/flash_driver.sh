#!/bin/bash
# Self-healing userdata flash driver.
# - Probes liveness with `getvar` (5s cap) before EVERY chunk: a dead or
#   idle-rotted bootloader is never attempted.
# - The 2s probe cadence doubles as USB keepalive between chunks.
# - Survives mid-transfer stalls: the phone watchdog-reboots itself back into
#   fastboot (~10s), the probe catches the fresh LK, the same chunk retries
#   (idempotent - sparse chunks write only their own range).
# - Exits when all chunks are in the progress file, or after 10 consecutive
#   failed chunks (real trouble -> human).
W="/media/nicklas/New Volume1/stuff/Carmelian hacks/work"
FB="$W/tools/fastboot"
DIR="$W/chunks100"
PROG="$DIR/flash-progress.txt"
LOG="$W/flash-driver.log"
touch "$PROG"
total=$(ls "$DIR"/part_*.img.sparse 2>/dev/null | wc -l)
echo "=== driver started $(date '+%H:%M:%S') total=$total" | tee -a "$LOG"

fails=0
while true; do
  done_count=$(wc -l < "$PROG")
  [ "$done_count" -ge "$total" ] && { echo "=== ALL $total CHUNKS DONE $(date '+%H:%M:%S')" | tee -a "$LOG"; exit 0; }

  # next incomplete chunk
  next=""
  for f in "$DIR"/part_*.img.sparse; do
    grep -q "^$(basename "$f")$" "$PROG" || { next="$f"; break; }
  done
  [ -z "$next" ] && { echo "=== ALL CHUNKS DONE $(date '+%H:%M:%S')" | tee -a "$LOG"; exit 0; }

  # wait for a LIVE device (responds to getvar)
  if ! timeout 5 "$FB" getvar product >/dev/null 2>&1; then
    sleep 2
    continue
  fi

  n=$(basename "$next")
  echo "[$(date '+%H:%M:%S')] flashing $n ($done_count/$total)" >> "$LOG"
  out=$(timeout 90 "$FB" flash userdata "$next" 2>&1); rc=$?
  if [ $rc -eq 0 ] && echo "$out" | grep -q "OKAY"; then
    echo "$n" >> "$PROG"
    fails=0
    echo "[$(date '+%H:%M:%S')] $n OKAY" >> "$LOG"
  else
    fails=$((fails+1))
    echo "[$(date '+%H:%M:%S')] $n FAILED rc=$rc (consecutive fails: $fails)" >> "$LOG"
    echo "$out" | tail -2 >> "$LOG"
    if [ "$fails" -ge 10 ]; then
      echo "!!! 10 consecutive failures - stopping for human $(date '+%H:%M:%S')" | tee -a "$LOG"
      exit 1
    fi
  fi
done
