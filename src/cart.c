/** \file
 *
 *  \brief Dragon/CoCo cartridge support.
 *
 *  \copyright Copyright 2005-2022 Ciaran Anscomb
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

#include "top-config.h"

#include <assert.h>
#include <ctype.h>
#include <libgen.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"
#include "c-strcase.h"
#include "delegate.h"
#include "sds.h"
#include "slist.h"
#include "xalloc.h"

#include "cart.h"
#include "crc32.h"
#include "events.h"
#include "fs.h"
#include "logging.h"
#include "machine.h"
#include "part.h"
#include "romlist.h"
#include "serialise.h"
#include "xconfig.h"
#include "xroar.h"

#define CART_CONFIG_SER_MPI_LOAD_SLOT (9)

static const struct ser_struct ser_struct_cart_config[] = {
	SER_ID_STRUCT_ELEM(1, ser_type_string,   struct cart_config, description),
	SER_ID_STRUCT_ELEM(2, ser_type_string,   struct cart_config, type),
	SER_ID_STRUCT_ELEM(3, ser_type_string,   struct cart_config, rom),
	SER_ID_STRUCT_ELEM(4, ser_type_string,   struct cart_config, rom2),
	SER_ID_STRUCT_ELEM(5, ser_type_bool,     struct cart_config, becker_port),
	SER_ID_STRUCT_ELEM(6, ser_type_int,      struct cart_config, autorun),
	SER_ID_STRUCT_ELEM(7, ser_type_sds_list, struct cart_config, opts),
	SER_ID_STRUCT_ELEM(8, ser_type_int,      struct cart_config, mpi.initial_slot),
	SER_ID_STRUCT_UNHANDLED(CART_CONFIG_SER_MPI_LOAD_SLOT),
};

static _Bool cart_config_read_elem(void *sptr, struct ser_handle *sh, int tag);
static _Bool cart_config_write_elem(void *sptr, struct ser_handle *sh, int tag);

static const struct ser_struct_data cart_config_ser_struct_data = {
	.elems = ser_struct_cart_config,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_cart_config),
	.read_elem = cart_config_read_elem,
	.write_elem = cart_config_write_elem,
};

#define CART_CONFIG_SER_MPI_LOAD_SLOT_NAME (1)

#define CART_SER_CART_CONFIG (1)

static const struct ser_struct ser_struct_cart[] = {
	SER_ID_STRUCT_UNHANDLED(CART_SER_CART_CONFIG),
	SER_ID_STRUCT_ELEM(2, ser_type_bool,   struct cart, EXTMEM),
	SER_ID_STRUCT_ELEM(3, ser_type_uint16, struct cart, rom_bank),
	SER_ID_STRUCT_ELEM(4, ser_type_event,  struct cart, firq_event),
};

static _Bool cart_read_elem(void *sptr, struct ser_handle *sh, int tag);
static _Bool cart_write_elem(void *sptr, struct ser_handle *sh, int tag);

// External; struct data nested by machines:
const struct ser_struct_data cart_ser_struct_data = {
	.elems = ser_struct_cart,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_cart),
	.read_elem = cart_read_elem,
	.write_elem = cart_write_elem,
};

static const struct ser_struct ser_struct_cart_rom[] = {
	SER_ID_STRUCT_NEST(1, &cart_ser_struct_data),
};

static const struct ser_struct_data cart_rom_ser_struct_data = {
        .elems = ser_struct_cart_rom,
        .num_elems = ARRAY_N_ELEMENTS(ser_struct_cart_rom),
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct slist *config_list = NULL;
static int next_id = 0;

/* Single config for auto-defined ROM carts */
static struct cart_config *rom_cart_config = NULL;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static uint8_t cart_rom_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static uint8_t cart_rom_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static void cart_rom_load(struct cart *c);
static void do_firq(void *);
static _Bool cart_rom_has_interface(struct cart *c, const char *ifname);

/**************************************************************************/

struct cart_config *cart_config_new(void) {
	struct cart_config *new = xmalloc(sizeof(*new));
	*new = (struct cart_config){0};
	new->id = next_id;
	new->autorun = ANY_AUTO;
	new->mpi.initial_slot = ANY_AUTO;
	config_list = slist_append(config_list, new);
	next_id++;
	return new;
}

