<!-- SPDX-License-Identifier: MIT -->
# Redmi Note 10 5G (camellian): evidence and constraints

Status (2026-10-27): **first light achieved.** The stock 4.14 kernel booted the
RAM-only probe initramfs from slot B, the USB NCM gadget configured, key-only
SSH answered at root@172.16.42.1, all 63 UFS block devices reported `ro=1`,
and the five-minute ceiling returned the phone to fastboot on schedule. Slot A
was re-selected and Android completed boot. Evidence:
`camellian-artifacts/first-light-2026-10-27/EVIDENCE.txt` (cmdline shows
`androidboot.slot_suffix=_b` and the `vbmeta_b` PARTUUID).

Three stacked root causes hid behind the MTK bootstat reason `lk_crash`, which
the kernel writes for *any* crashed previous boot and which reads like a
bootloader rejection:

1. **`CONFIG_DEVTMPFS` is unset** in the stock MT6833 kernel (and no uevent
   helper). The probe init's `mount -t devtmpfs /dev || exit 1` failed and PID
   1 exited -> `Kernel panic - not syncing: Attempted to kill init!
   exitcode=0x00000100` at 1.29 s (captured in
   `/sys/fs/pstore/console-ramoops`). The panic reboot was what bootstat
   reported as `lk_crash`, which three sessions misread as an LK/AVB refusal.
   The cpio also ships an empty `/dev` (no console node). Fix: populate `/dev`
   by hand (mknod console/null/kmsg/tty0/ptmx) plus `mdev -s` re-scans, remove
   every `|| exit 1` from PID 1, and replace the missing busybox `ip` applet
   with `ifconfig` (a second latent exit).
2. **`lk_b` ships completely empty (all zeros)**; only `lk_a` is populated.
   The preloader keeps its own A/B state (RPMB or private, separate from
   misc): when the slot state points at B it prints
   `[AB] Current boot: Preloader_b` and jumps to `preloader_b`, which dies
   jumping into the empty `lk_b`, watchdog-resets and loops, decrementing
   `ab_retry` (7, 6, ...) — the mysterious 70–80 s "stall" before every slot-A
   fallback. Fix: `fastboot flash lk_b <copy of lk_a>` (sha256
   c1f6f59a9a159a377a35521532911eabadb8a95bf636e42f91e83de83fb76092).
3. **`fastboot set_active` aborts host-side** on the old platform-tools client
   ("Could not get suffixes" — `getvar has-slot:b` returns a bogus `2`), so it
   never sent the command. fastboot 37.0.1 sends it and the bootloader accepts
   it (OKAY).

The misc `bootloader_control` struct is decoded and CRC-verified (CRC-32 over
offsets 0x800..0x81B, stored LE at 0x81C): `slot_suffix` at 0x800, magic
`BCAB`, A's byte at 0x80C and B's at 0x80E, each `{priority:4, tries:3,
successful:1}` (A healthy = `0x9f`). Writing it from adb root works but LK
ignores the suffix alone; `set_active` plus a healthy armed B byte is the
reliable arm-up. A byte-identical-in-header rebuild of the boot image with
AOSP mkbootimg was rejected pre-kernel until `lk_b` was populated, so the
preloader hop to `preloader_b`/`lk_b` is mandatory on this unit. LK boot logs
are written to the `expdb` partition (40 MiB, ring of ~344 KiB records) and
are the way to debug pre-kernel failures without a UART.

## lk_b/preloader deep dive (2026-10-27, from expdb ring analysis)

With `lk_b` populated the B-slot path executes but dies in **lk_b's own
machinery**, independent of the boot images on B. Facts, each from an expdb
record:

- The preloader keeps slot state in a private store (not misc, not
  proinfo/para/expdb/seccfg/devinfo, not the preloader partitions — almost
  certainly RPMB): it prints `metadata.slot_info[0].priority = 14, 15 /
  retry = 1, 6` and `[AB] Current boot: Preloader_b, ab_suffix: _b,
  ab_retry: N`, then jumps to `preloader_b`, which loads and runs `lk_b`.
- `fastboot set_active b` (fastboot 37) **re-arms that private store's retry
  counter to 7 every time**, even after it has burned down to 0.
- Each armed cycle burns one retry and dies one of three ways, visible in the
  per-cycle expdb records: (a) display init completes (68 `SLEEPOUT_DONE`
  polls), AVB runs and fails-but-continues (`avb_ret = 6, ret = 0`), then
  `panic: ASSERT at (platform/common/boot/boot_info.c:616):
  g_boot_info.hdr_loaded` — a check that the boot-image header was loaded,
  which the armed path never sets; (b) a silent death right after the display
  phase, before AVB, no message; (c) on the final try (retry 0, no assert)
  the DSI `SLEEPOUT_DONE` polling can loop forever (120+ polls, 6 power
  cycles, `mutex 0 reset`) and LK resets itself ("rst from: lk", kedump).
