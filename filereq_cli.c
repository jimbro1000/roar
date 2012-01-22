/*  XRoar - a Dragon/Tandy Coco emulator
 *  Copyright (C) 2003-2012  Ciaran Anscomb
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA  02110-1301, USA.
 */

#include "config.h"

#include <stdio.h>
#include <string.h>

#include "types.h"

#include "fs.h"
#include "logging.h"
#include "module.h"

static char *get_filename(const char **extensions);

FileReqModule filereq_cli_module = {
	.common = { .name = "cli",
	            .description = "Command-line file requester" },
	.load_filename = get_filename, .save_filename = get_filename
};

static char fnbuf[256];

static char *get_filename(const char **extensions) {
	char *in, *cr;
	int was_fullscreen;
	(void)extensions;  /* unused */

	was_fullscreen = video_module->is_fullscreen;
	if (video_module->set_fullscreen && was_fullscreen)
		video_module->set_fullscreen(0);
	printf("Filename? ");
	fflush(stdout);
	in = fgets(fnbuf, sizeof(fnbuf), stdin);
	if (video_module->set_fullscreen && was_fullscreen)
		video_module->set_fullscreen(1);
	if (!in)
		return NULL;
	cr = strrchr(fnbuf, '\n');
	if (cr)
		*cr = 0;
	return fnbuf;
}
