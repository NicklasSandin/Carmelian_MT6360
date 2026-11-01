zstd -d /tmp/mods.tar.zst -o /tmp/mods.tar 2>/dev/null || true
mkdir -p /tmp/modsx && cd /tmp/modsx
tar xf /tmp/mods.tar 2>/dev/null || tar xf /tmp/mods.tar.zst
ls lib/modules/7.2.0-omamobile-push1+/ | head -3
losetup -o 5368709120 /dev/loop0 /dev/sdc59 && echo LOOP-OK
mount -t ext4 -o rw /dev/loop0 /mnt && echo MOUNT-OK
rm -rf "/mnt/lib/modules/7.2.0-omamobile-push1+"
cp -a "/tmp/modsx/lib/modules/7.2.0-omamobile-push1+" "/mnt/lib/modules/" && echo MODULES-INSTALLED
cp /tmp/Image_nomemcg.gz /mnt/boot/ && echo KERNEL-COPIED-TO-BOOT
ls "/mnt/lib/modules/7.2.0-omamobile-push1+/" | head -4
sync
umount /mnt && losetup -d /dev/loop0 && echo SURGERY-DONE
