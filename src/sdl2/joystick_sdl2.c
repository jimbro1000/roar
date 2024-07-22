/** \file
 *
 *  \brief SDL2 joystick module.
 *
 *  \copyright Copyright 2015-2024 Ciaran Anscomb
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

#include <string.h>

#include <SDL.h>

#include "pl-string.h"
#include "slist.h"
#include "xalloc.h"

#include "joystick.h"
#include "logging.h"
#include "module.h"

#include "sdl2/common.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct joystick_axis *configure_axis(char *, unsigned);
static struct joystick_button *configure_button(char *, unsigned);
static void unmap_axis(struct joystick_axis *axis);
static void unmap_button(struct joystick_button *button);
static void sdl2_js_print_physical(void);

struct joystick_submodule sdl_js_submod_physical = {
	.name = "physical",
	.configure_axis = configure_axis,
	.configure_button = configure_button,
	.unmap_axis = unmap_axis,
	.unmap_button = unmap_button,
	.print_list = sdl2_js_print_physical,
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct joystick_submodule *js_submodlist[] = {
	&sdl_js_submod_physical,
	NULL
};

struct joystick_module sdl_js_mod_exported = {
	.common = { .name = "sdl", .description = "SDL2 joystick input" },
	.submodule_list = js_submodlist,
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static _Bool initialised = 0;
static unsigned num_joysticks;

// Wrap SDL_Joystick up in struct device.  close_device() will only
// close the underlying joystick once open_count reaches 0.
struct device {
	int joystick_index;
	SDL_Joystick *joystick;
	SDL_GameController *gamecontroller;
	unsigned open_count;
	unsigned num_axes;
	unsigned num_buttons;
	unsigned *debug_axes;
	unsigned *debug_buttons;
};

static struct slist *device_list = NULL;

struct control {
	struct device *device;
	unsigned control;
	_Bool inverted;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void sdl2_js_print_physical(void) {
	SDL_InitSubSystem(SDL_INIT_JOYSTICK);
	unsigned njstk = SDL_NumJoysticks();
	LOG_PRINT("%-3s %-31s %-7s %-7s\n", "Idx", "Description", "Axes", "Buttons");
	for (unsigned index = 0; index < njstk; index++) {
		SDL_Joystick *sdlj = SDL_JoystickOpen(index);
		if (!sdlj)
			continue;
		LOG_PRINT("%-3u ", index);
		const char *description = SDL_JoystickName(sdlj);
		if (!description)
			description = "";
		LOG_PRINT("%-31s ", description);
		LOG_PRINT("%-7d ", SDL_JoystickNumAxes(sdlj));
		LOG_PRINT("%-7d\n", SDL_JoystickNumButtons(sdlj));
		SDL_JoystickClose(sdlj);
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void sdl_js_physical_init(void) {
	if (initialised)
		return;
	SDL_InitSubSystem(SDL_INIT_JOYSTICK);
	num_joysticks = SDL_NumJoysticks();
	if (num_joysticks < 1) {
		LOG_DEBUG(1, "\tNo joysticks found\n");
	} else {
		LOG_DEBUG(1, "\t%u joysticks found\n", num_joysticks);
	}
	initialised = 1;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void report_device(struct device *d) {
	if (logging.level < 1)
		return;
	_Bool is_gamecontroller = (d->gamecontroller != NULL);
	LOG_PRINT("Opened joystick index %d as %s\n", d->joystick_index, is_gamecontroller ? "controller" : "joystick");
	const char *name = NULL;
	unsigned vendor_id = 0;
	unsigned product_id = 0;
	unsigned product_version = 0;
	if (is_gamecontroller) {
		name = SDL_GameControllerName(d->gamecontroller);
		vendor_id = SDL_GameControllerGetVendor(d->gamecontroller);
		product_id = SDL_GameControllerGetProduct(d->gamecontroller);
		product_version = SDL_GameControllerGetProductVersion(d->gamecontroller);
	} else {
		name = SDL_JoystickName(d->joystick);
		vendor_id = SDL_JoystickGetVendor(d->joystick);
		product_id = SDL_JoystickGetProduct(d->joystick);
		product_version = SDL_JoystickGetProductVersion(d->joystick);
	}
	if (name) {
		LOG_PRINT("\tName: %s\n", name);
	}
	if (vendor_id) {
		LOG_PRINT("\tVendor ID: 0x%04x\n", vendor_id);
	}
	if (product_id) {
		LOG_PRINT("\tProduct ID: 0x%04x\n", product_id);
	}
	if (product_version) {
		LOG_PRINT("\tProduct version: 0x%04x\n", product_version);
	}
	if (!is_gamecontroller) {
		LOG_PRINT("\t%d axes, %d buttons\n", d->num_axes, d->num_buttons);
	}
}

static struct device *open_device(int joystick_index) {
	// If the device is already open, just up its count and return it
	for (struct slist *iter = device_list; iter; iter = iter->next) {
		struct device *d = iter->data;
		if (d->joystick_index == joystick_index) {
			d->open_count++;
			return d;
		}
	}
	// Otherwise open and throw it on the list
	_Bool is_gamecontroller = SDL_IsGameController(joystick_index);
	if (is_gamecontroller) {
		SDL_GameController *j = SDL_GameControllerOpen(joystick_index);
		if (j) {
			struct device *d = xmalloc(sizeof(*d));
			*d = (struct device){0};
			d->joystick_index = joystick_index;
			d->gamecontroller = j;
			d->num_axes = SDL_CONTROLLER_AXIS_MAX;
			d->num_buttons = SDL_CONTROLLER_BUTTON_MAX;
			d->open_count = 1;
			device_list = slist_prepend(device_list, d);
			report_device(d);
			return d;
		}
	}
	SDL_Joystick *j = SDL_JoystickOpen(joystick_index);
	if (!j)
		return NULL;
	struct device *d = xmalloc(sizeof(*d));
	*d = (struct device){0};
	d->joystick_index = joystick_index;
	d->joystick = j;
	d->num_axes = SDL_JoystickNumAxes(j);
	d->num_buttons = SDL_JoystickNumButtons(j);
	d->open_count = 1;
	device_list = slist_prepend(device_list, d);
	report_device(d);
	return d;
}

static void close_device(struct device *d) {
	d->open_count--;
	if (d->open_count == 0) {
		if (d->gamecontroller) {
			SDL_GameControllerClose(d->gamecontroller);
		} else {
			SDL_JoystickClose(d->joystick);
		}
		device_list = slist_remove(device_list, d);
		free(d);
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void debug_controls(struct device *d) {
	if (!d->debug_axes) {
		d->debug_axes = xmalloc(d->num_axes * sizeof(unsigned));
		for (unsigned i = 0; i < d->num_axes; i++) {
			d->debug_axes[i] = 32768;
		}
	}
	if (!d->debug_buttons) {
		d->debug_buttons = xmalloc(d->num_buttons * sizeof(unsigned));
		for (unsigned i = 0; i < d->num_buttons; i++) {
			d->debug_buttons[i] = 0;
		}
	}
	_Bool report = 0;
	for (unsigned i = 0; i < d->num_axes; i++) {
		unsigned v;
		if (d->gamecontroller) {
			v = SDL_GameControllerGetAxis(d->gamecontroller, i) + 32768;
		} else {
			v = SDL_JoystickGetAxis(d->joystick, i) + 32768;
		}
		if (d->debug_axes[i] != v) {
			report = 1;
			d->debug_axes[i] = v;
		}
	}
	for (unsigned i = 0; i < d->num_buttons; i++) {
		unsigned v;
		if (d->gamecontroller) {
			v = SDL_GameControllerGetButton(d->gamecontroller, i);
		} else {
			v = SDL_JoystickGetButton(d->joystick, i);
		}
		if (d->debug_buttons[i] != v) {
			report = 1;
			d->debug_buttons[i] = v;
		}
	}
	if (report) {
		LOG_PRINT("JS%2d:", d->joystick_index);
		for (unsigned i = 0; i < d->num_axes; i++) {
			LOG_PRINT(" a%u: %5u", i, d->debug_axes[i]);
		}
		LOG_PRINT(" b: ");
		for (unsigned i = 0; i < d->num_buttons; i++) {
			LOG_PRINT("%u", d->debug_buttons[i]);
		}
		LOG_PRINT("\n");
	}
}

static unsigned read_axis(struct control *c) {
	if (logging.debug_ui & LOG_UI_JS_MOTION) {
		debug_controls(c->device);
	}
	unsigned ret;
	if (c->device->gamecontroller) {
		ret = SDL_GameControllerGetAxis(c->device->gamecontroller, c->control) + 32768;
	} else {
		ret = SDL_JoystickGetAxis(c->device->joystick, c->control) + 32768;
	}
	if (c->inverted)
		ret ^= 0xffff;
	return ret;
}

static _Bool read_button(struct control *c) {
	if (logging.debug_ui & LOG_UI_JS_MOTION) {
		debug_controls(c->device);
	}
	if (c->device->gamecontroller) {
		return SDL_GameControllerGetButton(c->device->gamecontroller, c->control);
	}
	return SDL_JoystickGetButton(c->device->joystick, c->control);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// axis & button specs are basically the same, just track a different
// "selected" variable.
static struct control *configure_control(char *spec, unsigned control) {
	unsigned joystick = 0;
	_Bool inverted = 0;
	char *tmp = NULL;
	if (spec)
		tmp = strsep(&spec, ",");
	if (tmp && *tmp) {
		control = strtol(tmp, NULL, 0);
	}
	if (spec && *spec) {
		joystick = control;
		if (*spec == '-') {
			inverted = 1;
			spec++;
		}
		if (*spec) {
			control = strtol(spec, NULL, 0);
		}
	}
	struct device *d = open_device(joystick);
	if (!d)
		return NULL;
	struct control *c = xmalloc(sizeof(*c));
	c->device = d;
	c->control = control;
	c->inverted = inverted;
	return c;
}

static struct joystick_axis *configure_axis(char *spec, unsigned jaxis) {
	sdl_js_physical_init();
	struct control *c = configure_control(spec, jaxis);
	if (!c)
		return NULL;
	if (c->control >= c->device->num_axes) {
		close_device(c->device);
		free(c);
		return NULL;
	}
	struct joystick_axis *axis = xmalloc(sizeof(*axis));
	*axis = (struct joystick_axis){0};
	axis->read = (js_read_axis_func)read_axis;
	axis->data = c;
	return axis;
}

static struct joystick_button *configure_button(char *spec, unsigned jbutton) {
	sdl_js_physical_init();
	struct control *c = configure_control(spec, jbutton);
	if (!c)
		return NULL;
	if (c->control >= c->device->num_buttons) {
		close_device(c->device);
		free(c);
		return NULL;
	}
	struct joystick_button *button = xmalloc(sizeof(*button));
	*button = (struct joystick_button){0};
	button->read = (js_read_button_func)read_button;
	button->data = c;
	return button;
}

static void unmap_axis(struct joystick_axis *axis) {
	if (!axis)
		return;
	struct control *c = axis->data;
	close_device(c->device);
	free(c);
	free(axis);
}

static void unmap_button(struct joystick_button *button) {
	if (!button)
		return;
	struct control *c = button->data;
	close_device(c->device);
	free(c);
	free(button);
}
