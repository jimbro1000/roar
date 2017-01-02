/*  Copyright 2003-2017 Ciaran Anscomb
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

#include "config.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "xalloc.h"

#include "cart.h"
#include "logging.h"
#include "sound.h"
#include "xroar.h"

static struct cart *orch90_new(struct cart_config *);

struct cart_module cart_orch90_module = {
	.name = "orch90",
	.description = "Orchestra 90-CC",
	.new = orch90_new,
};

struct orch90 {
	struct cart cart;
	float left;
	float right;
	struct sound_interface *snd;
};

static void orch90_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D);
static void orch90_reset(struct cart *c);
static void orch90_attach(struct cart *c);
static void orch90_detach(struct cart *c);
static _Bool orch90_has_interface(struct cart *c, const char *ifname);
static void orch90_attach_interface(struct cart *c, const char *ifname, void *intf);

static struct cart *orch90_new(struct cart_config *cc) {
	struct orch90 *o = xmalloc(sizeof(*o));
	struct cart *c = &o->cart;

	c->config = cc;
	cart_rom_init(c);
	c->write = orch90_write;
	c->reset = orch90_reset;
	c->attach = orch90_attach;
	c->detach = orch90_detach;
	c->has_interface = orch90_has_interface;
	c->attach_interface = orch90_attach_interface;

	o->left = 0.0;
	o->right = 0.0;

	return c;
}

static void orch90_reset(struct cart *c) {
	(void)c;
}

static void orch90_attach(struct cart *c) {
	cart_rom_attach(c);
	orch90_reset(c);
}

static void orch90_detach(struct cart *c) {
	struct orch90 *o = (struct orch90 *)c;
	sound_disable_external(o->snd);
	sound_set_cart_level(o->snd, 0.0);
	cart_rom_detach(c);
}

static _Bool orch90_has_interface(struct cart *c, const char *ifname) {
	return c && (0 == strcmp(ifname, "sound"));
}

static void orch90_attach_interface(struct cart *c, const char *ifname, void *intf) {
	if (!c || (0 != strcmp(ifname, "sound")))
		return;
	struct orch90 *o = (struct orch90 *)c;
	o->snd = intf;
	sound_enable_external(o->snd);
}

static void orch90_write(struct cart *c, uint16_t A, _Bool P2, _Bool R2, uint8_t D) {
	struct orch90 *o = (struct orch90 *)c;
	(void)P2;
	(void)R2;
	if (A == 0xff7a) {
		o->left = (float)D / 255.;
		sound_set_external_left(o->snd, o->left);
		sound_set_cart_level(o->snd, (o->left + o->right) / 2.0);
	}
	if (A == 0xff7b) {
		o->right = (float)D / 255.;
		sound_set_external_right(o->snd, o->right);
		sound_set_cart_level(o->snd, (o->left + o->right) / 2.0);
	}
}
