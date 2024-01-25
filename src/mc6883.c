/** \file
 *
 *  \brief Motorola SN74LS783/MC6883 Synchronous Address Multiplexer.
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
 *
 *  Research into how SAM VDG mode transitions affect addressing and the
 *  various associated "glitches" by Stewart Orchard.
 *
 *  As the code currently stands, implementation of this undocumented behaviour
 *  is partial and you shouldn't rely on it to accurately represent real
 *  hardware.  However, if you're testing on the real thing too, this could
 *  still allow you to achieve some nice effects.
 *
 *  Currently unoptimised as whole behaviour not implemented.  In normal
 *  operation, this adds <1% to execution time.  Pathological case of
 *  constantly varying SAM VDG mode adds a little over 5%.
 */

#include "top-config.h"

#include <stdint.h>
#include <stdlib.h>

#include "array.h"
#include "delegate.h"

#include "events.h"
#include "mc6883.h"
#include "part.h"
#include "serialise.h"

// Constants for address multiplexer
// SAM Data Sheet,
//   Figure 6 - Signal routing for address multiplexer

static uint16_t const ram_row_masks[4] = { 0x007f, 0x007f, 0x00ff, 0x00ff };
static int const ram_col_shifts[4] = { 2, 1, 0, 0 };
static uint16_t const ram_col_masks[4] = { 0x3f00, 0x7f00, 0xff00, 0xff00 };
static uint16_t const ram_ras1_bits[4] = { 0x1000, 0x4000, 0, 0 };

// VDG X & Y divider configurations and HSync clear mode.

enum { DIV1 = 0, DIV2, DIV3, DIV12 };
enum { CLRN = 0, CLR3, CLR4 };

static const int vdg_ydivs[8] = { DIV12, DIV1, DIV3, DIV1, DIV2, DIV1, DIV1, DIV1 };
static const int vdg_xdivs[8] = {  DIV1, DIV3, DIV1, DIV2, DIV1, DIV1, DIV1, DIV1 };
static const int vdg_hclrs[8] = {  CLR4, CLR3, CLR4, CLR3, CLR4, CLR3, CLR4, CLRN };

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

#define VC_B15_5  (0)
#define VC_YDIV4  (1)
#define VC_YDIV3  (2)
#define VC_YDIV2  (3)
#define VC_B4     (4)
#define VC_XDIV3  (5)
#define VC_XDIV2  (6)
#define VC_B3_0   (7)
#define VC_GROUND (8)
#define NUM_VCOUNTERS (9)

struct vcounter {
	uint16_t value;
	_Bool input;
	_Bool output;
	uint16_t val_mod;
	uint16_t out_mask;
	int input_from;
};

static struct ser_struct ser_struct_vcounter[] = {
	SER_ID_STRUCT_ELEM(1, ser_type_bool, struct vcounter, input),
	SER_ID_STRUCT_ELEM(2, ser_type_uint16, struct vcounter, value),
	SER_ID_STRUCT_ELEM(3, ser_type_bool, struct vcounter, output),
};

static const struct ser_struct_data vcounter_ser_struct_data = {
	.elems = ser_struct_vcounter,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_vcounter),
};