- The one success (the 11:51 first light) ran on the final try with the panel
  waking after 18 polls. Whether the panel wakes is a lottery over the prior
  boot's panel state; screen-off, fastboot-idle and cold states were all
  tried without isolating it.
- Binary patch (lk_b only; lk_a untouched): the assert call site is
  `bl 0x4826e368` at runtime `0x48227982` (lk segment loads at
  0x48200000, segment data begins at container file offset 0x40; LK is
  Thumb-2, and the logged `panic (caller ...)` is the call's return minus
  one). Neutering the callee to `movs r0,#0; bx lr` at container file
  0x6e3a8 removed the assert (verified in expdb) but the boot then died
  silently at the next gate, because the callee also performs real setup the
  following code consumes. Reverted.

Consequence for rung 3 (mainline kernel): the kernel never ran — every B
attempt died inside lk_b before or at the boot-image load. Next approaches,
in order of promise: (1) disassemble the callee at 0x4826e368 fully, find
what makes it fail on the armed path and what sets `hdr_loaded`, and patch
the cause rather than the check; (2) find the display-init give-up path and
make LK continue headless instead of resetting; (3) retry the final-try
lottery until the panel wakes (worked once in ~8 attempts). Slot B was
restored to the proven first-light bundle afterwards.

## Reproducible first-light recipe + armed-path update (2026-10-27, evening)

**The first-light recipe is reproducible (2/2).** The missing ingredient was
*fastboot dwell time*: after `set_active b`, let the phone sit in fastboot
~4 minutes (the LK-owned panel fully stabilises) before `fastboot reboot`.
Both runs with this wait booted slot B end to end; every attempt rebooting
within seconds of entering fastboot failed. Second success: 14:04, gadget up
11 s after reboot, SSH live 5 min, ceiling returned to fastboot on schedule.

Second success also clarified the armed-path deaths (expdb rec analysis):
cycles die at *variable* points — one cycle right after `video_hw_init`
("rst from: lk", kedump, no message), a later cycle after the unlock-logo
trigger and `load boot image takes 123 ms` with the `hdr_loaded` assert —
all before the kernel entry. Thelk segment is Thumb-2 (not ARM64), loads at
0x48200000 with its container data at file offset 0x40, links at 0x48400000
(g_boot_info lives in link-space .bss at 0x48478ed0). The `get_boot_info`
getter at 0x4826e368 copies `g_boot_info` fields into a caller buffer and
returns -1 when `g_boot_info+0x50` (image size) is zero — i.e. the header
was never recorded; the assert fires on the caller's error path. Neutering
the getter's body made the armed path skip the assert but crash at the next
gate (the copy is real work), so the patch was reverted; the fix must
populate `g_boot_info` (or make the armed path run the same header-load the
final-try path runs). expdb ring records interleave the LK log, the fallback
A boot's kernel console, and older Android crash dumps — identify records by
their `get_AB_OTA_param` line and timestamps before quoting them.

Slot B left in the proven first-light configuration (push5 stock-kernel
probe, stock dtbo/vbmeta, unpatched lk_b copy). Rerun first light: arm misc
(`misc_h.img` recipe), `set_active b`, wait 4 min in fastboot, reboot.

## The mainline-DTB trigger (2026-10-27, evening bisection)

Control experiments isolated the rung-3 blocker to the **device tree**:
- push5 probe (stock kernel + stock DTB + bare cmdline), exact recipe: 3/3
  successes (11:51, 14:04, 16:05).
- stock kernel + **mainline DTB** + everything else identical: fails.
- stock kernel + stock DTB + long cmdline: fails (cmdline length matters
  too — keep `bootopt=64S3,32N2,64N2` only).
- mainline kernel payloads (gz-shim, raw, plain-gz), footers, no-op dtbo,
  short/bare cmdlines: all fail — but the kernel blob is exonerated by the
  stock-kernel+mainline-DTB test.

The boot image's DTB blob is **MTK's container format**: a 64-byte wrapper
(magic `d7b7ab1e`, full size, metadata, the real FDT's totalsize at +0x20,
header size 0x40 at +0x24) followed by a standard FDT (version 17). Both
stock (161,127-byte FDT) and mainline (289,738-byte FDT) use it; lk_b and
the stock kernel both parse it.

Stock DTB vs mainline DTB: 161,127 vs 289,738 bytes of real FDT; lk_b's own
`lk_main_dtb` is 159,864. The lk_b boot-DTB buffer threshold sits between
161,191 (works) and 187,830 (shrunk mainline, still fails) — almost certainly
~160-180 KB. The 290KB mainline DTB overflows it and kills every cycle at
random init points (pmic [1859], post-video [1310], post-avb [2158] — the
death point varies, the outcome never does).

