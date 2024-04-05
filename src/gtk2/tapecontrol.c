/** \file
 *
 *  \brief GTK+ 2 tape control window.
 *
 *  \copyright Copyright 2011-2024 Ciaran Anscomb
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <gtk/gtk.h>
#pragma GCC diagnostic pop

#include "events.h"
#include "fs.h"
#include "tape.h"
#include "vdrive.h"
#include "xroar.h"

#include "gtk2/common.h"
#include "gtk2/tapecontrol.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Column indices within the input ListStore

enum {
	TC_FILENAME = 0,
	TC_POSITION,
	TC_FILE_POINTER,
	TC_MAX
};

// UI updates
static void update_tape_counters(void *);
static struct event update_tape_counters_event;

// Callbacks
static gboolean hide_tc_window(GtkWidget *widget, GdkEvent *event, gpointer user_data);
static void tc_play(GtkButton *button, gpointer user_data);
static void tc_pause(GtkButton *button, gpointer user_data);
static void tc_input_rewind(GtkButton *button, gpointer user_data);
static void tc_output_rewind(GtkButton *button, gpointer user_data);
static void tc_input_insert(GtkButton *button, gpointer user_data);
static void tc_output_insert(GtkButton *button, gpointer user_data);
static void tc_input_eject(GtkButton *button, gpointer user_data);
static void tc_output_eject(GtkButton *button, gpointer user_data);
static void tc_toggled_fast(GtkToggleButton *togglebutton, gpointer user_data);
static void tc_toggled_pad_auto(GtkToggleButton *togglebutton, gpointer user_data);
static void tc_toggled_rewrite(GtkToggleButton *togglebutton, gpointer user_data);
static gboolean tc_input_progress_change(GtkRange *range, GtkScrollType scroll, gdouble value, gpointer user_data);
static gboolean tc_output_progress_change(GtkRange *range, GtkScrollType scroll, gdouble value, gpointer user_data);

// Helper functions
static gchar *ms_to_string(int ms);
static void input_file_selected(GtkTreeView *tree_view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer user_data);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Tape dialog - create window

void gtk2_create_tc_window(struct ui_gtk2_interface *uigtk2) {
	uigtk2_add_from_resource(uigtk2, "/uk/org/6809/xroar/gtk2/tapecontrol.ui");

	// Connect signals
	uigtk2_signal_connect(uigtk2, "tc_window", "delete-event", hide_tc_window, uigtk2);
	uigtk2_signal_connect(uigtk2, "tc_window", "key-press-event", gtk2_dummy_keypress, uigtk2);
	uigtk2_signal_connect(uigtk2, "input_file_list_view", "row-activated", input_file_selected, uigtk2);
	uigtk2_signal_connect(uigtk2, "input_file_progress", "change-value", tc_input_progress_change, uigtk2);
	uigtk2_signal_connect(uigtk2, "output_file_progress", "change-value", tc_output_progress_change, uigtk2);
	uigtk2_signal_connect(uigtk2, "input_play", "clicked", tc_play, uigtk2);
	uigtk2_signal_connect(uigtk2, "input_pause", "clicked", tc_pause, uigtk2);
	uigtk2_signal_connect(uigtk2, "output_record", "clicked", tc_play, uigtk2);
	uigtk2_signal_connect(uigtk2, "output_pause", "clicked", tc_pause, uigtk2);
	uigtk2_signal_connect(uigtk2, "fast", "toggled", tc_toggled_fast, uigtk2);
	uigtk2_signal_connect(uigtk2, "pad_auto", "toggled", tc_toggled_pad_auto, uigtk2);
	uigtk2_signal_connect(uigtk2, "rewrite", "toggled", tc_toggled_rewrite, uigtk2);
	uigtk2_signal_connect(uigtk2, "input_rewind", "clicked", tc_input_rewind, uigtk2);
	uigtk2_signal_connect(uigtk2, "input_insert", "clicked", tc_input_insert, uigtk2);
	uigtk2_signal_connect(uigtk2, "input_eject", "clicked", tc_input_eject, uigtk2);
	uigtk2_signal_connect(uigtk2, "output_rewind", "clicked", tc_output_rewind, uigtk2);
	uigtk2_signal_connect(uigtk2, "output_insert", "clicked", tc_output_insert, uigtk2);
	uigtk2_signal_connect(uigtk2, "output_eject", "clicked", tc_output_eject, uigtk2);

	// Events
	event_init(&update_tape_counters_event, DELEGATE_AS0(void, update_tape_counters, uigtk2));
	update_tape_counters_event.at_tick = event_current_tick + EVENT_MS(500);
	event_queue(&UI_EVENT_LIST, &update_tape_counters_event);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Tape dialog - helper functions

static void update_input_list_store(struct ui_gtk2_interface *uigtk2) {
	GtkListStore *tc_input_list_store = GTK_LIST_STORE(gtk_builder_get_object(uigtk2->builder, "input_file_list_store"));

	// If there's anything in the tree already, don't scan it again
	GtkTreeIter iter;
	if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(tc_input_list_store), &iter)) {
		return;
	}

	if (!xroar.tape_interface || !xroar.tape_interface->tape_input) {
		return;
	}

	struct tape_file *file;
	long old_offset = tape_tell(xroar.tape_interface->tape_input);
	tape_rewind(xroar.tape_interface->tape_input);
	while ((file = tape_file_next(xroar.tape_interface->tape_input, 1))) {
		int ms = tape_to_ms(xroar.tape_interface->tape_input, file->offset);
		gchar *timestr = ms_to_string(ms);
		gtk_list_store_append(tc_input_list_store, &iter);
		gtk_list_store_set(tc_input_list_store, &iter,
				   TC_FILENAME, file->name,
				   TC_POSITION, timestr,
				   TC_FILE_POINTER, file,
				   -1);
	}
	tape_seek(xroar.tape_interface->tape_input, old_offset, SEEK_SET);
}

static gchar *ms_to_string(int ms) {
	static gchar timestr[9];
	int min, sec;
	sec = ms / 1000;
	min = sec / 60;
	sec %= 60;
	min %= 60;
	snprintf(timestr, sizeof(timestr), "%02d:%02d", min, sec);
	return timestr;
}

static void input_file_selected(GtkTreeView *tree_view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer user_data) {
	(void)tree_view;
	(void)column;
	struct ui_gtk2_interface *uigtk2 = user_data;

	GtkListStore *tc_input_list_store = GTK_LIST_STORE(gtk_builder_get_object(uigtk2->builder, "input_file_list_store"));

	GtkTreeIter iter;
	struct tape_file *file;
	gtk_tree_model_get_iter(GTK_TREE_MODEL(tc_input_list_store), &iter, path);
	gtk_tree_model_get(GTK_TREE_MODEL(tc_input_list_store), &iter, TC_FILE_POINTER, &file, -1);
	tape_seek_to_file(xroar.tape_interface->tape_input, file);
}

static void tc_seek(struct tape *tape, GtkScrollType scroll, gdouble value) {
	if (tape) {
		int seekms = 0;
		switch (scroll) {
			case GTK_SCROLL_STEP_BACKWARD:
				seekms = tape_to_ms(tape, tape->offset) - 1000;
				break;
			case GTK_SCROLL_STEP_FORWARD:
				seekms = tape_to_ms(tape, tape->offset) + 1000;
				break;
			case GTK_SCROLL_PAGE_BACKWARD:
				seekms = tape_to_ms(tape, tape->offset) - 5000;
				break;
			case GTK_SCROLL_PAGE_FORWARD:
				seekms = tape_to_ms(tape, tape->offset) + 5000;
				break;
			case GTK_SCROLL_JUMP:
				seekms = (int)value;
				break;
			default:
				return;
		}
		if (seekms < 0) return;
		long seek_to = tape_ms_to(tape, seekms);
		if (seek_to > tape->size) seek_to = tape->size;
		tape_seek(tape, seek_to, SEEK_SET);
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Tape dialog - scheduled event handlers

static void update_tape_counters(void *sptr) {
	struct ui_gtk2_interface *uigtk2 = sptr;

	static long omax = -1, opos = -1;
	static long imax = -1, ipos = -1;
	long new_omax = 0, new_opos = 0;
	long new_imax = 0, new_ipos = 0;

	if (xroar.tape_interface->tape_input) {
		new_imax = tape_to_ms(xroar.tape_interface->tape_input, xroar.tape_interface->tape_input->size);
		new_ipos = tape_to_ms(xroar.tape_interface->tape_input, xroar.tape_interface->tape_input->offset);
	}
	if (xroar.tape_interface->tape_output) {
		new_omax = tape_to_ms(xroar.tape_interface->tape_output, xroar.tape_interface->tape_output->size);
		new_opos = tape_to_ms(xroar.tape_interface->tape_output, xroar.tape_interface->tape_output->offset);
	}
	if (imax != new_imax) {
		imax = new_imax;
		uigtk2_adjustment_set_upper(uigtk2, "input_file_adjustment", (gdouble)imax);
	}
	if (ipos != new_ipos) {
		ipos = new_ipos;
		uigtk2_adjustment_set_value(uigtk2, "input_file_adjustment", (gdouble)ipos);
		uigtk2_label_set_text(uigtk2, "input_file_time", ms_to_string(new_ipos));
	}
	if (omax != new_omax) {
		omax = new_omax;
		uigtk2_adjustment_set_upper(uigtk2, "output_file_adjustment", (gdouble)omax);
	}
	if (opos != new_opos) {
		opos = new_opos;
		uigtk2_adjustment_set_value(uigtk2, "output_file_adjustment", (gdouble)opos);
		uigtk2_label_set_text(uigtk2, "output_file_time", ms_to_string(new_opos));
	}
	update_tape_counters_event.at_tick += EVENT_MS(500);
	event_queue(&UI_EVENT_LIST, &update_tape_counters_event);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Tape dialog - UI callbacks

void gtk2_update_tape_state(struct ui_gtk2_interface *uigtk2, int flags) {
	uigtk2_notify_toggle_button_set_active(uigtk2, "fast", (flags & TAPE_FAST) ? TRUE : FALSE, tc_toggled_fast);
	uigtk2_notify_toggle_button_set_active(uigtk2, "pad_auto", (flags & TAPE_PAD_AUTO) ? TRUE : FALSE, tc_toggled_pad_auto);
	uigtk2_notify_toggle_button_set_active(uigtk2, "rewrite", (flags & TAPE_REWRITE) ? TRUE : FALSE, tc_toggled_rewrite);
}

void gtk2_input_tape_filename_cb(struct ui_gtk2_interface *uigtk2, const char *filename) {
	GtkListStore *tc_input_list_store = GTK_LIST_STORE(gtk_builder_get_object(uigtk2->builder, "input_file_list_store"));

	uigtk2_label_set_text(uigtk2, "input_filename", filename);
	GtkTreeIter iter;
	if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(tc_input_list_store), &iter)) {
		do {
			struct tape_file *file;
			gtk_tree_model_get(GTK_TREE_MODEL(tc_input_list_store), &iter, TC_FILE_POINTER, &file, -1);
			g_free(file);
		} while (gtk_tree_model_iter_next(GTK_TREE_MODEL(tc_input_list_store), &iter));
	}
	gtk_list_store_clear(tc_input_list_store);
	if (uigtk2_toggle_action_get_active(uigtk2, "/MainMenu/FileMenu/TapeControl")) {
		update_input_list_store(uigtk2);
	}
}

void gtk2_output_tape_filename_cb(struct ui_gtk2_interface *uigtk2, const char *filename) {
	uigtk2_label_set_text(uigtk2, "output_filename", filename);
}

static void tc_toggled_fast(GtkToggleButton *togglebutton, gpointer user_data) {
	(void)user_data;
	int set = gtk_toggle_button_get_active(togglebutton) ? TAPE_FAST : 0;
	int flags = (tape_get_state(xroar.tape_interface) & ~TAPE_FAST) | set;
	tape_set_state(xroar.tape_interface, flags);
}

static void tc_toggled_pad_auto(GtkToggleButton *togglebutton, gpointer user_data) {
	(void)user_data;
	int set = gtk_toggle_button_get_active(togglebutton) ? TAPE_PAD_AUTO : 0;
	int flags = (tape_get_state(xroar.tape_interface) & ~TAPE_PAD_AUTO) | set;
	tape_set_state(xroar.tape_interface, flags);
}

static void tc_toggled_rewrite(GtkToggleButton *togglebutton, gpointer user_data) {
	(void)user_data;
	int set = gtk_toggle_button_get_active(togglebutton) ? TAPE_REWRITE : 0;
	int flags = (tape_get_state(xroar.tape_interface) & ~TAPE_REWRITE) | set;
	tape_set_state(xroar.tape_interface, flags);
}

void gtk2_update_tape_playing(struct ui_gtk2_interface *uigtk2, int playing) {
	uigtk2_widget_set_sensitive(uigtk2, "input_play", !playing);
	uigtk2_widget_set_sensitive(uigtk2, "input_pause", playing);
	uigtk2_widget_set_sensitive(uigtk2, "output_record", !playing);
	uigtk2_widget_set_sensitive(uigtk2, "output_pause", playing);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Tape dialog - signal handlers

void gtk2_toggle_tc_window(GtkToggleAction *current, gpointer user_data) {
	struct ui_gtk2_interface *uigtk2 = user_data;
	if (gtk_toggle_action_get_active(current)) {
		uigtk2_widget_show(uigtk2, "tc_window");
		update_input_list_store(uigtk2);
	} else {
		uigtk2_widget_hide(uigtk2, "tc_window");
	}
}

static gboolean hide_tc_window(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
	(void)widget;
	(void)event;
	struct ui_gtk2_interface *uigtk2 = user_data;
	uigtk2_toggle_action_set_active(uigtk2, "/MainMenu/FileMenu/TapeControl", 0);
	uigtk2_widget_hide(uigtk2, "tc_window");
	return TRUE;
}

// Tape dialog - signal handlers - input tab

static gboolean tc_input_progress_change(GtkRange *range, GtkScrollType scroll, gdouble value, gpointer user_data) {
	(void)range;
	(void)user_data;
	tc_seek(xroar.tape_interface->tape_input, scroll, value);
	return TRUE;
}

static void tc_play(GtkButton *button, gpointer user_data) {
	(void)button;
	(void)user_data;
	tape_set_playing(xroar.tape_interface, 1, 1);
}

static void tc_pause(GtkButton *button, gpointer user_data) {
	(void)button;
	(void)user_data;
	tape_set_playing(xroar.tape_interface, 0, 1);
}

static void tc_input_rewind(GtkButton *button, gpointer user_data) {
	(void)button;
	(void)user_data;
	if (xroar.tape_interface->tape_input) {
		tape_seek(xroar.tape_interface->tape_input, 0, SEEK_SET);
	}
}

static void tc_input_insert(GtkButton *button, gpointer user_data) {
	(void)button;
	(void)user_data;
	xroar_insert_input_tape();
}

static void tc_input_eject(GtkButton *button, gpointer user_data) {
	(void)button;
	(void)user_data;
	xroar_eject_input_tape();
}

// Tape dialog - signal handlers - output tab

static gboolean tc_output_progress_change(GtkRange *range, GtkScrollType scroll, gdouble value, gpointer user_data) {
	(void)range;
	(void)user_data;
	tc_seek(xroar.tape_interface->tape_output, scroll, value);
	return TRUE;
}

static void tc_output_rewind(GtkButton *button, gpointer user_data) {
	(void)button;
	(void)user_data;
	if (xroar.tape_interface && xroar.tape_interface->tape_output) {
		tape_seek(xroar.tape_interface->tape_output, 0, SEEK_SET);
	}
}

static void tc_output_insert(GtkButton *button, gpointer user_data) {
	(void)button;
	(void)user_data;
	xroar_insert_output_tape();
}

static void tc_output_eject(GtkButton *button, gpointer user_data) {
	(void)button;
	(void)user_data;
	xroar_eject_output_tape();
}