void cart_config_serialise(struct cart_config *cc, struct ser_handle *sh, unsigned otag) {
	if (!cc)
		return;
	ser_write_open_string(sh, otag, cc->name);
	ser_write_struct_data(sh, &cart_config_ser_struct_data, cc);
}

struct cart_config *cart_config_deserialise(struct ser_handle *sh) {
	char *name = ser_read_string(sh);
	if (!name)
		return NULL;
	struct cart_config *cc = cart_config_by_name(name);
	if (!cc) {
		cc = cart_config_new();
		cc->name = xstrdup(name);
	}
	ser_read_struct_data(sh, &cart_config_ser_struct_data, cc);
	if (strcmp(name, "romcart") == 0)
		rom_cart_config = cc;
	xroar_update_cartridge_menu();
	free(name);
	return cc;
}

static void deserialise_mpi_slot(struct cart_config *cc, struct ser_handle *sh, unsigned slot) {
	if (cc->mpi.slot_cart_name[slot]) {
		free(cc->mpi.slot_cart_name[slot]);
		cc->mpi.slot_cart_name[slot] = NULL;
	}
	int tag;
	while (!ser_error(sh) && (tag = ser_read_tag(sh)) > 0) {
		switch (tag) {
		case CART_CONFIG_SER_MPI_LOAD_SLOT_NAME:
			{
				cc->mpi.slot_cart_name[slot] = ser_read_string(sh);
			}
			break;
		default:
			ser_set_error(sh, ser_error_format);
			break;
		}
	}
}

static _Bool cart_config_read_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct cart_config *cc = sptr;
	switch (tag) {
	case CART_CONFIG_SER_MPI_LOAD_SLOT:
		{
			unsigned slot = ser_read_vuint32(sh);
			if (slot >= 4) {
				return 0;
			}
			deserialise_mpi_slot(cc, sh, slot);
		}
		break;
	default:
		return 0;
	}
	return 1;
}

static _Bool cart_config_write_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct cart_config *cc = sptr;
	switch (tag) {
	case CART_CONFIG_SER_MPI_LOAD_SLOT:
		for (unsigned i = 0; i < 4; i++) {
			ser_write_open_vuint32(sh, CART_CONFIG_SER_MPI_LOAD_SLOT, i);
			ser_write_string(sh, CART_CONFIG_SER_MPI_LOAD_SLOT_NAME, cc->mpi.slot_cart_name[i]);
			ser_write_close_tag(sh);
		}
		break;
	default:
		return 0;
	}
	return 1;
}

struct cart_config *cart_config_by_id(int id) {
	for (struct slist *l = config_list; l; l = l->next) {
		struct cart_config *cc = l->data;
		if (cc->id == id)
			return cc;
	}
	return NULL;
}

struct cart_config *cart_config_by_name(const char *name) {
	if (!name) return NULL;
	for (struct slist *l = config_list; l; l = l->next) {
		struct cart_config *cc = l->data;
		if (0 == strcmp(cc->name, name)) {
			return cc;
		}
	}
	/* If "name" turns out to be a loadable ROM file, create a special
	   ROM cart config for it. */
	if (xroar_filetype_by_ext(name) == FILETYPE_ROM) {
		if (!rom_cart_config) {
			rom_cart_config = cart_config_new();
			rom_cart_config->name = xstrdup("romcart");
		}
		if (rom_cart_config->description) {
			free(rom_cart_config->description);
		}
		if (rom_cart_config->type) {
			free(rom_cart_config->type);
			rom_cart_config->type = NULL;
		}
		/* Make up a description from filename */
		sds tmp_name = sdsnew(name);
		char *bname = basename(tmp_name);
		if (bname && *bname) {
			char *sep;
			/* this will strip off file extensions or TOSEC-style
			   metadata in brackets */
			for (sep = bname + 1; *sep; sep++) {
				if ((*sep == '(') ||
				    (*sep == '.') ||
				    (isspace((int)*sep) && *(sep+1) == '(')) {
					*sep = 0;
					break;
				}
			}
			rom_cart_config->description = xstrdup(bname);
		} else {
			rom_cart_config->description = xstrdup("ROM cartridge");
		}
		sdsfree(tmp_name);
		if (rom_cart_config->rom)
			free(rom_cart_config->rom);
		rom_cart_config->rom = xstrdup(name);
		rom_cart_config->autorun = 1;
		FILE *fd = fopen(name, "rb");
		if (fd) {
			off_t fsize = fs_file_size(fd);
			if (fsize > 0x4000) {
				rom_cart_config->type = xstrdup("gmc");
			}
			fclose(fd);
		}
		if (!rom_cart_config->type) {
			rom_cart_config->type = xstrdup("rom");
		}
		xroar_update_cartridge_menu();
		return rom_cart_config;
	}
	return NULL;
}

