# HP Victus RGB WMI Protocol

This document describes the WMI request format discovered during reverse engineering of the tested HP Victus firmware.

The protocol described here is based on both static ACPI analysis and successful physical hardware testing.

## HP WMI GUID

The RGB functionality is exposed through:

```text
5FB7F034-2C63-45E9-BE91-3D44E2C707E4
```

The corresponding ACPI WMI object has:

```text
Object ID: AA
Instance count: 1
Flags: 0x02
```

The WMI object is a method object.

## Generic request structure

HP's BIOS WMI interface uses the following structure:

```c
struct bios_args {
    u32 signature;
    u32 command;
    u32 commandtype;
    u32 datasize;
    u8 data[];
};
```

The fields are laid out as:

```text
Offset  Size  Field
------  ----  ----------------
0x00    4     Signature
0x04    4     Command
0x08    4     Command type
0x0C    4     Data size
0x10    N     Data
```

The signature is:

```text
0x55434553
```

or, in little-endian memory:

```text
53 45 43 55
```

## RGB read command

The RGB read path uses the:

```text
Command:
0x00020008
```

command family.

The command type is:

```text
0x04
```

which is dispatched by the firmware:

```text
GMCF(0x04, data)
    ↓
GC04(data)
```

The input contains one byte:

```text
byte 0 = zone
```

The complete request is:

```text
Signature:
0x55434553

Command:
0x00020008

Command type:
0x04

Data size:
1

Data:
[zone]
```

The WMI method ID used by the PoC is:

```text
2
```

## RGB read response

A successful response begins with:

```text
50 41 53 53
```

ASCII:

```text
PASS
```

The observed response format is:

```text
Offset  Value
------  ----------------
0x00    'P'
0x01    'A'
0x02    'S'
0x03    'S'
0x04    00
0x05    00
0x06    00
0x07    00
0x08    Red
0x09    Green
0x0A    Blue
0x0B    00
```

For example:

```text
50 41 53 53 00 00 00 00 FF 00 00 00
```

represents:

```text
R = 255
G = 0
B = 0
```

## RGB write command

The RGB write path uses the same command family:

```text
0x00020008
```

but a different command type:

```text
0x05
```

The firmware dispatches:

```text
GMCF(0x05, data)
    ↓
GC05(data)
```

The payload is:

```text
byte 0 = zone
byte 1 = red
byte 2 = green
byte 3 = blue
```

Therefore:

```text
[01 FF 00 00]
```

means:

```text
Zone 1
Red   = 255
Green = 0
Blue  = 0
```

The complete request is:

```text
Signature:
0x55434553

Command:
0x00020008

Command type:
0x05

Data size:
4

Data:
[zone, red, green, blue]
```

The WMI method ID is:

```text
1
```

## Firmware write behavior

`GC05` selects one of the logical RGB field sets:

```text
Selector 1:
Z01R
Z01G
Z01B

Selector 2:
Z02R
Z02G
Z02B

Selector 3:
Z03R
Z03G
Z03B

Selector 4:
Z04R
Z04G
Z04B
```

For selector 1, the firmware performs the equivalent of:

```text
Z01R = data[1]
Z01G = data[2]
Z01B = data[3]
```

The firmware returns a small response package.

A successful write observed during testing returned:

```text
50 41 53 53 00 00 00 00
```

or:

```text
PASS
```

## Alternative RGB path

The firmware also contains a second RGB command family:

```text
0x00020009
```

This is dispatched through:

```text
LDCF()
```

The relevant cases are:

```text
CMTP 0x02 → LC02
CMTP 0x03 → LC03
```

`LC02` provides RGB read formats.

`LC03` provides RGB write formats.

One of the formats supported by `LC03` is:

```text
04 R1 G1 B1 R2 G2 B2 R3 G3 B3 R4 G4 B4
```

Another is:

```text
01 R G B
```

The Stage 1 PoC does not use this path because the simpler `GC05` path was successfully verified on the tested hardware.

## Four logical zones

The firmware supports four RGB field sets.

However, firmware support does not necessarily imply four physical keyboard zones.

On the tested HP Victus:

```text
Zone 1 → physical keyboard responds
Zone 2 → not treated as independent physical zone
Zone 3 → not treated as independent physical zone
Zone 4 → not treated as independent physical zone
```

The final Stage 1 implementation therefore uses the proven physical behavior rather than assuming four physical zones.

## WMI method invocation

The PoC invokes the legacy WMI evaluation interface:

```c
wmi_evaluate_method(
    HP_WMI_GUID,
    0,
    method_id,
    &input,
    &output
);
```

The modern Linux WMI subsystem also provides bus-based interfaces such as `wmidev_invoke_method()`. The Stage 1 PoC intentionally uses the direct evaluation function to keep the experimental implementation small and closely aligned with the existing HP WMI implementation.

A future production implementation should follow the appropriate upstream Linux WMI/LED infrastructure rather than treating this research probe as a finished driver.

## Summary

The experimentally verified RGB write transaction is:

```text
GUID:
5FB7F034-2C63-45E9-BE91-3D44E2C707E4

Instance:
0

Method:
1

Input:

+0x00  0x55434553
+0x04  0x00020008
+0x08  0x00000005
+0x0C  0x00000004
+0x10  zone
+0x11  red
+0x12  green
+0x13  blue
```

The corresponding read transaction is:

```text
GUID:
5FB7F034-2C63-45E9-BE91-3D44E2C707E4

Instance:
0

Method:
2

Input:

+0x00  0x55434553
+0x04  0x00020008
+0x08  0x00000004
+0x0C  0x00000001
+0x10  zone
```

These transactions were successfully executed on the tested HP Victus under Linux.
