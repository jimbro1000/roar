/** \file
 *
 *  \brief GTK+ 3 joystick interfaces.
 *
 *  \copyright Copyright 2024 Ciaran Anscomb
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

// For strsep()
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _DARWIN_C_SOURCE

#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

#include "pl-string.h"
#include "xalloc.h"

#include "joystick.h"
#include "logging.h"
#include "module.h"

#include "gtk3/common.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct joystick_axis *configure_axis(char *, unsigned);
static struct joystick_button *configure_button(char *, unsigned);

static struct joystick_submodule gtk3_js_submod_mouse = {
	.name = "mouse",
	.configure_axis = configure_axis,
	.configure_button = configure_button,
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

extern struct joystick_submodule hkbd_js_keyboard;

static struct joystick_submodule *js_submodlist[] = {
	&hkbd_js_keyboard,
	&gtk3_js_submod_mouse,
	NULL
};

struct joystick_module gtk3_js_internal = {
	.common = { .name = "gtk3", .description = "GTK+ joystick" },
	.submodule_list = js_submodlist,
};

struct joystick_module *gtk3_js_modlist[] = {
	&gtk3_js_internal,
	NULL
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct gtk_mouse_js_axis {
	struct joystick_control joystick_control;
	struct ui_gtk3_interface *ui_gtk3_interface;
	int axis;
	float offset;
	float div;
};

struct gtk_mouse_js_button {
	struct joystick_control joystick_control;
	struct ui_gtk3_interface *ui_gtk3_interface;
	int button;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static int gtk_mouse_js_axis_read(void *);
static int gtk_mouse_js_button_read(void *);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct joystick_axis *configure_axis(char *spec, unsigned jaxis) {
	struct gtk_mouse_js_axis *axis = xmalloc(sizeof(*axis));
	*axis = (struct gtk_mouse_js_axis){0};

	axis->ui_gtk3_interface = global_uigtk3;

	jaxis %= 2;
	axis->axis = jaxis;

	float off0 = (jaxis == 0) ? 2.0 : 1.5;
	float off1 = (jaxis == 0) ? 254.0 : 190.5;
	char *tmp = NULL;
	if (spec)
		tmp = strsep(&spec, ",");
	if (tmp && *tmp)
		off0 = strtof(tmp, NULL);
	if (spec && *spec)
		off1 = strtof(spec, NULL);
	off0 -= 1.0;
	off1 -= 0.75;
	if (jaxis == 0) {
		if (off0 < -32.0) off0 = -32.0;
		if (off1 > 288.0) off0 = 288.0;
		axis->offset = off0 + 32.0;
		axis->div = off1 - off0;
	} else {
		if (off0 < -24.0) off0 = -24.0;
		if (off1 > 216.0) off0 = 216.0;
		axis->offset = off0 + 24.0;
		axis->div = off1 - off0;
	}

	axis->joystick_control.read = DELEGATE_AS0(int, gtk_mouse_js_axis_read, axis);
	axis->joystick_control.free = DELEGATE_AS0(void, free, axis);

	return (struct joystick_axis *)&axis->joystick_control;
}

static struct joystick_button *configure_button(char *spec, unsigned jbutton) {
	jbutton %= 3;
	if (spec && *spec)
		jbutton = strtol(spec, NULL, 0) - 1;
	if (jbutton >= 3)
		return NULL;

	struct gtk_mouse_js_button *button = xmalloc(sizeof(*button));
	*button = (struct gtk_mouse_js_button){0};

	button->ui_gtk3_interface = global_uigtk3;
	button->button = jbutton;

	button->joystick_control.read = DELEGATE_AS0(int, gtk_mouse_js_button_read, button);
	button->joystick_control.free = DELEGATE_AS0(void, free, button);

	return (struct joystick_button *)&button->joystick_control;
}

static int gtk_mouse_js_axis_read(void *sptr) {
	struct gtk_mouse_js_axis *axis = sptr;
	struct ui_gtk3_interface *uigtk3 = axis->ui_gtk3_interface;
	float v = (uigtk3->mouse.axis[axis->axis] - axis->offset) / axis->div;
	if (v < 0.0) v = 0.0;
	if (v > 1.0) v = 1.0;
	return (int)(v * 65535.);
}

static int gtk_mouse_js_button_read(void *sptr) {
	struct gtk_mouse_js_button *button = sptr;
	struct ui_gtk3_interface *uigtk3 = button->ui_gtk3_interface;
	return uigtk3->mouse.button[button->button];
}
