#!/bin/bash
# Rebuild boot_b's image with the no-memcg kernel.
# The new kernel is smaller than the old, so it is zero-padded into the old
# kernel's slot: the ramdisk, second, MTK DTB and everything after stay at
# their exact original offsets. The self-consistent vbmeta is content
# independent, so no vbmeta work is needed.
set -euo pipefail
W="/media/nicklas/New Volume1/stuff/Carmelian hacks/work"
A="/media/nicklas/New Volume1/stuff/Carmelian hacks/rung3-archive"
CMDLINE=("bootopt=64S3,32N2,64N2 ramoops.mem_address=0x48090000 ramoops.mem_size=0xe0000"
         "ramoops.console_size=0x40000 ramoops.pmsg_size=0x10000"
         "cgroup_disable=memory console=ttyGS0,115200")

python3 - "${W}" "${A}" <<'EOF'
import struct, sys, gzip
W, A = sys.argv[1], sys.argv[2]
u32 = lambda b: struct.unpack('<I', b)[0]
CMDLINE = ("bootopt=64S3,32N2,64N2 ramoops.mem_address=0x48090000 ramoops.mem_size=0xe0000"
           " ramoops.console_size=0x40000 ramoops.pmsg_size=0x10000"
           " cgroup_disable=memory console=ttyGS0,115200")
EOF_MARK = None

src = open(f"{A}/boot_pio_only_pivot_64m.img", 'rb').read()
newk = open(f"{W}/Image_nomemcg.gz", 'rb').read()
newrd = open(f"{W}/rd_offset5G/ramdisk.new.gz", 'rb').read()

ksz_o = u32(src[8:12]); psz = u32(src[36:40])
koff = psz
kpad_end = koff + ((ksz_o + psz - 1)//psz)*psz
assert len(newk) <= (kpad_end - koff), "new kernel larger than the old slot"

out = bytearray(src)
# new kernel into the old kernel's slot, zero-padded to the same boundary
out[koff:koff+len(newk)] = newk
out[koff+len(newk):kpad_end] = b'\0' * (kpad_end - koff - len(newk))
out[8:12] = struct.pack('<I', len(newk))

# current pivot ramdisk into the old ramdisk's slot, same treatment
rsz_o = u32(out[16:20])
rd_off = kpad_end
rd_padded_end = rd_off + ((rsz_o + psz - 1)//psz)*psz
assert len(newrd) <= (rd_padded_end - rd_off), "new ramdisk larger than the old slot"
out[rd_off:rd_off+len(newrd)] = newrd
out[rd_off+len(newrd):rd_padded_end] = b'\0' * (rd_padded_end - rd_off - len(newrd))
out[16:20] = struct.pack('<I', len(newrd))

cb = CMDLINE.encode()
assert len(cb) < 512
out[64:576] = cb + b'\0' * (512 - len(cb))
open(f"{W}/boot_nomemcg.img", 'wb').write(out)

# verification
chk = open(f"{W}/boot_nomemcg.img", 'rb').read()
print("kernel_size :", u32(chk[8:12]), "(old", ksz_o, ")")
print("ramdisk_size:", u32(chk[16:20]), "(old", rsz_o, ")")
print("cmdline     :", chk[64:576].split(b'\0')[0].decode()[-60:])
print("DTB intact  :", chk[24414272:24414276] == bytes.fromhex('d00dfeed'))
print("len         :", len(chk), "(must be", len(src), ")")
# ramdisk round-trip
rd = gzip.decompress(chk[rd_off:rd_off + u32(chk[16:20])])
print("ramdisk init: disarm=%s offset=%s" % (b'0x22000000' in rd, b'ROOTFS_OFFSET' in rd))
EOF
