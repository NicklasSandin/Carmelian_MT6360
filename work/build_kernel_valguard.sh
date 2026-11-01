#!/bin/bash
# Rebuild the 7.2.0-omamobile kernel with CONFIG_MEMCG=n.
# kbuild rejects spaces in paths, so the source is copied to /tmp for the
# build and REMOVED afterwards; all artifacts land back on the work disk.
set -euo pipefail
W="/media/nicklas/New Volume1/stuff/Carmelian hacks/work"
A="/media/nicklas/New Volume1/stuff/Carmelian hacks/rung3-archive"
KS="/media/nicklas/New Volume1/stuff/Carmelian hacks/linux-mt6833-camellian"
TC=/tmp/tc/bin
export PATH="$TC:$PATH"

cleanup() {
  echo "=== cleaning /tmp/ksrc6..."
  rm -rf /tmp/ksrc6
  df -h / | tail -1
}
trap cleanup EXIT

echo "=== copying the source to /tmp/ksrc6..."
rm -rf /tmp/ksrc6
cp -a "$KS" /tmp/ksrc6
df -h / | tail -1

cd /tmp/ksrc6
cp "$A/kernel-pio.config" .config

make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- olddefconfig
echo "=== CONFIG_MEMCG state:"
grep -E "^(CONFIG_MEMCG|# CONFIG_MEMCG)" .config || echo "(not set = disabled) OK"

JOBS=$(nproc)
echo "=== building Image.gz + modules with $JOBS jobs..."
make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- -j"$JOBS" Image.gz modules

# artifacts back to the work disk
cp arch/arm64/boot/Image.gz "$W/Image_valguard.gz"
rm -rf "$KS/modstage" && mkdir -p /tmp/ksrc6-modstage
find /tmp/ksrc6 -name '*.ko' -exec cp --parents {} /tmp/ksrc6-modstage/ \;
cd /tmp/ksrc6-modstage
tar cfJ "$W/modules-valguard.tar.zst" .
echo "=== artifacts:"
ls -la "$W/Image_valguard.gz" "$W/modules-valguard.tar.zst"