struct cart_config *cart_find_working_dos(struct machine_config *mc) {
	sds tmp = NULL;
	struct cart_config *cc = NULL;
	if (!mc || (strcmp(mc->architecture, "coco") != 0 && strcmp(mc->architecture, "coco3") != 0)) {
		if ((tmp = romlist_find("@dragondos_compat"))) {
			cc = cart_config_by_name("dragondos");
		} else if ((tmp = romlist_find("@delta"))) {
			cc = cart_config_by_name("delta");
		}
	} else {
		if (xroar.cfg.becker.prefer && (tmp = romlist_find("@rsdos_becker"))) {
			cc = cart_config_by_name("becker");
		} else if ((tmp = romlist_find("@rsdos"))) {
			cc = cart_config_by_name("rsdos");
		} else if (!xroar.cfg.becker.prefer && (tmp = romlist_find("@rsdos_becker"))) {
			cc = cart_config_by_name("becker");
		}
	}
	if (tmp)
		sdsfree(tmp);
	return cc;
}

void cart_config_complete(struct cart_config *cc) {
	if (!cc->type) {
		cc->type = xstrdup("rom");
	}
	if (!cc->description) {
		cc->description = xstrdup(cc->name);
	}
	if (cc->autorun == ANY_AUTO) {
		if (c_strcasecmp(cc->type, "rom") == 0) {
			cc->autorun = 1;
		} else {
			cc->autorun = 0;
		}
	}
}

struct slist *cart_config_list(void) {
	return config_list;
}

struct slist *cart_config_list_is_a(const char *is_a) {
	struct slist *l = NULL;
	for (struct slist *iter = config_list; iter; iter = iter->next) {
		struct cart_config *cc = iter->data;
		if (partdb_is_a(cc->type, is_a)) {
			l = slist_append(l, cc);
		}
	}
	return l;
}

void cart_config_print_all(FILE *f, _Bool all) {
	for (struct slist *l = config_list; l; l = l->next) {
		struct cart_config *cc = l->data;
		fprintf(f, "cart %s\n", cc->name);
		xroar_cfg_print_inc_indent();
		xroar_cfg_print_string(f, all, "cart-desc", cc->description, NULL);
		xroar_cfg_print_string(f, all, "cart-type", cc->type, NULL);
		xroar_cfg_print_string(f, all, "cart-rom", cc->rom, NULL);
		xroar_cfg_print_string(f, all, "cart-rom2", cc->rom2, NULL);
		xroar_cfg_print_bool(f, all, "cart-autorun", cc->autorun, (strcmp(cc->type, "rom") == 0));
		xroar_cfg_print_bool(f, all, "cart-becker", cc->becker_port, 0);
		for (struct slist *i2 = cc->opts; i2; i2 = i2->next) {
			const char *s = i2->data;
			xroar_cfg_print_string(f, all, "cart-opt", s, NULL);
		}
		if (cc->mpi.initial_slot >= 0) {
			xroar_cfg_print_int(f, all, "mpi-slot", cc->mpi.initial_slot, -1);
		}
		for (int i = 0; i < 4; i++) {
			if (cc->mpi.slot_cart_name[i]) {
				xroar_cfg_print_indent(f);
				fprintf(f, "mpi-load-cart %d=%s\n", i, cc->mpi.slot_cart_name[i]);
			}
		}
		xroar_cfg_print_dec_indent();
		fprintf(f, "\n");
	}
}

static void cart_config_free(struct cart_config *cc) {
	if (cc->name)
		free(cc->name);
	if (cc->description)
		free(cc->description);
	if (cc->type)
		free(cc->type);
	if (cc->rom)
		free(cc->rom);
	if (cc->rom2)
		free(cc->rom2);
	for (int i = 0; i < 4; i++) {
		if (cc->mpi.slot_cart_name[i]) {
			free(cc->mpi.slot_cart_name[i]);
		}
	}
	slist_free_full(cc->opts, (slist_free_func)sdsfree);
	free(cc);
}

