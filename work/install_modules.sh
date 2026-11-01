mkdir -p /tmp/modsx && cd /tmp/modsx
tar xf /tmp/mods.tar && echo EXTRACTED
ls lib/modules/7.2.0-omamobile-push1+/ | head -5
losetup -o 5368709120 /dev/loop0 /dev/sdc59 && echo LOOP-OK
mount -t ext4 -o rw /dev/loop0 /mnt && echo MOUNT-OK
rm -rf "/mnt/lib/modules/7.2.0-omamobile-push1+"
cp -a "/tmp/modsx/lib/modules/7.2.0-omamobile-push1+" "/mnt/lib/modules/" && echo MODULES-INSTALLED
ls "/mnt/lib/modules/7.2.0-omamobile-push1+/" | head -5
sync
umount /mnt && losetup -d /dev/loop0 && echo SURGERY-DONE
rm -f /tmp/mods.tar
