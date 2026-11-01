losetup -o 5368709120 /dev/loop0 /dev/sdc59 && echo LOOP-OK
mount -t ext4 -o rw /dev/loop0 /mnt && echo MOUNT-OK
printf '[Match]\nName=usb0\n\n[Network]\nAddress=172.16.42.1/24\nDHCPServer=yes\n\n[DHCPServer]\nPoolOffset=1\nPoolSize=1\n' > /mnt/etc/systemd/network/10-usb0.network && echo NETFILE-UPDATED
echo "--- NM mask:"; ls -la /mnt/etc/systemd/system/NetworkManager.service 2>/dev/null
echo "--- key:"; ls -la /mnt/root/.ssh/authorized_keys 2>/dev/null
echo "--- sshd enabled:"; ls -la /mnt/etc/systemd/system/multi-user.target.wants/sshd.service 2>/dev/null
sync
umount /mnt && losetup -d /dev/loop0 && echo SURGERY-DONE
/sbin/reboot-bootloader && echo TO-BOOTLOADER
