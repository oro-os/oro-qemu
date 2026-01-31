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

/* Oro kernel debug event IDs */
enum {
    /* x86/x86-64 Exception event
     * reg[1] = exception number (0-31)
     * reg[2] = error code (if applicable)
     * reg[3] = CR2 (page fault linear address)
     * reg[4] = RIP/EIP
     * reg[5] = CS
     * reg[6] = RFLAGS/EFLAGS
     * reg[7] = CPL (current privilege level)
     */
    ORO_KDBEVT_X86_EXCEPTION = 0x1000,

    /* x86/x86-64 Register dump 0: General purpose registers
     * reg[1] = RAX/EAX
     * reg[2] = RBX/EBX
     * reg[3] = RCX/ECX
     * reg[4] = RDX/EDX
     * reg[5] = RSI/ESI
     * reg[6] = RDI/EDI
     * reg[7] = RBP/EBP
     */
    ORO_KDBEVT_X86_REG_DUMP0 = 0x1001,

    /* x86/x86-64 Register dump 1: Stack pointer and R8-R13 (64-bit only)
     * reg[1] = RSP/ESP
     * reg[2] = R8  (0 in 32-bit mode)
     * reg[3] = R9  (0 in 32-bit mode)
     * reg[4] = R10 (0 in 32-bit mode)
     * reg[5] = R11 (0 in 32-bit mode)
     * reg[6] = R12 (0 in 32-bit mode)
     * reg[7] = R13 (0 in 32-bit mode)
     */
    ORO_KDBEVT_X86_REG_DUMP1 = 0x1002,

    /* x86/x86-64 Register dump 2: R14-R15 and segment selectors (64-bit only)
     * reg[1] = R14 (0 in 32-bit mode)
     * reg[2] = R15 (0 in 32-bit mode)
     * reg[3] = ES selector
     * reg[4] = DS selector
     * reg[5] = FS selector
     * reg[6] = GS selector
     * reg[7] = SS selector
     */
    ORO_KDBEVT_X86_REG_DUMP2 = 0x1003,

    /* x86/x86-64 Register dump 3: Control registers
     * reg[1] = CR0
     * reg[2] = CR3
     * reg[3] = CR4
     * reg[4] = unused (CR8 is APIC TPR, complex to access)
     * reg[5] = EFER (extended feature enable register)
     * reg[6] = unused
     * reg[7] = unused
     */
    ORO_KDBEVT_X86_REG_DUMP3 = 0x1004,

    /* x86/x86-64 Register dump 4: Debug registers
     * reg[1] = DR0
     * reg[2] = DR1
     * reg[3] = DR2
     * reg[4] = DR3
     * reg[5] = DR6
     * reg[6] = DR7
     * reg[7] = unused
     */
    ORO_KDBEVT_X86_REG_DUMP4 = 0x1005,

    /* AArch64 Exception event
     * reg[1] = exception index (QEMU internal)
     * reg[2] = ESR_EL (Exception Syndrome Register)
     * reg[3] = FAR_EL (Fault Address Register)
     * reg[4] = PC at time of exception
     * reg[5] = PSTATE at time of exception
     * reg[6] = current exception level (0-3)
     * reg[7] = SP at time of exception
     */
    ORO_KDBEVT_AA64_EXCEPTION = 0x2000,

    /* AArch64 Register dump 0: X0-X6
     * reg[1] = X0
     * reg[2] = X1
     * reg[3] = X2
     * reg[4] = X3
     * reg[5] = X4
     * reg[6] = X5
     * reg[7] = X6
     */
    ORO_KDBEVT_AA64_REG_DUMP0 = 0x2001,

    /* AArch64 Register dump 1: X7-X13
     * reg[1] = X7
     * reg[2] = X8
     * reg[3] = X9
     * reg[4] = X10
     * reg[5] = X11
     * reg[6] = X12
     * reg[7] = X13
     */
    ORO_KDBEVT_AA64_REG_DUMP1 = 0x2002,

    /* AArch64 Register dump 2: X14-X20
     * reg[1] = X14
     * reg[2] = X15
     * reg[3] = X16
     * reg[4] = X17
     * reg[5] = X18
     * reg[6] = X19
     * reg[7] = X20
     */
    ORO_KDBEVT_AA64_REG_DUMP2 = 0x2003,

    /* AArch64 Register dump 3: X21-X27
     * reg[1] = X21
     * reg[2] = X22
     * reg[3] = X23
     * reg[4] = X24
     * reg[5] = X25
     * reg[6] = X26
     * reg[7] = X27
     */
    ORO_KDBEVT_AA64_REG_DUMP3 = 0x2004,

    /* AArch64 Register dump 4: X28-X30, SP
     * reg[1] = X28
     * reg[2] = X29 (FP - Frame Pointer)
     * reg[3] = X30 (LR - Link Register)
     * reg[4] = unused
     * reg[5] = unused
     * reg[6] = unused
     * reg[7] = unused
     */
    ORO_KDBEVT_AA64_REG_DUMP4 = 0x2005,
};

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
