   GTK4 GUI / CLI  ──writes──>  /sys/devices/platform/hp-victus-rgb/{color,brightness}
        │                                    │
        │ (pkexec)                            │
        ▼                                    ▼
   hp-victus-rgb CLI            hp_victus_rgb.ko  (sysfs → HP WMI → ACPI → EC)
