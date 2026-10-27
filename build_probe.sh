#!/bin/bash
# SPDX-License-Identifier: MIT
# Build the stock-kernel probe boot image for camellian slot B.
# Inputs: stock boot_b.img, stock dtbo_b.img, the fixed initramfs, avbtool.
set -euo pipefail

STOCK_BOOT="${1:?stock boot_b.img required}"
INITRAMFS="${2:?fixed initramfs.gz required}"
AVBTOOL="${3:?avbtool path required}"
OUT="${4:?output directory required}"

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# Extract stock components (kernel, DTB from the MTK container)
python3 unpack_bootimg.py --boot_img "$STOCK_BOOT" --out "$WORK/unpacked"

# Unwrap the MTK DTB container (the real FDT is at +0x40)
python3 - "$WORK/unpacked/dtb" "$WORK/stock_fdt.dtb" <<'PY'
import struct, sys
d = open(sys.argv[1], 'rb').read()
ts, = struct.unpack_from('>I', d, 0x20)
open(sys.argv[2], 'wb').write(d[0x40:0x40+ts])
PY

# Pack the boot image (bare cmdline — LK adds its own prefix)
python3 mkbootimg.py \
    --header_version 2 --os_version 16.0.0 --os_patch_level 2026-08 \
    --kernel "$WORK/unpacked/kernel" \
    --ramdisk "$INITRAMFS" \
    --dtb "$WORK/stock_fdt.dtb" \
    --pagesize 0x800 --base 0x0 \
    --kernel_offset 0x40080000 --ramdisk_offset 0x51100000 \
    --second_offset 0x0 --tags_offset 0x47c80000 --dtb_offset 0x47c80000 \
    --board "" --cmdline "bootopt=64S3,32N2,64N2" \
    -o "$WORK/boot_bare.img"

# Add the AVB footer (algorithm NONE, flags 0 — the unlocked-device default)
cp "$WORK/boot_bare.img" "$OUT/boot_b_64m.img"
python3 "$AVBTOOL" add_hash_footer \
    --image "$OUT/boot_b_64m.img" \
    --partition_size 67108864 \
    --partition_name boot

echo "Built: $OUT/boot_b_64m.img"
echo "Flash: fastboot flash boot_b $OUT/boot_b_64m.img"
