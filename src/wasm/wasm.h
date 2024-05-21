/** \file
 *
 *  \brief WebAssembly (emscripten) support.
 *
 *  \copyright Copyright 2019-2024 Ciaran Anscomb
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

#ifdef HAVE_WASM

#ifndef XROAR_WASM_H_
#define XROAR_WASM_H_

#include <stdio.h>

#include "sdl2/common.h"

struct machine_config;
struct cart_config;

struct ui_wasm_interface {
	struct ui_sdl2_interface ui_sdl2_interface;

	_Bool done_first_frame;
	double last_t;
	double tickerr;
};

extern _Bool wasm_retry_open;
extern int wasm_waiting_files;

FILE *wasm_fopen(const char *pathname, const char *mode);

// UI state update handler.
void wasm_ui_update_state(void *sptr, int tag, int value, const void *data);

// Hooks into xroar_set_machine() and xroar_set_cart() to asyncify.
_Bool wasm_ui_prepare_machine(struct machine_config *mc);
_Bool wasm_ui_prepare_cartridge(struct cart_config *cc);

// Async browser interfaces to certain functions.
void wasm_set_machine_cart(const char *machine, const char *cart, const char *cart_rom, const char *cart_rom2);
void wasm_set_joystick(int port, const char *value);
void wasm_queue_basic(const char *string);

#endif

#endif
