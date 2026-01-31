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
#include "hw/core/cpu.h"
#include "migration/vmstate.h"
#include "chardev/char-fe.h"
#include "chardev/char-serial.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/lockable.h"
#include "trace.h"

#define ORO_KDBG_NO_THREAD_ID 0xFF

/* Global chardev for oro_kdbg, protected by mutex */
static QemuMutex oro_kdbg_global_lock;
static CharFrontend *oro_kdbg_global_chr = NULL;

static void __attribute__((__constructor__)) oro_kdbg_mutex_init(void)
{
    qemu_mutex_init(&oro_kdbg_global_lock);
}

void oro_kdbg_register_global(CharFrontend *chr)
{
    QEMU_LOCK_GUARD(&oro_kdbg_global_lock);
    oro_kdbg_global_chr = chr;
}

void oro_kdbg_emit_global(uint64_t command_id, const uint64_t regs[7])
{
    uint8_t cpu_index = ORO_KDBG_NO_THREAD_ID;
    uint64_t zero_regs[7] = {0};
    
    QEMU_LOCK_GUARD(&oro_kdbg_global_lock);
    
    if (!oro_kdbg_global_chr) {
        return;  /* Not registered yet */
    }
    
    /* Get CPU index from current executing CPU */
    if (current_cpu) {
        uint32_t idx = current_cpu->cpu_index;
        if (idx > 255) {
            cpu_index = 255;
        } else {
            cpu_index = (uint8_t)idx;
        }
    }
    
    oro_kdbg_send_event(true, cpu_index, command_id,
                       regs ? regs : zero_regs,
                       oro_kdbg_global_chr);
}

/*
 * Encode and send an oro_kdbg event packet
 * 
 * @is_qemu_event: true if QEMU-generated event, false if kernel event
 * @cpu_index: CPU core index (0-254) or ORO_KDBG_NO_THREAD_ID
 * @command_id: 48-bit command ID (bits 47-0)
 * @regs: Array of 7 register values
 * @chr: Character frontend to send to
 */
void oro_kdbg_send_event(bool is_qemu_event, uint8_t cpu_index, 
                         uint64_t command_id, const uint64_t regs[7],
                         CharFrontend *chr)
{
    uint64_t packet[8];
    uint8_t reg_count = 0;
    
    /* Validate command_id doesn't have top 16 bits set */
    assert((command_id & 0xFFFF000000000000ULL) == 0);
    
    /* Build register bitmask (which of regs[1-7] are non-zero) */
    uint8_t bitmask = 0;
    reg_count = 1; /* reg[0] always sent */
    for (int i = 0; i < 7; i++) {
        if (regs[i] != 0) {
            bitmask |= (1 << i);
            packet[reg_count++] = regs[i];
        }
    }
    
    /* Encode reg[0]:
     * - Bit 63: is_qemu_event
     * - Bits 62-56: register bitmask
     * - Bits 55-48: cpu_index
     * - Bits 47-0: command_id
     */
    packet[0] = command_id | 
                ((uint64_t)cpu_index << 48) |
                ((uint64_t)bitmask << 56) |
                ((uint64_t)is_qemu_event << 63);
    
    /* Send packet */
    qemu_chr_fe_write_all(chr, (const uint8_t *)packet, reg_count * sizeof(uint64_t));
}

DeviceState *oro_kdbg_create(hwaddr addr, Chardev *chr)
{
    DeviceState *dev;
    SysBusDevice *s;
    OroKdbgState *state;

    dev = qdev_new("oro_kdbg");
    s = SYS_BUS_DEVICE(dev);
    qdev_prop_set_chr(dev, "chardev", chr);
    sysbus_realize_and_unref(s, &error_fatal);
    sysbus_mmio_map(s, 0, addr);

    /* Register global chardev for VM-wide access */
    state = ORO_KDBG(dev);
    oro_kdbg_register_global(&state->chr);

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

    /* When first register is written, validate and send packet */
    if (reg_index == 0) {
        /* Validate kernel didn't set reserved bits (63-48) */
        if (value & (1ULL << 63)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "oro_kdbg: Kernel attempted to send QEMU event (bit 63 set)\n");
            return;
        }
        if (value & (0x7FULL << 56)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "oro_kdbg: Kernel attempted to manually set register bitmask (bits 62-56)\n");
            return;
        }
        if (value & (0xFFULL << 48)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "oro_kdbg: Kernel attempted to manually set thread ID (bits 55-48)\n");
            return;
        }
        
        /* Get CPU index from current executing CPU */
        uint8_t cpu_index = ORO_KDBG_NO_THREAD_ID;
        if (current_cpu) {
            uint32_t idx = current_cpu->cpu_index;
            if (idx > 254) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "oro_kdbg: CPU index %u exceeds 254, skipping\n", idx);
            } else {
                cpu_index = (uint8_t)idx;

                /* Send event as kernel event */
                oro_kdbg_send_event(false, cpu_index, value, 
                                    &s->regs[1], &s->chr);
            }
        }
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