static const struct {
	int input_from;
	uint16_t val_mod;
	uint16_t out_mask;
} vcounter_init[NUM_VCOUNTERS] = {
	{ VC_B4,    2048, 0 },
	{ VC_YDIV3,    4, 2 },
	{ VC_B4,       3, 2 },
	{ VC_B4,       2, 1 },
	{ VC_B3_0,     2, 1 },
	{ VC_B3_0,     3, 2 },
	{ VC_B3_0,     2, 1 },
	{ -1,         16, 8 },
	{ -1,          0, 0 }
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct MC6883_private {
	struct MC6883 public;

	// SAM control register
	unsigned reg;

	// Address decode
	_Bool map_type_1;

	// Address multiplexer
	uint16_t ram_row_mask;
	int ram_col_shift;
	uint16_t ram_col_mask;
	uint16_t ram_ras1_bit;
	uint16_t ram_ras1;
	uint16_t ram_page_bit;

	// MPU rate
	_Bool mpu_rate_fast;
	_Bool mpu_rate_ad;
	_Bool running_fast;
	_Bool extend_slow_cycle;

	struct {
		unsigned v;  // video mode
		uint16_t f;  // VDG address bits 15..9 latched on FSync

		// end of line clear mode
		int clr_mode;  // CLR4, CLR3 or CLRN

		struct vcounter vcounter[NUM_VCOUNTERS];
	} vdg;

};

static struct ser_struct ser_struct_mc6883[] = {
	SER_ID_STRUCT_ELEM(1, ser_type_unsigned, struct MC6883, S),
	SER_ID_STRUCT_ELEM(2, ser_type_unsigned, struct MC6883, Z),
	SER_ID_STRUCT_ELEM(3, ser_type_unsigned, struct MC6883, V),
	SER_ID_STRUCT_ELEM(4, ser_type_bool, struct MC6883, RAS),

	SER_ID_STRUCT_ELEM(5, ser_type_unsigned, struct MC6883_private, reg),

	SER_ID_STRUCT_ELEM(6, ser_type_bool, struct MC6883_private, map_type_1),

	SER_ID_STRUCT_ELEM(7, ser_type_uint16, struct MC6883_private, ram_row_mask),
	SER_ID_STRUCT_ELEM(8, ser_type_int, struct MC6883_private, ram_col_shift),
	SER_ID_STRUCT_ELEM(9, ser_type_uint16, struct MC6883_private, ram_col_mask),
	SER_ID_STRUCT_ELEM(10, ser_type_uint16, struct MC6883_private, ram_ras1_bit),
	SER_ID_STRUCT_ELEM(11, ser_type_uint16, struct MC6883_private, ram_ras1),
	SER_ID_STRUCT_ELEM(12, ser_type_uint16, struct MC6883_private, ram_page_bit),

	SER_ID_STRUCT_ELEM(13, ser_type_bool, struct MC6883_private, mpu_rate_fast),
	SER_ID_STRUCT_ELEM(14, ser_type_bool, struct MC6883_private, mpu_rate_ad),
	SER_ID_STRUCT_ELEM(15, ser_type_bool, struct MC6883_private, running_fast),
	SER_ID_STRUCT_ELEM(16, ser_type_bool, struct MC6883_private, extend_slow_cycle),

	SER_ID_STRUCT_ELEM(17, ser_type_unsigned, struct MC6883_private, vdg.v),
	SER_ID_STRUCT_ELEM(18, ser_type_uint16, struct MC6883_private, vdg.f),
	SER_ID_STRUCT_ELEM(19, ser_type_int, struct MC6883_private, vdg.clr_mode),

	SER_ID_STRUCT_SUBSTRUCT(20, struct MC6883_private, vdg.vcounter[VC_B15_5], &vcounter_ser_struct_data),
	SER_ID_STRUCT_SUBSTRUCT(21, struct MC6883_private, vdg.vcounter[VC_B4], &vcounter_ser_struct_data),
	SER_ID_STRUCT_SUBSTRUCT(22, struct MC6883_private, vdg.vcounter[VC_B3_0], &vcounter_ser_struct_data),
	SER_ID_STRUCT_SUBSTRUCT(23, struct MC6883_private, vdg.vcounter[VC_YDIV4], &vcounter_ser_struct_data),
	SER_ID_STRUCT_SUBSTRUCT(24, struct MC6883_private, vdg.vcounter[VC_YDIV3], &vcounter_ser_struct_data),
	SER_ID_STRUCT_SUBSTRUCT(25, struct MC6883_private, vdg.vcounter[VC_YDIV2], &vcounter_ser_struct_data),
	SER_ID_STRUCT_SUBSTRUCT(26, struct MC6883_private, vdg.vcounter[VC_XDIV3], &vcounter_ser_struct_data),
	SER_ID_STRUCT_SUBSTRUCT(27, struct MC6883_private, vdg.vcounter[VC_XDIV2], &vcounter_ser_struct_data),
};

static _Bool mc6883_read_elem(void *sptr, struct ser_handle *sh, int tag);
static _Bool mc6883_write_elem(void *sptr, struct ser_handle *sh, int tag);

static const struct ser_struct_data mc6883_ser_struct_data = {
	.elems = ser_struct_mc6883,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_mc6883),
	.read_elem = mc6883_read_elem,
	.write_elem = mc6883_write_elem,
};