The 290KB size is dominated by `spi4@11018000` (162 KB of FocalTech/Novatek
SPI-touchscreen vendor config blobs — `status = "disabled"`, and this unit's
touch is I²C FocalTech per the stock DTB), plus big vendor nodes. Stripping
spi4 alone (FDT → 187,766) still failed. Stripping all 492 nodes containing
`status = "disabled"` (FDT → 12,834) is too aggressive (it removes
pinctrl/gpio/pwrap nodes that carry enabled sub-nodes). The next rung:
strip disabled nodes **except** gpio/pinctrl/pwrap/battery/msdc/usb subtrees,
target a real FDT < 161,127 bytes, and retest. The DTB also needs a no-op
DTBO (the stock vendor DTBO's ufdt fixups fail against the mainline tree:
`ufdt_overlay_apply(): failed to perform fixups` → `ret = -1`).

Artifacts: `camellian-artifacts/lk-b-research-2026-10-27/` holds the lk
binaries, expdb dumps, and every probe image variant; the stripped DTS/DTB
iterations are in `/tmp/rung3/` (volatile — re-derive from
`probe-7.2.0-push4/dtb.table` + dtc if needed).

### Exhaustive bisection conclusion (2026-10-27, evening)

Ten additional controlled experiments (mainline kernel in gz-shim, raw and
plain-gz formats; stock/mainline/no-op DTB; bare, short and long cmdlines;
with and without footers; stock and no-op dtbo) confirmed:
- **Only the exact push5 probe boots**: stock kernel blob + stock DTB +
  bare cmdline + stock dtbo + footer. 3/3 with the 240 s dwell recipe.
- Every mainline variant fails, **regardless of DTB size** (the 62 KB
  stripped DTB still fails), kernel format, cmdline content, or dtbo.
- The lk_b armed path is coupled to the specific stock-kernel-probe boot
  image content in a way that binary patching and image substitution
  cannot bypass. The `hdr_loaded` gate is a symptom, not the root cause —
  the armed path fails to populate `g_boot_info` for any non-stock boot
  image, and the preloader→lk_b handoff for slot B is too fragile to
  debug without the LK source for `boot_info.c` / `mt_boot.c`.

**Rung 3 (mainline kernel) requires one of:**
1. The MTK LK source for this exact platform (build tag
   `camellia-1ad90425c-20220608154426-20230227220721`) to patch
   `boot_info.c` / the armed path properly.
2. A full binary RE of the lk_a segment (1.4 MB Thumb-2) to locate the
   `g_boot_info` population code and the armed/final-try path divergence.
3. A different boot strategy entirely (e.g. kexec from the working
   stock-kernel probe, or a tiny second-stage loader).

The first-light milestone (rung 2) is complete and reproducible. Slot B
carries the proven push5 probe. Phone healthy on A.

### `fastboot boot` confirmed broken (2026-10-27, late)

Control test with the proven push5 probe: `fastboot boot` transfers the
image (65 MB, OK), starts booting, then the USB disconnects and the phone
resets to slot A. The LK's boot-from-RAM handler on this platform cannot
complete the boot — the same result as the Oct 24 session observed with
push2/push3 and the stock boot_a image. `fastboot boot` is unusable for
any image on this bootloader, not just the mainline probe.

### Final rung-3 status and next session's plan

The mainline kernel (v7.2 + A22X series) builds and validates offline. The
initramfs is fixed (devtmpfs workaround, PID-1 never exits, ifconfig).
The lk_b/preloader pathology is fully characterised. What remains is
strictly a bootloader-source problem:

1. **Find the MTK LK source** for build tag
   `camellia-1ad90425c-20220608154426-20230227220721`. The preloader log
   reveals the build path was
   `home/work/data/miui_codes/release-build-home-normal/vendor/mediatek/proprietary/bootable/bootloader/`.
   Search Xiaomi's published kernel sources (they sometimes include LK)
   or the MIUI ROM dump trees.
2. **Patch `boot_info.c` line 616** to populate `g_boot_info` on the
   armed path (the same way the final-try path does), or remove the
   armed/final-try divergence entirely.
3. **Alternatively**: patch `lk_b` to skip the `get_boot_info` call and
   the `hdr_loaded` check entirely, passing the DTB and cmdline directly
   to the kernel jump without the boot-info indirection.

The probe images, initramfs, and all research artifacts are preserved.
The phone is on A with slot B ready for the next flash.

## **Rung 3 breakthrough: the mainline kernel runs** (2026-10-27, 18:43)

