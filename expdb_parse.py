#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Parse the expdb partition (40 MiB) and extract LK boot records.

The expdb partition is a ring buffer of boot records (~344 KiB each).
Each record contains the accumulated RAM_CONSOLE from one boot attempt:
preloader -> ATF -> LK. Records are written at each reset and wrap around.

Usage:
  python3 expdb_parse.py <expdb.img> [--all] [--last N] [--grep PATTERN]
"""
import struct, sys, re, argparse

def parse_records(data):
    starts = [m.start() for m in re.finditer(b'welcome to lk', data)]
    records = []
    for i, s in enumerate(starts):
        e = starts[i+1] if i+1 < len(starts) else len(data)
        chunk = data[s:e]
        lines = [m.group().decode(errors='replace')
                 for m in re.finditer(rb'[\x20-\x7e]{6,}', chunk)]
        ts = [int(m.group(1)) for l in lines
              if (m := re.match(r'\s*\[(\d+)\]', l))]
        txt = chunk.decode(errors='ignore')
        ab = re.search(r'p_AB_suffix: (\w+), AB_retry_count: (\d+)', txt)
        kedump = re.search(r'kedump: current time: \[([^\]]+)\]', txt)
        records.append({
            'index': i, 'offset': s, 'lines': lines,
            'last_ts': max(ts) if ts else 0,
            'slot': ab.group(1) if ab else '?',
            'retry': int(ab.group(2)) if ab else -1,
            'assert': 'hdr_loaded' in txt,
            'avb': 'avb_ret' in txt,
            'avb_ok': 'avb_ret = 0' in txt and 'ret = 0' in txt,
            'jump': 'jump to K64' in txt or 'jump to linux' in txt,
            'decompress': 'decompress_kernel' in txt,
            'kedump_time': kedump.group(1) if kedump else None,
        })
    return records

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('file', help='expdb partition image')
    p.add_argument('--all', action='store_true', help='dump all records')
    p.add_argument('--last', type=int, default=5, help='show last N records')
    p.add_argument('--grep', help='filter lines matching pattern')
    args = p.parse_args()
    data = open(args.file, 'rb').read()
    records = parse_records(data)
    print(f"{len(records)} LK boot records found\n")
    show = records if args.all else records[-args.last:]
    for r in show:
        flags = []
        if r['assert']: flags.append('HDR_ASSERT')
        if r['avb']: flags.append('AVB')
        if r['avb_ok']: flags.append('AVB_OK')
        if r['jump']: flags.append('KERNEL_JUMP')
        if r['decompress']: flags.append('DECOMP')
        print(f"  rec{r['index']} @0x{r['offset']:x} slot={r['slot']} "
              f"retry={r['retry']} last_ts={r['last_ts']} "
              f"lines={len(r['lines'])} [{','.join(flags)}] "
              f"kedump={r['kedump_time'] or '-'}")
    if args.grep:
        pat = re.compile(args.grep, re.I)
        for r in show:
            print(f"\n--- rec{r['index']} matching lines:")
            for l in r['lines']:
                if pat.search(l):
                    print(f"  {l[:160]}")

if __name__ == '__main__':
    main()
