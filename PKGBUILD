# Maintainer: Janindu <https://github.com/kra1t0>
# Stage 2 of hp-victus-16-rgb: sysfs kernel module + GTK4 GUI + CLI.
#
# This PKGBUILD builds from the LOCAL working tree (the directory it lives in),
# not from a remote git clone. Run it from the repo root, i.e. the directory
# that contains stage2/, stage1/, docs/, README.md.
#
#   cd ~/.../hp-victus-16-rgb
#   makepkg -si
#
# Nothing is downloaded: $startdir IS the source. This keeps the package in sync
# with whatever uncommitted stage2 work you have locally, and means stage2
# does not need to be pushed to GitHub before you can install it.

pkgname=hp-victus-rgb
pkgver=0.2.0
pkgrel=4
pkgdesc="HP Victus 16 keyboard RGB control (sysfs kernel module + GTK4 GUI + CLI) via HP WMI"
arch=('x86_64')
url="https://github.com/kra1t0/hp-victus-16-rgb"
license=('GPL-2.0-or-later')
depends=(
  # dkms is a runtime dep: the pacman hook invokes `dkms install` on package
  # install and on every kernel update to rebuild the module automatically.
  'dkms'
  'gtk4'        # GUI: GTK4 + cairo
  'python'      # GUI and CLI are plain Python 3
  'polkit'      # pkexec path for non-root color writes
  'kmod'        # modprobe in the install hook
)
makedepends=('linux-headers')
optdepends=(
  'linux-headers: rebuild module on kernel update (DKMS)'
)
provides=('hp-victus-rgb')
conflicts=('hp-victus-rgb-git')

# Build from the local tree. $startdir is the directory holding this PKGBUILD
# (the repo root). No source=() download is performed.
STAGE2="$startdir/stage2"

build() {
  # Build the module once here so a failed kernel build aborts the install
  # before anything is written to the system. The actual installed copy is
  # built later by DKMS against the running kernel.
  cd "$STAGE2/kernel"
  make
}

package() {
  cd "$STAGE2"

  # ---- kernel module sources via DKMS ----
  # NOTE: the install dir uses UNDERSCORES (hp_victus_rgb), matching
  # PACKAGE_NAME in stage2/kernel/dkms.conf. The hyphenated ${pkgname} would
  # make DKMS look for /usr/src/hp-victus-rgb-0.2.0/ and fail the build with
  # "specified external module directory does not exist".
  install -dvm755 "$pkgdir/usr/src/hp_victus_rgb-${pkgver}"
  cp -a kernel/. "$pkgdir/usr/src/hp_victus_rgb-${pkgver}/"

  # ---- load the module at boot ----
  # DKMS only BUILDS the .ko; it does not load it. systemd reads files in
  # /etc/modules-load.d/ at boot and modprobes each listed module. Without
  # this file the module is built but never loaded after a reboot, so the
  # GUI opens with "module not loaded". This is what makes it stick.
  install -Dvm644 /dev/stdin "$pkgdir/etc/modules-load.d/hp-victus-rgb.conf" <<EOF
# Loaded at boot so /sys/devices/platform/hp-victus-rgb/ exists for the GUI.
hp_victus_rgb
EOF

  # ---- CLI ----
  install -Dvm755 cli/hp-victus-rgb "$pkgdir/usr/bin/hp-victus-rgb"

  # ---- GUI ----
  install -Dvm755 gui/hp_victus_rgb.py "$pkgdir/usr/lib/$pkgname/hp_victus_rgb.py"
  install -dvm755 "$pkgdir/usr/bin"
  cat > "$pkgdir/usr/bin/hp-victus-rgb-gui" <<EOF
#!/bin/sh
exec python3 /usr/lib/$pkgname/hp_victus_rgb.py "\$@"
EOF
  chmod 755 "$pkgdir/usr/bin/hp-victus-rgb-gui"

  # ---- icon ----
  install -Dvm644 gui/hp-victus-rgb.svg \
    "$pkgdir/usr/share/icons/hicolor/scalable/apps/hp-victus-rgb.svg"

  # ---- .desktop ----
  install -Dvm644 gui/hp-victus-rgb.desktop \
    "$pkgdir/usr/share/applications/hp-victus-rgb.desktop"

  # ---- polkit policy ----
  install -Dvm644 polkit/org.hp.victus.rgb.policy \
    "$pkgdir/usr/share/polkit-1/actions/org.hp.victus.rgb.policy"

  # ---- pacman hook: auto-build the module on package install ----
  install -Dvm644 pkg/hp-victus-rgb-dkms.hook \
    "$pkgdir/usr/share/libalpm/hooks/hp-victus-rgb-dkms.hook"
  install -Dvm755 pkg/hp-victus-rgb-dkms.sh \
    "$pkgdir/usr/share/libalpm/scripts/hp-victus-rgb-dkms.sh"

  # ---- udev rule: open the sysfs attrs to the 'hp-victus' group ----
  # Members of the group can write colors without pkexec. The GUI falls back
  # to pkexec automatically if the user is not in the group.
  install -dvm755 "$pkgdir/usr/lib/udev/rules.d"
  cat > "$pkgdir/usr/lib/udev/rules.d/99-hp-victus-rgb.rules" <<EOF
# hp_victus_rgb kernel module sysfs attributes
KERNEL=="hp-victus-rgb", SUBSYSTEM=="platform", \\
  RUN+="/bin/chgrp hp-victus /sys/devices/platform/hp-victus-rgb/color"
KERNEL=="hp-victus-rgb", SUBSYSTEM=="platform", \\
  RUN+="/bin/chgrp hp-victus /sys/devices/platform/hp-victus-rgb/brightness"
KERNEL=="hp-victus-rgb", SUBSYSTEM=="platform", \\
  RUN+="/bin/chmod 664 /sys/devices/platform/hp-victus-rgb/color"
KERNEL=="hp-victus-rgb", SUBSYSTEM=="platform", \\
  RUN+="/bin/chmod 664 /sys/devices/platform/hp-victus-rgb/brightness"
EOF

  # ---- docs ----
  install -Dvm644 "$startdir/README.md" \
    "$pkgdir/usr/share/doc/$pkgname/README.md"
}

post_install() {
  # The DKMS hook built the .ko and a modules-load.d entry makes systemd load
  # it at every boot. We also load it right now so the user doesn't have to
  # reboot before the GUI works.
  modprobe hp_victus_rgb 2>/dev/null || true
  echo ":: hp-victus-rgb installed."
  echo "   The DKMS hook built the module; it is loaded now and will auto-load"
  echo "   at boot via /etc/modules-load.d/hp-victus-rgb.conf."
  echo "   Verify:  cat /sys/devices/platform/hp-victus-rgb/color"
  echo
  echo "   (Optional) Skip the pkexec prompt by joining the hp-victus group:"
  echo "     sudo groupadd -r hp-victus && sudo usermod -aG hp-victus \$USER"
  echo "     sudo udevadm control --reload && sudo udevadm trigger"
  echo "   (log out and back in for the group change to take effect)"
}

post_upgrade() {
  # The hook rebuilds for every installed kernel on upgrade; this is a fallback
  # for the case where someone runs `dkms` manually outside the hook.
  dkms remove hp_victus_rgb/"$2" --all 2>/dev/null || true
  dkms install hp_victus_rgb/"$1" || true
}

post_remove() {
  dkms remove hp_victus_rgb/"$1" --all 2>/dev/null || true
  groupdel hp-victus 2>/dev/null || true
}
