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

#endif

