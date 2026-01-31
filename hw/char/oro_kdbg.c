/*
 * Oro Operating System Debug MMIO Interface
 *
 * Copyright (c) 2026 the Oro Operating System Project.
 * Written by Joshua Lee Junon
 *
 * This code is licensed under the GPL.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/char/oro_kdbg.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "migration/vmstate.h"
#include "chardev/char-fe.h"
#include "chardev/char-serial.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "trace.h"

DeviceState *oro_kdbg_create(hwaddr addr, Chardev *chr)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_new("oro_kdbg");
    s = SYS_BUS_DEVICE(dev);
    qdev_prop_set_chr(dev, "chardev", chr);
    sysbus_realize_and_unref(s, &error_fatal);
    sysbus_mmio_map(s, 0, addr);

    return dev;
}

static uint64_t oro_kdbg_read(void *opaque, hwaddr offset,
                              unsigned size)
{
    qemu_log_mask(LOG_GUEST_ERROR,
                  "Oro kernel debug MMIO device is write-only; kernel performed a read\n");
    return 0;
}

static void oro_kdbg_write(void *opaque, hwaddr offset,
                           uint64_t value, unsigned size)
{
    OroKdbgState *s = (OroKdbgState *)opaque;
    unsigned reg_index = offset >> 3;  /* Divide by 8 */

    /* Ignore unaligned writes */
    if ((offset % 8) != 0) {
        return;
    }

    if (reg_index >= 8) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "oro_kdbg_write: Bad offset 0x%" HWADDR_PRIx "\n", offset);
        return;
    }

    s->regs[reg_index] = value;

    /* When first register is written, send all 8 registers (64 bytes) to chardev */
    if (reg_index == 0) {
        qemu_chr_fe_write_all(&s->chr, (const uint8_t *)s->regs, sizeof(s->regs));
    }
}

static const MemoryRegionOps oro_kdbg_ops = {
    .read = oro_kdbg_read,
    .write = oro_kdbg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
};

static void oro_kdbg_reset(DeviceState *dev)
{
    OroKdbgState *s = ORO_KDBG(dev);

    memset(s->regs, 0, sizeof(s->regs));
}

static const VMStateDescription vmstate_oro_kdbg = {
    .name = "oro_kdbg",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64_ARRAY(regs, OroKdbgState, 8),
        VMSTATE_END_OF_LIST()
    }
};

static const Property oro_kdbg_properties[] = {
    DEFINE_PROP_CHR("chardev", OroKdbgState, chr),
};

static void oro_kdbg_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    OroKdbgState *s = ORO_KDBG(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &oro_kdbg_ops, s, "oro_kdbg", 0x40);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void oro_kdbg_realize(DeviceState *dev, Error **errp)
{
    OroKdbgState *s = ORO_KDBG(dev);
    uint64_t init_packet[8];
    
    /* Send initialization packet of all 0xFF bytes */
    memset(init_packet, 0xFF, sizeof(init_packet));
    qemu_chr_fe_write_all(&s->chr, (const uint8_t *)init_packet, sizeof(init_packet));
}

static void oro_kdbg_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = oro_kdbg_realize;
    device_class_set_legacy_reset(dc, oro_kdbg_reset);
    dc->vmsd = &vmstate_oro_kdbg;
    device_class_set_props(dc, oro_kdbg_properties);
}

static const TypeInfo oro_kdbg_info = {
    .name          = TYPE_ORO_KDBG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(OroKdbgState),
    .instance_init = oro_kdbg_init,
    .class_init    = oro_kdbg_class_init,
};

static void oro_kdbg_register_types(void)
{
    type_register_static(&oro_kdbg_info);
}

type_init(oro_kdbg_register_types)
