# Reverse Engineering the HP Victus RGB Keyboard

## Overview

The objective of this research was to determine how the RGB keyboard on an HP Victus laptop is controlled by Windows and whether the same functionality could be reproduced under Linux.

The tested machine exposes no standard Linux RGB keyboard interface through the existing `hp_wmi` functionality.

Rather than directly accessing the embedded controller, the investigation followed the firmware's own ACPI/WMI implementation.

The resulting path was:

```text
ACPI firmware
    ↓
_WDG WMI registration
    ↓
HP WMI GUID
    ↓
WMAA
    ↓
WHCM
    ↓
WMI command dispatcher
    ↓
RGB handler
    ↓
EC RGB fields
```

The final result was a working Linux proof of concept capable of changing the physical keyboard RGB.

## Hardware

Testing was performed on:

```text
HP Victus by HP 16.1 Gaming Laptop PC
SKU:           7Z794PA
Board:         8BD4
Board version: 79.57
BIOS:          Insyde F.29
EC revision:   79.57
```

The Linux environment used during the investigation was:

```text
Arch Linux
Linux kernel 7.2.4-arch1-2
```

## Initial investigation

The Linux `hp_wmi` driver was already loaded on the system.

The platform device exposed several HP WMI functions, but no RGB keyboard interface.

The kernel log also reported:

```text
hp_wmi: Unknown EC layout for board 8BD4.
Thermal profile readback will be disabled.
```

The RGB functionality therefore appeared to be absent from the standard Linux interface despite being available through Windows OMEN Gaming Hub.

## ACPI extraction

The ACPI tables were dumped from the running system and extracted with ACPICA tools.

The relevant SSDT was decompiled into ASL:

```text
ssdt1.dsl
```

The SSDT contained the HP WMI implementation associated with the board.

Searching the decompiled firmware revealed:

```text
_SB.WMID
WQBC
WQBE
WHCM
WMAA
NBCI_FUNC_GETBACKLIGHT
Z01R
Z01G
Z01B
Z02R
Z02G
Z02B
Z03R
Z03G
Z03B
Z04R
Z04G
Z04B
```

## WMI GUID

The `_WDG` object inside the WMI device contained the following method GUID:

```text
5FB7F034-2C63-45E9-BE91-3D44E2C707E4
```

The object ID was:

```text
AA
```

and the object was marked as a WMI method object.

Linux exposed the corresponding WMI device:

```text
5FB7F034-2C63-45E9-BE91-3D44E2C707E4-8
```

The device was associated with `hp_bioscfg`.

The same GUID is also used by HP's WMI BIOS interface in the Linux kernel.

## RGB fields

The EC field definitions in the ACPI firmware revealed eight-bit RGB fields.

The relevant fields include:

```asl
Offset (0x808),
Z01R, 8,
Z01G, 8,
Z01B, 8,
Z02R, 8,
Z02G, 8,
Z02B, 8,
...
Offset (0x81D),
Z04R, 8,
Z04G, 8,
...
Offset (0x826),
Z03R, 8,
...
Offset (0x857),
Z04B, 8,
```

The exact physical meaning of these fields was then investigated experimentally.

The important observation is that the firmware exposes four logical RGB triplets, but the tested physical keyboard behaves as a single RGB zone.

## Finding the RGB handlers

Searching for references to the RGB fields showed that they were accessed by four relevant firmware methods:

```text
GC04
GC05
LC02
LC03
```

`GC04` reads RGB values.

`GC05` writes RGB values.

`LC02` provides another RGB read format.

`LC03` provides another RGB write format.

No other ACPI methods in the examined SSDT directly referenced the `Z01R` through `Z04B` fields.

This made `GC04` and `GC05` particularly useful for reconstructing the RGB protocol.

## GC04 — RGB read

The `GC04` method accepts a selector byte.

Its logic is effectively:

```text
selector 1 → Z01R/Z01G/Z01B
selector 2 → Z02R/Z02G/Z02B
selector 3 → Z03R/Z03G/Z03B
selector 4 → Z04R/Z04G/Z04B
```

The returned package contains the RGB values.

The WMI response observed from Linux was:

```text
50 41 53 53 00 00 00 00 RR GG BB 00
```

which corresponds to:

