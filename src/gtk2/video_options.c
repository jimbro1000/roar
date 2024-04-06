/** \file
 *
 *  \brief GTK+ 2 video options window.
 *
 *  \copyright Copyright 2023-2024 Ciaran Anscomb
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

#include "ao.h"
#include "sound.h"
#include "vo.h"
#include "xroar.h"

#include "gtk2/common.h"
#include "gtk2/video_options.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Callbacks
static void vo_change_gain(GtkSpinButton *spin_button, gpointer user_data);
static void vo_change_brightness(GtkSpinButton *spin_button, gpointer user_data);
static void vo_change_contrast(GtkSpinButton *spin_button, gpointer user_data);
static void vo_change_saturation(GtkSpinButton *spin_button, gpointer user_data);
static void vo_change_hue(GtkSpinButton *spin_button, gpointer user_data);
static void vo_change_picture(GtkComboBox *widget, gpointer user_data);
static void vo_change_ntsc_scaling(GtkToggleButton *widget, gpointer user_data);
static void vo_change_cmp_renderer(GtkComboBox *widget, gpointer user_data);
static void vo_change_cmp_fs(GtkComboBox *widget, gpointer user_data);
static void vo_change_cmp_fsc(GtkComboBox *widget, gpointer user_data);
static void vo_change_cmp_system(GtkComboBox *widget, gpointer user_data);
static void vo_change_cmp_colour_killer(GtkToggleButton *widget, gpointer user_data);

// Signal handlers
static gboolean hide_vo_window(GtkWidget *widget, GdkEvent *event, gpointer user_data);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void gtk2_vo_create_window(struct ui_gtk2_interface *uigtk2) {
	uigtk2_add_from_resource(uigtk2, "/uk/org/6809/xroar/gtk2/video_options.ui");

	// Build lists
	{
		GtkComboBoxText *cbt_picture = GTK_COMBO_BOX_TEXT(gtk_builder_get_object(uigtk2->builder, "cbt_picture"));
		for (unsigned i = 0; i < NUM_VO_PICTURE; i++) {
			gtk_combo_box_text_append_text(cbt_picture, vo_picture_name[i]);
		}
		g_signal_connect(cbt_picture, "changed", G_CALLBACK(vo_change_picture), uigtk2);
	}
	{
		GtkComboBoxText *cbt_cmp_renderer = GTK_COMBO_BOX_TEXT(gtk_builder_get_object(uigtk2->builder, "cbt_cmp_renderer"));
		for (unsigned i = 0; vo_cmp_ccr_list[i].name; i++) {
			gtk_combo_box_text_append_text(cbt_cmp_renderer, vo_cmp_ccr_list[i].description);
		}
		g_signal_connect(cbt_cmp_renderer, "changed", G_CALLBACK(vo_change_cmp_renderer), uigtk2);
	}
	{
		GtkComboBoxText *cbt_cmp_fs = GTK_COMBO_BOX_TEXT(gtk_builder_get_object(uigtk2->builder, "cbt_cmp_fs"));
		for (unsigned i = 0; i < NUM_VO_RENDER_FS; i++) {
			gtk_combo_box_text_append_text(cbt_cmp_fs, vo_render_fs_name[i]);
		}
		g_signal_connect(cbt_cmp_fs, "changed", G_CALLBACK(vo_change_cmp_fs), uigtk2);
	}
	{
		GtkComboBoxText *cbt_cmp_fsc = GTK_COMBO_BOX_TEXT(gtk_builder_get_object(uigtk2->builder, "cbt_cmp_fsc"));
		for (unsigned i = 0; i < NUM_VO_RENDER_FSC; i++) {
			gtk_combo_box_text_append_text(cbt_cmp_fsc, vo_render_fsc_name[i]);
		}
		g_signal_connect(cbt_cmp_fsc, "changed", G_CALLBACK(vo_change_cmp_fsc), uigtk2);
	}
	{
		GtkComboBoxText *cbt_cmp_system = GTK_COMBO_BOX_TEXT(gtk_builder_get_object(uigtk2->builder, "cbt_cmp_system"));
		for (unsigned i = 0; i < NUM_VO_RENDER_SYSTEM; i++) {
			gtk_combo_box_text_append_text(cbt_cmp_system, vo_render_system_name[i]);
		}
		g_signal_connect(cbt_cmp_system, "changed", G_CALLBACK(vo_change_cmp_system), uigtk2);
	}

	// Connect signals
	uigtk2_signal_connect(uigtk2, "vo_window", "delete-event", G_CALLBACK(hide_vo_window), uigtk2);
	uigtk2_signal_connect(uigtk2, "vo_window", "key-press-event", G_CALLBACK(gtk2_dummy_keypress), uigtk2);
	uigtk2_signal_connect(uigtk2, "sb_gain", "value-changed", G_CALLBACK(vo_change_gain), uigtk2);
	uigtk2_signal_connect(uigtk2, "sb_brightness", "value-changed", G_CALLBACK(vo_change_brightness), uigtk2);
	uigtk2_signal_connect(uigtk2, "sb_contrast", "value-changed", G_CALLBACK(vo_change_contrast), uigtk2);
	uigtk2_signal_connect(uigtk2, "sb_saturation", "value-changed", G_CALLBACK(vo_change_saturation), uigtk2);
	uigtk2_signal_connect(uigtk2, "sb_hue", "value-changed", G_CALLBACK(vo_change_hue), uigtk2);
	uigtk2_signal_connect(uigtk2, "tb_ntsc_scaling", "toggled", G_CALLBACK(vo_change_ntsc_scaling), uigtk2);
	uigtk2_signal_connect(uigtk2, "tb_cmp_colour_killer", "toggled", G_CALLBACK(vo_change_cmp_colour_killer), uigtk2);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Video options - update values in UI

void gtk2_vo_update_gain(struct ui_gtk2_interface *uigtk2, float value) {
	uigtk2_notify_spin_button_set_value(uigtk2, "sb_gain", value, vo_change_gain);
}

void gtk2_vo_update_brightness(struct ui_gtk2_interface *uigtk2, int value) {
	uigtk2_notify_spin_button_set_value(uigtk2, "sb_brightness", value, vo_change_brightness);
}

void gtk2_vo_update_contrast(struct ui_gtk2_interface *uigtk2, int value) {
	uigtk2_notify_spin_button_set_value(uigtk2, "sb_contrast", value, vo_change_contrast);
}

void gtk2_vo_update_saturation(struct ui_gtk2_interface *uigtk2, int value) {
	uigtk2_notify_spin_button_set_value(uigtk2, "sb_saturation", value, vo_change_saturation);
}

void gtk2_vo_update_hue(struct ui_gtk2_interface *uigtk2, int value) {
	uigtk2_notify_spin_button_set_value(uigtk2, "sb_hue", value, vo_change_hue);
}

void gtk2_vo_update_picture(struct ui_gtk2_interface *uigtk2, int value) {
	uigtk2_combo_box_set_active(uigtk2, "cbt_picture", value);
}

void gtk2_vo_update_ntsc_scaling(struct ui_gtk2_interface *uigtk2, int value) {
	uigtk2_notify_toggle_button_set_active(uigtk2, "tb_ntsc_scaling", value, vo_change_ntsc_scaling);
}

void gtk2_vo_update_cmp_renderer(struct ui_gtk2_interface *uigtk2, int value) {
	uigtk2_combo_box_set_active(uigtk2, "cbt_cmp_renderer", value);
}

void gtk2_vo_update_cmp_fs(struct ui_gtk2_interface *uigtk2, int value) {
	uigtk2_combo_box_set_active(uigtk2, "cbt_cmp_fs", value);
}

void gtk2_vo_update_cmp_fsc(struct ui_gtk2_interface *uigtk2, int value) {
	uigtk2_combo_box_set_active(uigtk2, "cbt_cmp_fsc", value);
}

void gtk2_vo_update_cmp_system(struct ui_gtk2_interface *uigtk2, int value) {
	uigtk2_combo_box_set_active(uigtk2, "cbt_cmp_system", value);
}

void gtk2_vo_update_cmp_colour_killer(struct ui_gtk2_interface *uigtk2, int value) {
	uigtk2_notify_toggle_button_set_active(uigtk2, "tb_cmp_colour_killer", value, vo_change_cmp_colour_killer);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Video options - signal handlers

void gtk2_vo_toggle_window(GtkToggleAction *current, gpointer user_data) {
	struct ui_gtk2_interface *uigtk2 = user_data;
	if (gtk_toggle_action_get_active(current)) {
		uigtk2_widget_show(uigtk2, "vo_window");
	} else {
		uigtk2_widget_hide(uigtk2, "vo_window");
	}
}

static gboolean hide_vo_window(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
	(void)widget;
	(void)event;
	struct ui_gtk2_interface *uigtk2 = user_data;
	uigtk2_toggle_action_set_active(uigtk2, "/MainMenu/ViewMenu/VideoOptions", 0);
	uigtk2_widget_hide(uigtk2, "vo_window");
	return TRUE;
}

static void vo_change_gain(GtkSpinButton *spin_button, gpointer user_data) {
	struct ui_gtk2_interface *uigtk2 = user_data;
	(void)uigtk2;
	float value = (float)gtk_spin_button_get_value(spin_button);
	if (value < -49.9)
		value = -999.;
	if (xroar.ao_interface) {
		sound_set_gain(xroar.ao_interface->sound_interface, value);
	}
}

static void vo_change_brightness(GtkSpinButton *spin_button, gpointer user_data) {
	struct ui_gtk2_interface *uigtk2 = user_data;
	(void)uigtk2;
	int value = (int)gtk_spin_button_get_value(spin_button);
	if (xroar.vo_interface) {
		DELEGATE_SAFE_CALL(xroar.vo_interface->set_brightness, value);
	}
}

static void vo_change_contrast(GtkSpinButton *spin_button, gpointer user_data) {
	struct ui_gtk2_interface *uigtk2 = user_data;
	(void)uigtk2;
	int value = (int)gtk_spin_button_get_value(spin_button);
	if (xroar.vo_interface) {
		DELEGATE_SAFE_CALL(xroar.vo_interface->set_contrast, value);
	}
}

static void vo_change_saturation(GtkSpinButton *spin_button, gpointer user_data) {
	struct ui_gtk2_interface *uigtk2 = user_data;
	(void)uigtk2;
	int value = (int)gtk_spin_button_get_value(spin_button);
	if (xroar.vo_interface) {
		DELEGATE_SAFE_CALL(xroar.vo_interface->set_saturation, value);
	}
}

static void vo_change_hue(GtkSpinButton *spin_button, gpointer user_data) {
	struct ui_gtk2_interface *uigtk2 = user_data;
	(void)uigtk2;
	int value = (int)gtk_spin_button_get_value(spin_button);
	if (xroar.vo_interface) {
		DELEGATE_SAFE_CALL(xroar.vo_interface->set_hue, value);
	}
}

static void vo_change_picture(GtkComboBox *widget, gpointer user_data) {
	struct ui_gtk2_interface *uigtk2 = user_data;
	(void)uigtk2;
	int value = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
	xroar_set_picture(0, value);
}

static void vo_change_ntsc_scaling(GtkToggleButton *widget, gpointer user_data) {
	struct ui_gtk2_interface *uigtk2 = user_data;
	(void)uigtk2;
	int value = gtk_toggle_button_get_active(widget);
	vo_set_ntsc_scaling(xroar.vo_interface, 0, value);
}

static void vo_change_cmp_renderer(GtkComboBox *widget, gpointer user_data) {
	struct ui_gtk2_interface *uigtk2 = user_data;
	(void)uigtk2;
	int value = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
	vo_set_cmp_ccr(xroar.vo_interface, 1, value);
}

static void vo_change_cmp_fs(GtkComboBox *widget, gpointer user_data) {
	struct ui_gtk2_interface *uigtk2 = user_data;
	(void)uigtk2;
	int value = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
	vo_set_cmp_fs(xroar.vo_interface, 0, value);
}

static void vo_change_cmp_fsc(GtkComboBox *widget, gpointer user_data) {
	struct ui_gtk2_interface *uigtk2 = user_data;
	(void)uigtk2;
	int value = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
	vo_set_cmp_fsc(xroar.vo_interface, 0, value);
}

static void vo_change_cmp_system(GtkComboBox *widget, gpointer user_data) {
	struct ui_gtk2_interface *uigtk2 = user_data;
	(void)uigtk2;
	int value = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
	vo_set_cmp_system(xroar.vo_interface, 0, value);
}

static void vo_change_cmp_colour_killer(GtkToggleButton *widget, gpointer user_data) {
	struct ui_gtk2_interface *uigtk2 = user_data;
	(void)uigtk2;
	int value = gtk_toggle_button_get_active(widget);
	vo_set_cmp_colour_killer(xroar.vo_interface, 0, value);
}
