/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_ORO_KDBG_H
#define HW_ORO_KDBG_H

#include "hw/core/sysbus.h"
#include "chardev/char-fe.h"
#include "qom/object.h"

#define TYPE_ORO_KDBG "oro_kdbg"
OBJECT_DECLARE_SIMPLE_TYPE(OroKdbgState, ORO_KDBG)

struct OroKdbgState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint64_t regs[8];
    CharFrontend chr;
};

DeviceState *oro_kdbg_create(hwaddr addr, Chardev *chr);

/*
 * Send an oro_kdbg event packet from QEMU
 * 
 * @is_qemu_event: true for QEMU-generated event, false for kernel event
 * @cpu_index: CPU core index (0-254) or 0xFF for no thread
 * @command_id: 48-bit command ID (must have bits 63-48 clear)
 * @regs: Array of 7 register values (regs[1-7])
 * @chr: Character frontend to send to
 */
void oro_kdbg_send_event(bool is_qemu_event, uint8_t cpu_index,
                         uint64_t command_id, const uint64_t regs[7],
                         CharFrontend *chr);

/*
 * Register the global oro_kdbg chardev
 * Called automatically by oro_kdbg_create()
 * 
 * @chr: CharFrontend pointer to store for global access
 */
void oro_kdbg_register_global(CharFrontend *chr);

/*
 * Emit an event from QEMU using the global chardev
 * Thread-safe, can be called from any QEMU thread
 * No-op if global chardev not registered
 * 
 * @command_id: 48-bit command ID (must have bits 63-48 clear)
 * @regs: Array of 7 register values, or NULL for all zeros
 * 
 * CPU index automatically determined from current_cpu if available
 * Always sets is_qemu_event=true (bit 63)
 */
void oro_kdbg_emit_global(uint64_t command_id, const uint64_t regs[7]);

#endif