```text
PASS
...
R
G
B
```

with RGB located at buffer offsets:

```text
8 = Red
9 = Green
10 = Blue
```

## GC05 — RGB write

The `GC05` method accepts a four-byte input.

The ACPI implementation is effectively:

```text
byte 0 = selector
byte 1 = red
byte 2 = green
byte 3 = blue
```

For selector 1:

```text
Z01R = byte 1
Z01G = byte 2
Z01B = byte 3
```

The same structure exists for selectors 2, 3, and 4.

This provided the first direct firmware-derived RGB write packet.

## WMI dispatcher

The ACPI method `GMCF` dispatches commands belonging to the:

```text
0x00020008
```

command family.

The relevant cases are:

```text
CMTP 0x04 → GC04
CMTP 0x05 → GC05
```

Therefore:

```text
0x00020008 + CMTP 0x04
```

is the RGB read path, while:

```text
0x00020008 + CMTP 0x05
```

is the RGB write path.

## Higher-level WMI path

The firmware's `WHCM` method receives the generic HP WMI request structure.

Relevant fields include:

```text
SNIN
COMD
CMTP
DASI
```

For the RGB write:

```text
COMD = 0x00020008
CMTP = 0x05
```

which causes:

```text
WHCM
  ↓
GMCF
  ↓
GC05
  ↓
Z01R/Z01G/Z01B
```

The outer method `WMAA` calls `WHCM`.

Thus the complete firmware path is:

```text
WMI request
    ↓
WMAA
    ↓
WHCM
    ↓
GMCF
    ↓
GC05
    ↓
EC RGB fields
```

## Reconstructing the generic HP WMI request

The installed `hp_wmi` and `hp_bioscfg` kernel modules were also examined.

The standard HP WMI request structure is:

```c
struct bios_args {
    u32 signature;
    u32 command;
    u32 commandtype;
    u32 datasize;
    u8 data[];
};
```

The signature used by HP is:

```text
0x55434553
```

which corresponds to the ASCII bytes:

```text
SECU
```

The request therefore becomes:

```text
offset 0x00 = 0x55434553
offset 0x04 = command
offset 0x08 = command type
offset 0x0C = data size
offset 0x10 = payload
```

The Linux implementation ultimately invokes:

```c
wmi_evaluate_method()
```

against the HP BIOS WMI GUID.

## Method ID

The installed `hp_wmi` implementation calculates the WMI method ID from the expected output size.

For the RGB write, no output data is required, resulting in method ID:

```text
1
```

For the RGB read, the response requires a buffer and the corresponding call uses:

```text
2
```

The experimentally successful calls were therefore:

### RGB read

```text
GUID:
5FB7F034-2C63-45E9-BE91-3D44E2C707E4

Instance:
0

Method:
2

Command:
0x00020008

Command type:
0x04

Data size:
1

Payload:
[zone]
```

### RGB write

```text
GUID:
5FB7F034-2C63-45E9-BE91-3D44E2C707E4

Instance:
0

Method:
1

Command:
0x00020008

Command type:
0x05

Data size:
4

Payload:
[zone, red, green, blue]
```

## What was experimentally proven

The firmware analysis predicted that the write packet should modify the RGB EC fields.

A Linux kernel module was then used to perform the operation.

The first successful test sent:

```text
Zone = 1
Red  = 255
Green = 0
Blue = 0
```

The firmware returned:

```text
PASS
```

A subsequent read returned:

```text
R:255 G:0 B:0
```

Most importantly, the physical keyboard visibly changed to red.

This establishes the complete control path from Linux to the physical keyboard.

## What remains hardware-specific

Although four logical RGB fields exist in the firmware, testing on this Victus demonstrated that the physical keyboard behaves as a single RGB zone.

Changing zone 1 reliably changes the entire keyboard.

The remaining logical zones were not treated as independent physical zones in the final Stage 1 implementation.

This distinction is important because ACPI firmware can contain generic functionality supporting multiple hardware configurations.

## Conclusion

The investigation transformed an initially unsupported Linux RGB keyboard into a reproducible firmware-level control path.

The important discovery was not a direct EC register poke. The firmware already contained a complete WMI command path for RGB control.

The Linux proof of concept simply reconstructed the request required to reach that existing functionality.
