#!/usr/bin/env python3
"""
HP Victus RGB - keyboard color control GUI.

A small GTK4 application that provides a color wheel and a brightness slider
for the HP Victus keyboard RGB. It talks to the sysfs interface exposed by
the Stage 2 kernel module:

    /sys/devices/platform/hp-victus-rgb/color
    /sys/devices/platform/hp-victus-rgb/brightness

Writing to those files requires root. The app itself does NOT run as root;
instead it shells out to the `hp-victus-rgb` CLI through pkexec (or sudo for
development), which is the right way to talk to hardware from a desktop app.

Run (dev):
    python3 hp_victus_rgb.py

Run (installed):
    hp-victus-rgb-gui
"""

import colorsys
import math
import os
import subprocess
import sys

import gi

gi.require_version("Gtk", "4.0")
gi.require_version("Gdk", "4.0")
gi.require_version("Graphene", "1.0")
from gi.repository import Gtk, Gdk, GLib, Gio, GObject, Graphene
import cairo

# ----------------------------------------------------------------------
# Backend: talk to the kernel module via the CLI helper.
# ----------------------------------------------------------------------

SYSFS = "/sys/devices/platform/hp-victus-rgb"
CLI = "hp-victus-rgb"          # must be on PATH; runs as root via pkexec/sudo


class Backend:
    """Thin wrapper around the sysfs attributes / CLI helper.

    Read is direct (sysfs is world-readable by default). Write needs root, so
    we delegate to the CLI which the PKGBUILD installs with a polkit policy.
    In a pinch `sudo hp-victus-rgb ...` also works for development.
    """

    def __init__(self) -> None:
        self.color_path = os.path.join(SYSFS, "color")
        self.bright_path = os.path.join(SYSFS, "brightness")

    def available(self) -> bool:
        return os.path.isdir(SYSFS)

    def read(self) -> tuple[str, int]:
        try:
            with open(self.color_path) as f:
                color = f.read().strip()
            with open(self.bright_path) as f:
                bright = int(f.read().strip())
        except OSError as e:
            print(f"backend read failed: {e}", file=sys.stderr)
            color = "ffffff"
            bright = 100
        return color, bright

    def write_color(self, hex_color: str) -> None:
        self._run_cli(["set", hex_color])

    def write_brightness(self, value: int) -> None:
        self._run_cli(["brightness", str(value)])

    def _run_cli(self, argv: list[str]) -> None:
        # Try the polkit path first (integration-friendly), fall back to
        # sudo for dev boxes without polkit.
        for launcher in (self._via_pkexec, self._via_sudo):
            try:
                rc = launcher(argv)
                if rc == 0:
                    return
            except FileNotFoundError:
                continue
        # Last resort: try a direct write (works if root or perms were changed).
        target = self.color_path if argv[0] == "set" else self.bright_path
        try:
            with open(target, "w") as f:
                f.write(argv[1] + "\n")
        except OSError as e:
            print(f"backend write failed: {e}", file=sys.stderr)

    def _via_pkexec(self, argv: list[str]) -> int:
        return subprocess.call(["pkexec", CLI, *argv])

    def _via_sudo(self, argv: list[str]) -> int:
        # `-n` so sudo never blocks the GUI waiting for a password prompt
        # that has no terminal to read from.
        return subprocess.call(["sudo", "-n", CLI, *argv])


# ----------------------------------------------------------------------
# Color wheel widget: HSV wheel with a draggable pointer.
# ----------------------------------------------------------------------

WHEEL_SIZE = 320


