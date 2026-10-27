<!-- SPDX-License-Identifier: MIT -->
# Camellian M1: microSD-root first light

This milestone ends with a mainline kernel booting a recovery initramfs and,
only after its guards pass, an Arch root on the external microSD. It does not
claim a graphical desktop, cellular service or a persistent installation.

## Safety invariants

- The phone's UFS main LUN is `sdc`, not the Pixel path `sda`.
- All `sd*` disks and partitions become read-only before USB, SSH, firmware
  loading, filesystem checks or root discovery.
- No UFS partition is mounted. There is no writable UFS allowlist for M1.
- The only writable block device is the verified external
  `/dev/mmcblk0p1`, and it must have ext4 label `omamobile-root`.
- `boot_a`, `dtbo_a`, `vbmeta_a` and all calibration/NV partitions remain
  untouched. Slot B writes are deferred until a rollback boot is proven.
- The initramfs contains a local public SSH key, never a private key. It
  exposes USB NCM and ACM and retains a serial console.
- Kernel panic halts instead of repeatedly rebooting slot B. Userspace guard
  or USB failures request bootloader mode, and a five-minute ceiling always
  returns a successful probe to bootloader. Slot A has independently booted
  completed Android and is the tested rollback path.

## Stage 0: offline inputs

1. Start from Linux v7.2 plus the A22X MT6833 patch series.
2. Create `mt6833-xiaomi-camellian.dts` from the live board evidence. Use
   4 GiB RAM, the bootloader framebuffer reservation at `0x7ba10000`, and the
   board's actual K19/FocalTech identities. Do not ship the vendor DT dump.
3. Port the A22X MSDC1, MT6360 LDO3/LDO5 and pinctrl nodes. Keep UFS and all
   remote processors disabled.
4. Build UART, watchdog, devtmpfs, ext4, MSDC1, USB MUSB gadget, configfs NCM
   and ACM into the kernel. Modules are not available before the SD root.
5. Build an Alpine initramfs with a fail-closed UFS guard and SD-label check.
6. Pack a 64 MiB-or-smaller Android-v2 boot image using the captured load
   addresses and 2048-byte pages. Generate a reviewed no-op DTBO separately.

Offline result, 2026-10-24: the kernel and DTB compile; the compressed kernel
plus DTB is 19.83 MiB. The dedicated RAM-only packer produces a 25,202,688-byte
boot image after adding its initramfs and the LK alignment shim. Re-unpacking
confirms header v2, `0x40080000` kernel, `0x51100000` ramdisk and `0x47c80000`
tags/DTB addresses. The initramfs contains key-only SSH, NCM/ACM and a
fail-closed guard with no writable UFS exceptions. Repository tests pass.
The kernel field is one clean gzip member, matching stock; the DTB is carried
only in the v2 DTB table. The one-entry no-op DTBO applies byte-identically to
the base DTB. No persistent mainline boot may use the stock vendor overlay.

## Stage 1: recovery-only inspection

Pass conditions:

- Both boot slots, DTBOs and vbmeta images are backed up and hashed off-phone.
- Slot A is independently booted and returns to ADB, or a second tested
  recovery path is documented.
- Fastboot reports the expected product, unlocked state and current slot.
- The boot image unpacks to the intended ARM64 kernel, initramfs and camellian
  DTB, and fits the 64 MiB partition.
- The no-op DTBO identifies only the expected board entry.

Stop if any identity, size, slot or hash differs from the recorded phone.

## Stage 2: first light through inactive slot B

`fastboot boot` is not usable on this bootloader: `push2`, corrected `push3`
and the untouched verified stock `boot_a` image all transferred, disconnected
during `Booting`, and returned Android on slot A with `lk_crash`. Therefore a
first-light attempt requires a separately approved inactive-slot-B write.

The reviewed bundle contains only `boot_b`, `dtbo_b` and `vbmeta_b` images.
It leaves slot A, userdata, microSD and every calibration/NV partition out of
scope. The no-op DTBO excludes the stock vendor overlay; both AVB images use
algorithm `NONE` and flags `3`. Expected evidence:

- kernel banner on UART/framebuffer or a captured pstore log;
- `/dev/mmcblk0p1` enumerates with label `omamobile-root`;
- UFS remains absent because its DT node is disabled; if any `sd*` device
  nevertheless enumerates, it and every child report `ro=1`;
- USB NCM/ACM enumerates and key-only SSH answers;
- userspace failure or the five-minute ceiling returns to fastboot, after
  which the host selects slot A and reboots Android;
- if Linux panics or hangs before userspace, it remains stopped and physical
  Power+Volume-Down recovery is required to reach fastboot and select slot A.

Do not rely on automatic slot fallback: Xiaomi reports both slots successful
with only one retry. A human must remain present for physical fastboot rescue,
and slot A must be restored active after every first-light session.

### First light (2026-10-27): PASS