static void update_vcounter_inputs(struct MC6883_private *sam);
static void update_from_register(struct MC6883_private *);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// SAM part creation

static struct part *mc6883_allocate(void);
static _Bool mc6883_finish(struct part *p);

static const struct partdb_entry_funcs mc6883_funcs = {
	.allocate = mc6883_allocate,
	.finish = mc6883_finish,

	.ser_struct_data = &mc6883_ser_struct_data,
};

const struct partdb_entry mc6883_part = { .name = "SN74LS783", .funcs = &mc6883_funcs };

static struct part *mc6883_allocate(void) {
	struct MC6883_private *sam = part_new(sizeof(*sam));
	struct MC6883 *samp = &sam->public;
	struct part *p = &samp->part;

	*sam = (struct MC6883_private){0};

	sam->public.cpu_cycle = DELEGATE_DEFAULT3(void, int, bool, uint16);
	sam->public.vdg_update = DELEGATE_DEFAULT0(void);

	// Set up VDG address divider sources.  Set initial V=7 so that first
	// call to reset() changes them.
	sam->vdg.v = 7;

	for (int i = 0; i < NUM_VCOUNTERS; i++) {
		sam->vdg.vcounter[i].input_from = vcounter_init[i].input_from;
		sam->vdg.vcounter[i].val_mod = vcounter_init[i].val_mod;
		sam->vdg.vcounter[i].out_mask = vcounter_init[i].out_mask;
	}

	return p;
}

static _Bool mc6883_finish(struct part *p) {
	struct MC6883_private *sam = (struct MC6883_private *)p;
	update_vcounter_inputs(sam);
	return 1;
}

// XXX There are currently no unhandled elements, so these do nothing useful.
// Not deleting, as some backwards compatibility will probably be needed soon.

static _Bool mc6883_read_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct MC6883_private *sam = sptr;
	(void)sam;
	(void)sh;
	switch (tag) {
	default:
		return 0;
	}
	return 1;
}

static _Bool mc6883_write_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct MC6883_private *sam = sptr;
	(void)sam;
	(void)sh;
	switch (tag) {
	default:
		return 0;
	}
	return 1;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void mc6883_reset(struct MC6883 *samp) {
	struct MC6883_private *sam = (struct MC6883_private *)samp;

	mc6883_set_register(samp, 0);
	mc6883_vdg_fsync(samp, 1);
	sam->running_fast = 0;
	sam->extend_slow_cycle = 0;
}

#define VRAM_TRANSLATE(a) ( \
		((a << sam->ram_col_shift) & sam->ram_col_mask) \
		| (a & sam->ram_row_mask) \
		| (!(a & sam->ram_ras1_bit) ? sam->ram_ras1 : 0) \
	)

#define RAM_TRANSLATE(a) (VRAM_TRANSLATE(a) | sam->ram_page_bit)

// The primary function of the SAM: translates an address (A) plus Read/!Write
// flag (RnW) into an S value and RAM address (Z).  Writes to the SAM control
// register will update the internal configuration.  The CPU delegate is called
// with the number of (SAM) cycles elapsed, RnW flag and translated address.

static unsigned const io_S[8] = { 4, 5, 6, 7, 7, 7, 7, 2 };
static unsigned const data_S[8] = { 7, 7, 7, 7, 1, 2, 3, 3 };

