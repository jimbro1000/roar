/** \file
 *
 *  \brief Motorola MC6809 CPU.
 *
 *  \copyright Copyright 2003-2024 Ciaran Anscomb
 *
 *  \licenseblock This file is part of XRoar, a Dragon/Tandy CoCo emulator.
 *
 *  XRoar is free software; you can redistribute it and/or modify it under the
 *  terms of the GNU General Public License as published by the Free Software
 *  Foundation, either version 3 of the License, or (at your option) any later
 *  version.
 *
 *  See COPYING.GPL for redistribution conditions.
 *
 *  \endlicenseblock
 */

#ifndef XROAR_MC6809_MC6809_H_
#define XROAR_MC6809_MC6809_H_

#include <stdint.h>

#include "delegate.h"
#include "pl-endian.h"

#include "debug_cpu.h"
#include "part.h"

struct ser_struct_data;

#ifdef TRACE
struct mc6809_trace;
#endif

#define MC6809_INT_VEC_RESET (0xfffe)
#define MC6809_INT_VEC_NMI (0xfffc)
#define MC6809_INT_VEC_SWI (0xfffa)
#define MC6809_INT_VEC_IRQ (0xfff8)
#define MC6809_INT_VEC_FIRQ (0xfff6)
#define MC6809_INT_VEC_SWI2 (0xfff4)
#define MC6809_INT_VEC_SWI3 (0xfff2)

#define MC6809_COMPAT_STATE_NORMAL (0)
#define MC6809_COMPAT_STATE_SYNC (1)
#define MC6809_COMPAT_STATE_CWAI (2)
#define MC6809_COMPAT_STATE_DONE_INSTRUCTION (11)
#define MC6809_COMPAT_STATE_HCF (12)

/* MPU state.  Represents current position in the high-level flow chart from
 * the data sheet (figure 14). */
enum mc6809_state {
	mc6809_state_label_a      = MC6809_COMPAT_STATE_NORMAL,
	mc6809_state_sync         = MC6809_COMPAT_STATE_SYNC,
	mc6809_state_dispatch_irq = MC6809_COMPAT_STATE_CWAI,
	mc6809_state_label_b,
	mc6809_state_reset,
	mc6809_state_reset_check_halt,
	mc6809_state_next_instruction,
	// page states not used in emulation, but kept for use in snapshots:
	mc6809_state_instruction_page_2,
	mc6809_state_instruction_page_3,
	mc6809_state_cwai_check_halt,
	mc6809_state_sync_check_halt,
	mc6809_state_done_instruction = MC6809_COMPAT_STATE_DONE_INSTRUCTION,
	mc6809_state_hcf          = MC6809_COMPAT_STATE_HCF,
	mc6809_state_irq_reset_vector,  // BA=0, BS=1
};

/* Interface shared with all 6809-compatible CPUs */
struct MC6809 {
	// Is a debuggable CPU, which is a part
	struct debug_cpu debug_cpu;

	/* Interrupt lines */
	_Bool halt, nmi, firq, irq;
	uint8_t D;

	/* Methods */

	void (*reset)(struct MC6809 *cpu);
	void (*run)(struct MC6809 *cpu);

	/* External handlers */

	/* Memory access cycle */
	DELEGATE_T2(void, bool, uint16) mem_cycle;

	/* Internal state */

	unsigned state;
	_Bool running;
	uint16_t page;  // 0, 0x200, or 0x300
#ifdef TRACE
	struct mc6809_trace *tracer;
#endif

	/* Registers */
	uint8_t reg_cc, reg_dp;
	uint16_t reg_d;
	uint16_t reg_x, reg_y, reg_u, reg_s, reg_pc;
	/* Interrupts */
	_Bool nmi_armed;
	_Bool nmi_latch, firq_latch, irq_latch;
	_Bool nmi_active, firq_active, irq_active;
};

#if __BYTE_ORDER == __BIG_ENDIAN
# define MC6809_REG_HI (0)
# define MC6809_REG_LO (1)
#else
# define MC6809_REG_HI (1)
# define MC6809_REG_LO (0)
#endif

#define MC6809_REG_A(cpu) (*((uint8_t *)&cpu->reg_d + MC6809_REG_HI))
#define MC6809_REG_B(cpu) (*((uint8_t *)&cpu->reg_d + MC6809_REG_LO))

extern const struct ser_struct_data mc6809_ser_struct_data;

inline void MC6809_HALT_SET(struct MC6809 *cpu, _Bool val) {
	cpu->halt = val;
}

inline void MC6809_NMI_SET(struct MC6809 *cpu, _Bool val) {
	cpu->nmi = val;
}

inline void MC6809_FIRQ_SET(struct MC6809 *cpu, _Bool val) {
	cpu->firq = val;
}

inline void MC6809_IRQ_SET(struct MC6809 *cpu, _Bool val) {
	cpu->irq = val;
}

// Used by MC6809-compatibles:
_Bool mc6809_is_a(struct part *p, const char *name);
unsigned mc6809_get_pc(void *sptr);

#endif