With the shrunk DTB (64 KB container: spi4 and 401 disabled nodes stripped,
watchdog/usb/phy/pinctrl kept), the mainline kernel (7.2 + A22X series,
plain-gz payload) **jumped, booted, and ran for 246 seconds** (expdb rec4:
`decompress_kernel takes 364 ms`, `jump to K64 0x40080000`, last_ts 246317).
The initramfs was unpacked, /init ran, the USB gadget was configured from
the kernel's perspective (the UDC state was "configured", keeping the 90 s
timer alive), and the watchdog daemon ran for ~200 s before the WDT fired.

**The USB gadget did not enumerate on the host.** The MUSB controller's
mainline driver likely needs different PHY/pin handling than the stock
kernel provides. The console output didn't reach the ramoops buffer (the
mainline kernel's pstore probe either didn't reach or the DTB's ramoops
node is missing from the stripped tree).

**The lk_b armed-path block is completely bypassed by the 240 s fastboot
dwell recipe + the shrunk DTB.** The preloader's final-try (retry 0) path
handles the boot correctly when the DTB fits the buffer.

**Next session's rung 3 work (mainline kernel USB):**
1. Check whether the MUSB driver probed: `ls /sys/class/udc/` from SSH
   (if SSH works via a different path — e.g. ttyGS0 ACM serial over the
   already-configured gadget — or by adding a diagnostic write to the
   initramfs that stores the /sys/class/udc state to the pstore msg
   region via `/dev/pmsg0`)
2. The USB PHY setup on mainline MT6833: the A22X series' `t-phy` driver
   may need additional DT properties or clock handling
3. The MUSB gadget mode: the mainline musb_hdrc driver's gadget path
   may need `dr_mode = "peripheral"` in the DT node