void mc6883_mem_cycle(void *sptr, _Bool RnW, uint16_t A) {
	struct MC6883 *samp = sptr;
	struct MC6883_private *sam = (struct MC6883_private *)samp;
	int ncycles;
	_Bool fast_cycle;
	_Bool want_register_update = 0;

	if ((A >> 8) == 0xff) {
		// I/O area
		samp->S = io_S[(A >> 5) & 7];
		samp->RAS = 0;
		fast_cycle = sam->mpu_rate_fast || (samp->S != 4 && sam->mpu_rate_ad);
		if (samp->S == 7 && !RnW && A >= 0xffc0) {
			if (A < 0xffc6) {
				// this is a change of video mode, so update VDG
				DELEGATE_CALL(samp->vdg_update);
			}
			unsigned b = 1 << ((A >> 1) & 0x0f);
			if (A & 1) {
				sam->reg |= b;
			} else {
				sam->reg &= ~b;
			}
			want_register_update = 1;
		}
	} else if ((A & 0x8000) && !sam->map_type_1) {
		samp->S = data_S[A >> 13];
		samp->RAS = 0;
		fast_cycle = sam->mpu_rate_fast || sam->mpu_rate_ad;
	} else {
		samp->S = RnW ? 0 : data_S[A >> 13];
		samp->RAS = 1;
		samp->Z = RAM_TRANSLATE(A);
		fast_cycle = sam->mpu_rate_fast;
	}

	if (!sam->running_fast) {
		// Last cycle was slow
		if (!fast_cycle) {
			// Slow cycle
			ncycles = EVENT_TICKS_14M31818(16);
		} else {
			// Transition slow to fast
			ncycles = EVENT_TICKS_14M31818(15);
			sam->running_fast = 1;
		}
	} else {
		// Last cycle was fast
		if (!fast_cycle) {
			// Transition fast to slow
			if (!sam->extend_slow_cycle) {
				// Still interleaved
				ncycles = EVENT_TICKS_14M31818(17);
			} else {
				// Re-interleave
				ncycles = EVENT_TICKS_14M31818(25);
				sam->extend_slow_cycle = 0;
			}
			sam->running_fast = 0;
		} else {
			// Fast cycle, may become un-interleaved
			ncycles = EVENT_TICKS_14M31818(8);
			sam->extend_slow_cycle = !sam->extend_slow_cycle;
		}
	}

	DELEGATE_CALL(samp->cpu_cycle, ncycles, RnW, A);

	if (want_register_update) {
		update_from_register(sam);
	}

}

static void vcounter_set(struct MC6883_private *sam, int i, int val);

static void vcounter_update(struct MC6883_private *sam, int i) {
	_Bool old_input = sam->vdg.vcounter[i].input;
	_Bool new_input = sam->vdg.vcounter[sam->vdg.vcounter[i].input_from].output;
	if (new_input != old_input) {
		sam->vdg.vcounter[i].input = new_input;
		if (!new_input) {
			vcounter_set(sam, i, (sam->vdg.vcounter[i].value + 1) % sam->vdg.vcounter[i].val_mod);
		}
	}
}

static void vcounter_set(struct MC6883_private *sam, int i, int val) {
	sam->vdg.vcounter[i].value = val;
	sam->vdg.vcounter[i].output = val & sam->vdg.vcounter[i].out_mask;
	// Never need to check VC_GROUND or VC_B3_0
	for (int j = 0; j < NUM_VCOUNTERS - 2; j++) {
		if (sam->vdg.vcounter[j].input_from == i)
			vcounter_update(sam, j);
	}
}

void mc6883_vdg_hsync(struct MC6883 *samp, _Bool level) {
	struct MC6883_private *sam = (struct MC6883_private *)samp;
	if (level)
		return;

	switch (sam->vdg.clr_mode) {

	case CLR4:
		// clear bits 4..1
		sam->vdg.vcounter[VC_B3_0].value = 0;
		sam->vdg.vcounter[VC_B3_0].output = 0;
		sam->vdg.vcounter[VC_XDIV3].input = 0;
		sam->vdg.vcounter[VC_XDIV2].input = 0;
		sam->vdg.vcounter[VC_B4].input = 0;
		sam->vdg.vcounter[VC_B4].value = 0;
		sam->vdg.vcounter[VC_B4].output = 0;
		vcounter_update(sam, VC_YDIV2);
		vcounter_update(sam, VC_YDIV3);
		vcounter_update(sam, VC_YDIV4);
		vcounter_update(sam, VC_B15_5);
		break;

	case CLR3:
		// clear bits 3..1
		sam->vdg.vcounter[VC_B3_0].value = 0;
		sam->vdg.vcounter[VC_B3_0].output = 0;
		vcounter_update(sam, VC_XDIV2);
		vcounter_update(sam, VC_XDIV3);
		vcounter_update(sam, VC_B4);
		break;

	default:
		break;
	}

}

