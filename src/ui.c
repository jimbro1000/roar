/** \file
 *
 *  \brief User-interface modules & interfaces.
 *
 *  \copyright Copyright 2003-2021 Ciaran Anscomb
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

#include <stdlib.h>
#include <stdio.h>

#include "module.h"
#include "ui.h"
#include "xconfig.h"

extern struct ui_module ui_gtk2_module;
extern struct ui_module ui_null_module;
extern struct ui_module ui_windows32_module;
extern struct ui_module ui_wasm_module;
extern struct ui_module ui_sdl_module;
static struct ui_module * const default_ui_module_list[] = {
#ifdef HAVE_GTK2
#ifdef HAVE_GTKGL
	&ui_gtk2_module,
#endif
#endif
#ifdef WINDOWS32
	&ui_windows32_module,
#endif
#ifdef HAVE_WASM
	&ui_wasm_module,
#endif
#ifdef WANT_UI_SDL
	&ui_sdl_module,
#endif
	&ui_null_module,
	NULL
};

struct ui_module * const *ui_module_list = default_ui_module_list;

struct xconfig_enum ui_gl_filter_list[] = {
	{ XC_ENUM_INT("auto", UI_GL_FILTER_AUTO, "Automatic") },
	{ XC_ENUM_INT("nearest", UI_GL_FILTER_NEAREST, "Nearest-neighbour filter") },
	{ XC_ENUM_INT("linear", UI_GL_FILTER_LINEAR, "Linear filter") },
	{ XC_ENUM_END() }
};

void ui_print_vo_help(void) {
	for (int i = 0; ui_module_list[i]; i++) {
		if (!ui_module_list[i]->vo_module_list)
			continue;
		printf("Video modules for %s (ui %s)\n", ui_module_list[i]->common.description, ui_module_list[i]->common.name);
		module_print_list((struct module * const*)ui_module_list[i]->vo_module_list);
	}
}
