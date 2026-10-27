# Carmelian MT6833: Omarchy Mobile bring-up research

Research log for booting Omarchy Mobile (Arch Linux ARM) on a Xiaomi
Redmi Note 10 5G (`camellian`, MT6833 Dimensity 700) via the A/B slot
mechanism. The reference implementation is
[omarchy-mobile-camellian](https://github.com/NicklasSandin/Carmelian_MT6360).

## Milestone: first light achieved (2026-10-27)

The stock-kernel probe (v4.14.357 + RAM-only initramfs) booted from slot B
three times with the recipe below. USB NCM + ACM gadget configured, key-only
SSH at `root@172.16.42.1`, all UFS block devices forced read-only, and the
five-minute safety ceiling returned the phone to fastboot every time.

## Milestone: mainline kernel runs (2026-10-27)

The mainline kernel (v7.2 + A22X MT6833 series) decompressed, jumped, and
ran for **246 seconds** on slot B. The initramfs unpacked, /init executed,
and the USB gadget was configured from the kernel's perspective. The USB
gadget did not enumerate on the host (a mainline MUSB/t-phy driver issue,
not a bootloader problem).

## The boot recipe (reproducible)

```sh
# 1. Flash the probe to slot B (never slot A)
fastboot flash boot_b  boot_probe_64m.img
fastboot flash dtbo_b  dtbo_no-op.img
fastboot set_active b          # requires fastboot >= 35 (the old client
                               # aborts with "Could not get suffixes")

# 2. Wait 240 s in fastboot — the LK-owned panel must fully stabilise
sleep 240

# 3. Reboot into slot B
fastboot reboot
```

After the 5-minute ceiling the phone returns to fastboot; select A and
reboot to restore Android.

## Root causes discovered

| # | Problem | Root cause | Fix |
|---|---------|-----------|-----|
| 1 | Kernel panics at 1.29 s (`Attempted to kill init!`) | Stock MT6833 kernel has **no devtmpfs** (`CONFIG_DEVTMPFS` unset, no uevent helper) | Initramfs: hand-populate /dev (mknod console/null/kmsg/tty0/ptmx + `mdev -s`) |
| 2 | PID 1 exits → panic | `mount -t devtmpfs /dev \|\| exit 1` fails | Remove every `\|\| exit 1` from init; report and survive |
| 3 | `lk_b` ships **completely empty** (all zeros) | Xiaomi ships only `lk_a`; `preloader_b` dies jumping into the void | `fastboot flash lk_b lk_a.img` |
| 4 | `fastboot set_active` aborts host-side | Old fastboot's `getvar has-slot:b` returns a bogus `2` | Use fastboot ≥ 35 (platform-tools 37 works) |
| 5 | lk_b armed path: `hdr_loaded` assert | The armed (retry>0) path never populates `g_boot_info` for non-stock boot images | Use the 240 s dwell → the final-try (retry 0) path works |
| 6 | lk_b cycles die at random init points | The boot DTB must fit lk_b's buffer (~160–180 KB) | Shrink the mainline DTB (see below) |
| 7 | MTK DTB container | Boot DTB blob = 64-byte MTK wrapper (magic `d7b7ab1e`) + standard FDT | Parse the wrapper, patch the FDT, rewrap |

## Key discoveries

### MTK DTB container format
```
+0x00: magic 0xd7b7ab1e (MTK DTB container)
+0x04: full container size (u32 BE)
+0x08–0x1C: metadata (0x20, 0x20, 1, 0x20, 0x800, 0)
+0x20: real FDT totalsize (u32 BE)
+0x24: wrapper header size = 0x40
+0x40: standard FDT (magic 0xd00dfeed, version 17)
```

### lk_b/preloader behaviour
- The preloader keeps slot state in a **private store** (likely RPMB),
  separate from the misc partition's `bootloader_control` struct.
- `fastboot set_active b` re-arms the private store's retry counter to 7.
- Each armed cycle burns one retry and dies at a random point in lk_b
  init. The final-try (retry 0) path populates `g_boot_info` correctly.
- The lk segment is **Thumb-2** (not ARM64), loads at 0x48200000,
  links at 0x48400000, segment data begins at container file offset 0x40.
- `lk_b` ships **completely empty** (all zeros) on this unit.
- LK boot logs are written to the `expdb` partition (40 MiB, ring of
  ~344 KiB records) — the no-UART debugging channel.

### misc partition `bootloader_control` (decoded + CRC-verified)
```
+0x800: slot_suffix[4]   "_a\0\0" or "_b\0\0"
+0x804: magic[4]         "BCAB"
+0x808: version u8       1
+0x809: nb_slot u8       2
+0x80A: recovery_tries   0
+0x80B: merge_status     0
+0x80C: slot A byte      {priority:4, tries:3, successful:1} = 0x9F
+0x80E: slot B byte      same layout
+0x81C: CRC32 (zlib) of bytes 0x800..0x81B, stored LE
```

### Stock kernel quirks
- `CONFIG_DEVTMPFS` **unset** (and `CONFIG_UEVENT_HELPER` unset)
- The boot image's DTB blob uses the MTK container (magic `d7b7ab1e`)
- The kernel blob is gzip-compressed (`Image.gz`)
- `console=tty0 console=ttyS0,921600n1` comes from LK's own cmdline prefix
- `initcall_debug=1` is in LK's prefix (useful for early-boot debugging)

### Mainline DTB shrink (290 KB → 64 KB)
The A22X mainline DTB carries 162 KB of `spi4@11018000` touchscreen config
blobs (FocalTech/Novatek, `status = "disabled"`, wrong touch controller for
this unit) plus ~400 other disabled nodes. Stripping them:
1. Decompile the MTK container (patch magic to `d00dfeed`, `dtc -I dtb -O dts`)
2. Remove `spi4@11018000` and all top-level nodes containing
   `status = "disabled"` (keep gpio/pinctrl/pwrap/msdc/usb/i2c/uart/timer/
   gic/apmixed/topckgen/infracfg/spm/pmic/auxadc/efuse/cpu/gce/mipi/phy/
   wdt/rtc/dvfsrc/thermal/dramc/toprgu/watchdog/t-phy/musb/sspm/mt6360)
3. Enable `mt6360_pmu_dts` (change status to `"ok"`) for SD card power
4. Recompile with dtc, restore the `d7b7ab1e` magic, update the wrapper sizes

## Files

| File | Description |
|------|-------------|
| [NOTES.md](NOTES.md) | Full research notes (mirrored from the working tree) |
| [init](init) | Fixed initramfs init script (devtmpfs workaround, PID-1 never exits, microSD diagnostics) |
| [dtb_shrink.py](dtb_shrink.py) | MTK DTB container parser + node stripper |
| [expdb_parse.py](expdb_parse.py) | expdb partition record parser (LK boot log extraction) |
| [build_probe.sh](build_probe.sh) | Boot image build recipe (mkbootimg + avbtool) |
| [boot_recipe.sh](boot_recipe.sh) | The flash + boot recipe |

## Next steps for the mainline kernel

1. The USB MUSB + t-phy driver bring-up (the gadget configures but doesn't
   enumerate on the host — a mainline driver issue)
2. The msdc1 + MT6360 regulator path for microSD diagnostics
3. The pstore/ramoops DT node for console visibility
4. The lk_b armed-path fix (requires the MTK LK source for build tag
   `camellia-1ad90425c-20220608154426-20230227220721`)

## License

SPDX-License-Identifier: MIT
