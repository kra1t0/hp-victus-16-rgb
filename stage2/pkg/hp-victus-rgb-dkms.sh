#!/bin/bash
#
# pacman PostTransaction hook for hp-victus-rgb.
# Fires when the hp-victus-rgb package itself is installed or upgraded.
#
# Job: register the module sources with DKMS and build+install hp_victus_rgb.ko
#      for every installed kernel that has its matching -headers package,
#      then depmod + modprobe. After this, /sys/devices/platform/hp-victus-rgb/
#      exists and userspace can talk to it immediately.
#
# Kernel updates are handled by the SEPARATE hook that the distro `dkms` package
# ships (/usr/lib/systemd/system/alpm-hooks/dkms-install.hook on Arch). That
# hook fires when a new `linux` / `linux-lts` / `linux-zen` package is installed
# and rebuilds every registered DKMS module — including this one — against
# the new kernel. So `pacman -Syu` requires no manual recompile.
#
# This hook only needs to handle the package's own install/upgrade; it does not
# need to react to kernel package changes.
#
# `NeedsTargets` is set, so pacman feeds the package targets on stdin. We
# don't actually need them — DKMS figures out which kernels to build for —
# but the directive keeps the hook from firing on unrelated transactions.

set -e

MODULE="hp_victus_rgb"
VERSION="0.2.0"
SRC="/usr/src/${MODULE}-${VERSION}"

if [ ! -d "$SRC" ]; then
    # Sources gone -> package was removed. Tear down any built modules.
    dkms remove "$MODULE/$VERSION" --all >/dev/null 2>&1 || true
    exit 0
fi

# Install or upgrade. Register the sources with DKMS (idempotent), then build
# + install for every kernel whose headers are present.
dkms add "$MODULE/$VERSION" >/dev/null 2>&1 || true    # already-added is fine
dkms install "$MODULE/$VERSION" >/dev/null 2>&1 || true

# Run depmod for each kernel the module was built for.
for kdir in /lib/modules/*; do
    [ -d "$kdir" ] || continue
    if find "$kdir" -name "${MODULE}.ko" -print -quit 2>/dev/null | grep -q .; then
        depmod "$(basename "$kdir")" >/dev/null 2>&1 || true
    fi
done

# Load the freshly built module if not already loaded.
if ! lsmod | grep -q "^${MODULE} "; then
    modprobe "$MODULE" 2>/dev/null || true
fi
