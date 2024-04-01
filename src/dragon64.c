/** \file
 *
 *  \brief Dragon 64 support.
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
 *  This file is included into dragon.c and provides the code specific to the
 *  Dragon 64.
 *
 *  This machine is basically the same as the Dragon 32, but includes 64K RAM
 *  by default, an extra BASIC ROM and an ACIA for serial comms.
 *
 *  The ACIA is not emulated beyond some status registers to fool the ROM code
 *  into thinking it is present.
 */

#include "mos6551.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct machine_dragon64 {
	struct machine_dragon machine_dragon;

	// Points to either md->rom0 (32K BASIC) or md->rom1 (64K BASIC)
	uint8_t *rom;

	struct MOS6551 *ACIA;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void dragon64_config_complete(struct machine_config *);

static void dragon64_reset(struct machine *, _Bool hard);

static _Bool dragon64_read_byte(struct machine_dragon *, unsigned A);
static _Bool dragon64_write_byte(struct machine_dragon *, unsigned A);

static void dragon64_pia1b_data_postwrite(void *);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct part *dragon64_allocate(void);
static void dragon64_initialise(struct part *, void *options);
static _Bool dragon64_finish(struct part *);
static void dragon64_free(struct part *);

static const struct partdb_entry_funcs dragon64_funcs = {
	.allocate = dragon64_allocate,
	.initialise = dragon64_initialise,
	.finish = dragon64_finish,
	.free = dragon64_free,

	// XXX will need to serialise more than stock dragon
	.ser_struct_data = &dragon_ser_struct_data,

	.is_a = machine_is_a,
};

const struct machine_partdb_extra dragon64_machine_extra = {
	.config_complete = dragon64_config_complete,
	.is_working_config = dragon_is_working_config,
	.cart_arch = "dragon-cart",
};

const struct partdb_entry dragon64_part = { .name = "dragon64", .funcs = &dragon64_funcs, .extra = { &dragon64_machine_extra } };

static struct part *dragon64_allocate(void) {
	struct machine_dragon64 *mdp = part_new(sizeof(*mdp));
	struct machine_dragon *md = &mdp->machine_dragon;
	struct machine *m = &md->public;
	struct part *p = &m->part;

	*mdp = (struct machine_dragon64){0};

	dragon_allocate_common(md);

	m->reset = dragon64_reset;

	md->read_byte = dragon64_read_byte;
	md->write_byte = dragon64_write_byte;

	md->is_dragon = 1;

	return p;
}

static void dragon64_initialise(struct part *p, void *options) {
	assert(p != NULL);
	assert(options != NULL);
	struct machine_dragon64 *mdp = (struct machine_dragon64 *)p;
	struct machine_dragon *md = &mdp->machine_dragon;
	struct machine_config *mc = options;

	dragon64_config_complete(mc);

	dragon_initialise_common(md, mc);

	// ACIA
	part_add_component(p, part_create("MOS6551", NULL), "ACIA");
}

static _Bool dragon64_finish(struct part *p) {
	assert(p != NULL);
	struct machine_dragon64 *mdp = (struct machine_dragon64 *)p;
	struct machine_dragon *md = &mdp->machine_dragon;
	struct machine *m = &md->public;
	struct machine_config *mc = m->config;
	assert(mc != NULL);

	if (!dragon_finish_common(md))
		return 0;

	if (md->has_combined) {
		_Bool forced = 0, valid_crc = 0;

		md->crc_combined = crc32_block(CRC32_RESET, md->rom0, 0x4000);
		valid_crc = crclist_match("@d64_1", md->crc_combined);
		if (xroar.cfg.force_crc_match) {
			md->crc_combined = 0x84f68bf9;  // Dragon 64 32K mode BASIC
			forced = 1;
		}

		(void)forced;  // avoid warning if no logging
		LOG_DEBUG(1, "\t32K mode BASIC CRC = 0x%08x%s\n", md->crc_combined, forced ? " (forced)" : "");
		if (!valid_crc) {
			LOG_WARN("Invalid CRC for combined BASIC ROM\n");
		}
	}

	if (md->has_altbas) {
		_Bool forced = 0, valid_crc = 0;

		md->crc_altbas = crc32_block(CRC32_RESET, md->rom1, 0x4000);
		valid_crc = crclist_match("@d64_2", md->crc_altbas);
		if (xroar.cfg.force_crc_match) {
			md->crc_altbas = 0x17893a42;  // Dragon 64 64K mode BASIC
			forced = 1;
		}
		(void)forced;  // avoid warning if no logging
		LOG_DEBUG(1, "\t64K mode BASIC CRC = 0x%08x%s\n", md->crc_altbas, forced ? " (forced)" : "");
		if (!valid_crc) {
			LOG_WARN("Invalid CRC for alternate BASIC ROM\n");
		}
	}

	// Find attached parts
	mdp->ACIA = (struct MOS6551 *)part_component_by_id_is_a(p, "ACIA", "MOS6551");

	// Check all required parts are attached
	if (!mdp->ACIA) {
		return 0;
	}

	// Default to 32K BASIC
	mdp->rom = md->rom0;

	// Override PIA1 PB2 as ROMSEL
	md->PIA1->b.in_source |= (1<<2);  // pull-up
	md->PIA1->b.data_postwrite = DELEGATE_AS0(void, dragon64_pia1b_data_postwrite, mdp);

	// VDG
	md->VDG->is_dragon64 = 1;
	md->VDG->is_dragon32 = 0;
	md->VDG->is_coco = 0;

	return 1;
}

