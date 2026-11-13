#!/bin/bash
# Boot the XPS 17 9720 on the AMD eGPU (daily eGPU config: WhateverGreen on, WhateverXe off). Plug the eGPU in.
# Run: sudo bash tools/use-egpu-config.sh   then reboot.
set -e
diskutil mount disk0s1 >/dev/null 2>&1 || true
cp /Volumes/EFI/EFI/OC/config-egpu.plist /Volumes/EFI/EFI/OC/config.plist && plutil -lint /Volumes/EFI/EFI/OC/config.plist && sync && echo "eGPU config active; plug the eGPU in and reboot"
