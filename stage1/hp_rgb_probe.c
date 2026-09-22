#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/moduleparam.h>
#include <linux/acpi.h>

#define HP_WMI_GUID "5FB7F034-2C63-45E9-BE91-3D44E2C707E4"

struct hp_wmi_input {
    u32 signature;
    u32 command;
    u32 commandtype;
    u32 datasize;
    u8 data[];
};

static unsigned int zone = 1;
static unsigned int r = 255;
static unsigned int g = 0;
static unsigned int b = 0;

module_param(zone, uint, 0644);
MODULE_PARM_DESC(zone, "RGB zone (1-4)");

module_param(r, uint, 0644);
MODULE_PARM_DESC(r, "Red value (0-255)");

module_param(g, uint, 0644);
MODULE_PARM_DESC(g, "Green value (0-255)");

module_param(b, uint, 0644);
MODULE_PARM_DESC(b, "Blue value (0-255)");

static int hp_wmi_rgb_write(u8 zone, u8 r, u8 g, u8 b)
{
    struct hp_wmi_input *input;
    struct acpi_buffer in;
    struct acpi_buffer out;
    acpi_status status;

    input = kzalloc(sizeof(*input) + 4, GFP_KERNEL);
    if (!input)
        return -ENOMEM;

    input->signature = 0x55434553;
    input->command = 0x00020008;
    input->commandtype = 0x05;
    input->datasize = 4;

    input->data[0] = zone;
    input->data[1] = r;
    input->data[2] = g;
    input->data[3] = b;

    in.length = sizeof(*input) + 4;
    in.pointer = input;

    out.length = ACPI_ALLOCATE_BUFFER;
    out.pointer = NULL;

    pr_info(
        "hp_rgb_probe: writing zone %u = R:%u G:%u B:%u\n",
        zone, r, g, b
    );

    status = wmi_evaluate_method(
        HP_WMI_GUID,
        0,
        1,
        &in,
        &out
    );

    pr_info(
        "hp_rgb_probe: write ACPI status = 0x%08x\n",
        status
    );

    if (ACPI_SUCCESS(status) && out.pointer) {
        union acpi_object *obj = out.pointer;

        if (obj->type == ACPI_TYPE_BUFFER) {
            print_hex_dump(
                KERN_INFO,
                "hp_rgb_probe: write response: ",
                DUMP_PREFIX_OFFSET,
                16,
                1,
                obj->buffer.pointer,
                obj->buffer.length,
                true
            );
        }
    }

    if (out.pointer)
        kfree(out.pointer);

    kfree(input);

    return ACPI_SUCCESS(status) ? 0 : -EIO;
}

static void hp_wmi_rgb_read(u8 zone)
{
    struct hp_wmi_input *input;
    struct acpi_buffer in;
    struct acpi_buffer out;
    acpi_status status;

    input = kzalloc(sizeof(*input) + 1, GFP_KERNEL);
    if (!input)
        return;

    input->signature = 0x55434553;
    input->command = 0x00020008;
    input->commandtype = 0x04;
    input->datasize = 1;
    input->data[0] = zone;

    in.length = sizeof(*input) + 1;
    in.pointer = input;

    out.length = ACPI_ALLOCATE_BUFFER;
    out.pointer = NULL;

    status = wmi_evaluate_method(
        HP_WMI_GUID,
        0,
        2,
        &in,
        &out
    );

    if (ACPI_SUCCESS(status) && out.pointer) {
        union acpi_object *obj = out.pointer;

        if (obj->type == ACPI_TYPE_BUFFER &&
            obj->buffer.length >= 12 &&
            obj->buffer.pointer[0] == 'P' &&
            obj->buffer.pointer[1] == 'A' &&
            obj->buffer.pointer[2] == 'S' &&
            obj->buffer.pointer[3] == 'S') {

            pr_info(
                "hp_rgb_probe: zone %u readback = R:%u G:%u B:%u\n",
                zone,
                obj->buffer.pointer[8],
                obj->buffer.pointer[9],
                obj->buffer.pointer[10]
            );
        }
    }

    if (out.pointer)
        kfree(out.pointer);

    kfree(input);
}

static int __init hp_rgb_probe_init(void)
{
    int ret;

    if (zone < 1 || zone > 4) {
        pr_err("hp_rgb_probe: zone must be 1-4\n");
        return -EINVAL;
    }

    if (r > 255 || g > 255 || b > 255) {
        pr_err("hp_rgb_probe: RGB values must be 0-255\n");
        return -EINVAL;
    }

    pr_info("hp_rgb_probe: RGB WRITE\n");

    ret = hp_wmi_rgb_write(
        (u8)zone,
        (u8)r,
        (u8)g,
        (u8)b
    );

    if (ret) {
        pr_err("hp_rgb_probe: RGB write failed\n");
        return ret;
    }

    hp_wmi_rgb_read((u8)zone);

    pr_info("hp_rgb_probe: complete\n");

    return 0;
}

static void __exit hp_rgb_probe_exit(void)
{
    pr_info("hp_rgb_probe: unloaded\n");
}

module_init(hp_rgb_probe_init);
module_exit(hp_rgb_probe_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Janindu");
MODULE_DESCRIPTION("HP Victus RGB WMI research probe");