_Bool cart_config_remove(const char *name) {
	struct cart_config *cc = cart_config_by_name(name);
	if (!cc)
		return 0;
	config_list = slist_remove(config_list, cc);
	cart_config_free(cc);
	return 1;
}

void cart_config_remove_all(void) {
	slist_free_full(config_list, (slist_free_func)cart_config_free);
	config_list = NULL;
	rom_cart_config = NULL;
}

/* ---------------------------------------------------------------------- */

struct cart *cart_create(const char *cc_name) {
	struct cart_config *cc = cart_config_by_name(cc_name);
	if (!cc)
		return NULL;

	cart_config_complete(cc);
	if (!partdb_is_a(cc->type, "cart")) {
		return NULL;
	}
	struct cart *c = (struct cart *)part_create(cc->type, cc);
	if (c && !part_is_a((struct part *)c, "cart")) {
		part_free((struct part *)c);
		c = NULL;
	}
	if (!c) {
		LOG_WARN("Cartridge create FAILED: [%s]\n", cc->type);
		return NULL;
	}
	LOG_DEBUG(1, "Cartridge: [%s] %s\n", cc->type, cc->description);
	if (c->attach)
		c->attach(c);
	return c;
}

void cart_finish(struct cart *c) {
#ifdef HAVE_WASM
	// This is a bodge to ensure that ROM files are fetched during snapshot
	// loads in WASM builds.  Possible real fix is to a) have a "filename"
	// string type during serialisation, and b) record actual filename used
	// for any ROMs (after searching paths).
	struct cart_config *cc = c->config;
	if (cc->rom) {
		if (cc->rom[0] != '@' && strchr(cc->rom, '/') == NULL) {
			FILE *f = fopen(cc->rom, "a");
			if (f)
				fclose(f);
		}
	}
	if (cc->rom2) {
		if (cc->rom2[0] != '@' && strchr(cc->rom2, '/') == NULL) {
			FILE *f = fopen(cc->rom2, "a");
			if (f)
				fclose(f);
		}
	}
#endif
	cart_rom_load(c);
	if (c->firq_event.next == &c->firq_event) {
		event_queue(&MACHINE_EVENT_LIST, &c->firq_event);
	}
}

static _Bool cart_is_a(struct part *p, const char *name) {
	(void)p;
	return strcmp(name, "cart") == 0;
}

_Bool dragon_cart_is_a(struct part *p, const char *name) {
	(void)p;
	return strcmp(name, "dragon-cart") == 0 || cart_is_a(p, name);
}

_Bool mc10_cart_is_a(struct part *p, const char *name) {
	(void)p;
	return strcmp(name, "mc10-cart") == 0 || cart_is_a(p, name);
}

static _Bool cart_read_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct cart *c = sptr;
	switch (tag) {
	case CART_SER_CART_CONFIG:
		c->config = cart_config_deserialise(sh);
		break;
	default:
		return 0;
	}
	return 1;
}

static _Bool cart_write_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct cart *c = sptr;
	switch (tag) {
	case CART_SER_CART_CONFIG:
		cart_config_serialise(c->config, sh, tag);
		break;
	default:
		return 0;
	}
	return 1;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// ROM cart part creation

static struct part *cart_rom_allocate(void);
static void cart_rom_initialise(struct part *p, void *options);
static _Bool cart_rom_finish(struct part *p);

static const struct partdb_entry_funcs cart_rom_funcs = {
	.allocate = cart_rom_allocate,
	.initialise = cart_rom_initialise,
	.finish = cart_rom_finish,
	.free = cart_rom_free,

	.ser_struct_data = &cart_rom_ser_struct_data,

	.is_a = dragon_cart_is_a,
};

const struct partdb_entry cart_rom_part = { .name = "rom", .description = "ROM cartridge", .funcs = &cart_rom_funcs };

static struct part *cart_rom_allocate(void) {
	struct cart *c = part_new(sizeof(*c));
	struct part *p = &c->part;

	*c = (struct cart){0};

	cart_rom_init(c);

	return p;
}

static void cart_rom_initialise(struct part *p, void *options) {
	struct cart_config *cc = options;
	assert(cc != NULL);

	struct cart *c = (struct cart *)p;

	c->config = cc;
}

static _Bool cart_rom_finish(struct part *p) {
	struct cart *c = (struct cart *)p;
	cart_finish(c);
	return 1;
}

