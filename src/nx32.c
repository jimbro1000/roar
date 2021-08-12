/** \file
 *
 *  \brief NX32 RAM expansion cartridge.
 *
 *  \copyright Copyright 2016-2018 Tormod Volden
 *
 *  \copyright Copyright 2016-2021 Ciaran Anscomb
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "becker.h"
#include "cart.h"
#include "part.h"
#include "spi65.h"
#include "spi_sdcard.h"

/* number of 32KB banks in memory cartridge: 1, 4 or 16 */
#define EXTBANKS 16

static struct cart *nx32_new(struct cart_config *);

struct cart_module cart_nx32_module = {
	.name = "nx32",
	.description = "NX32 memory cartridge",
	.new = nx32_new,
};

struct nx32 {
	struct cart cart;
	struct spi65 *spi65;
	uint8_t extmem[0x8000 * EXTBANKS];
	_Bool extmem_map;
	_Bool extmem_ty;
	uint8_t extmem_bank;
	struct becker *becker;
};

static void nx32_reset(struct cart *c);
static uint8_t nx32_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static uint8_t nx32_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static void nx32_detach(struct cart *c);
static void nx32_free(struct part *p);

struct cart *nx32_new(struct cart_config *cc) {
	struct nx32 *n = part_new(sizeof(*n));
	*n = (struct nx32){0};
	struct cart *c = &n->cart;
	part_init(&c->part, "nx32");
	c->part.free = nx32_free;

	c->config = cc;
	cart_rom_init(c);
	c->read = nx32_read;
	c->write = nx32_write;
	c->reset = nx32_reset;
	c->detach = nx32_detach;

	if (cc->becker_port) {
		n->becker = becker_new();
		part_add_component(&c->part, (struct part *)n->becker, "becker");
	}

	// 65SPI/B for interfacing to SD card
	n->spi65 = spi65_new();
	part_add_component(&c->part, (struct part *)n->spi65, "SPI65");

	// Attach an SD card (SPI mode) to 65SPI/B
	struct spi65_device *sdcard = spi_sdcard_new("sdcard.img");
	spi65_add_device(n->spi65, sdcard, 0);

	return c;
}

static void nx32_reset(struct cart *c) {
	struct nx32 *n = (struct nx32 *)c;
	cart_rom_reset(c);
	n->extmem_map = 0;
	n->extmem_ty = 0;
	n->extmem_bank = 0;
	if (n->becker)
		becker_reset(n->becker);
	spi65_reset(n->spi65);
}

static void nx32_detach(struct cart *c) {
	struct nx32 *n = (struct nx32 *)c;
	if (n->becker)
		becker_reset(n->becker);
	cart_rom_detach(c);
}

static void nx32_free(struct part *p) {
	cart_rom_free(p);
}

static uint8_t nx32_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D) {
	struct nx32 *n = (struct nx32 *)c;
	(void)R2;
	c->EXTMEM = 0;

	if ((A & 0xFFFC) == 0xFF6C)
		return spi65_read(n->spi65, A & 3);

	if (A > 0x7fff && A < 0xff00 && !n->extmem_ty && n->extmem_map) {
		c->EXTMEM = 1;
		return n->extmem[0x8000 * n->extmem_bank + (A & 0x7fff)];
	}
	if (P2 && n->becker) {
		switch (A & 3) {
		case 0x1:
			return becker_read_status(n->becker);
		case 0x2:
			return becker_read_data(n->becker);
		default:
			break;
		}
	}
	return D;
}

static uint8_t nx32_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D) {
	struct nx32 *n = (struct nx32 *)c;
	(void)R2;
	c->EXTMEM = 0;

	if ((A & 0xFFFC) == 0xFF6C)
		spi65_write(n->spi65, A & 3, D);

	if ((A & ~1) == 0xFFDE) {
		n->extmem_ty = A & 1;
	} else if ((A & ~1) == 0xFFBE) {
		n->extmem_map = A & 1;
		n->extmem_bank = D & (EXTBANKS - 1);
		c->EXTMEM = 1;
	} else if (A > 0x7fff && A < 0xff00 && !n->extmem_ty && n->extmem_map) {
		n->extmem[0x8000 * n->extmem_bank + (A & 0x7fff)] = D;
		c->EXTMEM = 1;
	}
	if (P2 && n->becker) {
		switch (A & 3) {
		case 0x2:
			becker_write_data(n->becker, D);
			break;
		default:
			break;
		}
	}
	return D;
}