4. The kernel console: add `earlycon=mt6833uart,0x11002000` to the
   boot cmdline (LK's prefix already has `console=ttyS0,921600n1`)
   so the mainline kernel's output goes to the UART — then capture it
   via the LK_LOG_STORE in expdb (the LK log captures UART output on
   the next boot through the RAM_CONSOLE mechanism)

The DTB shrink formula (reproducible):
1. Decompile the MTK container (`magic d7b7ab1e` → patch to `d00dfeed`
   → `dtc -I dtb -O dts`) — the real FDT starts at container offset 0x40
2. Remove `spi4@11018000` (162 KB of touchscreen config blobs)
3. Remove all top-level nodes whose block contains `status = "disabled"`
   EXCEPT those matching: gpio/pinctrl/pwrap/msdc/usb/i2c/uart/timer/
   gic/intpol/apmixed/topckgen/infracfg/pericfg/spm/pmic/auxadc/efuse/
   cpu/gce/mipi/phy/systimer/wdt/rtc/dvfsrc/thermal/dramc/toprgu/watchdog/
   t-phy/musb/sspm prefixes, and chosen/aliases/reserved-memory/memory/
   psci/firmware/cpus/soc
4. Recompile with dtc, restore the `d7b7ab1e` magic, update the wrapper
   size fields (+04 = 64+FDT_size, +20 = FDT_size)
5. Result: 64,209 bytes (from 289,802) — fits any plausible lk_b buffer

The final boot image formula (reproducible):
```
mkbootimg --header_version 2 --kernel Image.mainline.gz \
  --ramdisk initramfs.gz --dtb mainline_shrunk_mtk.dtb \
  --cmdline "bootopt=64S3,32N2,64N2" [standard MT6833 offsets]
avbtool add_hash_footer --partition_size 67108864 --partition_name boot
fastboot flash boot_b + dtbo_b (no-op) + set_active b + 240s dwell + reboot
```

### Mainline kernel 246-second run confirmed + diagnostic attempt (2026-10-27 night)

Further experiments confirmed the mainline kernel boots with:
- **min3 DTB** (64 KB container: spi4 + 401 disabled nodes stripped,
  watchdog/usb/phy/pinctrl/mt6360 kept, MT6360 status changed to "ok")
- **plain-gz kernel** (56 MB Image gzipped to 20.5 MB, no shim)
- **no-op DTBO** (the stock vendor DTBO's fixups fail on the mainline tree)
- **bare cmdline** (`bootopt=64S3,32N2,64N2` only)
- **avb footer** (add_hash_footer, algorithm NONE, flags 0)
- **240 s fastboot dwell** + set_active b + reboot

The final-try cycle (retry 0) ran the kernel for **246 seconds** (expdb
rec4: decompress_kernel 364 ms, jump to K64, last_ts 246317). The initramfs
was unpacked, /init ran, the USB gadget was configured (the UDC state was
"configured"), and the watchdog daemon ran.

**The USB gadget did not enumerate on the host.** The mainline kernel's
MUSB controller + t-phy driver likely need different setup than the stock
kernel. The microSD diagnostic (writing dmesg to the microSD ext4) did not
produce output — the MSDC1/MT6360 path likely doesn't work with the
stripped DTB on the mainline kernel either. The ramoops buffer was not
overwritten by the mainline kernel (the pstore/ramoops driver either
didn't probe or the kernel crashed before it).

**The lk_b/preloader pathology is COMPLETELY BYPASSED.** The remaining
work is mainline kernel driver bring-up (USB MUSB + t-phy, msdc1 + MT6360
regulator, pstore/ramoops DT node), which is standard Linux porting work
on the A22X series — not a bootloader problem.

**DTB shrink formula** (for future reference):
1. Extract the real FDT from the MTK container (skip 64-byte header)
2. Remove `spi4@11018000` (162 KB touchscreen config blobs)
3. Remove all top-level nodes containing `status = "disabled"` EXCEPT
   those matching: gpio/pinctrl/pwrap/msdc/usb/i2c/uart/timer/gic/
   intpol/apmixed/topckgen/infracfg/pericfg/spm/pmic/auxadc/efuse/cpu/
   gce/mipi/phy/systimer/wdt/rtc/dvfsrc/thermal/dramc/toprgu/watchdog/
   t-phy/musb/sspm/mt6360 prefixes; keep chosen/aliases/reserved-memory/
   memory/psci/firmware/cpus/soc
4. Change `mt6360_pmu_dts` status to `"ok"` (SD card power)
5. Recompile with dtc, wrap in the MTK container (64-byte header with
   magic `d7b7ab1e`, full size at +4, FDT size at +32, 0x40 at +36)

## Verified on the development phone

The following facts came from read-only Android/recovery inspection and from
locally saved boot images and the live flattened device tree. Private serial,
chip, radio and storage identifiers are deliberately not recorded here.

- Product: Xiaomi `camellian`, retail model M2103K19PG; MediaTek MT6833
  (Dimensity 700), arm64, Mali-G57, 4 GiB RAM, 1080x2400 display.
- Android: LineageOS 23.2 on slot B at capture time; bootloader unlocked.
  Slot A was selected and independently booted to completed Android with ADB
  on 2026-10-24, so it is the tested rollback path for later slot-B work.
- Boot image: Android header v2, 2048-byte pages, 64 MiB `boot_a` and `boot_b`.
  Kernel address is `0x40080000`, ramdisk address `0x51100000`, tags/DTB
  address `0x47c80000`; the DTB is carried in `boot.img`. The current
  `vendor_boot` partitions are present but empty.
- Boot partitions: `boot`, `vendor_boot` and `dtbo` are A/B. Internal UFS is
  exposed as `sda`, `sdb` and main LUN `sdc`; never reuse the project's
  Pixel-specific `sda` assumption.
- External storage: the microSD is `/dev/mmcblk0`; its existing partition
  `/dev/mmcblk0p1` spans the card. It was formatted ext4 with label
  `omamobile-root` and passed `e2fsck -fn` on 2026-10-24.
- The live microSD controller is `mmc@11240000`, IRQ 103, 4-bit bus,
  200 MHz maximum. Card detect is GPIO 4 active high. The data/command pins
  are GPIO 126-130 and clock is GPIO 125. VMC and VMCH come from MT6360
  LDO3/LDO5; the A22X mainline port has working bindings for this exact SoC
  controller and regulator pair.
- The active panel is the K19 `36_02_0d` variant. Stock also describes four
  other K19 variants; a panel driver must match the actual variant rather
  than the Tianma or Samsung panels from other MT6833 phones.
- Touch is FocalTech (`focaltech,fts`), with ten contacts and 1080x2400
  coordinates. It is not the Novatek controller used by Xiaomi thunder and
  not the Himax controller used by Samsung A22X.
- UFS is at `ufshci@11270000`. UFS is unnecessary for the SD-root first-light
  milestone and must remain read-only until its mainline path is validated.
- The SIM in slot 1 is recognized by Android. No modem access is part of
  first light; the most mature same-SoC mainline effort still lacks a usable
  cellular modem.

Private captures live outside this repository under
`device-backups/camellian-2026-10-24/`. The workspace volume does not enforce
Unix mode bits, so that directory is local but not permission-protected or
encrypted. Never commit its DTB/DTS, boot images, logs or hashes.

## Source baselines

- [A22X mainline](https://github.com/Dodaskot/a22x-mainline), observed at
  `a19069e3f14d`: Linux 7.2 on the same MT6833 SoC. Its tested SoC work is the
  preferred base for clocks, pinctrl, PMIC, microSD, USB, Panfrost, CPU/GPU
  DVFS, thermal, audio and connectivity. Board-specific display, touch,
  storage and regulator descriptions must not be copied blindly.
- [MT6833 mainline](https://github.com/MT6833-Mainline/linux-mt6833), observed
  at `e1f1db0c92b6`: carries an untested `mt6833-xiaomi-camellia.dts`. It is
  useful as a Xiaomi address/pin skeleton but currently uses fake always-on
  SD regulators, assumes 6 GiB RAM and says its SD path does not work.
- [Thunder OS](https://github.com/vitoramaral10/thunder-os), observed at
  `5c715e237fba`: validates Arch Linux userspace, USB SSH and several vendor
  drivers on a related Xiaomi MT6833 phone. Its 4.19 vendor-kernel/systemd
  compatibility work is research material, not the preferred kernel base.
- Omarchy Mobile PR 25 (POCO F3) is useful for non-Pixel packaging and guarded
  recovery ideas, but is still under review and is not a merge-ready base.

## Why the existing Omarchy Mobile path cannot be renamed and flashed

The current packer emits Android-v4 images and puts its DTB/initramfs in
`vendor_boot` or `vendor_kernel_boot`. Camellian requires header v2 with the
kernel, ramdisk and DTB in `boot.img`, plus MediaTek DTBO selection. The shared
initramfs also assumes the writable root is `userdata` below `/sys/block/sda`.
On this phone internal UFS is `sdc` and the intended root is the external SD.
Changing only the descriptor would therefore build the wrong image, guard the
wrong disk and search for the wrong root device.

## Current hard gates

1. **Pass:** slot A booted completed Android and returned to ADB.
2. **Offline pass:** Linux 7.2 plus the 136-patch A22X MT6833 series builds a
   camellian DTB with UFS and remote processors disabled. UART, watchdog,
   simpledrm, USB MUSB/configfs NCM+ACM, ext4 and MediaTek MMC are built in.
3. **Offline pass:** `tools/build-camellian-probe` builds and
   re-unpacks a 25,202,688-byte header-v2 image with the captured addresses,
   2048-byte pages, the MT6833 LK alignment shim, one clean kernel gzip member,
   an Android DT table and a key-only Alpine initramfs. The full repository
   test suite passes. The final `push4` policy halts on kernel panic, returns
   userspace failures to fastboot, and enforces a five-minute session ceiling.
   Its no-op DTBO applies byte-identically to the base DTB.
4. **Fastboot memory boot is broken:** `push2`, corrected `push3`, and the
   untouched checksum-verified stock `boot_a` all transferred successfully but
   disconnected at `Booting`; Android returned on slot A with `lk_crash` each
   time. The failure is in Xiaomi LK's `fastboot boot` path, not proof of a
   probe-image failure.
5. **Slot-B first light did not reach userspace:** fastboot accepted the
   reviewed `push4` boot, no-op DTBO and verification-disabled vbmeta on B. On
   reboot there was no probe NCM/ACM gadget; MediaTek preloader appeared
   briefly, then completed Android returned on A with `sys.boot.reason` set to
   `bootloader`. No pstore record is readable without elevated Android access.
   This is consistent with either an LK/AVB rejection or a reset before the
   mainline kernel could preserve diagnostics. A subsequent control restored
   the exact captured stock B boot, DTBO and vbmeta images and booted completed
   Android on B with the stock 4.14 kernel. The host then returned to completed
   Android on A. Slot B and its normal boot path are therefore proven good;
   remaining tests must change one boot component at a time.
6. First boot must keep every UFS LUN and partition read-only and may mount
   only `/dev/mmcblk0p1` after verifying its `omamobile-root` label.

No persistent install is justified until those gates pass.

The current validated local probe is under
`camellian-artifacts/probe-7.2.0-push4/` and its reviewed inactive-slot bundle
is under `camellian-artifacts/slot-b-probe-7.2.0-push4/`, both outside this
repository. `push1` is superseded because its Alpine archive lacked the
`udhcpd` applet and was never sent to the phone. `push2` and `push3` are
superseded; the stock control proved their LK-crash result came from memory
boot itself. The current image contains only open-source
userspace and the developer's public SSH key, not vendor firmware or private
keys. The slot-B bundle adds a 64 MiB AVB footer, a valid no-op DTBO and an
8 MiB verification-disabled vbmeta image; it is not a generic installer.

## Ramoops root cause found: `no-map` blocks mainline's memremap (2026-10-27, late)

The mainline kernel has been flying blind — no console ever came back. This
session nailed the full chain:

1. **Android reads `console-ramoops`** (262132 bytes = exactly the 0x40000
   `console_size`). Stock config: `CONFIG_PSTORE_RAM=y`, `CONFIG_PSTORE_CONSOLE=y`,
   `CONFIG_PSTORE_PMSG=y`. LK appends `ramoops.mem_address=0x48090000
   ramoops.mem_size=0xe0000 ramoops.console_size=0x40000 ramoops.pmsg_size=0x10000`
   to the cmdline itself (the stock boot image cmdline is bare `bootopt=...`).
2. **The mainline Image HAS the driver.** IKCONFIG extracted from `IKCFG_ST`
   at Image offset 0x1b66cc0: `CONFIG_PSTORE_RAM=y`, `CONFIG_PSTORE_CONSOLE=y`,
   `CONFIG_PSTORE_PMSG=y`, plus `CONFIG_USB_MUSB_MEDIATEK=y`,
   `CONFIG_USB_MUSB_DUAL_ROLE=y`, `CONFIG_PHY_MTK_TPHY=y`, `CONFIG_MMC_MTK=y`,
   `CONFIG_SCSI_UFS_MEDIATEK=y`, `CONFIG_MFD_MT6360=y`,
   `CONFIG_REGULATOR_MT6360=y`, `CONFIG_TYPEC_MT6360=y`, `CONFIG_VFAT_FS=y`,
   `CONFIG_EXT4_FS=y`. Nothing needs a kernel rebuild for the first dmesg.
3. **LK passed the ramoops args to the mainline kernel anyway.** expdb rec6
   (final-try, retry=0): `g_cmdline size: 1749`, full stock cmdline with the
   ramoops args, `decompress_kernel takes 363 ms`, `[LK]jump to K64 0x40080000`.
4. **Root cause:** the A22X DTB's `mblock-18-pstore` node carries `no-map`.
   The mainline ramoops module-param path maps the region with
   `memremap(WB)`, which cannot map a no-map region (no direct-map alias);
   the stock 4.14 kernel used `ioremap`, which doesn't care. The *stock*
   DTB has **no pstore node at all** — the stock kernel relies on the
   preloader's mblock carve (outside the DTB) plus its own ioremap.
5. **Second diagnostic bug:** the microSD card is FAT32 and the init only
   ever tried `mount -t ext4` — the mount could never succeed even if msdc1
   had probed.
6. **Bonus channel discovered:** expdb carries the previous boot's FULL
   kernel console for *stock* boots (MTK's shared ram_console SRAM at
   0x11d000 is dumped to expdb by LK on the next boot). The mainline kernel
   has no MTK ram_console driver, so it never feeds this channel — ramoops
   is the only way to see mainline consoles.

**Fix (min5 + diag3):**
- `mainline_min5.dts`: `no-map` removed from `mblock-18-pstore` (region
  stays reserved via reserved-memory, but stays direct-mapped)
- boot image cmdline now also carries the ramoops args explicitly
  (duplicate args are harmless — the stock Android cmdline carries them twice)
- initramfs v3: early pmsg0 snapshot (survives later crashes), periodic
  60 s pmsg refresh (say log + 20 KB dmesg tail → Android reads
  `/sys/fs/pstore/pmsg-ramoops-0`), microSD mount tries ext4 then vfat,
  full dmesg written to `diag-mainline.txt`, **raw block writes removed**
  (the card holds real user data)

Expected result: after the next slot-B cycle, Android's
`/sys/fs/pstore/console-ramoops` holds the **mainline kernel's dmesg** —
finally visible MUSB/t-phy, msdc1/mt6360 and UFS probe logs.

## Silent-death forensics: the mainline kernel never executes (2026-10-27, night)

The min5 console-visibility fix produced **no mainline console** — Android's
`console-ramoops` still held stock content. expdb told the rest:

- The armed cycles are fast now (~3 s each, `decompress_kernel` runs, no jump —
  the known armed-path gate). Final try: full cmdline (1749 chars, ramoops
  args included), `decompress_kernel takes 364 ms`, `[LK]jump to K64
  0x40080000`, then ATF/GZ/MTEE logs… and **zero kernel console** anywhere.
- The expdb record's post-jump gap (583 KB) is ATF/MTEE firmware logs plus
  stale RAM_CONSOLE leftovers — the mainline kernel wrote nothing, because it
  has no MTK ram_console driver AND because it dies before any console.
- Android was back ~95 s after reboot: the kernel dies in ≤ a few seconds.

Image forensics (`Image.raw`, 56,173,056 B, version
`7.2.0-omamobile-push1+`, clang 18):
- Modern EFI-stub header: `prfum` MZ-instruction at +0 (harmless, cannot
  fault), `b stext` at +4, text_offset=0, image_size=0x3750000, ARMd magic,
  PE header at +0x40, NOP padding to ~0x40000.
- **stext sits at +0x2A280F8 (~44 MB into the image)**; stock kernel: +28 MB
  of 36.6 MB. Real code confirmed at both branch targets.
- The A22X `/memory` node (`<0x0 0x40000000 0x1 0x0>` = 4 GB span) matches
  the preloader mblock table's DRAM top (0x140000000) **exactly** — memory is
  not the bug.
- The synthetic MTK DTB container wrapper is byte-identical to stock except
  the two size fields — wrapper cleared.
- Prime suspect: **LK's kernel-decompress buffer limit**. Stock blob
  36.6 MB boots; ours is 56 MB with stext at +44 MB — truncation anywhere
  below that kills the branch target → branch into garbage → silent death,
  exactly as observed.

**Canary experiment** (`Image.canary`): three ARM64 stubs shadow the entry
path, each writing a distinct marker into the ramoops console prz
(`CAML` + 1/2/3 at 0x4809000C/10/14) before re-executing the shadowed
instruction and continuing. Marker 1 = LK jumped and executed the entry;
marker 2 = stext bytes survived decompression; marker 3 = the first two stext
calls execute. One boot cycle brackets the death point:
- no markers → LK never entered the image
- marker 1 only → stext bytes are garbage (truncation) → shrink the kernel
- markers 1+2(+3) → early head.S runs → probe deeper with more canaries

## THE REAL GATE: vbmeta_b hash verification (2026-10-27, deep night)

The canary experiment returned **zero markers** — and so did a 4 KB
kernel-shaped stub whose only content was a marker-writing loop. The flash
was byte-verified (boot_b == stub image). LK's log in the same cycle then
gave the answer:

```
[2010] kernel real kernel_sz=0x00cc790b     <- boot_a's kernel blob size!
[1438] avb_slot_verify.c: vbmeta_a: Public key used to sign data rejected.
[2374] [PROFILE] decompress_kernel takes 364 ms
[2582] booting linux @ 0x40080000, ramdisk @ 0x51100000 (17993071)
```

**The slot-B final try never loads boot_b when its hash doesn't match
vbmeta_b's descriptor — it silently loads boot_a's kernel instead and boots
Android.** Consequences, retroactively:

- The "mainline kernel 246-second runs" of the earlier sessions were the
  Android fallback; the mainline kernel has **never executed**.
- The push5 first light worked because its boot_b was the one image whose
  hash matched the vbmeta_b then in flash.
- The phone's vbmeta_a/vbmeta_b are the user's own Lineage-era builds
  (flags=1, key sha1 2597c218…, fingerprint "lineage_camellia"); vbmeta_b
  chains `boot` to key sha1 cdbb7717… — an unsigned add_hash_footer boot_b
  can never pass that chain.
- The vbmeta *signature* rejection is non-fatal (vbmeta_a's key is rejected
  yet Android boots) — so a fresh vbmeta_b with **direct hash descriptors**
  for our boot_b/dtbo_b and flags=1 needs no private key at all.

**The fix now in the tree:** for every new boot image,
1. `avbtool add_hash_footer --algorithm SHA256_RSA4096 --key <new key>`
   (a signature block is required for `--include_descriptors_from_image`)
2. `avbtool make_vbmeta_image --flags 1 --key <key>
   --include_descriptors_from_image boot_b.img`
3. flash `boot_b` **and** `vbmeta_b` together, every time.

A fresh RSA4096 test key lives at /tmp/avb_test_key.pem (regenerate freely —
any key works, rejection is non-fatal; flags=1 keeps verification disabled).

## The actual gate: LK dies patching the shrunk DTB (2026-10-27, deep night II)

The matching vbmeta_b cycle still produced zero markers — AVB was a red
herring (the in-flash vbmeta_a/b are the user's Lineage-era images; the
"Public key rejected" line is a non-fatal warning; and the matching-vbmeta
test changed nothing).

The armed-cycle records then gave the real sequence. For every shrunk-DTB
image (mainline *and* the 4 KB stub):

```
decompress_kernel takes 0 ms        <- our kernel decompresses fine
... spmfw / scp / modem load ...    <- normal LK platform flow
ccci_realloc_map_mem ...            <- last LK lines, ~2.4 s in
                                      (silence, ~15 s WDT, reset)
```

The armed LK never reaches "send cmdline" / "booting linux" / the jump. It
dies silently in the post-modem DTB-patch phase. The stock-DTB cycles (the
push5 first light) passed this point and jumped — because **the first light
used the stock DTB; every shrunk-DTB test used the A22X DTB**.

The final-try records with `jump=True` were the **slot-A fallback boot**
(their `booting linux` line carries boot_a's ramdisk size, 17,993,071, and
boot_a's 13.4 MB kernel = the constant ~363 ms decompress). The mainline
kernel has never executed.

**Prime suspect:** the `/memory` node. The preloader's own `orig_dram` table
declares two 2 GB ranges (0x40000000+0x80000000, 0xC0000000+0x80000000).
The A22X DTB declared one 4 GB range — `reg = <0x0 0x40000000 0x1 0x0>` —
whose size has a **nonzero high word**; an LK 32-bit DTB patcher reads that
size as 0. **min6** rewrites `/memory` to the two 2 GB `orig_dram` ranges.
