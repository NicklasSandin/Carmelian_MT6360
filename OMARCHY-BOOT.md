# Omarchy on Redmi Note 10 5G (camellian) — SD-Root Boot via nhstub

**systemd 262 boots and manages services on a Xiaomi Redmi Note 10 5G (MT6833 Dimensity 700) from a microSD rootfs, via slot B, using an LD_PRELOAD shim to bridge a 10-year kernel gap.**

## Summary

We boot [Omarchy](https://omarchy.org) (Arch Linux ARM with systemd 262) on slot B of a camellian phone from a microSD ext4 rootfs. The stock MTK kernel (4.14.357-openela) lacks ~40 features systemd 262 expects (pidfd_spawn, P_PIDFD waitid, STATX_MNT_ID, faccessat AT_EMPTY_PATH flags, clone3). Instead of porting the kernel, we run a single LD_PRELOAD interposer (nhstub) inside PID1 that emulates the missing syscalls and suppresses the fatal kernel bugs.

After 87+ boot iterations, the manager **allocates, starts the journal, spawns services, listens on 25+ sockets, and tracks unit invocations**. When the manager freezes on a defensive assert, the initramfs detects it and directly spawns the critical daemons (udevd, dbus-broker, sshd, NetworkManager).

## Architecture

```
LK bootloader (slot B)
  → boot.img (stock kernel + custom initramfs)
    → /init (busybox)
      → mount SD ext4 rootfs
      → plant boot-prepare.sh
      → spawn Eternal Shell (the ghost console, survives pivot)
      → pivot_root into the SD rootfs
        → exec /sbin/init (systemd 262) with LD_PRELOAD=/preload.so (nhstub)
          → manager allocates, units load, services spawn
          → on assert/crash: abort() intercepted → continue
          → on manager freeze: init detects → direct-spawn critical daemons
```

## The nhstub fix stack

Each syscall override targets a specific 4.14 kernel gap that systemd 262 hits:

| Fix | Kernel gap | What it solves |
|-----|-----------|---------------|
| `faccessat`/`faccessat2` | 3-arg faccessat drops AT_EMPTY_PATH | EINVAL on empty-path access |
| `waitid` P_PIDFD→P_PID | P_PIDFD = kernel 5.4+ | every spawn-wait fails EINVAL |
| `pidfd_spawn`→ENOSYS | forces classic posix_spawn | pidfd spawn+wait chain = 5.4+-only |
| `abort()`→return | defensive asserts kill PID1 | safe_close(-EBADF) assert freeze |
| `close()` EBADF→0 | safe_close asserts on result | close of invalid fd = assert |
| statx synthetic | kernel statx returns rot data | mount-consistency checks abort |
| readlink EUNATCH→magic | symlink resolution always fails | unit alias resolution |
| open EUNATCH→magic | new path walks always fail | unit file loading |
| O_PATH retry | flaky O_PATH opens | executor pin, canonicalize |
| clone3/clone pidfd map | pidfd→pid association for waitid | P_PIDFD translation |

The `/proc/1/root/<path>` magic-path fallback is the key workaround: direct SD path walks rot (EUNATCH), but the kernel's magic resolution through procfs is healthy.

## The direct-spawn fallback

When the manager freezes on a mount-table assert (safe_close with a rotted fd), the initramfs's post-pivot job detects it (`systemctl is-system-running` fails) and directly spawns the critical services as independent daemons — bypassing systemctl/dbus entirely. Boot #89 confirmed: udevd RUNNING, sshd RUNNING.

## Results

| Metric | Before | After |
|--------|--------|-------|
| Manager allocates | No (87 boots failed) | Yes |
| Journal service | Never | Started |
| Sockets listening | 0 | 25+ |
| Unit invocations tracked | 0 | Yes |
| Services spawned | 0 | Yes (via fallback) |
| udevd running | No | Yes (direct-spawn) |
| sshd running | No | Yes (direct-spawn) |
| Console stable | ~35 min | Same (gadget rot) |

## Files

- `work/HANDOFF-2026-11-08.md` — the complete engineering log (87 boots, every fix, every failure)
- `work/initramfs/init` — the pivot initramfs init script
- `work/tools/nhstub25.c` — the LD_PRELOAD interposer source
- `work/tools/build_pivot_inplace.py` — builds the slot B boot image
- `work/tools/build_initramfs.sh` — builds the initramfs cpio.gz
- `work/tools/rboot.c` — RESTART2 bootloader re-entry (no BCB write)
- `work/tools/probe_serial.ps1` — serial console probe (auto-detect, drain)
- `work/tools/burst_kill.ps1` — fast console health check

## Hardware

- Xiaomi Redmi Note 10 5G (camellian), MT6833 Dimensity 700
- Slot B: Omarchy rootfs on microSD ext4
- Slot A: LineageOS (recovery/fallback)
- USB: ACM serial console (COM11) + RNDIS network (TX only — the MTK gadget RX bug)

## License

GPL-2.0 (matches the kernel and systemd licensing)
