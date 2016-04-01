/*  Copyright 2003-2016 Ciaran Anscomb
 *
 *  This file is part of XRoar.
 *
 *  XRoar is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  XRoar is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XRoar.  If not, see <http://www.gnu.org/licenses/>.
 */

/* Sources:
 *     DragonDOS cartridge detail:
 *         http://www.dragon-archive.co.uk/
 */

/* TODO: I've hacked in an optional "becker port" at $FF49/$FF4A.  Is this the
 * best place for it? */

#include "config.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "xalloc.h"

#include "becker.h"
#include "cart.h"
#include "delegate.h"
#include "logging.h"
#include "vdrive.h"
#include "wd279x.h"
#include "xroar.h"

static struct cart *dragondos_new(struct cart_config *);

struct cart_module cart_dragondos_module = {
	.name = "dragondos",
	.description = "DragonDOS",
	.new = dragondos_new,
};

struct dragondos {
	struct cart cart;
	unsigned ic1_old;
	unsigned ic1_drive_select;
	_Bool ic1_motor_enable;
	_Bool ic1_precomp_enable;
	_Bool ic1_density;
	_Bool ic1_nmi_enable;
	_Bool have_becker;
	WD279X *fdc;
	struct vdrive_interface *vdrive_interface;
};

static uint8_t dragondos_read(struct cart *c, uint16_t A, _Bool P2, uint8_t D);
static void dragondos_write(struct cart *c, uint16_t A, _Bool P2, uint8_t D);
static void dragondos_reset(struct cart *c);
static void dragondos_detach(struct cart *c);
static _Bool dragondos_has_interface(struct cart *c, const char *ifname);
static void dragondos_attach_interface(struct cart *c, const char *ifname, void *intf);

static void ff48_write(struct dragondos *d, unsigned flags);

/* Handle signals from WD2797 */
static void set_drq(void *, _Bool);
static void set_intrq(void *, _Bool);

static struct cart *dragondos_new(struct cart_config *cc) {
	struct dragondos *d = xmalloc(sizeof(*d));
	*d = (struct dragondos){0};
	struct cart *c = &d->cart;

	c->config = cc;
	cart_rom_init(c);
	c->read = dragondos_read;
	c->write = dragondos_write;
	c->reset = dragondos_reset;
	c->detach = dragondos_detach;
	c->has_interface = dragondos_has_interface;
	c->attach_interface = dragondos_attach_interface;

	d->have_becker = (cc->becker_port && becker_open());
	d->fdc = wd279x_new(WD2797);

	return c;
}

static void dragondos_reset(struct cart *c) {
	struct dragondos *d = (struct dragondos *)c;
	wd279x_reset(d->fdc);
	d->ic1_old = 0xff;
	ff48_write(d, 0);
	if (d->have_becker)
		becker_reset();
}

static void dragondos_detach(struct cart *c) {
	struct dragondos *d = (struct dragondos *)c;
	vdrive_disconnect(d->vdrive_interface);
	wd279x_disconnect(d->fdc);
	wd279x_free(d->fdc);
	d->fdc = NULL;
	if (d->have_becker)
		becker_close();
	cart_rom_detach(c);
}

static uint8_t dragondos_read(struct cart *c, uint16_t A, _Bool P2, uint8_t D) {
	struct dragondos *d = (struct dragondos *)c;
	if (!P2) {
		return c->rom_data[A & 0x3fff];
	}
	if ((A & 0xc) == 0) {
		return wd279x_read(d->fdc, A);
	}
	if (!(A & 8))
		return D;
	if (d->have_becker) {
		switch (A & 3) {
		case 0x1:
			return becker_read_status();
		case 0x2:
			return becker_read_data();
		default:
			break;
		}
	}
	return D;
}

static void dragondos_write(struct cart *c, uint16_t A, _Bool P2, uint8_t D) {
	struct dragondos *d = (struct dragondos *)c;
	if (!P2)
		return;
	if ((A & 0xc) == 0) {
		wd279x_write(d->fdc, A, D);
		return;
	}
	if (!(A & 8))
		return;
	if (d->have_becker) {
		switch (A & 3) {
		case 0x0:
			ff48_write(d, D);
			break;
		case 0x2:
			becker_write_data(D);
			break;
		default:
			break;
		}
	} else {
		ff48_write(d, D);
	}
}

static _Bool dragondos_has_interface(struct cart *c, const char *ifname) {
	return c && (0 == strcmp(ifname, "floppy"));
}

