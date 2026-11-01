#!/usr/bin/env python3
"""Split a raw image into standalone Android sparse images for resumable fastboot flashing.

Each output sparse file covers range [start, start+length) of the source raw image:
  - leading DONT_CARE chunk skips blocks before `start` (never written by the bootloader)
  - RAW chunks carry the actual data at their correct partition offsets
  - CRC32 + SHA256 computed and verified per chunk
  - file padded to a 4096-byte multiple

Default chunk size: 100 MiB data per file -> each file downloads in ONE fastboot
round (max-download-size on camellian is 128 MiB), so fastboot never needs to
resparse (which requires aligned file sizes and failed for big files).

Usage:
  make_sparse_chunks.py all SRC OUTDIR [first] [last]
Env:
  CHUNK_BYTES  data bytes per sparse file (default 104857600 = 100 MiB)
"""
import struct, zlib, hashlib, os, sys, time

BLK = 4096
MAGIC = 0xED26FF3A
RAW, FILL, DONT_CARE, CRC = 0xCAC1, 0xCAC2, 0xCAC3, 0xCAC4
CHUNK_BYTES = int(os.environ.get('CHUNK_BYTES', 100 * 1024 * 1024))


def make(src, out, start, length, src_start=None):
    """start/length are PARTITION offsets; src_start is the matching offset in
    the source file (defaults to start). They differ when the image is placed
    at a nonzero partition offset (e.g. the 5 GiB camellian rootfs)."""
    if src_start is None:
        src_start = start
    assert start % BLK == 0 and length % BLK == 0 and src_start % BLK == 0
    sb = start // BLK
    tb = length // BLK
    # total_blks must cover the full span from partition offset 0 (DONT_CARE prefix + data),
    # because the device's parser accumulates chunk_sz against it.
    nchunks = (length + CHUNK_BYTES - 1) // CHUNK_BYTES
    total_hdr_chunks = nchunks + (1 if sb else 0)
    sha = hashlib.sha256()
    crc = 0
    with open(src, 'rb') as f, open(out, 'wb') as o:
        o.write(struct.pack('<IHHHHIIII', MAGIC, 1, 0, 28, 12, BLK, sb + tb, total_hdr_chunks, 0))
        if sb:
            o.write(struct.pack('<HHII', DONT_CARE, 0, sb, 12))
        f.seek(src_start)
        left = length
        while left:
            nb = min(CHUNK_BYTES, left) // BLK
            data = f.read(nb * BLK)
            if len(data) != nb * BLK:
                raise IOError(f'short read at offset {start + length - left}: got {len(data)}')
            sha.update(data)
            crc = zlib.crc32(data, crc)
            o.write(struct.pack('<HHII', RAW, 0, nb, 12 + nb * BLK))
            o.write(data)
            left -= nb * BLK
        pad = (-o.tell()) % 4096
        if pad:
            o.write(b'\0' * pad)
        o.seek(0)
        o.write(struct.pack('<IHHHHIIII', MAGIC, 1, 0, 28, 12, BLK, sb + tb, total_hdr_chunks, crc & 0xFFFFFFFF))
    return sha.hexdigest()


def verify(sp, src, start, length, expect_sha, src_start=None):
    if src_start is None:
        src_start = start
    with open(sp, 'rb') as f, open(src, 'rb') as s:
        # s is opened only for interface compatibility; data lives in the
        # source at src_start, so re-open lazily below
        hdr = f.read(28)
        magic, maj, minr, fhs, chs, blk, tb, nc, crc0 = struct.unpack('<IHHHHIIII', hdr)
        assert magic == MAGIC, f'bad magic {magic:#x}'
        assert blk == BLK
        pos_blocks = 0
        sha = hashlib.sha256()
        crc = 0
        for _ in range(nc):
            h = f.read(chs)
            assert len(h) == chs, 'truncated chunk header'
            typ, res, csz, tsz = struct.unpack('<HHII', h)
            if typ == RAW:
                data = f.read(tsz - chs)
                assert len(data) == tsz - chs, 'truncated RAW chunk'
                sha.update(data)
                crc = zlib.crc32(data, crc)
                pos_blocks += csz
            elif typ == DONT_CARE:
                assert pos_blocks == 0, 'DONT_CARE only expected at head'
                pos_blocks += csz
            else:
                raise AssertionError(f'unexpected chunk type {typ}')
        assert pos_blocks == tb, f'chunk coverage {pos_blocks} != header span {tb}'
        assert tb == start // BLK + length // BLK, f'header span {tb} != expected {start//BLK + length//BLK}'
        assert (crc & 0xFFFFFFFF) == crc0, 'sparse CRC mismatch'
        got = sha.hexdigest()
        assert got == expect_sha, f'DATA MISMATCH: {got} != {expect_sha}'
    return got


def main():
    mode = sys.argv[1]
    if mode != 'all':
        raise SystemExit('usage: make_sparse_chunks.py all SRC OUTDIR [first] [last]')
    src, outdir = sys.argv[2], sys.argv[3]
    first = int(sys.argv[4]) if len(sys.argv) > 4 else 0
    last = int(sys.argv[5]) if len(sys.argv) > 5 else 10 ** 9
    total = os.path.getsize(src)
    assert total % BLK == 0
    n_total = (total + CHUNK_BYTES - 1) // CHUNK_BYTES
    os.makedirs(outdir, exist_ok=True)
    for i in range(first, min(last + 1, n_total)):
        start = i * CHUNK_BYTES
        length = min(CHUNK_BYTES, total - start)
        out = os.path.join(outdir, f'part_{i:03d}.img.sparse')
        t0 = time.time()
        sha = make(src, out, start, length)
        sz = os.path.getsize(out)
        verify(out, src, start, length, sha)
        print(f'part_{i:03d}: start={start} len={length} sparse={sz} sha256={sha[:16]}... VERIFIED ({time.time()-t0:.0f}s)', flush=True)
    print(f'DONE: chunks {first}..{min(last, n_total-1)} of {n_total}')


if __name__ == '__main__':
    main()
