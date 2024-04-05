/** \file
 *
 *  \brief GTK+ 2 drive control window.
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

#include "vdisk.h"
#include "vdrive.h"
#include "xroar.h"

#include "gtk2/common.h"
#include "gtk2/drivecontrol.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

const char *label_filename_drive[4] = {
	"filename_drive1", "filename_drive2", "filename_drive3", "filename_drive4"
};

const char *tb_we_drive[4] = {
	"we_drive1", "we_drive2", "we_drive3", "we_drive4"
};

const char *tb_wb_drive[4] = {
	"wb_drive1", "wb_drive2", "wb_drive3", "wb_drive4"
};

// UI updates
static void update_drive_cyl_head(void *sptr, unsigned drive, unsigned cyl, unsigned head);

// Callbacks
static gboolean hide_dc_window(GtkWidget *widget, GdkEvent *event, gpointer user_data);
static void dc_insert(GtkButton *button, gpointer user_data);
static void dc_eject(GtkButton *button, gpointer user_data);
static void dc_toggled_we(GtkToggleButton *togglebutton, gpointer user_data);
static void dc_toggled_wb(GtkToggleButton *togglebutton, gpointer user_data);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Floppy dialog - create window

void gtk2_create_dc_window(struct ui_gtk2_interface *uigtk2) {
	uigtk2_add_from_resource(uigtk2, "/uk/org/6809/xroar/gtk2/drivecontrol.ui");

	// Connect signals
	uigtk2_signal_connect(uigtk2, "dc_window", "delete-event", hide_dc_window, uigtk2);
	uigtk2_signal_connect(uigtk2, "dc_window", "key-press-event", gtk2_dummy_keypress, uigtk2);
	for (int i = 0; i < 4; i++) {
		uigtk2_signal_connect(uigtk2, tb_we_drive[i], "toggled", G_CALLBACK(dc_toggled_we), (gpointer)(intptr_t)i);
		uigtk2_signal_connect(uigtk2, tb_wb_drive[i], "toggled", G_CALLBACK(dc_toggled_wb), (gpointer)(intptr_t)i);
	}
	uigtk2_signal_connect(uigtk2, "eject_drive1", "clicked", dc_eject, (gpointer)(intptr_t)0);
	uigtk2_signal_connect(uigtk2, "eject_drive2", "clicked", dc_eject, (gpointer)(intptr_t)1);
	uigtk2_signal_connect(uigtk2, "eject_drive3", "clicked", dc_eject, (gpointer)(intptr_t)2);
	uigtk2_signal_connect(uigtk2, "eject_drive4", "clicked", dc_eject, (gpointer)(intptr_t)3);
	uigtk2_signal_connect(uigtk2, "insert_drive1", "clicked", dc_insert, (gpointer)(intptr_t)0);
	uigtk2_signal_connect(uigtk2, "insert_drive2", "clicked", dc_insert, (gpointer)(intptr_t)1);
	uigtk2_signal_connect(uigtk2, "insert_drive3", "clicked", dc_insert, (gpointer)(intptr_t)2);
	uigtk2_signal_connect(uigtk2, "insert_drive4", "clicked", dc_insert, (gpointer)(intptr_t)3);

	xroar.vdrive_interface->update_drive_cyl_head = DELEGATE_AS3(void, unsigned, unsigned, unsigned, update_drive_cyl_head, uigtk2);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Floppy dialog - insert disk

void gtk2_insert_disk(struct ui_gtk2_interface *uigtk2, int drive) {
	static GtkFileChooser *file_dialog = NULL;
	static GtkComboBox *drive_combo = NULL;
	if (!file_dialog) {
		file_dialog = GTK_FILE_CHOOSER(
		    gtk_file_chooser_dialog_new("Insert Disk",
			GTK_WINDOW(uigtk2->top_window),
			GTK_FILE_CHOOSER_ACTION_OPEN, GTK_STOCK_CANCEL,
			GTK_RESPONSE_CANCEL, GTK_STOCK_OPEN,
			GTK_RESPONSE_ACCEPT, NULL));
	}
	if (!drive_combo) {
		drive_combo = (GtkComboBox *)gtk_combo_box_text_new();
		gtk_combo_box_text_append_text((GtkComboBoxText *)drive_combo, "Drive 1");
		gtk_combo_box_text_append_text((GtkComboBoxText *)drive_combo, "Drive 2");
		gtk_combo_box_text_append_text((GtkComboBoxText *)drive_combo, "Drive 3");
		gtk_combo_box_text_append_text((GtkComboBoxText *)drive_combo, "Drive 4");
		gtk_file_chooser_set_extra_widget(file_dialog, GTK_WIDGET(drive_combo));
		gtk_combo_box_set_active(drive_combo, 0);
	}
	if (drive >= 0 && drive <= 3) {
		gtk_combo_box_set_active(drive_combo, drive);
	}
	if (gtk_dialog_run(GTK_DIALOG(file_dialog)) == GTK_RESPONSE_ACCEPT) {
		char *filename = gtk_file_chooser_get_filename(file_dialog);
		drive = gtk_combo_box_get_active(drive_combo);
		if (drive < 0 || drive > 3)
			drive = 0;
		if (filename) {
			xroar_insert_disk_file(drive, filename);
			g_free(filename);
		}
	}
	gtk_widget_hide(GTK_WIDGET(file_dialog));
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Floppy dialog - UI callbacks

void gtk2_update_drive_write_enable(struct ui_gtk2_interface *uigtk2, int drive, _Bool write_enable) {
	if (drive >= 0 && drive <= 3) {
		uigtk2_toggle_button_set_active(uigtk2, tb_we_drive[drive], write_enable ? TRUE : FALSE);
	}
}

void gtk2_update_drive_write_back(struct ui_gtk2_interface *uigtk2, int drive, _Bool write_back) {
	if (drive >= 0 && drive <= 3) {
		uigtk2_toggle_button_set_active(uigtk2, tb_wb_drive[drive], write_back ? TRUE : FALSE);
	}
}

void gtk2_update_drive_disk(struct ui_gtk2_interface *uigtk2, int drive, const struct vdisk *disk) {
	if (drive < 0 || drive > 3)
		return;
	char *filename = NULL;
	_Bool we = 0, wb = 0;
	if (disk) {
		filename = disk->filename;
		we = !disk->write_protect;
		wb = disk->write_back;
	}

	uigtk2_label_set_text(uigtk2, label_filename_drive[drive], filename);
	gtk2_update_drive_write_enable(uigtk2, drive, we);
	gtk2_update_drive_write_back(uigtk2, drive, wb);
}

static void update_drive_cyl_head(void *sptr, unsigned drive, unsigned cyl, unsigned head) {
	struct ui_gtk2_interface *uigtk2 = sptr;
	char string[16];
	snprintf(string, sizeof(string), "Dr %01u Tr %02u He %01u", drive + 1, cyl, head);
	uigtk2_label_set_text(uigtk2, "drive_cyl_head", string);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Floppy dialog - signal handlers

void gtk2_toggle_dc_window(GtkToggleAction *current, gpointer user_data) {
	struct ui_gtk2_interface *uigtk2 = user_data;
	if (gtk_toggle_action_get_active(current)) {
		uigtk2_widget_show(uigtk2, "dc_window");
	} else {
		uigtk2_widget_hide(uigtk2, "dc_window");
	}
}

static gboolean hide_dc_window(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
	(void)widget;
	(void)event;
	struct ui_gtk2_interface *uigtk2 = user_data;
	uigtk2_toggle_action_set_active(uigtk2, "/MainMenu/FileMenu/DriveControl", 0);
	uigtk2_widget_hide(uigtk2, "dc_window");
	return TRUE;
}

static void dc_insert(GtkButton *button, gpointer user_data) {
	int drive = (intptr_t)user_data;
	(void)button;
	xroar_insert_disk(drive);
}

static void dc_eject(GtkButton *button, gpointer user_data) {
	int drive = (intptr_t)user_data;
	(void)button;
	xroar_eject_disk(drive);
}

static void dc_toggled_we(GtkToggleButton *togglebutton, gpointer user_data) {
	int set = gtk_toggle_button_get_active(togglebutton) ? 1 : 0;
	int drive = (intptr_t)user_data;
	xroar_set_write_enable(0, drive, set);
}

static void dc_toggled_wb(GtkToggleButton *togglebutton, gpointer user_data) {
	int set = gtk_toggle_button_get_active(togglebutton) ? 1 : 0;
	int drive = (intptr_t)user_data;
	xroar_set_write_back(0, drive, set);
}
