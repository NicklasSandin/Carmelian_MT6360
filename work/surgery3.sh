losetup -o 5368709120 /dev/loop0 /dev/sdc59 && echo LOOP-OK
mount -t ext4 -o rw /dev/loop0 /mnt && echo MOUNT-OK
mkdir -p /mnt/etc/systemd/system/multi-user.target.wants
cat > /mnt/etc/systemd/system/omamobile-watchdog.service <<'UNIT'
[Unit]
Description=Feed the hardware watchdog (armed by the boot initramfs)
After=multi-user.target

[Service]
Type=simple
ExecStart=/bin/sh -c 'while :; do printf V > /dev/watchdog0; sleep 5; done'
Restart=always
RestartSec=1

[Install]
WantedBy=multi-user.target
UNIT
ln -sf /etc/systemd/system/omamobile-watchdog.service /mnt/etc/systemd/system/multi-user.target.wants/omamobile-watchdog.service && echo WATCHDOG-UNIT-ENABLED
ls -la /mnt/etc/systemd/system/multi-user.target.wants/ | grep -E "watchdog|sshd"
sync
umount /mnt && losetup -d /dev/loop0 && echo SURGERY-DONE
/sbin/reboot-bootloader && echo TO-BOOTLOADER
