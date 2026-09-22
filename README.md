# HP Victus RGB Keyboard - Linux Reverse Engineering & Proof of Concept

Reverse engineering the undocumented?? RGB keyboard control interface found in HP Victus firmware, with a working *Linux* proof of concept.

This project documents the discovery of an HP WMI/ACPI path that can read and modify RGB values stored in the embedded controller (EC) of a tested HP Victus laptop.

The Stage 1 proof of concept was developed and tested on Arch Linux and successfully changed the physical keyboard lighting from Linux.

> **Status:** Stage 1 — Reverse engineering and proof of concept complete.

## What this project does

The tested HP Victus does not expose a standard Linux interface for controlling its RGB keyboard.

The Windows OMEN Gaming Hub can configure the keyboard RGB, but the standard Linux `hp_wmi` interface on the tested system does not expose an RGB control interface.

By examining the ACPI firmware, found:

* An HP WMI method GUID
* The WMI method dispatch chain used by the firmware
* Four logical RGB fields in the EC
* A firmware method that reads RGB values
* A firmware method that writes RGB values
* The exact WMI command and packet format required to reach the RGB write method

A Linux kernel module was then written as a research probe.

The probe successfully performed:

```text
Linux kernel module
        ↓
HP WMI
        ↓
ACPI WMI method
        ↓
Firmware RGB handler
        ↓
EC RGB fields
        ↓
Physical keyboard
```

A write of:

```text
Zone 1 = 255, 0, 0
```

caused the physical keyboard to turn red.

The same value was then read back through the firmware's RGB read method:

```text
R:255 G:0 B:0
```

## Tested hardware

The primary test system is:

```text
HP Victus by HP 16.1 Gaming Laptop PC
SKU:          7Z794PA
Board:        8BD4
Board version: 79.57
BIOS:         Insyde F.29
EC revision:  79.57
```

Software environment:

```text
Arch Linux
Kernel: Linux 7.2.4-arch1-2
```

The results in this repository should therefore be considered **hardware and firmware specific unless independently tested on another HP model**.

## Important limitation

The ACPI firmware exposes four logical RGB zones:

```text
Z01R / Z01G / Z01B
Z02R / Z02G / Z02B
Z03R / Z03G / Z03B
Z04R / Z04G / Z04B
```

However, on the tested Victus keyboard these do **not** correspond to four independently observable physical keyboard zones.

The physical keyboard behaves as a single RGB zone.

Zone 1 can be changed reliably and the entire keyboard responds to that change.

The remaining logical zone fields may exist for other hardware configurations or firmware-supported keyboard implementations. This project does not claim that they represent four physical zones on every Victus.

## Stage 1

Stage 1 focuses on reverse engineering and proving the RGB control path.

It includes:

* ACPI table extraction
* ACPI/DSDT/SSDT analysis
* WMI GUID identification
* WMI method identification
* RGB field identification
* Packet format reconstruction
* Linux kernel proof of concept
* Physical hardware validation

The current PoC is intentionally small and experimental. It is not intended to be a production-quality kernel driver.

## Stage 2

A future Stage 2 may provide a proper Linux userspace/desktop interface, including GNOME integration.

Possible functionality includes:

* RGB color picker
* Brightness control
* GNOME control-panel integration
* Profiles
* Additional lighting functionality if the firmware protocol is later identified

Stage 2 is deliberately outside the scope of the current proof of concept.

## Repository structure

```text
hp-victus-rgb/
├── README.md
├── LICENSE
├── docs/
│   ├── reverse-engineering.md
│   ├── wmi-protocol.md
│   └── experiments.md
│
└── stage1/
    ├── hp_rgb_probe.c
    └── Makefile
```

## Stage 1 proof of concept

The PoC uses the HP WMI method interface to send the firmware-defined RGB command.

The basic RGB write packet is:

```text
Command:      0x00020008
Command type: 0x05
Method ID:    1
Data size:    4

Data:
  byte 0 = zone
  byte 1 = red
  byte 2 = green
  byte 3 = blue
```

For example:

```text
Zone 1 → RGB(255, 0, 0)
```

corresponds to:

```text
01 FF 00 00
```

The firmware returns:

```text
PASS
```

and the value can subsequently be read back through the RGB read method.

## Running the proof of concept

Build the module:

```bash
cd stage1
make
```

Then load it with the desired RGB values:

```bash
sudo insmod ./hp_rgb_probe.ko zone=1 r=255 g=0 b=0
```

Check the kernel log:

```bash
sudo dmesg | tail -10
```

Then unload the module:

```bash
sudo rmmod hp_rgb_probe
```

The module performs one write and one readback operation when loaded.

Example:

```text
hp_rgb_probe: RGB WRITE
hp_rgb_probe: writing zone 1 = R:255 G:0 B:0
hp_rgb_probe: write ACPI status = 0x00000000
hp_rgb_probe: write response: 00000000: 50 41 53 53 00 00 00 00
hp_rgb_probe: zone 1 readback = R:255 G:0 B:0
hp_rgb_probe: complete
```

## Safety

This project intentionally uses the firmware's WMI interface rather than directly accessing EC registers.

During the research process:

* No firmware was flashed.
* No ACPI override was installed.
* No direct EC write was performed.
* The `hp_bioscfg` WMI device was not unbound.
* The RGB write path was derived from the ACPI firmware itself.
* Writes were initially tested one zone at a time.
* Every successful write was followed by a readback.

The PoC should still be considered experimental kernel code.

Do not assume that the same WMI commands are safe or applicable to an unrelated HP model without examining its firmware.

## Research notes

Detailed findings are documented in:

* [`docs/reverse-engineering.md`](docs/reverse-engineering.md)
* [`docs/wmi-protocol.md`](docs/wmi-protocol.md)
* [`docs/experiments.md`](docs/experiments.md)

## Credits and related work

This project is an independent reverse-engineering effort focused on the HP Victus firmware described above.

Linux kernel work around HP OMEN/Victus RGB support is also relevant to this area of hardware support. The long-term goal of this project is not to replace proper upstream Linux support, but to document the firmware behavior and provide a reproducible proof of concept.

**PS:**

Just to be clear, I'm not a professional Linux driver developer. I looked around online for a proper solution to control the keyboard lighting on my HP Victus running Arch Linux, but I couldn't find one that worked for my hardware. So I decided to dig into it and build my own solution.

I used AI to help me with parts of the research, understanding the firmware, and figuring out potential solutions. I don't claim to fully understand every part of the firmware or the Linux kernel code involved.

This project worked on my specific HP Victus, but that doesn't mean it will work on every HP laptop. The code is experimental, so **use it at your own risk and be careful with what you run on your hardware.**