class ColorWheel(Gtk.Widget):
    """A circular HSV color wheel. Saturation increases from center (white)
    to edge (pure hue); hue is the angle. Clicking or dragging sets the
    current color and emits `color-changed` with the new hex string.
    """

    __gtype_name__ = "HpVictusColorWheel"

    __gsignals__ = {
        "color-changed": (
            GObject.SignalFlags.RUN_FIRST,
            None,
            (str,),
        ),
    }

    def __init__(self) -> None:
        super().__init__()
        self._hue = 0.0       # 0..1
        self._sat = 0.0       # 0..1 (distance from center, fraction of radius)
        self.set_size_request(WHEEL_SIZE, WHEEL_SIZE)

        # Pointer / drag handling via GTK4 EventController.
        drag = Gtk.GestureDrag()
        drag.set_button(0)  # any button
        drag.connect("drag-begin", self._on_drag_begin)
        drag.connect("drag-update", self._on_drag_update)
        self.add_controller(drag)

        click = Gtk.GestureClick()
        click.set_button(0)
        click.connect("pressed", self._on_click_pressed)
        self.add_controller(click)

    def do_get_request_mode(self):  # type: ignore[override]
        return Gtk.SizeRequestMode.CONSTANT_SIZE

    def get_hex(self) -> str:
        r, g, b = colorsys.hsv_to_rgb(self._hue, max(self._sat, 0.0), 1.0)
        return "%02x%02x%02x" % (int(r * 255), int(g * 255), int(b * 255))

    def set_from_hex(self, hex_color: str) -> None:
        r = int(hex_color[0:2], 16) / 255.0
        g = int(hex_color[2:4], 16) / 255.0
        b = int(hex_color[4:6], 16) / 255.0
        h, s, _v = colorsys.rgb_to_hsv(r, g, b)
        self._hue = h
        self._sat = s
        self.queue_draw()

    def _pointer_to_hsv(self, x: float, y: float) -> tuple[float, float]:
        cx = WHEEL_SIZE / 2.0
        cy = WHEEL_SIZE / 2.0
        dx = x - cx
        dy = y - cy
        dist = math.hypot(dx, dy)
        radius = WHEEL_SIZE / 2.0
        sat = min(dist / radius, 1.0)
        # Angle measured clockwise from 12 o'clock so red sits at the right.
        angle = math.atan2(dx, -dy)
        if angle < 0:
            angle += 2 * math.pi
        hue = angle / (2 * math.pi)
        return hue, sat

    def _handle_point(self, x: float, y: float) -> None:
        h, s = self._pointer_to_hsv(x, y)
        self._hue = h
        self._sat = s
        self.queue_draw()
        self.emit("color-changed", self.get_hex())

    # --- input handlers ---

    def _on_click_pressed(self, g, _n, x, y):
        self._handle_point(x, y)

    def _on_drag_begin(self, _g, _x, _y):
        pass

    def _on_drag_update(self, g, _dx, _dy):
        sx = g.get_start_x()
        sy = g.get_start_y()
        ox, oy = g.get_offset()
        self._handle_point(sx + ox, sy + oy)

    # --- drawing ---

    def do_snapshot(self, snapshot):  # type: ignore[override]
        width = height = WHEEL_SIZE
        surf = cairo.ImageSurface(cairo.Format.ARGB32, width, height)
        cr = cairo.Context(surf)
        cx = width / 2.0
        cy = height / 2.0
        outer = min(width, height) / 2.0 - 2

        # Hue ring: 360 thin pie slices around the disk, each at saturation 1.
        steps = 360
        for i in range(steps):
            t0 = i / steps
            t1 = (i + 1) / steps
            r, g, b = colorsys.hsv_to_rgb(t0, 1.0, 1.0)
            cr.set_source_rgb(r, g, b)
            cr.move_to(cx, cy)
            cr.arc(cx, cy, outer,
                   2 * math.pi * t0 - math.pi / 2.0,
                   2 * math.pi * t1 - math.pi / 2.0)
            cr.close_path()
            cr.fill()

        # Overlay a white radial gradient so the center becomes white
        # (saturation=0) and the edge stays the pure hue (saturation=1),
        # giving the classic HSV wheel look.
        pat = cairo.RadialGradient(cx, cy, 0, cx, cy, outer)
        pat.add_color_stop_rgba(0.0, 1, 1, 1, 1.0)
        pat.add_color_stop_rgba(1.0, 1, 1, 1, 0.0)
        cr.set_source(pat)
        cr.arc(cx, cy, outer, 0, 2 * math.pi)
        cr.fill()

        # Outline.
        cr.set_source_rgba(0, 0, 0, 0.3)
        cr.set_line_width(1.5)
        cr.arc(cx, cy, outer, 0, 2 * math.pi)
        cr.stroke()

        # Pointer marker at the current hue/sat position.
        angle = 2 * math.pi * self._hue - math.pi / 2.0
        px = cx + math.cos(angle) * self._sat * outer
        py = cy + math.sin(angle) * self._sat * outer
        cr.set_source_rgb(1, 1, 1)
        cr.arc(px, py, 9, 0, 2 * math.pi)
        cr.fill()
        cr.set_source_rgb(0, 0, 0)
        cr.set_line_width(1.5)
        cr.arc(px, py, 9, 0, 2 * math.pi)
        cr.stroke()

        # Hand the cairo surface to GTK4's snapshot.
        texture = Gdk.Texture.new_for_surface(surf)
        rect = Graphene.Rect.alloc()
        rect.init(0, 0, width, height)
        snapshot.append_texture(texture, rect)