static inline void vcounter_reset(struct MC6883_private *sam, int i) {
	sam->vdg.vcounter[i].input = 0;
	sam->vdg.vcounter[i].value = 0;
	sam->vdg.vcounter[i].output = 0;
}

void mc6883_vdg_fsync(struct MC6883 *samp, _Bool level) {
	struct MC6883_private *sam = (struct MC6883_private *)samp;
	if (!level) {
		return;
	}
	vcounter_reset(sam, VC_B3_0);
	vcounter_reset(sam, VC_XDIV2);
	vcounter_reset(sam, VC_XDIV3);
	vcounter_reset(sam, VC_B4);
	vcounter_reset(sam, VC_YDIV2);
	vcounter_reset(sam, VC_YDIV3);
	vcounter_reset(sam, VC_YDIV4);
	vcounter_reset(sam, VC_B15_5);
	sam->vdg.vcounter[VC_B15_5].value = sam->vdg.f >> 5;
}

// Called with the number of bytes of video data required.  Any one call will
// provide data up to a limit of the next 16-byte boundary, meaning multiple
// calls may be required.  Updates V to the translated base address of the
// available data, and returns the number of bytes available there.
//
// When the 16-byte boundary is reached, there is a falling edge on the input
// to the X divider (bit 3 transitions from 1 to 0), which may affect its
// output, thus advancing bit 4.  This in turn alters the input to the Y
// divider.

int mc6883_vdg_bytes(struct MC6883 *samp, int nbytes) {
	struct MC6883_private *sam = (struct MC6883_private *)samp;

	// In fast mode, there's no time to latch video RAM, so just point at
	// whatever was being access by the CPU.  This won't be terribly
	// accurate, as this function is called a lot less frequently than the
	// CPU address changes.
	uint16_t b3_0 = sam->vdg.vcounter[VC_B3_0].value;
	uint16_t V = (sam->vdg.vcounter[VC_B15_5].value << 5) | (sam->vdg.vcounter[VC_B4].value << 4) | b3_0;
	samp->V = sam->mpu_rate_fast ? samp->Z : VRAM_TRANSLATE(V);

	// Either way, need to advance the VDG address pointer.

	// Simple case is where nbytes takes us to below the next 16-byte
	// boundary.  Need to record any rising edge of bit 3 (as input to X
	// divisor), but it will never fall here, so don't need to check for
	// that.
	if ((b3_0 + nbytes) < 16) {
		vcounter_set(sam, VC_B3_0, b3_0 + nbytes);
		return nbytes;
	}

	// Otherwise we have reached the boundary.  Bit 3 will always provide a
	// falling edge to the X divider, so work through how that affects
	// subsequent address bits.
	nbytes = 16 - b3_0;
	vcounter_set(sam, VC_B3_0, 15);  // in case rising edge of b3 was skipped
	vcounter_set(sam, VC_B3_0, 0);  // falling edge of b3
	return nbytes;
}

void mc6883_set_register(struct MC6883 *samp, unsigned int value) {
	struct MC6883_private *sam = (struct MC6883_private *)samp;
	sam->reg = value;
	update_from_register(sam);
}

unsigned int mc6883_get_register(struct MC6883 *samp) {
	struct MC6883_private *sam = (struct MC6883_private *)samp;
	return sam->reg;
}

static void update_vcounter_inputs(struct MC6883_private *sam) {
	int v = sam->reg & 7;
	switch (vdg_ydivs[v]) {
	case DIV12:
		sam->vdg.vcounter[VC_B15_5].input_from = VC_YDIV4;
		break;
	case DIV3:
		sam->vdg.vcounter[VC_B15_5].input_from = VC_YDIV3;
		break;
	case DIV2:
		sam->vdg.vcounter[VC_B15_5].input_from = VC_YDIV2;
		break;
	case DIV1: default:
		sam->vdg.vcounter[VC_B15_5].input_from = VC_B4;
		break;
	}
	switch (vdg_xdivs[v]) {
	case DIV3:
		sam->vdg.vcounter[VC_B4].input_from = VC_XDIV3;
		break;
	case DIV2:
		sam->vdg.vcounter[VC_B4].input_from = VC_XDIV2;
		break;
	case DIV1: default:
		sam->vdg.vcounter[VC_B4].input_from = VC_B3_0;
		break;
	}
}

