#!/bin/bash
set -e
W="/mnt/c/stuff/Carmelian hacks"
R=/tmp/ramfs
rm -rf "$R"
mkdir -p "$R/bin" "$R/dev" "$R/proc" "$R/sys" "$R/run" "$R/mnt/omarchy" "$R/sbin" "$R/etc"
cp "$W/work/initramfs/init" "$R/init"
cp "$W/work/initramfs/bin/busybox" "$R/bin/busybox"
# nhstub ships inside the initramfs so stub updates don't need adb
cp "$W/work/artifacts/preload.so.gz" "$R/preload.so.gz" 2>/dev/null || echo "WARN: no preload.so.gz - LD_PRELOAD will be missing"
chmod 755 "$R/init" "$R/bin/busybox"
ln -sf busybox "$R/bin/sh"
cd "$R"
find . | cpio -o -H newc 2>/dev/null | gzip -9 > "$W/work/artifacts/initramfs_pivot_sd.gz"
ls -la "$W/work/artifacts/initramfs_pivot_sd.gz"
