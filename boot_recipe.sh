#!/bin/bash
# SPDX-License-Identifier: MIT
# The reproducible slot-B boot recipe for camellian (MT6833).
# Prerequisites: fastboot >= 35 (platform-tools 37 works), the probe
# image on boot_b, the no-op DTBO on dtbo_b, the misc partition armed.
set -euo pipefail

FB="${FB:-fastboot}"
SERIAL="${SERIAL:-}"

adb reboot bootloader
echo "Waiting for fastboot..."
for i in $(seq 1 30); do
    sleep 2
    "$FB" devices | grep -q fastboot && break
done

# Arm the misc partition (bootloader_control: slot_suffix=_b, B healthy)
# This must be done from Android BEFORE rebooting to fastboot (see NOTES).
# The misc_h.img file contains the armed struct (CRC-verified).

echo "Selecting slot B..."
"$FB" set_active b    # "Could not get suffixes" error is OK on old fastboot

echo "Dwell: 240 s in fastboot (LK-owned panel must stabilise)..."
sleep 240

echo "Rebooting into slot B..."
"$FB" reboot

echo "Watching for the USB gadget (1d6b:0104)..."
for i in $(seq 1 30); do
    sleep 10
    if lsusb 2>/dev/null | grep -q "1d6b:0104"; then
        echo "FIRST LIGHT: gadget up!"
        echo "SSH: ssh root@172.16.42.1"
        exit 0
    fi
    if adb devices 2>/dev/null | grep -q device; then
        echo "Fell back to slot A"
        exit 1
    fi
done
echo "Timeout: no gadget, no adb"
exit 1
