/*
 *  x86 exception helpers
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "qemu/log.h"
#include "system/runstate.h"
#include "exec/helper-proto.h"
#include "helper-tcg.h"
#include "qemu/plugin.h"
#include "hw/char/oro_kdbg.h"

G_NORETURN void helper_raise_interrupt(CPUX86State *env, int intno,
                                          int next_eip_addend)
{
    raise_interrupt(env, intno, next_eip_addend);
}

G_NORETURN void helper_raise_exception(CPUX86State *env, int exception_index)
{
    raise_exception(env, exception_index);
}

/*
 * Check nested exceptions and change to double or triple fault if
 * needed. It should only be called, if this is not an interrupt.
 * Returns the new exception number.
 */
static int check_exception(CPUX86State *env, int intno, int *error_code,
                           uintptr_t retaddr)
{
    int first_contributory = env->old_exception == 0 ||
                              (env->old_exception >= 10 &&
                               env->old_exception <= 13);
    int second_contributory = intno == 0 ||
                               (intno >= 10 && intno <= 13);

    qemu_log_mask(CPU_LOG_INT, "check_exception old: 0x%x new 0x%x\n",
                env->old_exception, intno);

#if !defined(CONFIG_USER_ONLY)
    if (env->old_exception == EXCP08_DBLE) {
        if (env->hflags & HF_GUEST_MASK) {
            cpu_vmexit(env, SVM_EXIT_SHUTDOWN, 0, retaddr); /* does not return */
        }

        qemu_log_mask(CPU_LOG_RESET, "Triple fault\n");

        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        return EXCP_HLT;
    }
#endif

    if ((first_contributory && second_contributory)
        || (env->old_exception == EXCP0E_PAGE &&
            (second_contributory || (intno == EXCP0E_PAGE)))) {
        intno = EXCP08_DBLE;
        *error_code = 0;
    }

    if (second_contributory || (intno == EXCP0E_PAGE) ||
        (intno == EXCP08_DBLE)) {
        env->old_exception = intno;
    }

    return intno;
}

/*
 * Signal an interruption. It is executed in the main CPU loop.
 * is_int is TRUE if coming from the int instruction. next_eip is the
 * env->eip value AFTER the interrupt instruction. It is only relevant if
 * is_int is TRUE.
 */
