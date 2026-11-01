#!/bin/bash
# Rebuild boot_pio_offset5G.img from the pristine base:
#   - current initramfs (rd_offset5G/rd via the stock cpio pipeline)
#   - deterministic cmdline: base + cgroup_disable=memory + console=ttyGS0,115200
# The vbmeta_b_selffix.img (self-hash-fixed stock vbmeta) is independent of the
# boot image content, so it never needs regenerating.
set -euo pipefail
W="/media/nicklas/New Volume1/stuff/Carmelian hacks/work"
A="/media/nicklas/New Volume1/stuff/Carmelian hacks/rung3-archive"
INIT="/media/nicklas/New Volume1/stuff/Carmelian hacks/omarchy-mobile-camellian/device/camellian/initramfs/init"

sh -n "$INIT"
cd "$W/rd_offset5G/rd"
cp "$INIT" ./init && chmod 755 ./init
rm -f ../ramdisk.new.gz
find . | cpio -o -H newc --quiet 2>/dev/null | gzip -9 > ../ramdisk.new.gz

python3 - <<'EOF'
import struct, gzip
W = "/media/nicklas/New Volume1/stuff/Carmelian hacks/work"
A = "/media/nicklas/New Volume1/stuff/Carmelian hacks/rung3-archive"
CMDLINE = ("bootopt=64S3,32N2,64N2 ramoops.mem_address=0x48090000 ramoops.mem_size=0xe0000"
           " ramoops.console_size=0x40000 ramoops.pmsg_size=0x10000"
           " cgroup_disable=memory console=ttyGS0,115200")
newrd = open(f"{W}/rd_offset5G/ramdisk.new.gz", 'rb').read()
u32 = lambda b: struct.unpack('<I', b)[0]
src = bytearray(open(f"{A}/boot_pio_only_pivot_64m.img", 'rb').read())
ksz = u32(src[8:12]); rsz = u32(src[16:20]); psz = u32(src[36:40])
rd_off = psz + ((ksz + psz - 1)//psz)*psz
rd_padded_end = rd_off + ((rsz + psz - 1)//psz)*psz
assert len(newrd) <= rsz, f"ramdisk {len(newrd)} > {rsz}"
src[rd_off:rd_padded_end] = newrd + b'\0' * (rd_padded_end - rd_off - len(newrd))
src[16:20] = struct.pack('<I', len(newrd))
cb = CMDLINE.encode()
assert len(cb) < 512
src[64:576] = cb + b'\0' * (512 - len(cb))
open(f"{W}/boot_pio_offset5G.img", 'wb').write(src)

# verify: cmdline + ramdisk markers
chk = open(f"{W}/boot_pio_offset5G.img", 'rb').read()
cmd = chk[64:576].split(b'\0')[0].decode()
rd = gzip.decompress(chk[rd_off:rd_off + u32(chk[16:20])])
marks = {
    'cmdline.memcg': 'cgroup_disable=memory' in cmd,
    'cmdline.console': 'console=ttyGS0' in cmd,
    'ramdisk.offset': b'ROOTFS_OFFSET' in rd,
    'ramdisk.rofix': b'/sys/class/block/${dev##*/}/ro' in rd,
    'ramdisk.guard': b'/sys/block/$name/*/uevent' in rd,
    'ramdisk.disarm': b'0x22000000' in rd,
    'ramdisk.devnull': b'/dev nodes missing; recreating' in rd,
    'ramdisk.ceiling': b'24-hour' in rd,
}
for k, v in marks.items():
    print(f"{k}: {'OK' if v else 'MISSING!!'}")
assert all(marks.values()), "marker check failed"
print("boot_pio_offset5G.img rebuilt and verified")
EOF