static void dragondos_attach_interface(struct cart *c, const char *ifname, void *intf) {
	if (!c || (0 != strcmp(ifname, "floppy")))
		return;
	struct dragondos *d = (struct dragondos *)c;
	d->vdrive_interface = intf;

	d->fdc->set_dirc = DELEGATE_AS1(void, int, d->vdrive_interface->set_dirc, d->vdrive_interface);
	d->fdc->set_dden = DELEGATE_AS1(void, bool, d->vdrive_interface->set_dden, d->vdrive_interface);
	d->fdc->set_sso = DELEGATE_AS1(void, unsigned, d->vdrive_interface->set_sso, d->vdrive_interface);
	d->fdc->set_drq = DELEGATE_AS1(void, bool, set_drq, c);
	d->fdc->set_intrq = DELEGATE_AS1(void, bool, set_intrq, c);
	d->fdc->get_head_pos = DELEGATE_AS0(unsigned, d->vdrive_interface->get_head_pos, d->vdrive_interface);
	d->fdc->step = DELEGATE_AS0(void, d->vdrive_interface->step, d->vdrive_interface);
	d->fdc->write = DELEGATE_AS1(void, uint8, d->vdrive_interface->write, d->vdrive_interface);
	d->fdc->skip = DELEGATE_AS0(void, d->vdrive_interface->skip, d->vdrive_interface);
	d->fdc->read = DELEGATE_AS0(uint8, d->vdrive_interface->read, d->vdrive_interface);
	d->fdc->write_idam = DELEGATE_AS0(void, d->vdrive_interface->write_idam, d->vdrive_interface);
	d->fdc->time_to_next_byte = DELEGATE_AS0(unsigned, d->vdrive_interface->time_to_next_byte, d->vdrive_interface);
	d->fdc->time_to_next_idam = DELEGATE_AS0(unsigned, d->vdrive_interface->time_to_next_idam, d->vdrive_interface);
	d->fdc->next_idam = DELEGATE_AS0(uint8p, d->vdrive_interface->next_idam, d->vdrive_interface);
	d->fdc->update_connection = DELEGATE_AS0(void, d->vdrive_interface->update_connection, d->vdrive_interface);

	d->vdrive_interface->ready = DELEGATE_AS1(void, bool, wd279x_ready, d->fdc);
	d->vdrive_interface->tr00 = DELEGATE_AS1(void, bool, wd279x_tr00, d->fdc);
	d->vdrive_interface->index_pulse = DELEGATE_AS1(void, bool, wd279x_index_pulse, d->fdc);
	d->vdrive_interface->write_protect = DELEGATE_AS1(void, bool, wd279x_write_protect, d->fdc);
	wd279x_update_connection(d->fdc);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/* DragonDOS cartridge circuitry */
static void ff48_write(struct dragondos *d, unsigned flags) {
	if (flags != d->ic1_old) {
		LOG_DEBUG(2, "DragonDOS: Write to FF48: ");
		if ((flags ^ d->ic1_old) & 0x03) {
			LOG_DEBUG(2, "DRIVE SELECT %01u, ", flags & 0x03);
		}
		if ((flags ^ d->ic1_old) & 0x04) {
			LOG_DEBUG(2, "MOTOR %s, ", (flags & 0x04)?"ON":"OFF");
		}
		if ((flags ^ d->ic1_old) & 0x08) {
			LOG_DEBUG(2, "DENSITY %s, ", (flags & 0x08)?"SINGLE":"DOUBLE");
		}
		if ((flags ^ d->ic1_old) & 0x10) {
			LOG_DEBUG(2, "PRECOMP %s, ", (flags & 0x10)?"ON":"OFF");
		}
		if ((flags ^ d->ic1_old) & 0x20) {
			LOG_DEBUG(2, "NMI %s, ", (flags & 0x20)?"ENABLED":"DISABLED");
		}
		LOG_DEBUG(2, "\n");
		d->ic1_old = flags;
	}
	d->ic1_drive_select = flags & 0x03;
	d->vdrive_interface->set_drive(d->vdrive_interface, d->ic1_drive_select);
	d->ic1_motor_enable = flags & 0x04;
	d->ic1_density = flags & 0x08;
	wd279x_set_dden(d->fdc, !d->ic1_density);
	d->ic1_precomp_enable = flags & 0x10;
	d->ic1_nmi_enable = flags & 0x20;
}

static void set_drq(void *sptr, _Bool value) {
	struct cart *c = sptr;
	DELEGATE_CALL1(c->signal_firq, value);
}

static void set_intrq(void *sptr, _Bool value) {
	struct cart *c = sptr;
	struct dragondos *d = sptr;
	if (value) {
		if (d->ic1_nmi_enable) {
			DELEGATE_CALL1(c->signal_nmi, 1);
		}
	} else {
		DELEGATE_CALL1(c->signal_nmi, 0);
	}
}