static G_NORETURN
void raise_interrupt2(CPUX86State *env, int intno,
                      int is_int, int error_code,
                      int next_eip_addend,
                      uintptr_t retaddr)
{
    CPUState *cs = env_cpu(env);
    uint64_t last_pc = env->eip + env->segs[R_CS].base;

    if (!is_int) {
        cpu_svm_check_intercept_param(env, SVM_EXIT_EXCP_BASE + intno,
                                      error_code, retaddr);
        intno = check_exception(env, intno, &error_code, retaddr);
        
#if !defined(CONFIG_USER_ONLY)
        /* Emit oro_kdbg exception event and register dumps */
        {
            uint64_t regs[7];
            int eflags = cpu_compute_eflags(env);
            
            /* Exception event */
            regs[0] = intno;
            regs[1] = error_code;
            regs[2] = env->cr[2];  /* CR2 for page faults */
            regs[3] = env->eip;
            regs[4] = env->segs[R_CS].selector;
            regs[5] = eflags;
            regs[6] = env->hflags & HF_CPL_MASK;  /* CPL */
            oro_kdbg_emit_global(ORO_KDBEVT_X86_EXCEPTION, regs);
            
            /* REG_DUMP0: General purpose registers */
            regs[0] = env->regs[R_EAX];
            regs[1] = env->regs[R_EBX];
            regs[2] = env->regs[R_ECX];
            regs[3] = env->regs[R_EDX];
            regs[4] = env->regs[R_ESI];
            regs[5] = env->regs[R_EDI];
            regs[6] = env->regs[R_EBP];
            oro_kdbg_emit_global(ORO_KDBEVT_X86_REG_DUMP0, regs);
            
            /* REG_DUMP1: RSP and R8-R13 */
            regs[0] = env->regs[R_ESP];
#ifdef TARGET_X86_64
            regs[1] = env->regs[8];
            regs[2] = env->regs[9];
            regs[3] = env->regs[10];
            regs[4] = env->regs[11];
            regs[5] = env->regs[12];
            regs[6] = env->regs[13];
#else
            regs[1] = 0;
            regs[2] = 0;
            regs[3] = 0;
            regs[4] = 0;
            regs[5] = 0;
            regs[6] = 0;
#endif
            oro_kdbg_emit_global(ORO_KDBEVT_X86_REG_DUMP1, regs);
            
            /* REG_DUMP2: R14-R15 and segment selectors */
#ifdef TARGET_X86_64
            regs[0] = env->regs[14];
            regs[1] = env->regs[15];
#else
            regs[0] = 0;
            regs[1] = 0;
#endif
            regs[2] = env->segs[R_ES].selector;
            regs[3] = env->segs[R_DS].selector;
            regs[4] = env->segs[R_FS].selector;
            regs[5] = env->segs[R_GS].selector;
            regs[6] = env->segs[R_SS].selector;
            oro_kdbg_emit_global(ORO_KDBEVT_X86_REG_DUMP2, regs);
            
            /* REG_DUMP3: Control registers */
            regs[0] = env->cr[0];
            regs[1] = env->cr[3];
            regs[2] = env->cr[4];
            /* CR8 is mapped to APIC TPR, skip for now */
            regs[3] = 0;
            regs[4] = env->efer;
            regs[5] = 0;
            regs[6] = 0;
            oro_kdbg_emit_global(ORO_KDBEVT_X86_REG_DUMP3, regs);
            
            /* REG_DUMP4: Debug registers */
            regs[0] = env->dr[0];
            regs[1] = env->dr[1];
            regs[2] = env->dr[2];
            regs[3] = env->dr[3];
            regs[4] = env->dr[6];
            regs[5] = env->dr[7];
            regs[6] = 0;
            oro_kdbg_emit_global(ORO_KDBEVT_X86_REG_DUMP4, regs);
        }
#endif
    } else {
        cpu_svm_check_intercept_param(env, SVM_EXIT_SWINT, 0, retaddr);
    }

    cs->exception_index = intno;
    env->error_code = error_code;
    env->exception_is_int = is_int;
    env->exception_next_eip = env->eip + next_eip_addend;
    qemu_plugin_vcpu_exception_cb(cs, last_pc);
    cpu_loop_exit_restore(cs, retaddr);
}

/* shortcuts to generate exceptions */

G_NORETURN void raise_interrupt(CPUX86State *env, int intno, int next_eip_addend)
{
    raise_interrupt2(env, intno, 1, 0, next_eip_addend, 0);
}

G_NORETURN void raise_exception_err(CPUX86State *env, int exception_index,
                                    int error_code)
{
    raise_interrupt2(env, exception_index, 0, error_code, 0, 0);
}

G_NORETURN void raise_exception_err_ra(CPUX86State *env, int exception_index,
                                       int error_code, uintptr_t retaddr)
{
    raise_interrupt2(env, exception_index, 0, error_code, 0, retaddr);
}

G_NORETURN void raise_exception(CPUX86State *env, int exception_index)
{
    raise_interrupt2(env, exception_index, 0, 0, 0, 0);
}

G_NORETURN void raise_exception_ra(CPUX86State *env, int exception_index,
                                   uintptr_t retaddr)
{
    raise_interrupt2(env, exception_index, 0, 0, 0, retaddr);
}

G_NORETURN void helper_icebp(CPUX86State *env)
{
    CPUState *cs = env_cpu(env);

    do_end_instruction(env);

    /*
     * INT1 aka ICEBP generates a trap-like #DB, but it is pretty special.
     *
     * "Although the ICEBP instruction dispatches through IDT vector 1,
     * that event is not interceptable by means of the #DB exception
     * intercept".  Instead there is a separate fault-like ICEBP intercept.
     */
    cs->exception_index = EXCP01_DB;
    env->error_code = 0;
    env->exception_is_int = 0;
    env->exception_next_eip = env->eip;
    cpu_loop_exit(cs);
}

G_NORETURN void handle_unaligned_access(CPUX86State *env, vaddr vaddr,
                                        MMUAccessType access_type,
                                        uintptr_t retaddr)
{
    /*
     * Unaligned accesses are currently only triggered by SSE/AVX
     * instructions that impose alignment requirements on memory
     * operands. These instructions raise #GP(0) upon accessing an
     * unaligned address.
     */
    raise_exception_ra(env, EXCP0D_GPF, retaddr);
}
