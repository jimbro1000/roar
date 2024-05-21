/** \file
 *
 *  \brief GTK+ 2 user-interface common functions.
 *
 *  \copyright Copyright 2014-2023 Ciaran Anscomb
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

#ifndef XROAR_GTK2_COMMON_H_
#define XROAR_GTK2_COMMON_H_

#include <stdint.h>

#include <glib.h>
#include <gtk/gtk.h>

#include "events.h"
#include "ui.h"
#include "vo.h"

struct ui_gtk2_interface {
	struct ui_interface public;

	struct ui_cfg *cfg;

	GtkBuilder *builder;

	// Shared GTK+ objects
	GtkWidget *top_window;
	GtkUIManager *menu_manager;
	GtkWidget *menubar;
	GtkWidget *drawing_area;
	// Dynamic menus
	GtkActionGroup *machine_action_group;
	guint merge_machines;
	GtkActionGroup *cart_action_group;
	guint merge_carts;

	// Window geometry
	struct vo_draw_area draw_area;
	struct vo_picture_area picture_area;
	_Bool user_specified_geometry;

	// Keyboard state
	struct {
		// Is a non-preempted control key pressed?
		_Bool control;
	} keyboard;

	// Mouse tracking
	float mouse_xoffset;
	float mouse_yoffset;
	float mouse_xdiv;
	float mouse_ydiv;
	unsigned mouse_axis[2];
	_Bool mouse_button[3];
	// Cursor hiding
	_Bool cursor_hidden;
	GdkCursor *old_cursor;
	GdkCursor *blank_cursor;

};

// Eventually, everything should be delegated properly, but for now assure
// there is only ever one instantiation of ui_gtk2 and make it available
// globally.
extern struct ui_gtk2_interface *global_uigtk2;

void gtk2_keyboard_init(struct ui_cfg *ui_cfg);
gboolean gtk2_keyboard_handle_key_press(GtkWidget *, GdkEventKey *, gpointer);
gboolean gtk2_keyboard_handle_key_release(GtkWidget *, GdkEventKey *, gpointer);

gboolean gtk2_dummy_keypress(GtkWidget *, GdkEventKey *, gpointer);
gboolean gtk2_handle_key_press(GtkWidget *, GdkEventKey *, gpointer);
gboolean gtk2_handle_key_release(GtkWidget *, GdkEventKey *, gpointer);
gboolean gtk2_handle_motion_notify(GtkWidget *, GdkEventMotion *, gpointer);
gboolean gtk2_handle_button_press(GtkWidget *, GdkEventButton *, gpointer);
gboolean gtk2_handle_button_release(GtkWidget *, GdkEventButton *, gpointer);

extern struct joystick_module *gtk2_js_modlist[];

void gtk2_joystick_init(struct ui_gtk2_interface *uigtk2);

// Wrappers for notify-only updating of UI elements.  Blocks callback so that
// no further action is taken.

void uigtk2_notify_toggle_button_set(GtkToggleButton *o, gboolean v,
				     gpointer func, gpointer data);

void uigtk2_notify_toggle_action_set(GtkToggleAction *o, gboolean v,
				     gpointer func, gpointer data);

void uigtk2_notify_radio_action_set(GtkRadioAction *o, gint v, gpointer func, gpointer data);

void uigtk2_notify_spin_button_set(GtkSpinButton *spin_button, gdouble value,
                                   gpointer func, gpointer data);

// This function doesn't exist in GTK+ 2, but does in later versions:
GtkBuilder *gtk_builder_new_from_resource(const gchar *path);

#ifndef GLIB_VERSION_2_50
#define g_abort() abort()
#endif

#endif
