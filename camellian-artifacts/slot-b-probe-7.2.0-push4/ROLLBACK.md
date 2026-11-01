# Camellian push4 rollback

Slot A was independently booted to completed Android before this bundle was
built. If the probe returns to fastboot, select A and reboot:

```sh
fastboot set_active a
fastboot reboot
```

If Linux panics or hangs before returning to fastboot, hold Power and Volume
Down until the fastboot screen appears, then run the same two commands.

After Android is recovered on A, the captured slot-B images can be restored
from `device-backups/camellian-2026-10-24/`:

```sh
fastboot flash boot_b boot_b.img
fastboot flash dtbo_b dtbo_b.img
fastboot flash vbmeta_b vbmeta_b.img
```

Never substitute slot A in those restore commands. Verify the captured hashes
before restoring.