The stock-kernel probe (fixed initramfs) booted from slot B and reached full
first light: USB NCM configured, key-only SSH at `root@172.16.42.1`, all 63
UFS block devices `ro=1`, microSD reported, and the five-minute ceiling
returned the phone to fastboot on schedule. Slot A was re-selected and Android
completed boot. Evidence in `camellian-artifacts/first-light-2026-10-27/`.
Prerequisites discovered on the way (details in [NOTES.md](NOTES.md)):
populate `lk_b` with a copy of `lk_a` (it ships empty and `preloader_b` dies
jumping into it), use fastboot 37 for `set_active`, and never let PID 1 exit
(the stock kernel has no devtmpfs; the panic it causes is misreported as
`lk_crash`).

The next rung is the mainline kernel with the camellian DTB, packed with the
same bare-image + stock-vbmeta wrapping and booted through the now-proven
slot-B path (`fastboot flash lk_b` once per phone, healthy-armed misc,
`set_active b`, reboot).

### Mainline kernel status (2026-10-27, evening): boots, console fix staged

The mainline kernel (v7.2 + A22X series, plain-gz) decompresses, jumps and
runs on slot B with the shrunk DTB (`mainline_min5_mtk.dtb`, 68 KB container).
The USB gadget reaches `configured` state on the phone side but never
enumerates on the host, and the microSD never appears — but neither could be
debugged because the mainline console was never captured.

Root cause of the missing console (found this session, full chain in
[NOTES.md](NOTES.md)): the A22X DTB marks the pstore region `no-map`; the
mainline kernel's ramoops maps with `memremap(WB)` which cannot map no-map
memory (the stock 4.14 kernel used `ioremap` and worked). Fix = `min5` DTB
with `no-map` removed from `mblock-18-pstore`, plus the ramoops cmdline args
carried in the boot image itself. The probe initramfs now also writes an
early snapshot plus a 60 s refresh to `/dev/pmsg0` (recovered on Android as
`/sys/fs/pstore/pmsg-ramoops-0`) and tries ext4 *and* vfat on the microSD
(the card is FAT32 — the old init only tried ext4).

IKCONFIG extracted from the running Image confirms no rebuild is needed for
the first dmesg: `PSTORE_RAM/PSTORE_CONSOLE/PSTORE_PMSG`, `MUSB_MEDIATEK +
MUSB_DUAL_ROLE`, `PHY_MTK_TPHY`, `MMC_MTK`, `SCSI_UFS_MEDIATEK`, `MFD/REGULATOR/
TYPEC_MT6360`, `VFAT_FS`, `EXT4_FS` are all built in.

### First slot-B result (2026-10-24)

The reviewed `push4` bundle was written only to `boot_b`, `dtbo_b` and
`vbmeta_b`. Fastboot accepted all three images and reported B as successful,
bootable and having one retry before the reboot. Slot A remained untouched.

The phone did not expose the probe's NCM or ACM gadget and did not reach its
initramfs. It briefly exposed MediaTek preloader USB (`0e8d:0003`) after the
attempt, then the A/B boot flow returned to completed Android on slot A. The
surviving evidence is `ro.boot.slot=a`, `sys.boot.reason=bootloader` and the
stock 4.14 kernel. There is no accessible pstore record from the failed boot.
Consequently this attempt proves the rollback path, but it does not distinguish
an LK/AVB rejection from a reset in very early mainline kernel entry.

The next test must be a diagnostic ladder, not another all-at-once image:

1. **Pass (2026-10-24):** restoring the captured stock `boot_b`, `dtbo_b`
   and `vbmeta_b` booted completed Android on B with the stock 4.14 kernel.
   The host then selected A and completed Android there again. This proves the
   B slot, captured images and normal partition boot path are sound.
2. Replace only the B ramdisk while retaining the stock kernel, stock DTB and
   stock DTBO. Preserve the stock AVB rollback index and descriptor layout
   while disabling verification for the deliberately modified boot image.
3. Change to the mainline kernel and camellian DTB only after the custom
   initramfs is known to run on this boot chain.

Each rung requires a new reviewed bundle and explicit approval before writes.
Android A must be verified healthy again after every rung.

## Stage 3: Arch root on microSD

After Stage 2 passes, populate the SD from recovery with a minimal Arch Linux
ARM root containing the exact kernel modules. Boot systemd with SSH and USB
networking first. Add Omarchy Mobile packages only after a plain Arch boot is
repeatable. Software rendering precedes Panfrost; native panel and FocalTech
touch follow as separate hardware milestones.

## Deferred hardware

Bring up in this order: simpledrm, physical keys, FocalTech touch, Panfrost,
battery/charging, Wi-Fi, Bluetooth, native K19 DSI, audio, sensors and sleep.
UFS write support comes only after repeated read-only validation. Modem, GPS,
camera, NFC and fingerprint are independent long-term work and are excluded
from M1.
