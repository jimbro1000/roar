/** \file
 *
 *  \brief GTK+ 3 keyboard support.
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

#include <stdlib.h>

#include <gtk/gtk.h>

#include "hkbd.h"
#include "xroar.h"

#include "gtk3/common.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static gboolean map_keyboard(GdkKeymap *gdk_keymap, gpointer user_data) {
	(void)gdk_keymap;
	(void)user_data;
	hk_update_keymap();
	return FALSE;
}

void gtk3_keyboard_init(struct ui_cfg *ui_cfg) {
	(void)ui_cfg;
	hk_init();
	GdkKeymap *gdk_keymap = gdk_keymap_get_for_display(gdk_display_get_default());
	map_keyboard(gdk_keymap, NULL);
	g_signal_connect(G_OBJECT(gdk_keymap), "keys-changed", G_CALLBACK(map_keyboard), NULL);
}

gboolean gtk3_keyboard_handle_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
	(void)widget;
	struct ui_gtk3_interface *uigtk3 = user_data;

	// XXX EITHER we remove all ability for the UI to do something with this,
	// OR we add a callback to hkbd - note that we'd have to pass the event as
	// data here.
	// OR we add an extra call up front like hk_scan_preempted() and only call
	// this if it wouldn't be?

	// If GTK+ has something configured for the current combo:
	if (gtk_window_activate_key(GTK_WINDOW(uigtk3->top_window), event) == TRUE) {
		return TRUE;
	}

	// If an OS-specific keyboard scancode mapping could be determined:
	if (event->hardware_keycode < hk_num_os_scancodes) {
		hk_scan_press(os_scancode_to_hk_scancode[event->hardware_keycode]);
	}

	return TRUE;
}

gboolean gtk3_keyboard_handle_key_release(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
	(void)widget;
	(void)user_data;

	// If an OS-specific keyboard scancode mapping could be determined:
	if (event->hardware_keycode < hk_num_os_scancodes) {
		hk_scan_release(os_scancode_to_hk_scancode[event->hardware_keycode]);
	}

	return FALSE;
}
