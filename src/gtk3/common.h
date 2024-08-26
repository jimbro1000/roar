/** \file
 *
 *  \brief GTK+ 3 user-interface common functions.
 *
 *  \copyright Copyright 2014-2024 Ciaran Anscomb
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

#ifndef XROAR_GTK3_COMMON_H_
#define XROAR_GTK3_COMMON_H_

#include <stdint.h>

#include <glib.h>
#include <gtk/gtk.h>

#include "events.h"
#include "ui.h"
#include "vo.h"

#define GTK_KBD_JS_MAX_AXES (4)
#define GTK_KBD_JS_MAX_BUTTONS (4)

struct gtk_kbd_js_axis;
struct gtk_kbd_js_button;

struct ui_gtk3_interface {
	struct ui_interface public;

	struct ui_cfg *cfg;

	// UI builders
	GtkBuilder *builder;
	GtkUIManager *menu_manager;

	// Top window
	GtkWidget *top_window;
	GdkDisplay *display;  // Display for top window
	GtkWidget *drawing_area;

	// Menubar
	GtkWidget *menubar;

	// Dynamic menus
	GtkActionGroup *machine_action_group;
	guint merge_machines;
	GtkActionGroup *cart_action_group;
	guint merge_carts;
	GtkActionGroup *joy_right_action_group;
	guint merge_right_joysticks;
	GtkActionGroup *joy_left_action_group;
	guint merge_left_joysticks;

	// Window geometry
	_Bool user_specified_geometry;

	// Printer state
	struct {
		char *pipe;
	} printer;

	// Keyboard state
	struct {
		// Is a non-preempted control key pressed?
		_Bool control;
		struct gtk_kbd_js_axis *enabled_axis[GTK_KBD_JS_MAX_AXES];
		struct gtk_kbd_js_button *enabled_button[GTK_KBD_JS_MAX_BUTTONS];
	} keyboard;

	// Cursor hiding
	_Bool cursor_hidden;
	GdkCursor *old_cursor;
	GdkCursor *blank_cursor;

};

// Eventually, everything should be delegated properly, but for now assure
// there is only ever one instantiation of ui_gtk3 and make it available
// globally.
extern struct ui_gtk3_interface *global_uigtk3;

_Bool gtk3_vo_init(struct ui_gtk3_interface *);

void gtk3_keyboard_init(struct ui_cfg *ui_cfg);
gboolean gtk3_keyboard_handle_key_press(GtkWidget *, GdkEventKey *, gpointer);
gboolean gtk3_keyboard_handle_key_release(GtkWidget *, GdkEventKey *, gpointer);

gboolean gtk3_dummy_keypress(GtkWidget *, GdkEventKey *, gpointer);
gboolean gtk3_handle_key_press(GtkWidget *, GdkEventKey *, gpointer);
gboolean gtk3_handle_key_release(GtkWidget *, GdkEventKey *, gpointer);
gboolean gtk3_handle_motion_notify(GtkWidget *, GdkEventMotion *, gpointer);
gboolean gtk3_handle_button_press(GtkWidget *, GdkEventButton *, gpointer);
gboolean gtk3_handle_button_release(GtkWidget *, GdkEventButton *, gpointer);

extern struct joystick_module *gtk3_js_modlist[];

#ifndef GLIB_VERSION_2_50
#define g_abort() abort()
#endif

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// UI builder helpers

void uigtk3_add_from_resource(struct ui_gtk3_interface *uigtk3, const gchar *path);

void do_uigtk3_signal_connect(struct ui_gtk3_interface *uigtk3, const gchar *o_name,
			      const gchar *detailed_signal,
			      GCallback c_handler,
			      gpointer data);

#define uigtk3_signal_connect(uigtk3, o_name, detailed_signal, c_handler, data) \
	do_uigtk3_signal_connect((uigtk3), (o_name), \
				 (detailed_signal), G_CALLBACK(c_handler), (data))

// Notify-only menu manager update helpers.
//
// Blocks callback so that no further action is taken.

void uigtk3_notify_radio_action_set_current_value(struct ui_gtk3_interface *uigtk3,
						  const gchar *path, gint v, gpointer func);

void uigtk3_notify_toggle_action_set_active(struct ui_gtk3_interface *uigtk3,
					    const gchar *path, gboolean v, gpointer func);

// Notify-only UI update helpers.
//
// Blocks callback so that no further action is taken.

void uigtk3_notify_spin_button_set_value(struct ui_gtk3_interface *uigtk3,
					 const gchar *sb_name, gdouble value, gpointer func);

void uigtk3_notify_toggle_button_set_active(struct ui_gtk3_interface *uigtk3,
					    const gchar *tb_name,
					    gboolean v, gpointer func);

// Menu manager update helpers

gboolean uigtk3_toggle_action_get_active(struct ui_gtk3_interface *uigtk3, const gchar *path);

void uigtk3_toggle_action_set_active(struct ui_gtk3_interface *uigtk3, const gchar *path,
				     gboolean v);

// UI update helpers

void uigtk3_adjustment_set_lower(struct ui_gtk3_interface *uigtk3, const gchar *a_name,
				 gdouble lower);

void uigtk3_adjustment_set_upper(struct ui_gtk3_interface *uigtk3, const gchar *a_name,
				 gdouble upper);

void uigtk3_adjustment_set_value(struct ui_gtk3_interface *uigtk3, const gchar *a_name,
				 gdouble value);

void uigtk3_combo_box_set_active(struct ui_gtk3_interface *uigtk3, const gchar *cbt_name,
				 gint index_);

void uitgk3_editable_set_editable(struct ui_gtk3_interface *uigtk3, const gchar *e_name,
				  gboolean is_editable);

void uigtk3_label_set_text(struct ui_gtk3_interface *uigtk3, const gchar *l_name,
			   const gchar *str);

void uigtk3_toggle_button_set_active(struct ui_gtk3_interface *uigtk3, const gchar *tb_name,
                                     gboolean v);

void uigtk3_widget_hide(struct ui_gtk3_interface *uigtk3, const gchar *w_name);

void uigtk3_widget_set_sensitive(struct ui_gtk3_interface *uigtk3, const gchar *w_name,
				 gboolean sensitive);

void uigtk3_widget_show(struct ui_gtk3_interface *uigtk3, const gchar *w_name);

#endif
