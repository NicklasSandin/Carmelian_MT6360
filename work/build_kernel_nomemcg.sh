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
  echo "=== cleaning /tmp/ksrc..."
  rm -rf /tmp/ksrc
  df -h / | tail -1
}
trap cleanup EXIT

echo "=== copying the source to /tmp/ksrc..."
rm -rf /tmp/ksrc
cp -a "$KS" /tmp/ksrc
df -h / | tail -1

cd /tmp/ksrc
cp "$A/kernel-pio.config" .config
scripts/config --file .config -d MEMCG
make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- olddefconfig
echo "=== CONFIG_MEMCG state:"
grep -E "^(CONFIG_MEMCG|# CONFIG_MEMCG)" .config || echo "(not set = disabled) OK"

JOBS=$(nproc)
echo "=== building Image.gz + modules with $JOBS jobs..."
make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- -j"$JOBS" Image.gz modules

# artifacts back to the work disk
cp arch/arm64/boot/Image.gz "$W/Image_nomemcg.gz"
rm -rf "$KS/modstage" && mkdir -p /tmp/ksrc-modstage
find /tmp/ksrc -name '*.ko' -exec cp --parents {} /tmp/ksrc-modstage/ \;
cd /tmp/ksrc-modstage
tar cfJ "$W/modules-nomemcg.tar.zst" .
echo "=== artifacts:"
ls -la "$W/Image_nomemcg.gz" "$W/modules-nomemcg.tar.zst"
