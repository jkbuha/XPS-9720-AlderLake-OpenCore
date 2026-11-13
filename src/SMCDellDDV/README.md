# SMCDellDDV

VirtualSMC plugin that reads Dell laptop fan RPM through the firmware's **DDV WMI** method
(`WMDV`, method ID `0x20`, the interface Linux's `dell-wmi-ddv` uses) instead of the legacy SMM call that
SMCDellSensors relies on. Written for the XPS 17 9720, where SMM fan queries return nothing.

- Matches the `PNP0C14` WMI device with `_UID` `ADDV` (`\_SB.AMWV` on the 9720).
- Retries discovery every 2 s for up to 2 minutes, because the EC only answers once macOS's EC driver is up.
- Publishes `FNum`, `F<n>ID`, `F<n>Ac`, `F<n>Mn`, `F<n>Mx`. Read-only: it never sets fan speeds.

Build (needs [VirtualSMC](https://github.com/acidanthera/VirtualSMC) `VirtualSMCSDK` headers, Lilu headers and MacKernelSDK):

```sh
xcrun clang++ -arch x86_64 -std=c++17 -O2 -mkernel -fapple-kext -fno-exceptions -fno-rtti -fno-stack-protector \
  -fno-builtin -nostdinc -DKERNEL -DKERNEL_PRIVATE -I$SDK/Headers -I$LILU/Contents/Resources -I$VSMC \
  -mmacosx-version-min=10.15 -c SMCDellDDV.cpp -o SMCDellDDV.o
xcrun clang -arch x86_64 -mkernel -nostdinc -DKERNEL -mmacosx-version-min=10.15 -I$SDK/Headers -c info.c -o info.o
xcrun ld -arch x86_64 -kext -undefined dynamic_lookup -no_fixup_chains -macos_version_min 10.15 \
  -o SMCDellDDV SMCDellDDV.o info.o $SDK/Library/x86_64/libkmod.a
```

Load it after VirtualSMC in `config.plist`.
