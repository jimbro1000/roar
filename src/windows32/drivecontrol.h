/** \file
 *
 *  \brief Windows drive control window.
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

#ifndef XROAR_WINDOWS_DRIVECONTROL_H_
#define XROAR_WINDOWS_DRIVECONTROL_H_

struct ui_windows32_interface;

void windows32_dc_create_window(struct ui_windows32_interface *);

void windows32_dc_update_state(struct ui_windows32_interface *,
			       int tag, int value, const void *data);

#endif
