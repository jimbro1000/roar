/** \file
 *
 *  \brief Null video output module.
 *
 *  \copyright Copyright 2011-2024 Ciaran Anscomb
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

#include <stdint.h>
#include <stdlib.h>

#include "xalloc.h"

#include "module.h"
#include "vo.h"

static void *new(void *cfg);

struct module vo_null_module = {
	.name = "null", .description = "No video",
	.new = new,
};

static void null_free(void *sptr);
static void no_op_render(void *sptr, unsigned burst, unsigned npixels, uint8_t const *data);

static void *new(void *cfg) {
	(void)cfg;
	struct vo_interface *vo = xmalloc(sizeof(*vo));
	*vo = (struct vo_interface){0};

	vo->free = DELEGATE_AS0(void, null_free, vo);

	// Used by machine to render video
	vo->render_line = DELEGATE_AS3(void, unsigned, unsigned, uint8cp, no_op_render, vo->renderer);

	return vo;
}

static void null_free(void *sptr) {
	struct vo_interface *vo = sptr;
	free(vo);
}

static void no_op_render(void *sptr, unsigned burst, unsigned npixels, uint8_t const *data) {
	(void)sptr;
	(void)burst;
	(void)npixels;
	(void)data;
}
