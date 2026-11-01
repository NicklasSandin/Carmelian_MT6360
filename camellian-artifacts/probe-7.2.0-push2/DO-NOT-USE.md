# Do not use this artifact

This `push2` image was transferred with `fastboot boot` on 2026-10-24. The
bootloader disconnected during the handoff and Android returned on the tested
slot A fallback with boot reason `lk_crash`; the mainline kernel never emitted
observable output.

The kernel field contains a raw FDT after its gzip member, unlike Xiaomi's
stock boot image. `push3` removes that tail and supersedes this artifact.
