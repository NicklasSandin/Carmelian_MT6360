#!/bin/bash
# Finish the offset rootfs over SSH (PIO kernel, stable USB).
# Chunks 44..111 of rootfs_full.img -> /dev/sdc59 at 5 GiB + i*100 MiB.
W="/media/nicklas/New Volume1/stuff/Carmelian hacks/work"
O="/media/nicklas/New Volume1/stuff/Carmelian hacks/omarchy-mobile-camellian/out"
PROG="$W/ssh-flash-progress.txt"
LOG="$W/ssh-flash.log"
SSH="ssh -o ConnectTimeout=15 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@172.16.42.1"
touch "$PROG"

echo "=== ssh transfer started $(date '+%H:%M:%S')" | tee -a "$LOG"
fails=0
for i in $(seq 44 111); do
  grep -q "^$i$" "$PROG" && continue
  ok=0
  for try in 1 2 3; do
    # liveness check first
    if ! timeout 20 $SSH 'true' >/dev/null 2>&1; then
      echo "[$(date '+%H:%M:%S')] ssh down; waiting (chunk $i)" | tee -a "$LOG"
      sleep 15
      continue
    fi
    echo "[$(date '+%H:%M:%S')] chunk $i attempt $try" >> "$LOG"
    dd if="$O/rootfs_full.img" bs=1M skip=$((i*100)) count=100 2>/dev/null | \
      timeout 180 $SSH "dd of=/dev/sdc59 bs=1M seek=$((5120 + i*100)) conv=notrunc 2>/dev/null" >/dev/null 2>&1
    rc=$?
    if [ $rc -eq 0 ]; then
      echo "$i" >> "$PROG"; fails=0; ok=1
      echo "[$(date '+%H:%M:%S')] chunk $i OK ($(grep -c . "$PROG")/68)" >> "$LOG"
      break
    fi
    fails=$((fails+1))
    echo "[$(date '+%H:%M:%S')] chunk $i FAILED rc=$rc (consec $fails)" >> "$LOG"
    [ "$fails" -ge 8 ] && { echo "!!! 8 consecutive failures - stopping $(date '+%H:%M:%S')" | tee -a "$LOG"; exit 1; }
  done
  [ $ok -eq 1 ] || { echo "!!! chunk $i exhausted retries $(date '+%H:%M:%S')" | tee -a "$LOG"; exit 1; }
done
echo "=== ALL 68 SSH CHUNKS DONE $(date '+%H:%M:%S')" | tee -a "$LOG"