void cart_rom_free(struct part *p) {
	struct cart *c = (struct cart *)p;
	if (c->detach) {
		c->detach(c);
	}
	if (c->rom_data) {
		free(c->rom_data);
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/* ROM cart routines */

void cart_rom_init(struct cart *c) {
	c->read = cart_rom_read;
	c->write = cart_rom_write;
	c->reset = cart_rom_reset;
	c->attach = cart_rom_attach;
	c->detach = cart_rom_detach;
	c->rom_data = xzalloc(0x10000);
	c->rom_mask = 0x3fff;
	c->rom_bank = 0;

	event_init(&c->firq_event, DELEGATE_AS0(void, do_firq, c));
	c->signal_firq = DELEGATE_DEFAULT1(void, bool);
	c->signal_nmi = DELEGATE_DEFAULT1(void, bool);
	c->signal_halt = DELEGATE_DEFAULT1(void, bool);
	c->EXTMEM = 0;
	c->has_interface = cart_rom_has_interface;
}

static uint8_t cart_rom_read(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D) {
	(void)P2;
	if (R2)
		return c->rom_data[c->rom_bank | (A & c->rom_mask)];
	return D;
}

static uint8_t cart_rom_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D) {
	(void)P2;
	if (R2)
		return c->rom_data[c->rom_bank | (A & c->rom_mask)];
	return D;
}

static void cart_rom_load(struct cart *c) {
	struct cart_config *cc = c->config;

	if (cc->rom) {
		sds tmp = romlist_find(cc->rom);
		if (tmp) {
			int size = machine_load_rom(tmp, c->rom_data, 0x10000);
			(void)size;  // avoid warnings if...
#ifdef LOGGING
			if (size > 0) {
				uint32_t crc = crc32_block(CRC32_RESET, c->rom_data, size);
				LOG_DEBUG(1, "\tCRC = 0x%08x\n", crc);
			}
#endif
			sdsfree(tmp);
			if (size > 0x4000) {
				c->rom_mask = 0x7fff;
			} else if (size > 0x2000) {
				c->rom_mask = 0x3fff;
			} else {
				c->rom_mask = 0x1fff;
			}
		}
	}
	if (cc->rom2) {
		sds tmp = romlist_find(cc->rom2);
		if (tmp) {
			int size = machine_load_rom(tmp, c->rom_data + 0x2000, 0x2000);
			(void)size;  // avoid warnings if...
#ifdef LOGGING
			if (size > 0) {
				uint32_t crc = crc32_block(CRC32_RESET, c->rom_data + 0x2000, size);
				LOG_DEBUG(1, "\tCRC = 0x%08x\n", crc);
			}
#endif
			c->rom_mask = 0x3fff;
			sdsfree(tmp);
		}
	}
}

void cart_rom_reset(struct cart *c, _Bool hard) {
	if (hard)
		cart_rom_load(c);
	c->rom_bank = 0;
}

// The general approach taken by autostarting carts is to tie the CART FIRQ
// line to the Q clock, providing a continuous series of edge triggers to the
// PIA.  Emulating that would be quite CPU intensive, so split the difference
// by scheduling a toggle every 100ms.  Technically, this does mean that more
// time passes than would happen on a real machine (so the BASIC interpreter
// will have initialised more), but it hasn't been a problem for anything so
// far.

void cart_rom_attach(struct cart *c) {
	struct cart_config *cc = c->config;
	if (cc->autorun) {
		c->firq_event.at_tick = event_current_tick + EVENT_MS(100);
		event_queue(&MACHINE_EVENT_LIST, &c->firq_event);
	} else {
		event_dequeue(&c->firq_event);
	}
}

void cart_rom_detach(struct cart *c) {
	event_dequeue(&c->firq_event);
}

void cart_rom_select_bank(struct cart *c, uint16_t bank) {
	c->rom_bank = bank;
}

// Toggles the cartridge interrupt line.
static void do_firq(void *data) {
	static _Bool level = 0;
	struct cart *c = data;
	DELEGATE_SAFE_CALL(c->signal_firq, level);
	c->firq_event.at_tick = event_current_tick + EVENT_MS(100);
	event_queue(&MACHINE_EVENT_LIST, &c->firq_event);
	level = !level;
}

/* Default has_interface() - no interfaces supported */

static _Bool cart_rom_has_interface(struct cart *c, const char *ifname) {
	(void)c;
	(void)ifname;
	return 0;
}
