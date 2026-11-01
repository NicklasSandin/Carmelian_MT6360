# Superseded safety policy — do not flash

This bundle used a probe with `panic=10`, which can repeatedly reboot slot B
on a bootloader that keeps the slot marked successful. A replacement bundle
must halt on kernel panic and use a bounded userspace return to bootloader.
