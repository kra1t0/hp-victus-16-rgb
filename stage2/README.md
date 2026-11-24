# Stage 2 — userspace & desktop interface for the HP Victus RGB keyboard

Stage 1 proved the WMI protocol: `insmod hp_rgb_probe.ko zone=1 r=… g=… b=…`
writes one color to the EC and reads it back. Stage 2 turns that one-shot probe
into a live, usable desktop utility: a kernel module that stays loaded and
exposes a **sysfs interface**, plus a GTK4 GUI and a small CLI that talk to
it. You no longer `insmod`/`rmmod` for every color change — the module loads
once at boot and userspace just writes strings to a file.

> Same hardware limitation as Stage 1: the tested HP Victus keyboard is a
> **single physical zone**, even though the firmware exposes four logical
> zone fields. This driver fixes the active zone to **1** and does not expose
> zone selection to userspace. If a future model turns out to have multiple
> physical zones, expose a `zone` sysfs attribute and extend the GUI.

## Architecture

```
   GTK4 GUI / CLI  ──writes──>  /sys/devices/platform/hp-victus-rgb/{color,brightness}
        │                                    │
        │ (pkexec)                            │
        ▼                                    ▼
   hp-victus-rgb CLI            hp_victus_rgb.ko  (sysfs → HP WMI → ACPI → EC)
```

The GUI and CLI are unprivileged. They read the sysfs files directly (those
are world-readable) and write through the `hp-victus-rgb` CLI, which the
PKGBUILD installs with a **polkit policy** so an active desktop user can
authenticate once per session. Alternatively, add yourself to the `hp-victus`
group and the udev rule opens the sysfs files to that group directly — no
pkexec prompt at all.

## Repository layout

```
stage2/
├── kernel/
│   ├── hp_victus_rgb.c     sysfs-backed module (load once, write via sysfs)
│   ├── Makefile
│   └── dkms.conf           auto-rebuilds on kernel updates
├── cli/
│   └── hp-victus-rgb       python CLI: set / brightness / get / watch
├── gui/
│   ├── hp_victus_rgb.py    GTK4 app: color wheel + brightness slider
│   ├── hp-victus-rgb.svg   app icon (hicolor/scalable)
│   └── hp-victus-rgb.desktop
├── polkit/
│   └── org.hp.victus.rgb.policy
└── pkg/
    ├── PKGBUILD            Arch package (DKMS + GUI + CLI + polkit + udev)
    ├── hp-victus-rgb-dkms.hook   pacman PostTransaction hook
    └── hp-victus-rgb-dkms.sh     hook script (dkms add/install/depmod/modprobe)
```

## Build & install

### Option A — Arch package (recommended)

```bash
cd stage2/pkg
makepkg -si
```

That's it. The PKGBUILD installs a pacman hook that automatically:

1. registers the module sources with DKMS,
2. builds `hp_victus_rgb.ko` for every installed kernel that has its matching
   `-headers` package,
3. runs `depmod`, and
4. `modprobe`s the module.

So `cat /sys/devices/platform/hp-victus-rgb/color` works immediately after
the install finishes — no manual `dkms install` or `modprobe`.

Kernel updates are handled by a **separate** hook that the distro `dkms`
package itself ships (Arch's `dkms` provides
`/usr/lib/systemd/system/alpm-hooks/dkms-install.hook`). That hook fires
when a new `linux` (or `linux-lts`, `linux-zen`, …) package is installed and
rebuilds every registered DKMS module — including this one — against the
new kernel. So `pacman -Syu` requires **no manual recompile**, which is the
whole point of using DKMS instead of "user compiles from source."

```bash
# verify after install
cat /sys/devices/platform/hp-victus-rgb/color
hp-victus-rgb get
hp-victus-rgb-gui        # launch from the app menu or terminal
```

#### Why the module still works after reboot

DKMS only *builds* the `.ko` against each kernel; it does not load anything.
The package also installs `/etc/modules-load.d/hp-victus-rgb.conf` containing
`hp_victus_rgb`, so systemd `modprobe`s the module at every boot. Without that
file the GUI would open with "module not loaded" after a reboot even though
`dkms status` reports the module as installed.

#### Icon not appearing?

The icon is a single scalable SVG at
`/usr/share/icons/hicolor/scalable/apps/hp-victus-rgb.svg`. GNOME caches icons
per session, so after the first install:

```bash
sudo gtk-update-icon-cache -f /usr/share/icons/hicolor/
```

then **log out and back in** (on Wayland there is no live shell restart; a
re-login is required for the app grid to pick up new icons).

### Option B — manual build (any distro)

```bash
cd stage2/kernel
make
sudo insmod ./hp_victus_rgb.ko
```

Then run the GUI/CLI directly from this tree (they read
`/sys/devices/platform/hp-victus-rgb/`, which now exists). For writes you'll
need `sudo hp-victus-rgb …` in dev mode (the polkit policy is only installed
by the package).

## Sysfs interface

Once loaded, the module creates:

```
/sys/devices/platform/hp-victus-rgb/
├── color       rw  RRGGBB hex, e.g. "ff8800"  (brightness-scaled on write)
├── brightness  rw  0-100 percent
└── raw          rw  RRGGBB hex, bypasses brightness scaling (debug)
```

Quick test:

```bash
sudo sh -c 'echo ff8800 > /sys/devices/platform/hp-victus-rgb/color'
sudo sh -c 'echo 80 > /sys/devices/platform/hp-victus-rgb/brightness'
cat /sys/devices/platform/hp-victus-rgb/color       # -> ff8800 (stored hue)
cat /sys/devices/platform/hp-victus-rgb/raw         # -> current hardware read
```

`color` holds the **stored hue** at full brightness; the actual value sent to
the EC is `color * brightness / 100`. `raw` reads back the live value the
firmware reports and lets you bypass scaling for experiments.

## CLI

```bash
hp-victus-rgb get                          # show current color + brightness
sudo hp-victus-rgb set "#ff8800"           # set color
sudo hp-victus-rgb set ff8800 -b 80        # color + brightness
sudo hp-victus-rgb brightness 65           # brightness only, keep hue
hp-victus-rgb watch                        # poll & print changes (debug)
```

The `get` command also prints a one-line `#ff8800 80%` form that's handy for
status bars (Waybar, polybar, etc.).

## GUI

Launch from the application menu ("HP Victus RGB") or run `hp-victus-rgb-gui`.

- **Color wheel** — drag the marker anywhere; hue is the angle, saturation
  is the distance from the center (white at center, pure hue at the edge).
- **Hex field** — type a `RRGGBB` value and press Enter to set it exactly.
- **Brightness slider** — 0-100%, re-applies the current hue on the fly.
- **Preview swatch** — shows the brightness-scaled color that will hit the
  keyboard, so you know what `#ff8800 @ 30%` actually looks like.
- **Apply while dragging** — when on (default), every pointer move on the
  wheel writes immediately. Turn it off to compose a color, then click
  **Apply to keyboard** once. Useful on battery or if you want to avoid a
  burst of WMI calls.

If the module isn't loaded, the GUI shows a build-and-load hint instead of
the controls.

## Permissions model

The module creates the sysfs files `0644 root:root` by default, so a write
needs root. Two supported paths:

1. **polkit (default).** The GUI/CLI shell out to
   `pkexec hp-victus-rgb …`. The installed policy
   (`org.hp.victus.rgb.policy`) lets an active desktop user authenticate
   once per session (`auth_admin_keep`), so dragging the wheel doesn't
   re-prompt constantly. Tighten to `auth_admin` if you'd rather re-auth
   every write.
2. **group + udev (no prompts).** Add yourself to the `hp-victus` group:

   ```bash
   sudo groupadd -r hp-victus
   sudo usermod -aG hp-victus "$USER"   # then log out & back in
   sudo udevadm control --reload && sudo udevadm trigger
   ```

   The udev rule in the package chgrps the sysfs files to `hp-victus` and
   makes them `0664`, so the GUI writes directly — no pkexec. The GUI still
   falls back to pkexec automatically if the user isn't in the group.

## Cross-DE

The GUI is plain GTK4 (no libadwaita hard requirement, no GNOME Shell
internals). It runs on GNOME 51 (where this was developed), KDE Plasma,
XFCE, Cinnamon — anything that ships GTK4. We deliberately did **not** write
a GNOME Shell extension: Shell extensions are version-fragile across GNOME
major bumps (the exact pain the kernel-DKMS approach avoids), and tying a
hardware utility to one DE is the wrong tradeoff.

## Kernel updates

Because the module is registered with DKMS, a `pacman -Syu` that installs a
new `linux` package triggers DKMS to rebuild `hp_victus_rgb.ko` against the
new headers automatically. You only need to install the kernel headers
matching the new kernel (`sudo pacman -S linux-headers`) — DKMS does the
rest. There is no need to manually recompile per kernel version, which was
the original concern.

## Safety

Stage 2 uses exactly the same WMI write path as the Stage 1 probe — the
same GUID, the same `0x00020008 / commandtype 0x05` packet, the same zone 1
field. Nothing about the firmware access changed; only the userspace
interface did. The same disclaimers from Stage 1 apply: this is
hardware/firmware-specific to the tested HP Victus, and you run it at your
own risk. No firmware is flashed, no ACPI override is installed, no direct
EC writes are performed — everything goes through the firmware's own WMI
method.

## What's not here (future work)

- **Multiple zones** — only exposed when a model is confirmed to have them.
- **Effects / animations** — breathing, wave, per-key. The firmware may or
  may not support these; the current WMI write sets a static color only.
  Investigate the `commandtype` space before claiming any effect works.
- **Persistence across reboot** — the EC may or may not retain the color.
  If it doesn't, ship a small systemd unit that re-applies the last color
  on boot (read it from `/var/lib/hp-victus-rgb/last`).
- **Upstreaming** — the long-term goal of the project is documentation and a
  reproducible PoC, not a replacement for proper `hp_wmi` upstream support.