static void update_from_register(struct MC6883_private *sam) {
	int old_v = sam->vdg.v;

	int old_ydiv = vdg_ydivs[old_v];
	int old_xdiv = vdg_xdivs[old_v];

	int new_v = sam->reg & 7;

	int new_ydiv = vdg_ydivs[new_v];
	int new_xdiv = vdg_xdivs[new_v];
	int new_hclr = vdg_hclrs[new_v];

	sam->vdg.v = sam->reg & 7;
	sam->vdg.f = (sam->reg << 6) & 0xfe00;
	sam->vdg.clr_mode = new_hclr;

	if (new_ydiv != old_ydiv) {
		switch (new_ydiv) {
		case DIV12:
			if (old_ydiv == DIV3) {
				// 'glitch'
				sam->vdg.vcounter[VC_B15_5].input_from = VC_GROUND;
				vcounter_update(sam, VC_B15_5);
			} else if (old_ydiv == DIV2) {
				// 'glitch'
				sam->vdg.vcounter[VC_B15_5].input_from = VC_B4;
				vcounter_update(sam, VC_B15_5);
			}
			sam->vdg.vcounter[VC_B15_5].input_from = VC_YDIV4;
			break;
		case DIV3:
			if (old_ydiv == DIV12) {
				// 'glitch'
				sam->vdg.vcounter[VC_B15_5].input_from = VC_GROUND;
				vcounter_update(sam, VC_B15_5);
			}
			sam->vdg.vcounter[VC_B15_5].input_from = VC_YDIV3;
			break;
		case DIV2:
			if (old_ydiv == DIV12) {
				// 'glitch'
				sam->vdg.vcounter[VC_B15_5].input_from = VC_B4;
				vcounter_update(sam, VC_B15_5);
			}
			sam->vdg.vcounter[VC_B15_5].input_from = VC_YDIV2;
			break;
		case DIV1: default:
			sam->vdg.vcounter[VC_B15_5].input_from = VC_B4;
			break;
		}
		vcounter_update(sam, VC_YDIV2);
		vcounter_update(sam, VC_YDIV3);
		vcounter_update(sam, VC_YDIV4);
		vcounter_update(sam, VC_B15_5);
	}

	if (new_xdiv != old_xdiv) {
		switch (new_xdiv) {
		case DIV3:
			if (old_xdiv == DIV2) {
				// 'glitch'
				sam->vdg.vcounter[VC_B4].input_from = VC_GROUND;
				vcounter_update(sam, VC_B4);
			}
			sam->vdg.vcounter[VC_B4].input_from = VC_XDIV3;
			break;
		case DIV2:
			if (old_xdiv == DIV3) {
				// 'glitch'
				sam->vdg.vcounter[VC_B4].input_from = VC_GROUND;
				vcounter_update(sam, VC_B4);
			}
			sam->vdg.vcounter[VC_B4].input_from = VC_XDIV2;
			break;
		case DIV1: default:
			sam->vdg.vcounter[VC_B4].input_from = VC_B3_0;
			break;
		}
		vcounter_update(sam, VC_XDIV2);
		vcounter_update(sam, VC_XDIV3);
		vcounter_update(sam, VC_B4);
	}

	int memory_size = (sam->reg >> 13) & 3;
	sam->ram_row_mask = ram_row_masks[memory_size];
	sam->ram_col_shift = ram_col_shifts[memory_size];
	sam->ram_col_mask = ram_col_masks[memory_size];
	sam->ram_ras1_bit = ram_ras1_bits[memory_size];
	switch (memory_size) {
	case 0: // 4K
	case 1: // 16K
		sam->ram_page_bit = 0;
		sam->ram_ras1 = 0x8080;
		break;
	default:
	case 2:
	case 3: // 64K
		sam->ram_page_bit = (sam->reg & 0x0400) << 5;
		sam->ram_ras1 = 0;
		break;
	}

	sam->map_type_1 = ((sam->reg & 0x8000) != 0);
	sam->mpu_rate_fast = sam->reg & 0x1000;
	sam->mpu_rate_ad = !sam->map_type_1 && (sam->reg & 0x800);
}
