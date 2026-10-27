#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Parse the MTK DTB container (magic 0xd7b7ab1e), strip disabled nodes,
and rewrap. Produces a DTB small enough for lk_b's boot-DTB buffer.

The MTK container is a 64-byte wrapper around a standard FDT:
  +0x00: magic 0xd7b7ab1e
  +0x04: full container size (u32 BE)
  +0x08-0x1C: metadata
  +0x20: real FDT totalsize (u32 BE)
  +0x24: wrapper header size = 0x40
  +0x40: standard FDT (magic 0xd00dfeed, version 17)

Usage:
  python3 dtb_shrink.py <input.dtb> <output.dtb> [--keep-frontend]

The stripper removes all top-level nodes containing `status = "disabled"`
except those whose name matches the KEEP_PREFIXES list (USB, storage,
clocks, pinctrl, etc. — the hardware the probe needs).
"""

import struct, sys, re, subprocess, tempfile, os

MTK_MAGIC = 0xd7b7ab1e
FDT_MAGIC = 0xd00dfeed
WRAPPER_SIZE = 0x40

KEEP_PREFIXES = (
    'gpio', 'pinctrl', 'pwrap', 'msdc', 'usb', 'i2c', 'uart', 'timer',
    'gic', 'intpol', 'apmixed', 'topckgen', 'infracfg', 'pericfg', 'sleep',
    'spm', 'pmic', 'auxadc', 'efuse', 'cpu', 'gce', 'mipi', 'mipi_rx',
    'u2phy', 'u3phy', 'phy', 'systimer', 'mcu', 'gpt', 'apxgpt', 'wdt',
    'rtc', 'sejava', 'dvfsrc', 'cpu_thermal', 'thermal', 'rank', 'dramc',
    'toprgu', 'watchdog', 't-phy', 'tphy', 'musb', 'ssusb', 'pericfg',
    'mt6360',
)

KEEP_EXACT = {
    'chosen', 'aliases', 'reserved-memory', 'memory', 'psci', 'firmware',
    'cpus', 'soc', 'keys', 'regulators',
}


def unwrap_mtk(data):
    """Extract the real FDT from the MTK container."""
    magic, size = struct.unpack_from('>II', data, 0)
    if magic == MTK_MAGIC:
        fdt_size = struct.unpack_from('>I', data, 0x20)[0]
        fdt_off = struct.unpack_from('>I', data, 0x24)[0]
        print(f"MTK container: {len(data)} bytes, FDT {fdt_size} at +{fdt_off}")
        return bytes(data[fdt_off:fdt_off + fdt_size]), fdt_size
    elif magic == FDT_MAGIC:
        print(f"Plain FDT: {len(data)} bytes")
        return bytes(data), len(data)
    else:
        raise ValueError(f"Unknown magic {magic:08x}")


def wrap_mtk(fdt):
    """Wrap a standard FDT in the MTK container."""
    ts = len(fdt)
    w = bytearray(WRAPPER_SIZE)
    struct.pack_into('>I', w, 0, MTK_MAGIC)
    struct.pack_into('>I', w, 4, WRAPPER_SIZE + ts)
    struct.pack_into('>I', w, 8, 0x20)
    struct.pack_into('>I', w, 12, 0x20)
    struct.pack_into('>I', w, 16, 1)
    struct.pack_into('>I', w, 20, 0x20)
    struct.pack_into('>I', w, 24, 0x800)
    struct.pack_into('>I', w, 28, 0)
    struct.pack_into('>I', w, 32, ts)
    struct.pack_into('>I', w, 36, WRAPPER_SIZE)
    return bytes(w) + fdt


def strip_disabled_nodes(dts_text, extra_keep=()):
    """Remove top-level nodes containing status="disabled" except keep-list."""
    lines = dts_text.split('\n')
    keep_all = set(KEEP_EXACT) | set(extra_keep)
    root_start = next((i for i, l in enumerate(lines) if l.strip() == '/ {'), None)
    if root_start is None:
        raise ValueError("no root node found")
    root_end = max((i for i, l in enumerate(lines) if l == '};'), default=len(lines) - 1)

    out = lines[:root_start + 1]
    removed, kept = [], []
    i = root_start + 1
    while i < root_end:
        m = re.match(r'\t([a-zA-Z][a-zA-Z0-9_-]+(?:@[0-9a-fA-F]+)?)\s*\{', lines[i])
        if not m:
            out.append(lines[i]); i += 1; continue
        name = m.group(1)
        depth, j, body = 1, i + 1, [lines[i]]
        while j < root_end and depth > 0:
            l = lines[j]; depth += l.count('{') - l.count('}'); body.append(l); j += 1
        block = '\n'.join(body)
        keep = (name in keep_all or
                any(name.startswith(p) for p in KEEP_PREFIXES))
        if keep:
            # enable mt6360 if present
            if name.startswith('mt6360'):
                body = [re.sub(r'status = "disabled"', 'status = "ok"', l) for l in body]
            kept.append(name)
            out.extend(body)
        elif 'status = "disabled"' in block:
            removed.append((name, len(block)))
        else:
            kept.append(name)
            out.extend(body)
        i = j
    out.extend(lines[root_end:])
    return '\n'.join(out), removed, kept


def main():
    if len(sys.argv) < 3:
        print(__doc__); sys.exit(1)
    inp, outp = sys.argv[1], sys.argv[2]
    data = open(inp, 'rb').read()

    fdt, fdt_size = unwrap_mtk(data)
    # patch the magic so dtc can parse it
    patched = bytearray(fdt)
    struct.pack_into('>I', patched, 0, FDT_MAGIC)
    with tempfile.NamedTemporaryFile(suffix='.dtb', delete=False) as f:
        f.write(patched); tmp_dtb = f.name
    # decompile
    r = subprocess.run(['dtc', '-I', 'dtb', '-O', 'dts', tmp_dtb],
                       capture_output=True, text=True)
    os.unlink(tmp_dtb)
    if r.returncode != 0:
        print(f"dtc error: {r.stderr[:200]}"); sys.exit(1)
    dts = r.stdout

    # fix stray root closers
    lines = dts.split('\n')
    closes = [i for i, l in enumerate(lines) if l == '};']
    if len(closes) > 1:
        keep = closes[-1]
        lines = [l for i, l in enumerate(lines) if not (l == '};' and i != keep)]
        dts = '\n'.join(lines)

    # strip
    stripped, removed, kept = strip_disabled_nodes(dts)
    print(f"Stripped {len(removed)} nodes ({sum(x[1] for x in removed)} bytes of DTS); kept {len(kept)}")
    big = sorted(removed, key=lambda x: -x[1])[:8]
    for n, s in big:
        print(f"  -{s:6d} {n}")

    # recompile
    with tempfile.NamedTemporaryFile(suffix='.dts', mode='w', delete=False) as f:
        f.write(stripped); tmp_dts = f.name
    r = subprocess.run(['dtc', '-I', 'dts', '-O', 'dtb', tmp_dts, '-o', outp],
                       capture_output=True, text=True)
    os.unlink(tmp_dts)
    if r.returncode != 0:
        print(f"dtc recompile error: {r.stderr[:200]}"); sys.exit(1)

    # rewrap in the MTK container
    new_fdt = open(outp, 'rb').read()
    wrapped = wrap_mtk(new_fdt)
    open(outp, 'wb').write(wrapped)
    print(f"Output: {outp} ({len(wrapped)} bytes container, FDT {len(new_fdt)} bytes)")


if __name__ == '__main__':
    main()
