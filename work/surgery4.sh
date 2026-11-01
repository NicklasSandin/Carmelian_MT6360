losetup -o 5368709120 /dev/loop0 /dev/sdc59 && echo LOOP-OK
mount -t ext4 -o rw /dev/loop0 /mnt && echo MOUNT-OK
cat > /mnt/etc/systemd/system/omamobile-watchdog.service <<'UNIT'
[Unit]
Description=Feed the hardware watchdog (re-arms it after the pivot disarmed it)
After=multi-user.target

[Service]
Type=simple
# Hold the device open and write periodically: open-close per write would
# disable the watchdog on every close (magic 'V' + no nowayout).
ExecStart=/bin/sh -c 'exec 3>/dev/watchdog0; while :; do printf V >&3; sleep 5; done'
Restart=always
RestartSec=1

[Install]
WantedBy=multi-user.target
UNIT
ln -sf /etc/systemd/system/omamobile-watchdog.service /mnt/etc/systemd/system/multi-user.target.wants/omamobile-watchdog.service && echo UNIT-UPDATED
sync
umount /mnt && losetup -d /dev/loop0 && echo SURGERY-DONE
/sbin/reboot-bootloader && echo TO-BOOTLOADER