# ----------------------------------------------------------------------
# Main application window
# ----------------------------------------------------------------------

class AppWindow(Gtk.ApplicationWindow):
    def __init__(self, app: Gtk.Application) -> None:
        super().__init__(application=app, title="HP Victus RGB")
        self.set_default_size(420, 580)
        self.backend = Backend()
        self._suppress_wheel = False

        if not self.backend.available():
            self._show_missing()
            return

        self._build_ui()
        self._load_current()

    # ---- UI ----

    def _build_ui(self) -> None:
        outer = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=18)
        outer.set_margin_top(24)
        outer.set_margin_bottom(24)
        outer.set_margin_start(24)
        outer.set_margin_end(24)

        title = Gtk.Label(label="HP Victus 16 - Keyboard RGB")
        title.add_css_class("title-2")
        outer.append(title)

        subtitle = Gtk.Label(
            label="Single zone (Z01). Drag the wheel to pick a color.")
        subtitle.add_css_class("dim-label")
        outer.append(subtitle)

        # Color wheel
        self.wheel = ColorWheel()
        self.wheel.connect("color-changed", self._on_wheel_changed)
        wheel_box = Gtk.Box()
        wheel_box.set_halign(Gtk.Align.CENTER)
        wheel_box.set_margin_top(18)
        wheel_box.set_margin_bottom(18)
        wheel_box.append(self.wheel)
        outer.append(wheel_box)

        # Hex entry
        hex_row = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        hex_row.set_halign(Gtk.Align.CENTER)
        hex_label = Gtk.Label(label="Hex")
        hex_label.set_xalign(0)
        self.hex_entry = Gtk.Entry()
        self.hex_entry.set_width_chars(8)
        self.hex_entry.set_max_length(7)  # allow leading #
        self.hex_entry.connect("activate", self._on_hex_activate)
        hex_row.append(hex_label)
        hex_row.append(self.hex_entry)
        outer.append(hex_row)

        # Brightness
        bright_label = Gtk.Label(label="Brightness")
        bright_label.set_xalign(0)
        outer.append(bright_label)

        self.bright_scale = Gtk.Scale.new_with_range(
            Gtk.Orientation.HORIZONTAL, 0, 100, 1)
        self.bright_scale.set_draw_value(True)
        self.bright_scale.set_value_pos(Gtk.PositionType.RIGHT)
        self.bright_scale.connect("value-changed", self._on_bright_changed)
        outer.append(self.bright_scale)

        # Color preview swatch (shows brightness-scaled result)
        self.preview = Gtk.DrawingArea()
        self.preview.set_content_height(48)
        self.preview.set_draw_func(self._draw_preview, None)
        outer.append(self.preview)

        # Apply-on-drag toggle
        self.live_switch = Gtk.Switch()
        self.live_switch.set_active(True)
        live_row = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        live_row.set_halign(Gtk.Align.CENTER)
        live_row.append(Gtk.Label(label="Apply while dragging"))
        live_row.append(self.live_switch)
        outer.append(live_row)

        # Apply button (for non-live mode)
        self.apply_btn = Gtk.Button(label="Apply to keyboard")
        self.apply_btn.add_css_class("suggested-action")
        self.apply_btn.connect("clicked", self._on_apply_clicked)
        self.apply_btn.set_halign(Gtk.Align.CENTER)
        outer.append(self.apply_btn)

        self.set_child(outer)

    def _show_missing(self) -> None:
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12,
                      valign=Gtk.Align.CENTER, halign=Gtk.Align.CENTER)
        box.set_margin_start(32)
        box.set_margin_end(32)
        box.append(Gtk.Label(label="Kernel module not loaded"))
        warn = Gtk.Label(
            label=("The hp_victus_rgb module is not loaded.\n\n"
                   "Build and load it first:\n\n"
                   "  cd stage2/kernel && make && sudo insmod ./hp_victus_rgb.ko\n\n"
                   "Or install via DKMS:\n\n"
                   "  sudo dkms install hp_victus_rgb/0.2.0"))
        warn.set_wrap(True)
        box.append(warn)
        self.set_child(box)

    # ---- state ----

    def _load_current(self) -> None:
        color, bright = self.backend.read()
        self._suppress_wheel = True
        self.wheel.set_from_hex(color)
        self.hex_entry.set_text("#" + color)
        self.bright_scale.set_value(bright)
        self._suppress_wheel = False
        self.preview.queue_draw()

    # ---- handlers ----

    def _current_hex(self) -> str:
        return self.hex_entry.get_text().lstrip("#")

    def _on_wheel_changed(self, _w, hex_color: str) -> None:
        self.hex_entry.set_text("#" + hex_color)
        self.preview.queue_draw()
        if self.live_switch.get_active() and not self._suppress_wheel:
            self.backend.write_color(hex_color)

    def _on_hex_activate(self, _e) -> None:
        v = self._current_hex()
        if len(v) == 6:
            self._suppress_wheel = True
            self.wheel.set_from_hex(v)
            self._suppress_wheel = False
            self.preview.queue_draw()
            if self.live_switch.get_active():
                self.backend.write_color(v)

    def _on_bright_changed(self, s) -> None:
        val = int(s.get_value())
        self.preview.queue_draw()
        if self.live_switch.get_active():
            self.backend.write_brightness(val)

    def _on_apply_clicked(self, _b) -> None:
        self.backend.write_color(self._current_hex())
        self.backend.write_brightness(int(self.bright_scale.get_value()))

    def _draw_preview(self, area, cr, _w, _h, _data) -> None:
        v = self._current_hex()
        if len(v) != 6:
            return
        r = int(v[0:2], 16) / 255.0
        g = int(v[2:4], 16) / 255.0
        b = int(v[4:6], 16) / 255.0
        scale = int(self.bright_scale.get_value()) / 100.0
        cr.set_source_rgb(r * scale, g * scale, b * scale)
        w = area.get_allocated_width()
        cr.rectangle(0, 0, w, 48)
        cr.fill()


# ----------------------------------------------------------------------
# Application
# ----------------------------------------------------------------------

class App(Gtk.Application):
    def __init__(self) -> None:
        super().__init__(application_id="org.hp.victus.rgb",
                         flags=Gio.ApplicationFlags.FLAGS_NONE)

    def do_activate(self):  # type: ignore[override]
        win = AppWindow(self)
        win.present()


def main() -> int:
    return App().run(sys.argv)


if __name__ == "__main__":
    sys.exit(main())