static void dragon64_free(struct part *p) {
	dragon_free(p);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void dragon64_config_complete(struct machine_config *mc) {
	// Default ROMs
	set_default_rom(mc->extbas_dfn, &mc->extbas_rom, "@dragon64");
	set_default_rom(mc->altbas_dfn, &mc->altbas_rom, "@dragon64_alt");

	// Validate requested total RAM
	if (mc->ram < 16 || mc->ram > 64) {
		mc->ram = 64;
	} else if (mc->ram < 32) {
		mc->ram = 16;
	} else if (mc->ram < 64) {
		mc->ram = 32;
	} else {
		mc->ram = 64;
	}

	// Pick RAM org based on requested total RAM if not specified
	if (mc->ram_org == ANY_AUTO) {
		if (mc->ram < 32) {
			mc->ram_org = RAM_ORG_16Kx1;
		} else if (mc->ram < 64) {
			mc->ram_org = RAM_ORG_32Kx1;
		} else {
			mc->ram_org = RAM_ORG_64Kx1;
		}
	}

	dragon_config_complete_common(mc);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void dragon64_reset(struct machine *m, _Bool hard) {
	struct machine_dragon64 *mdp = (struct machine_dragon64 *)m;
	dragon_reset(m, hard);
	mos6551_reset(mdp->ACIA);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static _Bool dragon64_read_byte(struct machine_dragon *md, unsigned A) {
	struct machine_dragon64 *mdp = (struct machine_dragon64 *)md;

	switch (md->SAM->S) {
	case 1:
	case 2:
		md->CPU->D = mdp->rom[A & 0x3fff];
		return 1;

	case 4:
		if ((A & 4) != 0) {
			mos6551_access(mdp->ACIA, 1, A, &md->CPU->D);
			return 1;
		}
		break;

	default:
		break;
	}
	return 0;
}

static _Bool dragon64_write_byte(struct machine_dragon *md, unsigned A) {
	struct machine_dragon64 *mdp = (struct machine_dragon64 *)md;

	if (md->SAM->S & 4) switch (md->SAM->S) {
	case 1:
	case 2:
		md->CPU->D = mdp->rom[A & 0x3fff];
		return 1;

	case 4:
		if ((A & 4) != 0) {
			mos6551_access(mdp->ACIA, 0, A, &md->CPU->D);
			return 1;
		}
		break;

	default:
		break;
	}
	return 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void dragon64_pia1b_data_postwrite(void *sptr) {
	struct machine_dragon64 *mdp = sptr;
	struct machine_dragon *md = &mdp->machine_dragon;

	_Bool is_32k = PIA_VALUE_B(md->PIA1) & 0x04;
	if (is_32k) {
		mdp->rom = md->rom0;
		keyboard_set_chord_mode(md->keyboard.interface, keyboard_chord_mode_dragon_32k_basic);
	} else {
		mdp->rom = md->rom1;
		keyboard_set_chord_mode(md->keyboard.interface, keyboard_chord_mode_dragon_64k_basic);
	}
	pia1b_data_postwrite(sptr);
}
