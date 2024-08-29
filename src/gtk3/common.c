/** \file
 *
 *  \brief GTK+ 3 user-interface common functions.
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

#include <ctype.h>

#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "xalloc.h"

#include "auto_kbd.h"
#include "xroar.h"

#include "gtk3/common.h"

// Eventually, everything should be delegated properly, but for now assure
// there is only ever one instantiation of ui_gtk3 and make it available
// globally.
struct ui_gtk3_interface *global_uigtk3 = NULL;

// Event handlers

// Used within tape/drive control dialogs to eat keypresses but still allow GUI
// controls.

gboolean gtk3_dummy_keypress(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
	(void)widget;
	struct ui_gtk3_interface *uigtk3 = user_data;

	if (gtk_window_activate_key(GTK_WINDOW(uigtk3->top_window), event) == TRUE) {
		return TRUE;
	}

	return FALSE;
}

// Key press/release

gboolean gtk3_handle_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
	struct ui_gtk3_interface *uigtk3 = user_data;

#ifndef WINDOWS32
	// Hide cursor
	if (!uigtk3->cursor_hidden) {
		GdkWindow *window = gtk_widget_get_window(uigtk3->drawing_area);
		uigtk3->old_cursor = gdk_window_get_cursor(window);
		gdk_window_set_cursor(window, uigtk3->blank_cursor);
		uigtk3->cursor_hidden = 1;
	}
#endif

	// Pass off to keyboard code
	return gtk3_keyboard_handle_key_press(widget, event, user_data);
}

gboolean gtk3_handle_key_release(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
	// Pass off to keyboard code
	return gtk3_keyboard_handle_key_release(widget, event, user_data);
}

// Pointer motion

gboolean gtk3_handle_motion_notify(GtkWidget *widget, GdkEventMotion *event, gpointer user_data) {
	struct ui_gtk3_interface *uigtk3 = user_data;
	struct vo_interface *vo = uigtk3->public.vo_interface;
	(void)widget;

#ifndef WINDOWS32
	// Unhide cursor
	if (uigtk3->cursor_hidden) {
		GdkWindow *window = gtk_widget_get_window(uigtk3->drawing_area);
		gdk_window_set_cursor(window, uigtk3->old_cursor);
		uigtk3->cursor_hidden = 0;
	}
#endif

	// Update position data (for mouse mapped joystick)
	vo->mouse.axis[0] = event->x;
	vo->mouse.axis[1] = event->y;

	return FALSE;
}

// Button press/release

static void clipboard_text_received(GtkClipboard *clipboard, const gchar *text, gpointer data) {
	(void)clipboard;
	(void)data;
	if (!text)
		return;
	char *ntext = xstrdup(text);
	if (!ntext)
		return;
	guint state = (uintptr_t)data;
	_Bool uc = state & GDK_SHIFT_MASK;
	for (char *p = ntext; *p; p++) {
		if (*p == '\n')
			*p = '\r';
		if (uc)
			*p = toupper(*p);
	}
	ak_parse_type_string(xroar.auto_kbd, ntext);
	free(ntext);
}

gboolean gtk3_handle_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
	struct ui_gtk3_interface *uigtk3 = user_data;
	struct vo_interface *vo = uigtk3->public.vo_interface;
	(void)widget;

	if (event->button == 2) {
		GdkDisplay *d = gtk_widget_get_display(uigtk3->top_window);
		GtkClipboard *cb = gtk_clipboard_get_for_display(d, GDK_SELECTION_PRIMARY);
		gtk_clipboard_request_text(cb, clipboard_text_received, (gpointer)(uintptr_t)event->state);
		return FALSE;
	}

	// Update button data (for mouse mapped joystick)
	if (event->button >= 1 && event->button <= 3) {
		vo->mouse.button[event->button-1] = 1;
	}

	return FALSE;
}

gboolean gtk3_handle_button_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
	struct ui_gtk3_interface *uigtk3 = user_data;
	struct vo_interface *vo = uigtk3->public.vo_interface;
	(void)widget;

	// Update button data (for mouse mapped joystick)
	if (event->button >= 1 && event->button <= 3) {
		vo->mouse.button[event->button-1] = 0;
	}

	return FALSE;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// UI builder helpers

FUNC_ATTR_NORETURN static void do_g_abort(const gchar *format, GError *error) {
	(void)format;
	if (error) {
		g_message("gtk_builder_add_from_resource() failed: %s", error->message);
		g_error_free(error);
	}
	g_abort();
}

void uigtk3_add_from_resource(struct ui_gtk3_interface *uigtk3, const gchar *path) {
	GError *error = NULL;
	GBytes *resource = g_resources_lookup_data(path, 0, &error);
	if (!resource) {
		do_g_abort("g_resources_lookup_data() failed: %s", error);
	}

	gsize xml_size;
	const gchar *xml = g_bytes_get_data(resource, &xml_size);

	if (gtk_builder_add_from_string(uigtk3->builder, xml, xml_size, &error) == 0) {
		do_g_abort("gtk_builder_add_from_string() failed: %s", error);
	}

	g_bytes_unref(resource);
}

void do_uigtk3_signal_connect(struct ui_gtk3_interface *uigtk3, const gchar *o_name,
			      const gchar *detailed_signal,
			      GCallback c_handler,
			      gpointer data) {
	GObject *o = gtk_builder_get_object(uigtk3->builder, o_name);
	g_signal_connect(o, detailed_signal, c_handler, data);
}

// Notify-only menu manager update helpers.
//
// Blocks callback so that no further action is taken.

void uigtk3_notify_radio_action_set_current_value(struct ui_gtk3_interface *uigtk3,
						  const gchar *path, gint v, gpointer func) {
	GtkRadioAction *ra = GTK_RADIO_ACTION(gtk_ui_manager_get_action(uigtk3->menu_manager, path));
	g_signal_handlers_block_by_func(ra, G_CALLBACK(func), uigtk3);
	gtk_radio_action_set_current_value(ra, v);
	g_signal_handlers_unblock_by_func(ra, G_CALLBACK(func), uigtk3);
}

void uigtk3_notify_toggle_action_set_active(struct ui_gtk3_interface *uigtk3,
					    const gchar *path, gboolean v, gpointer func) {
	GtkToggleAction *ta = GTK_TOGGLE_ACTION(gtk_ui_manager_get_action(uigtk3->menu_manager, path));
	g_signal_handlers_block_by_func(ta, G_CALLBACK(func), uigtk3);
	gtk_toggle_action_set_active(ta, v);
	g_signal_handlers_unblock_by_func(ta, G_CALLBACK(func), uigtk3);
}

// Notify-only UI update helpers.
//
// Blocks callback so that no further action is taken.

void uigtk3_notify_spin_button_set_value(struct ui_gtk3_interface *uigtk3,
					 const gchar *sb_name, gdouble value, gpointer func) {
	GtkSpinButton *sb = GTK_SPIN_BUTTON(gtk_builder_get_object(uigtk3->builder, sb_name));
	g_signal_handlers_block_by_func(sb, G_CALLBACK(func), uigtk3);
	gtk_spin_button_set_value(sb, value);
	g_signal_handlers_unblock_by_func(sb, G_CALLBACK(func), uigtk3);
}

void uigtk3_notify_toggle_button_set_active(struct ui_gtk3_interface *uigtk3,
					    const gchar *tb_name,
					    gboolean v, gpointer func) {
	GtkToggleButton *tb = GTK_TOGGLE_BUTTON(gtk_builder_get_object(uigtk3->builder, tb_name));
	g_signal_handlers_block_by_func(tb, G_CALLBACK(func), uigtk3);
	gtk_toggle_button_set_active(tb, v);
	g_signal_handlers_unblock_by_func(tb, G_CALLBACK(func), uigtk3);
}

// Menu manager helpers

gboolean uigtk3_toggle_action_get_active(struct ui_gtk3_interface *uigtk3, const gchar *path) {
	GtkToggleAction *ta = GTK_TOGGLE_ACTION(gtk_ui_manager_get_action(uigtk3->menu_manager, path));
	return gtk_toggle_action_get_active(ta);
}

void uigtk3_toggle_action_set_active(struct ui_gtk3_interface *uigtk3, const gchar *path,
				     gboolean v) {
	GtkToggleAction *ta = GTK_TOGGLE_ACTION(gtk_ui_manager_get_action(uigtk3->menu_manager, path));
	gtk_toggle_action_set_active(ta, v);
}

// UI helpers

void uigtk3_adjustment_set_lower(struct ui_gtk3_interface *uigtk3, const gchar *a_name,
				 gdouble lower) {
	GtkAdjustment *a = GTK_ADJUSTMENT(gtk_builder_get_object(uigtk3->builder, a_name));
	gtk_adjustment_set_lower(a, lower);
}

void uigtk3_adjustment_set_upper(struct ui_gtk3_interface *uigtk3, const gchar *a_name,
				 gdouble upper) {
	GtkAdjustment *a = GTK_ADJUSTMENT(gtk_builder_get_object(uigtk3->builder, a_name));
	gtk_adjustment_set_upper(a, upper);
}

void uigtk3_adjustment_set_value(struct ui_gtk3_interface *uigtk3, const gchar *a_name,
				 gdouble value) {
	GtkAdjustment *a = GTK_ADJUSTMENT(gtk_builder_get_object(uigtk3->builder, a_name));
	gtk_adjustment_set_value(a, value);
}

void uigtk3_combo_box_set_active(struct ui_gtk3_interface *uigtk3, const gchar *cbt_name,
				 gint index_) {
	GtkComboBoxText *cbt = GTK_COMBO_BOX_TEXT(gtk_builder_get_object(uigtk3->builder, cbt_name));
	gtk_combo_box_set_active(GTK_COMBO_BOX(cbt), index_);
}

void uigtk3_editable_set_editable(struct ui_gtk3_interface *uigtk3, const gchar *e_name,
				 gboolean is_editable) {
	GtkEditable *e = GTK_EDITABLE(gtk_builder_get_object(uigtk3->builder, e_name));
	gtk_editable_set_editable(e, is_editable);
}

void uigtk3_label_set_text(struct ui_gtk3_interface *uigtk3, const gchar *l_name,
			   const gchar *str) {
	GtkLabel *l = GTK_LABEL(gtk_builder_get_object(uigtk3->builder, l_name));
	gtk_label_set_text(l, str);
}

void uigtk3_toggle_button_set_active(struct ui_gtk3_interface *uigtk3, const gchar *tb_name,
				     gboolean v) {
	GtkToggleButton *tb = GTK_TOGGLE_BUTTON(gtk_builder_get_object(uigtk3->builder, tb_name));
	gtk_toggle_button_set_active(tb, v);
}

void uigtk3_widget_hide(struct ui_gtk3_interface *uigtk3, const gchar *w_name) {
	GtkWidget *w = GTK_WIDGET(gtk_builder_get_object(uigtk3->builder, w_name));
	gtk_widget_hide(w);
}

void uigtk3_widget_set_sensitive(struct ui_gtk3_interface *uigtk3, const gchar *w_name,
				 gboolean sensitive) {
	GtkWidget *w = GTK_WIDGET(gtk_builder_get_object(uigtk3->builder, w_name));
	gtk_widget_set_sensitive(w, sensitive);
}

void uigtk3_widget_show(struct ui_gtk3_interface *uigtk3, const gchar *w_name) {
	GtkWidget *w = GTK_WIDGET(gtk_builder_get_object(uigtk3->builder, w_name));
	gtk_widget_show(w);
}
