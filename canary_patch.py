#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Canary-patch an arm64 kernel Image to bracket a silent boot death.

Shadows the entry path with stubs that write markers into the pstore/ramoops
console buffer (0x48090000, readable from Android as console-ramoops) and
then re-execute the shadowed instruction. See NOTES.md for the full story.
"""
import struct, sys

def patch(src, dst):
    img = bytearray(open(src, 'rb').read())
    stext = 0x2A280F8          # b-target of the +4 branch (per this build)
    # canary1: header stub; canary2/3: PE-header cave (unused by non-EFI boot)
    canary1 = [0x52A00129, 0x72890009, 0x5282886A, 0x72A989AA,
               0xB9000D2A, 0x5280002B, 0xB900112B,
               0x14000000 | (((stext - 0x30) >> 2) & 0x3FFFFFF)]
    canary2 = [0x52A00129, 0x72890009, 0x5280004B, 0xB900132B,
               0x94000000 | (((0x2A3A7C4 - 0x50) >> 2) & 0x3FFFFFF),
               0x14000000 | (((stext + 4 - 0x54) >> 2) & 0x3FFFFFF)]
    canary3 = [0x52A00129, 0x72890009, 0x5280006B, 0xB900192B,
               0x94000000 | (((0x2A3A818 - 0x68) >> 2) & 0x3FFFFFF),
               0x14000000 | (((stext + 8 - 0x6C) >> 2) & 0x3FFFFFF)]
    def put(off, words):
        for i, w in enumerate(words):
            struct.pack_into('<I', img, off + i*4, w)
    put(0x04, [0x14000005])
    put(0x18, canary1)
    put(0x40, canary2)
    put(0x58, canary3)
    put(stext,     [0x14000000 | (((0x40 - stext) >> 2) & 0x3FFFFFF)])
    put(stext + 4, [0x14000000 | (((0x58 - stext - 4) >> 2) & 0x3FFFFFF)])
    open(dst, 'wb').write(bytes(img))
    print(f"canary image: {dst}")

if __name__ == '__main__':
    patch(sys.argv[1], sys.argv[2])
