#!/bin/bash
# Boot the XPS 17 9720 on its iGPU (WhateverXe xps-unified build). Unplug the eGPU first.
# Run: sudo bash tools/use-igpu-config.sh   then reboot.
set -e
diskutil mount disk0s1 >/dev/null 2>&1 || true
cp /Volumes/EFI/EFI/OC/config-igpu.plist /Volumes/EFI/EFI/OC/config.plist && plutil -lint /Volumes/EFI/EFI/OC/config.plist && sync && echo "iGPU config active; reboot with the eGPU unplugged"
