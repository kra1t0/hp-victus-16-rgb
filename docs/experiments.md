# Experiments

This document records the controlled experiments used to validate the HP Victus RGB protocol.

The purpose is to distinguish firmware observations from assumptions and to provide reproducible evidence for the Stage 1 implementation.

## Experiment 1 — Initial WMI transport test

The first probe tested whether the HP WMI method interface could be invoked successfully.

The request used the HP WMI GUID:

```text
5FB7F034-2C63-45E9-BE91-3D44E2C707E4
```

The WMI call returned:

```text
ACPI status = 0x00000000
```

which indicates successful WMI/ACPI transport.

However, the firmware returned:

```text
FAIL
```

This demonstrated that the WMI transport itself worked but that the selected command parameters were not a valid RGB request.

No RGB write occurred.

## Experiment 2 — RGB read

The firmware analysis identified:

```text
Command = 0x00020008
Command type = 0x04
```

as the path to `GC04`.

A one-byte zone selector was supplied.

The first successful response was:

```text
50 41 53 53 00 00 00 00 FF FF FF 00
```

The RGB bytes were identified at offsets 8–10:

```text
R = 255
G = 255
B = 255
```

This established a working read-only RGB oracle.

## Experiment 3 — Read all logical zones

The read probe was extended to query all four logical zones.

The firmware returned different RGB values for the logical fields at one point:

```text
Zone 1 = 255,255,255
Zone 2 = 0,255,0
Zone 3 = 0,0,255
Zone 4 = 255,255,255
```

The physical keyboard did not behave as four independently observable zones.

This prevented the firmware's four logical fields from being directly interpreted as four physical keyboard zones.

## Experiment 4 — OMEN Gaming Hub color validation

Windows OMEN Gaming Hub was used to establish a known keyboard configuration.

The keyboard was configured to:

```text
Effect: Static
Color: Purple
Brightness: 50%
```

The machine was then rebooted into Linux.

The read-only probe returned:

```text
Zone 1 = R:95 G:7 B:125
Zone 2 = R:95 G:7 B:125
Zone 3 = R:95 G:7 B:125
Zone 4 = R:95 G:7 B:125
```

This was significant because the values had changed as a result of the Windows configuration.

It demonstrated that the RGB fields discovered in ACPI correspond to real lighting state used by the keyboard controller.

## Experiment 5 — Brightness scaling

The same static purple color was tested at three OMEN Gaming Hub brightness settings.

### 25%

The readback was:

```text
R:47 G:3 B:62
```

### 50%

The readback was:

```text
R:95 G:7 B:125
```

### 100%

The readback was:

```text
R:191 G:15 B:250
```

The results were consistent across all four logical fields.

The values approximately scale linearly with brightness:

```text
25%   → 47, 3, 62
50%   → 95, 7, 125
100%  → 191, 15, 250
```

This strongly suggests that, for the tested static-color configuration, OMEN Gaming Hub stores brightness-scaled RGB values in the RGB fields rather than simply storing an unmodified RGB color alongside a separately observed brightness value.

This observation is specific to the tested configuration and does not by itself establish how every lighting mode represents brightness.

## Experiment 6 — First Linux RGB write

The firmware's `GC05` method provided the following candidate write operation:

```text
Command:
0x00020008

Command type:
0x05

Payload:
[zone, red, green, blue]
```

The first write test used:

```text
Zone = 1
R = 255
G = 0
B = 0
```

The resulting payload was:

```text
01 FF 00 00
```

The WMI call returned:

```text
ACPI status = 0x00000000
```

The firmware response was:

```text
PASS
```

The subsequent RGB read returned:

```text
Zone 1 = R:255 G:0 B:0
```

Most importantly, the physical keyboard visibly changed to red.

This was the first complete Linux-to-hardware validation of the reverse-engineered write path.

## Experiment 7 — Repeated color changes

The zone 1 write operation was repeated manually with different RGB values.

Each test successfully changed the physical keyboard color.

This established that the operation was not a one-time state transition and that the discovered write packet could be used repeatedly.

## Experiment 8 — Testing other logical zones

The firmware exposes four logical selectors.

Tests were performed against selectors 2, 3, and 4.

The physical keyboard did not show independent colors corresponding to four separate regions.

The result was therefore treated as evidence that the tested physical keyboard is effectively a single RGB zone, despite the firmware containing four logical RGB field sets.

The project does not currently claim independent four-zone hardware control.

## Final verified operation

The most important Stage 1 operation is:

```text
Input:
Zone 1
RGB = 255,0,0
```

Result:

```text
Firmware response:
PASS

Readback:
255,0,0

Physical result:
Keyboard turned red
```

This provides three independent validation points:

```text
WMI transport succeeded
        +
Firmware accepted the command
        +
Physical keyboard changed
```

## Safety boundaries during testing

The investigation deliberately avoided several higher-risk approaches.

No direct EC register writes were performed.

No firmware flashing was performed.

No ACPI override was installed.

The `hp_bioscfg` WMI device was not unbound.

No competing WMI driver was bound to the method device.

The first write test targeted a single RGB selector and was immediately followed by a readback.

## Current conclusion

The following is experimentally established for the tested HP Victus:

```text
HP WMI GUID
    ↓
0x00020008
    ↓
CMTP 0x05
    ↓
GC05
    ↓
RGB EC fields
    ↓
Physical keyboard RGB
```

The read path is:

```text
HP WMI GUID
    ↓
0x00020008
    ↓
CMTP 0x04
    ↓
GC04
    ↓
RGB response
```

The Stage 1 proof of concept can therefore control the physical keyboard RGB from Linux without direct EC access.

## Future investigation

Potential future work includes:

* Packaging the RGB interface as proper Linux software.
* GNOME integration.
* A graphical RGB color picker.
* Brightness controls.
* Persistent profiles.
* Investigation of additional lighting/effect commands.
* Testing additional HP Victus/OMEN hardware.
* Integration with upstream Linux RGB/LED infrastructure.

These are intentionally outside the scope of the Stage 1 proof of concept.
