#!/bin/bash
# SPDX-License-Identifier: MIT
# The camellian Omarchy installer: writes slot B's boot images and the
# Omarchy rootfs to the userdata partition. Stock Android stays on slot A
# (its system, boot_a and vbmeta_a are untouched); its user data on
# userdata is replaced by the Arch root.
#
# Usage: install-camellian.sh <rootfs.img.zst|rootfs.img> [boot.img]
#
# Safety: never touches bootloader, preloader, lk, calibration/NV,
# boot_a, vbmeta_a or any partition on slot A.
set -euo pipefail

FB=${FB:-/tmp/platform-tools/fastboot}
ADB=${ADB:-adb}
SERIAL=${SERIAL:-5xbas4ozs459eqw8}
MISC_ARM=${MISC_ARM:-/data/local/tmp/misc_h.img}
ROOTFS=${1:?usage: install-camellian.sh <rootfs.img> [boot.img]}
BOOT=${2:-/tmp/rung3/boot_omarchy2_64m.img}
VBMETA=${VBMETA:-/tmp/rung3/vbmeta_b_omarchy2.img}

echo "=== boot:   $BOOT"
echo "=== vbmeta: $VBMETA"

# --- the owner's first-boot file, injected into a copy of the root image ---
work=$(mktemp -d /tmp/camellian-install.XXXXXX)
chmod 700 "$work"
trap 'rm -rf "$work"' EXIT
copy=$work/root.img

case $ROOTFS in
  *.zst) zstd -q -d -f "$ROOTFS" -o "$copy" ;;
  *) cp "$ROOTFS" "$copy" ;;
esac
[[ $(dumpe2fs -h "$copy" 2>/dev/null | sed -n 's/^Filesystem volume name: *//p') == omamobile-root ]] ||
  { echo "not an Omarchy Mobile root image (label omamobile-root)"; exit 1; }

read -rp "username [nicklas]: " username
username=${username:-nicklas}
read -rp "hostname [camellian]: " hostname
hostname=${hostname:-camellian}
read -rp "timezone [Europe/Stockholm]: " timezone
timezone=${timezone:-Europe/Stockholm}
key_file=$(read -rp "SSH public key [$HOME/.ssh/id_ed25519.pub]: " k; echo "${k:-$HOME/.ssh/id_ed25519.pub}")
[[ -f $key_file ]] || { echo "no SSH key at $key_file"; exit 1; }
ssh_key=$(head -1 "$key_file")
read -rsp "login password: " password; echo
password_hash=$(openssl passwd -6 "$password")
unset password

conf=$work/firstboot.conf
keys_file=$work/authorized_keys
umask 077
{
  echo "# Written by install-camellian.sh; omamobile-firstboot reads and deletes it."
  echo "USERNAME=$username"
  echo "PASSWORD_HASH=$password_hash"
  echo "HOSTNAME=$hostname"
  echo "TIMEZONE=$timezone"
  echo "SSH_KEY=$ssh_key"
} > "$conf"
printf '%s\n' "$ssh_key" > "$keys_file"
unset password_hash

debugfs -w -f - "$copy" >/dev/null <<EOF || true
mkdir /etc/omamobile
rm /etc/omamobile/firstboot.conf
write $conf /etc/omamobile/firstboot.conf
sif /etc/omamobile/firstboot.conf mode 0100600
sif /etc/omamobile/firstboot.conf uid 0
sif /etc/omamobile/firstboot.conf gid 0
mkdir /root/.ssh
sif /root/.ssh mode 040700
sif /root/.ssh uid 0
sif /root/.ssh gid 0
rm /root/.ssh/authorized_keys
write $keys_file /root/.ssh/authorized_keys
sif /root/.ssh/authorized_keys mode 0100600
sif /root/.ssh/authorized_keys uid 0
sif /root/.ssh/authorized_keys gid 0
EOF
echo "=== firstboot config injected"

# --- flash -------------------------------------------------------------------
$ADB -s $SERIAL root >/dev/null 2>&1; sleep 2
$ADB -s $SERIAL shell "dd if=$MISC_ARM of=/dev/block/by-name/misc bs=4096 conv=notrunc" >/dev/null

$ADB -s $SERIAL reboot bootloader
for i in $(seq 1 30); do sleep 3; $FB devices 2>/dev/null | grep -q fastboot && break; done

echo "=== flashing boot_b (graft kernel + pivot initramfs)"
$FB flash boot_b "$BOOT"
echo "=== flashing vbmeta_b (verification disabled, our key)"
$FB flash vbmeta_b "$VBMETA"
echo "=== flashing userdata (the Omarchy root) — this erases Android's data"
$FB flash userdata "$copy"
echo "=== selecting slot B"
$FB set_active b

echo "=== dwell 240 s (the LK-owned panel must stabilise)"
sleep 240
$FB devices 2>/dev/null | grep -q fastboot && $FB reboot

echo "=== waiting for the Omarchy root to come up over USB NCM"
for i in $(seq 1 100); do
  ping -c1 -W1 172.16.42.1 >/dev/null 2>&1 && {
    echo "=== ARCH IS UP"
    exit 0
  }
  sleep 5
done
echo "=== timeout; check /sys/fs/pstore after the fallback"
exit 1
