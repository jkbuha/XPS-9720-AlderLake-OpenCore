# Dell XPS 17 9720 macOS Tahoe with OpenCore

<!-- Add a screenshot as ./screenshot/screen.jpg -->

# Details

| OpenCore Version | 1.0.8 |
| --- | --- |
| macOS Version | 26.7.3 (Tahoe) |
| SMBios | MacBookAir9,1 |
| iGPU driver | [WhateverXe](https://github.com/jkbuha/WhateverXe/tree/xps-unified) (`xps-unified`) + WhateverXe Graphics |

# Hardware Specifications

| Hardware | Specification | Status |
| --- | --- | --- |
| CPU | Intel Core i9-12900HK (Alder Lake-H, 6P + 8E) | ✅ Working |
| RAM | DDR5 64GB (2× 32GB SO-DIMM) | ✅ Working |
| GPU | Intel Iris Xe Graphics (`8086:46A6` rev `0x0C`, 96 EU) | ✅ Working (Metal 2, 1536 MB) |
| eGPU | AMD Radeon RX 6600 XT (Thunderbolt) | ✅ Working (eGPU config) |
| Display | 17" 1920 x 1200 FHD+ eDP | ✅ Working |
| External display | HDMI through both USB-C ports (iGPU) | ✅ Working |
| Audio (internal) | SoundWire: RT711 headset codec, 2× RT1316 amps, RT714 mics | ❌ Not Working |
| Audio (iGPU HDMI) | Intel display codec | ❌ Not Working ([#124](https://github.com/b00t0x/WhateverXe/issues/124)) |
| Audio (eGPU HDMI/DP) | AMD | ✅ Working |
| WiFi | Intel AX211 (CNVi, `8086:51F0`) | ✅ Working (AirportItlwm) |
| Bluetooth | Intel AX211 | ✅ Working |
| SSD | Lexar NM790 4TB | ✅ Working |
| Keyboard | - | ✅ Working |
| Trackpad | I2C Connection | ✅ Working |
| Webcam | Integrated Webcam | ✅ Working |
| MicroSD Card | RTS5260 Card Reader | 🔶 Driver loads (RealtekCardReader), not fully tested |
| Fans | 2 fans, RPM in HWMonitorSMC2 | ✅ Working (SMCDellDDV) |
| Fingerprint Sensor | - | ❌ Not Working |
| Sleep / S4 | Hibernation (`hibernatemode 25`) | ✅ Working |
| Display sleep / wake | - | ✅ Working |

# Overview

The XPS 17 9720 runs macOS Tahoe in two ways from the same EFI:

- **iGPU** (`config-igpu.plist`): native **Iris Xe acceleration** with b00t0x's [WhateverXe](https://github.com/b00t0x/WhateverXe)
  plus the XPS fixes on the `xps-unified` branch of [jkbuha/WhateverXe](https://github.com/jkbuha/WhateverXe/tree/xps-unified).
  The internal panel, HDMI on both USB-C ports, display sleep and hibernation all work.
- **eGPU** (`config-egpu.plist`): the daily-driver setup with an AMD Radeon on Thunderbolt (WhateverGreen, no WhateverXe).

`config.plist` is the one OpenCore boots; copy whichever you want over it (see [Tools](#tools)).
The iGPU kext is the same one used on the [XPS 13 9320 repo](https://github.com/jkbuha/XPS-9320-RaptorLake-OpenCore);
only the boot-args differ.

# Graphics (WhateverXe)

Graphics needs both halves of WhateverXe:

1. `EFI/OC/Kexts/WhateverXe.kext` (in this repo): the Lilu plugin, built from `xps-unified`.
2. **WhateverXe Graphics** (Apple's Tiger Lake driver bundles in `/Library/Extensions`); see the [WhateverXe README](https://github.com/b00t0x/WhateverXe).

Boot-args specific to this machine (all in `config-igpu.plist`):

| Boot-arg | Purpose |
| --- | --- |
| `-wex-panel-handoff` | Admits the firmware-trained eDP panel port, which the BIOS leaves in "mode 0" |
| `-wex-edp-keep` | Keeps the firmware-trained eDP link instead of retraining it (retraining froze the machine) |
| `-wex-edp-blank` | Display sleep blanks the panel instead of a full link teardown |
| `-wex-no-edp-hpd` | Masks the spurious panel hot-plug pulse that hard-hung display wake |
| `-wex-resume-park` | Clean display hand-over on hibernation resume |
| `-wex-tc1-legacy -wex-tc4-legacy` | The USB-C ports' HDMI is wired as legacy DP/HDMI, not Type-C alt mode |
| `-wex-audio-wake` | Optional: wakes the iGPU HDMI audio codec (diagnostic, harmless) |

The eDP fixes come from nickk19 (`-wex-edp-keep`, `-wex-edp-blank`) and this machine's bring-up (`-wex-no-edp-hpd`);
the ADL-H hand-off/CDCLK/IRQ fixes are renedis' port of Thatisjigen's work.

# Fans (SMCDellDDV)

Dell's legacy SMM fan interface (used by SMCDellSensors) doesn't answer on this model, but the firmware exposes fan RPM
through Dell's **DDV WMI** method (`\_SB.AMWV.WMDV`, method `0x20`). `SMCDellDDV.kext` is a small VirtualSMC plugin that
calls it every 2 s and publishes standard SMC fan keys, so HWMonitorSMC2 and similar apps show both fans. Source:
[`src/SMCDellDDV`](src/SMCDellDDV). It only reads; it never sets fan speeds.

# SMBIOS

The serial number, MLB, SystemUUID and ROM in all three configs are **blank**. Generate your own before booting:

1. Run [GenSMBIOS](https://github.com/corpnewt/GenSMBIOS) and generate a `MacBookAir9,1` SMBIOS.
2. Fill `PlatformInfo > Generic > SystemSerialNumber`, `MLB`, `SystemUUID`, and set `ROM` to your NIC MAC (6 bytes),
   **identically in all three configs**.
3. Check the serial is **not** valid on Apple's coverage site before using iCloud/iMessage.

# BIOS Settings

| Setting | Option |
| --- | --- |
| SATA/NVMe Operation | AHCI/NVMe (not RAID) |
| Fast Boot | Thorough |
| Secure Boot | Disabled |
| TPM 2.0 Security | Disabled |
| Intel SGX | Disabled |
| VT for Direct I/O | Disabled |
| Fingerprint Reader | Disabled |

# UEFI IFR edits

As with the XPS 9520/9730, disable `CFG_LOCK` with modGRUBshell before installing. **Check the variable offsets against
your own BIOS version's IFR dump**; they change between Dell BIOS releases.

# Sleep / Hibernation

Hibernation (`sudo pmset -a hibernatemode 25`) works on the iGPU config: the machine powers off fully and resumes into the
same session. `SSDT-Dell_XPS9730_Raptorlake-RTC1.aml` shrinks the RTC I/O range to fix the "CMOS battery error" Dell's
firmware raised after macOS hibernation.

# Undervolting ⚠️

`VoltageShift.kext` is included. On this machine, with the **iGPU** driving the displays:

- **System Agent and Analog/Digital I/O offsets must stay at 0 mV.** Negative SA/IO offsets caused silent single-bit
  memory corruption (APFS damage, damaged apps, kernel panics) and occasional hangs at restart, even though the same offsets were
  stable for months on the eGPU config.
- CPU core and Cache at -110 mV and GPU at -50 mV have been clean so far.

If you see APFS errors, damaged app bundles or random panics on the iGPU config, set every offset to 0 first.

# Tools

| Script | Use |
| --- | --- |
| `tools/use-igpu-config.sh` | `sudo bash tools/use-igpu-config.sh`: boot on the iGPU (unplug the eGPU), then reboot |
| `tools/use-egpu-config.sh` | `sudo bash tools/use-egpu-config.sh`: boot on the eGPU, then reboot |

# Known Issues

- Internal speakers/headphones/microphones are SoundWire (like the XPS 13 Plus); no macOS driver yet.
- iGPU HDMI audio doesn't work yet: the codec enumerates but doesn't answer ([b00t0x/WhateverXe#124](https://github.com/b00t0x/WhateverXe/issues/124)).
  Use the eGPU or a USB audio device.
- Fingerprint reader not supported.

# Credits

acidanthera (OpenCore, Lilu, VirtualSMC, AppleALC, WhateverGreen, HibernationFixup, RestrictEvents, BrightnessKeys),
[b00t0x](https://github.com/b00t0x/WhateverXe) (WhateverXe), nickk19, renedis and Thatisjigen (ADL eDP/hand-off fixes),
zxystd (itlwm / IntelBluetoothFirmware), VoodooI2C team, 0xFireWolf (RealtekCardReader), sicreative (VoltageShift),
dhinakg (AMFIPass), USBToolBox.

# Donate

If you like and use this project, feel free to contribute anything you can below:

Paypal: https://paypal.me/jkbuha

Bitcoin: bc1qjuy6hy5shqqdnyzzcjlc2h4ua47gpugphuxdae
