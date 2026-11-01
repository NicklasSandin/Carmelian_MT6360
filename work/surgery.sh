losetup -o 5368709120 /dev/loop0 /dev/sdc59 && echo LOOP-OK
mount -t ext4 -o rw /dev/loop0 /mnt && echo MOUNT-OK
# 1. mask NetworkManager (it DHCPs usb0, fails, and flushes our static IP)
ln -sf /dev/null /mnt/etc/systemd/system/NetworkManager.service && echo NM-MASKED
# 2. root ssh key
mkdir -p /mnt/root/.ssh
printf '%s\n' "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIEbKIJXiHhTJHx0mxcpnduiUA/he0imCERGh33p1QTvS nicklas@brandsentrypro.com" > /mnt/root/.ssh/authorized_keys
chmod 700 /mnt/root/.ssh; chmod 600 /mnt/root/.ssh/authorized_keys && echo KEY-INJECTED
# 3. static network file (networkd is enabled; usb0 gets our address even after a flush)
mkdir -p /mnt/etc/systemd/network
printf '[Match]\nName=usb0\n\n[Network]\nAddress=172.16.42.1/24\n' > /mnt/etc/systemd/network/10-usb0.network && echo NETFILE-OK
# 4. fstab sanity
echo "--- fstab:"; cat /mnt/etc/fstab 2>/dev/null
sync
umount /mnt && losetup -d /dev/loop0 && echo SURGERY-DONE
