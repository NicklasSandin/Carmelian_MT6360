#!/bin/bash
W="/media/nicklas/New Volume1/stuff/Carmelian hacks/work"
FB="$W/tools/fastboot"
echo "=== probe: waiting for live bootloader"
until timeout 5 "$FB" getvar product >/dev/null 2>&1; do sleep 3; done
echo "[$(date '+%H:%M:%S')] live — TEST 1: s44_2 (write 4.34-4.37 GB, expect HANG if boundary theory holds)"
out=$(timeout 60 "$FB" flash userdata "$W/subchunks/s44_2.img.sparse" 2>&1); rc=$?
echo "$out" | tail -2
echo "TEST1 rc=$rc $(echo "$out" | grep -q OKAY && echo OKAY || echo NO-OKAY)"
echo "[$(date '+%H:%M:%S')] TEST 2: probeA (DONT_CARE spans cursed, write 4.50-4.52 GB)"
out=$(timeout 60 "$FB" flash userdata "$W/subchunks/probeA_above.img.sparse" 2>&1); rc=$?
echo "$out" | tail -2
echo "TEST2 rc=$rc $(echo "$out" | grep -q OKAY && echo OKAY || echo NO-OKAY)"
