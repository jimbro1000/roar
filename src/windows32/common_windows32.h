/** \file
 *
 *  \brief Windows user-interface common functions.
 *
 *  \copyright Copyright 2006-2024 Ciaran Anscomb
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

#ifndef XROAR_COMMON_WINDOWS32_H_
#define XROAR_COMMON_WINDOWS32_H_

#include <windows.h>

#include "sdl2/common.h"

struct ui_windows32_interface {
	struct ui_sdl2_interface ui_sdl2_interface;
};

extern HWND windows32_main_hwnd;

/// Various initialisation required for Windows32.
int windows32_init(_Bool alloc_console);

/// Cleanup before exit.
void windows32_shutdown(void);

// Draw a control using DrawText() with DT_PATH_ELLIPSIS
void windows32_drawtext_path(HWND hWnd, LPDRAWITEMSTRUCT pDIS);

#endif
