import struct, hashlib, os, sys

SRC = r'C:\stuff\Carmelian hacks\work\artifacts\success-era\boot_b.img'
RD_GZ = r'C:\stuff\Carmelian hacks\work\artifacts\initramfs_pivot_sd.gz'
OUT = r'C:\stuff\Carmelian hacks\work\artifacts\boot_pivot_inplace_v0sha.img'
EXPECT = 67108864

st = os.stat(SRC)
print('stat: size=%d mtime=%d' % (st.st_size, st.st_mtime))

# chunked read
buf = bytearray()
with open(SRC, 'rb') as f:
    while True:
        c = f.read(1 << 20)
        if not c:
            break
        buf += c
print('chunked read:', len(buf))
if len(buf) != EXPECT:
    print('FATAL: read length mismatch!')
    sys.exit(1)
assert hashlib.sha256(buf).hexdigest().startswith('43deb16b'), 'content mismatch!'

img = bytearray(buf)
gz = open(RD_GZ, 'rb').read()
print('gz:', len(gz))

page_size, = struct.unpack_from('<I', img, 36)
kern_size, = struct.unpack_from('<I', img, 8)
rd_size, = struct.unpack_from('<I', img, 16)
kern_pages = (kern_size + page_size - 1) // page_size
rd_off = page_size * (1 + kern_pages)
assert len(img) == EXPECT and page_size == 2048 and kern_size == 13400331 and rd_size == 4418470

region = bytearray(rd_size)
region[:len(gz)] = gz
img[rd_off:rd_off + rd_size] = region

h = hashlib.sha1()
h.update(img[page_size:page_size + kern_size]); h.update(struct.pack('<I', kern_size))
h.update(bytes(region)); h.update(struct.pack('<I', rd_size))
# id field is 32 bytes; sha1 digest is 20 - write into the FIRST 20 bytes only,
# the trailing 12 bytes of the field stay zero. (Assigning digest to the whole
# 32-byte slice shrinks the buffer by 12 and shifts the whole image - the bug
# that corrupted every previously built boot image!)
img[576:596] = h.digest()

cmd = img[64:576].split(b'\x00')[0].decode()
if 'console=ttyGS0' not in cmd:
    # systemd debug narration to kmsg: the Eternal Shell reads it via dmesg
    # cpuidle.off=1: the SPM deep idle gates the MSDC and systemd's first
    # three mount-point checks die with EUNATCH ("Protocol driver not
    # attached") -> "Freezing execution." Keep the CPUs in shallow idle.
    # cgroup_no_v1=all: force cgroup v2 so /proc/self/cgroup has the "0::"
    # line that systemd 262 needs for cg_pid_get_path
    # printk.devkmsg=1: no ratelimit - SAFE at log_level=info (the
    # #58-#63 panics were devkmsg=1 + the DEBUG flood; at info the volume
    # is tiny). Without it the freeze reason was being swallowed.
    new_cmd = (cmd + ' console=ttyGS0,115200 systemd.log_level=info systemd.log_target=kmsg cpuidle.off=1 cgroup_no_v1=all printk.devkmsg=1').encode()[:512]
    img[64:64 + len(new_cmd)] = new_cmd
    print('cmdline:', new_cmd.decode())

with open(OUT, 'wb') as f:
    f.write(bytes(img))
    f.flush()
    os.fsync(f.fileno())
print('written', OUT, 'disk size now:', os.path.getsize(OUT))
assert os.path.getsize(OUT) == EXPECT, 'on-disk size wrong!'

src = bytes(buf)
out = open(OUT, 'rb').read()
diffs = [i for i, (a, b) in enumerate(zip(src, out)) if a != b]
outside = [i for i in diffs if not (i < 1728 or rd_off <= i < rd_off + rd_size)]
print('diff bytes:', len(diffs), 'first:', diffs[0] if diffs else None, 'last:', diffs[-1] if diffs else None)
print('diffs OUTSIDE header+ramdisk:', outside[:10] if outside else 'NONE - clean in-place swap')
