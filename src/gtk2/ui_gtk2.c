/** \file
 *
 *  \brief GTK+ 2 user-interface module.
 *
 *  \copyright Copyright 2010-2024 Ciaran Anscomb
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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <gtk/gtk.h>
#pragma GCC diagnostic pop

#include "pl-string.h"
#include "slist.h"

#include "cart.h"
#include "events.h"
#include "joystick.h"
#include "keyboard.h"
#include "logging.h"
#include "machine.h"
#include "module.h"
#include "ui.h"
#include "vdrive.h"
#include "vo.h"
#include "xroar.h"

#include "gtk2/common.h"
#include "gtk2/drivecontrol.h"
#include "gtk2/tapecontrol.h"
#include "gtk2/video_options.h"

static void *ui_gtk2_new(void *cfg);
static void ui_gtk2_free(void *sptr);
static void ui_gtk2_run(void *sptr);
static void ui_gtk2_update_state(void *, int tag, int value, const void *data);

extern struct module vo_gtkgl_module;
static struct module * const gtk2_vo_module_list[] = {
#ifdef HAVE_GTKGL
	&vo_gtkgl_module,
#endif
	NULL
};

extern struct module filereq_gtk2_module;
extern struct module filereq_cli_module;
extern struct module filereq_null_module;

static struct module * const gtk2_filereq_module_list[] = {
	&filereq_gtk2_module,
#ifdef HAVE_CLI
	&filereq_cli_module,
#endif
	&filereq_null_module,
	NULL
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct ui_module ui_gtk2_module = {
	.common = { .name = "gtk2", .description = "GTK+ 2 UI",
		.new = ui_gtk2_new,
	},
	.filereq_module_list = gtk2_filereq_module_list,
	.vo_module_list = gtk2_vo_module_list,
	.joystick_module_list = gtk2_js_modlist,
};

/* Dynamic menus */
static void gtk2_update_machine_menu(void *sptr);
static void gtk2_update_cartridge_menu(void *sptr);

static gboolean run_cpu(gpointer data);

/* Helpers */
static char *escape_underscores(const char *str);

/* This feels stupid... */
static void insert_disk1(GtkEntry *entry, gpointer user_data) { (void)entry; struct ui_gtk2_interface *uigtk2 = user_data; gtk2_insert_disk(uigtk2, 0); }
static void insert_disk2(GtkEntry *entry, gpointer user_data) { (void)entry; struct ui_gtk2_interface *uigtk2 = user_data; gtk2_insert_disk(uigtk2, 1); }
static void insert_disk3(GtkEntry *entry, gpointer user_data) { (void)entry; struct ui_gtk2_interface *uigtk2 = user_data; gtk2_insert_disk(uigtk2, 2); }
static void insert_disk4(GtkEntry *entry, gpointer user_data) { (void)entry; struct ui_gtk2_interface *uigtk2 = user_data; gtk2_insert_disk(uigtk2, 3); }

static void save_snapshot(GtkEntry *entry, gpointer user_data) {
	(void)entry;
	struct ui_gtk2_interface *uigtk2 = user_data;
	g_idle_remove_by_data(uigtk2->top_window);
	xroar_save_snapshot();
	g_idle_add(run_cpu, uigtk2->top_window);
}

static void save_screenshot(GtkEntry *entry, gpointer user_data) {
	(void)entry;
	struct ui_gtk2_interface *uigtk2 = user_data;
	g_idle_remove_by_data(uigtk2->top_window);
#ifdef SCREENSHOT
	xroar_screenshot();
#endif
	g_idle_add(run_cpu, uigtk2->top_window);
}

static void do_quit(GtkEntry *entry, gpointer user_data) {
	(void)entry;
	(void)user_data;
	xroar_quit();
}

static void do_soft_reset(GtkEntry *entry, gpointer user_data) {
	(void)entry;
	(void)user_data;
	xroar_soft_reset();
}

static void do_hard_reset(GtkEntry *entry, gpointer user_data) {
	(void)entry;
	(void)user_data;
	xroar_hard_reset();
}

static void zoom_1_1(GtkEntry *entry, gpointer user_data) {
	struct ui_gtk2_interface *uigtk2 = user_data;
	(void)entry;
	if (!xroar.vo_interface)
		return;

	struct vo_render *vr = xroar.vo_interface->renderer;

	int qw = vr->viewport.w / 4;
	int qh = vr->viewport.h / 2;

	uigtk2->user_specified_geometry = 0;
	DELEGATE_SAFE_CALL(xroar.vo_interface->resize, qw * 2, qh * 2);
}

static void zoom_2_1(GtkEntry *entry, gpointer user_data) {
	struct ui_gtk2_interface *uigtk2 = user_data;
	(void)entry;
	if (!xroar.vo_interface)
		return;

	struct vo_render *vr = xroar.vo_interface->renderer;

	int qw = vr->viewport.w / 4;
	int qh = vr->viewport.h / 2;

	uigtk2->user_specified_geometry = 0;
	DELEGATE_SAFE_CALL(xroar.vo_interface->resize, qw * 4, qh * 4);
}

static void zoom_in(GtkEntry *entry, gpointer user_data) {
	struct ui_gtk2_interface *uigtk2 = user_data;
	(void)entry;
	if (!xroar.vo_interface)
		return;

	struct vo_render *vr = xroar.vo_interface->renderer;

	int qw = vr->viewport.w / 4;
	int qh = vr->viewport.h / 2;

	if (vr->is_60hz) {
		qh = (qh * 6) / 5;
	}

	int xscale = uigtk2->picture_area.w / qw;
	int yscale = uigtk2->picture_area.h / qh;
	int scale;
	if (xscale < yscale)
		scale = yscale;
	else if (xscale > yscale)
		scale = xscale;
	else
		scale = xscale + 1;
	if (scale < 1)
		scale = 1;
	uigtk2->user_specified_geometry = 0;
	DELEGATE_SAFE_CALL(xroar.vo_interface->resize, qw * scale, qh * scale);
}

static void zoom_out(GtkEntry *entry, gpointer user_data) {
	struct ui_gtk2_interface *uigtk2 = user_data;
	(void)entry;
	if (!xroar.vo_interface)
		return;

	struct vo_render *vr = xroar.vo_interface->renderer;

	int qw = vr->viewport.w / 4;
	int qh = vr->viewport.h / 2;

	if (vr->is_60hz) {
		qh = (qh * 6) / 5;
	}

	int xscale = uigtk2->picture_area.w / qw;
	int yscale = uigtk2->picture_area.h / qh;
	int scale = 1;
	if (xscale < yscale)
		scale = xscale;
	else if (xscale > yscale)
		scale = yscale;
	else
		scale = xscale - 1;
	if (scale < 1)
		scale = 1;
	uigtk2->user_specified_geometry = 0;
	DELEGATE_SAFE_CALL(xroar.vo_interface->resize, qw * scale, qh * scale);
}

static void toggle_inverse_text(GtkToggleAction *current, gpointer user_data) {
	(void)user_data;
	gboolean val = gtk_toggle_action_get_active(current);
	xroar_set_vdg_inverted_text(0, val);
}

static void set_fullscreen(GtkToggleAction *current, gpointer user_data) {
	(void)user_data;
	gboolean val = gtk_toggle_action_get_active(current);
	xroar_set_fullscreen(0, val);
}

static void set_ccr(GtkRadioAction *action, GtkRadioAction *current, gpointer user_data) {
	struct ui_gtk2_interface *uigtk2 = user_data;
	gint val = gtk_radio_action_get_current_value(current);
	(void)action;
	xroar_set_ccr(0, val);
	gtk2_vo_update_cmp_renderer(uigtk2, val);
}

static void set_tv_input(GtkRadioAction *action, GtkRadioAction *current, gpointer user_data) {
	(void)user_data;
	gint val = gtk_radio_action_get_current_value(current);
	(void)action;
	xroar_set_tv_input(0, val);
}

static void set_machine(GtkRadioAction *action, GtkRadioAction *current, gpointer user_data) {
	(void)user_data;
	gint val = gtk_radio_action_get_current_value(current);
	(void)action;
	xroar_set_machine(1, val);
}

static void set_cart(GtkRadioAction *action, GtkRadioAction *current, gpointer user_data) {
	(void)user_data;
	gint val = gtk_radio_action_get_current_value(current);
	(void)action;
	struct cart_config *cc = cart_config_by_id(val);
	xroar_set_cart(1, cc ? cc->name : NULL);
}

static void set_keymap(GtkRadioAction *action, GtkRadioAction *current, gpointer user_data) {
	(void)user_data;
	gint val = gtk_radio_action_get_current_value(current);
	(void)action;
	xroar_set_keyboard_type(0, val);
}

static char const * const joystick_name[] = {
	NULL, "joy0", "joy1", "kjoy0", "mjoy0"
};

static void set_joy_right(GtkRadioAction *action, GtkRadioAction *current, gpointer user_data) {
	gint val = gtk_radio_action_get_current_value(current);
	(void)action;
	(void)user_data;
	if (val >= 0 && val <= 4)
		xroar_set_joystick(0, 0, joystick_name[val]);
}

static void set_joy_left(GtkRadioAction *action, GtkRadioAction *current, gpointer user_data) {
	gint val = gtk_radio_action_get_current_value(current);
	(void)action;
	(void)user_data;
	if (val >= 0 && val <= 4)
		xroar_set_joystick(0, 1, joystick_name[val]);
}

static void swap_joysticks(GtkEntry *entry, gpointer user_data) {
	(void)entry;
	(void)user_data;
	xroar_swap_joysticks(1);
}

static void toggle_keyboard_translation(GtkToggleAction *current, gpointer user_data) {
	gboolean val = gtk_toggle_action_get_active(current);
	(void)user_data;
	xroar_set_kbd_translate(0, val);
}

static void toggle_ratelimit(GtkToggleAction *current, gpointer user_data) {
	gboolean val = gtk_toggle_action_get_active(current);
	(void)user_data;
	xroar_set_ratelimit_latch(0, val);
}

static void close_about(GtkDialog *dialog, gint response_id, gpointer user_data) {
	(void)response_id;
	struct ui_gtk2_interface *uigtk2 = user_data;
	(void)uigtk2;
	gtk_widget_hide(GTK_WIDGET(dialog));
	gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void about(GtkMenuItem *item, gpointer user_data) {
	(void)item;
	struct ui_gtk2_interface *uigtk2 = user_data;
	GtkAboutDialog *dialog = (GtkAboutDialog *)gtk_about_dialog_new();
	gtk_about_dialog_set_version(dialog, VERSION);
	gtk_about_dialog_set_copyright(dialog, "Copyright © " PACKAGE_YEAR " Ciaran Anscomb <xroar@6809.org.uk>");
	gtk_about_dialog_set_license(dialog,
"XRoar is free software; you can redistribute it and/or modify it under\n"
"the terms of the GNU General Public License as published by the Free Free\n"
"Software Foundation, either version 3 of the License, or (at your option)\n"
"any later version.\n"
"\n"
"XRoar is distributed in the hope that it will be useful, but WITHOUT\n"
"ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or\n"
"FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License\n"
"for more details.\n"
"\n"
"You should have received a copy of the GNU General Public License along\n"
"with XRoar.  If not, see <https://www.gnu.org/licenses/>."
	);
	gtk_about_dialog_set_website(dialog, "https://www.6809.org.uk/xroar/");
	g_signal_connect(dialog, "response", G_CALLBACK(close_about), uigtk2);
	gtk_widget_show(GTK_WIDGET(dialog));
}

static void do_load_file(GtkEntry *entry, gpointer user_data) { (void)entry; (void)user_data; xroar_load_file(); }
static void do_run_file(GtkEntry *entry, gpointer user_data) { (void)entry; (void)user_data; xroar_run_file(); }

static GtkActionEntry const ui_entries[] = {
	/* Top level */
	{ .name = "FileMenuAction", .label = "_File" },
	{ .name = "ViewMenuAction", .label = "_View" },
	{ .name = "HardwareMenuAction", .label = "H_ardware" },
	{ .name = "ToolMenuAction", .label = "_Tool" },
	{ .name = "HelpMenuAction", .label = "_Help" },
	/* File */
	{ .name = "RunAction", .stock_id = GTK_STOCK_EXECUTE, .label = "_Run…",
	  .accelerator = "<shift><control>L",
	  .tooltip = "Load and attempt to autorun a file",
	  .callback = G_CALLBACK(do_run_file) },
	{ .name = "LoadAction", .stock_id = GTK_STOCK_OPEN, .label = "_Load…",
	  .accelerator = "<control>L",
	  .tooltip = "Load a file",
	  .callback = G_CALLBACK(do_load_file) },
	/* XXX { .name = "InsertDiskAction",
	  .label = "Insert _disk…",
	  .tooltip = "Load a virtual disk image",
	  .callback = G_CALLBACK(insert_disk) }, */
	{ .name = "InsertDisk1Action", .accelerator = "<control>1", .callback = G_CALLBACK(insert_disk1) },
	{ .name = "InsertDisk2Action", .accelerator = "<control>2", .callback = G_CALLBACK(insert_disk2) },
	{ .name = "InsertDisk3Action", .accelerator = "<control>3", .callback = G_CALLBACK(insert_disk3) },
	{ .name = "InsertDisk4Action", .accelerator = "<control>4", .callback = G_CALLBACK(insert_disk4) },
	{ .name = "SaveSnapshotAction", .stock_id = GTK_STOCK_SAVE_AS, .label = "_Save Snapshot…",
	  .accelerator = "<control>S",
	  .callback = G_CALLBACK(save_snapshot) },
	{ .name = "ScreenshotAction", .label = "Screenshot to PNG…",
	  .accelerator = "<control><shift>S",
	  .callback = G_CALLBACK(save_screenshot) },
	{ .name = "QuitAction", .stock_id = GTK_STOCK_QUIT, .label = "_Quit",
	  .accelerator = "<control>Q",
	  .tooltip = "Quit",
	  .callback = G_CALLBACK(do_quit) },
	/* View */
	{ .name = "TVInputMenuAction", .label = "_TV input" },
	{ .name = "CCRMenuAction", .label = "Composite _rendering" },
	{ .name = "ZoomMenuAction", .label = "_Zoom" },
	{ .name = "zoom_in", .label = "Zoom In",
	  .accelerator = "<control>plus",
	  .callback = G_CALLBACK(zoom_in) },
	{ .name = "zoom_out", .label = "Zoom Out",
	  .accelerator = "<control>minus",
	  .callback = G_CALLBACK(zoom_out) },
	{ .name = "zoom_1_1", .label = "1:1",
	  .callback = G_CALLBACK(zoom_1_1) },
	{ .name = "zoom_2_1", .label = "2:1",
	  .callback = G_CALLBACK(zoom_2_1) },
	{ .name = "zoom_reset", .label = "Reset",
	  .accelerator = "<control>0",
	  .callback = G_CALLBACK(zoom_2_1) },
	/* Hardware */
	{ .name = "MachineMenuAction", .label = "_Machine" },
	{ .name = "CartridgeMenuAction", .label = "_Cartridge" },
	{ .name = "KeymapMenuAction", .label = "_Keyboard type" },
	{ .name = "JoyRightMenuAction", .label = "_Right joystick" },
	{ .name = "JoyLeftMenuAction", .label = "_Left joystick" },
	{ .name = "JoySwapAction", .label = "Swap _joysticks",
	  .accelerator = "<control><shift>J",
	  .callback = G_CALLBACK(swap_joysticks) },
	{ .name = "SoftResetAction", .label = "_Soft reset",
	  .accelerator = "<control>R",
	  .tooltip = "Soft reset machine",
	  .callback = G_CALLBACK(do_soft_reset) },
	{ .name = "HardResetAction",
	  .label = "_Hard reset",
	  .accelerator = "<shift><control>R",
	  .tooltip = "Hard reset machine (power cycle)",
	  .callback = G_CALLBACK(do_hard_reset) },
	/* Help */
	{ .name = "AboutAction", .stock_id = GTK_STOCK_ABOUT,
	  .label = "_About",
	  .callback = G_CALLBACK(about) },
};

static GtkToggleActionEntry const ui_toggles[] = {
	// File
	{ .name = "TapeControlAction", .label = "Cassette _tapes",
	  .accelerator = "<control>T",
	  .callback = G_CALLBACK(gtk2_toggle_tc_window) },
	{ .name = "DriveControlAction", .label = "Floppy _disks",
	  .accelerator = "<control>D",
	  .callback = G_CALLBACK(gtk2_toggle_dc_window) },
	// View
	{ .name = "VideoOptionsAction", .label = "TV _controls",
	  .accelerator = "<control><shift>V",
	  .callback = G_CALLBACK(gtk2_vo_toggle_window) },
	{ .name = "InverseTextAction", .label = "_Inverse text",
	  .accelerator = "<shift><control>I",
	  .callback = G_CALLBACK(toggle_inverse_text) },
	{ .name = "FullScreenAction", .label = "_Full screen",
	  .stock_id = GTK_STOCK_FULLSCREEN,
	  .accelerator = "F11", .callback = G_CALLBACK(set_fullscreen) },
	// Tool
	{ .name = "TranslateKeyboardAction", .label = "_Keyboard translation",
	  .accelerator = "<control>Z",
	  .callback = G_CALLBACK(toggle_keyboard_translation) },
	{ .name = "RateLimitAction", .label = "_Rate limit",
	  .accelerator = "<shift>F12",
	  .callback = G_CALLBACK(toggle_ratelimit) },
};

static GtkRadioActionEntry const ccr_radio_entries[] = {
	{ .name = "ccr-palette", .label = "None", .value = VO_CMP_CCR_PALETTE },
	{ .name = "ccr-2bit", .label = "Simple (2-bit LUT)", .value = VO_CMP_CCR_2BIT },
	{ .name = "ccr-5bit", .label = "5-bit LUT", .value = VO_CMP_CCR_5BIT },
	{ .name = "ccr-partial", .label = "Partial NTSC", .value = VO_CMP_CCR_PARTIAL },
	{ .name = "ccr-simulated", .label = "Simulated", .value = VO_CMP_CCR_SIMULATED },
};

static GtkRadioActionEntry const tv_input_radio_entries[] = {
	{ .name = "tv-input-svideo", .label = "S-Video", .value = TV_INPUT_SVIDEO },
	{ .name = "tv-input-cmp-kbrw", .label = "Composite (blue-red)", .value = TV_INPUT_CMP_KBRW },
	{ .name = "tv-input-cmp-krbw", .label = "Composite (red-blue)", .value = TV_INPUT_CMP_KRBW },
	{ .name = "tv-input-rgb", .label = "RGB", .value = TV_INPUT_RGB },
};

static GtkRadioActionEntry const keymap_radio_entries[] = {
	{ .name = "keymap_dragon", .label = "Dragon Layout", .value = dkbd_layout_dragon },
	{ .name = "keymap_dragon200e", .label = "Dragon 200-E Layout", .value = dkbd_layout_dragon200e },
	{ .name = "keymap_coco", .label = "CoCo Layout", .value = dkbd_layout_coco },
	{ .name = "keymap_coco3", .label = "CoCo 3 Layout", .value = dkbd_layout_coco3 },
	{ .name = "keymap_mc10", .label = "MC-10 Layout", .value = dkbd_layout_mc10 },
	{ .name = "keymap_alice", .label = "Alice Layout", .value = dkbd_layout_alice },
};

static GtkRadioActionEntry const joy_right_radio_entries[] = {
	{ .name = "joy_right_none", .label = "None", .value = 0 },
	{ .name = "joy_right_joy0", .label = "Joystick 0", .value = 1 },
	{ .name = "joy_right_joy1", .label = "Joystick 1", .value = 2 },
	{ .name = "joy_right_kjoy0", .label = "Keyboard", .value = 3 },
	{ .name = "joy_right_mjoy0", .label = "Mouse", .value = 4 },
};

static GtkRadioActionEntry const joy_left_radio_entries[] = {
	{ .name = "joy_left_none", .label = "None", .value = 0 },
	{ .name = "joy_left_joy0", .label = "Joystick 0", .value = 1 },
	{ .name = "joy_left_joy1", .label = "Joystick 1", .value = 2 },
	{ .name = "joy_left_kjoy0", .label = "Keyboard", .value = 3 },
	{ .name = "joy_left_mjoy0", .label = "Mouse", .value = 4 },
};

// Work around gtk_exit() being deprecated:
static void ui_gtk2_destroy(GtkWidget *w, gpointer user_data) {
	(void)w;
	exit((intptr_t)user_data);
}

static void *ui_gtk2_new(void *cfg) {
	struct ui_cfg *ui_cfg = cfg;

	// Be sure we've not made more than one of these
	assert(global_uigtk2 == NULL);

	gtk_init(NULL, NULL);

	g_set_application_name("XRoar");

	GError *error = NULL;

	struct ui_gtk2_interface *uigtk2 = g_malloc(sizeof(*uigtk2));
	*uigtk2 = (struct ui_gtk2_interface){0};
	struct ui_interface *ui = &uigtk2->public;

	uigtk2->builder = gtk_builder_new();
	uigtk2_add_from_resource(uigtk2, "/uk/org/6809/xroar/gtk2/application.ui");

	// Make available globally for other GTK+ 2 code
	global_uigtk2 = uigtk2;
	uigtk2->cfg = cfg;

	ui->free = DELEGATE_AS0(void, ui_gtk2_free, uigtk2);
	ui->run = DELEGATE_AS0(void, ui_gtk2_run, uigtk2);
	ui->update_state = DELEGATE_AS3(void, int, int, cvoidp, ui_gtk2_update_state, uigtk2);

	// Flag which file requester belongs to this UI
	ui->filereq_module = &filereq_gtk2_module;

	/* Fetch top level window */
	uigtk2->top_window = GTK_WIDGET(gtk_builder_get_object(uigtk2->builder, "top_window"));
	g_signal_connect(uigtk2->top_window, "destroy", G_CALLBACK(ui_gtk2_destroy), (gpointer)(intptr_t)0);

	/* Fetch vbox */
	GtkWidget *vbox = GTK_WIDGET(gtk_builder_get_object(uigtk2->builder, "vbox1"));

	/* Create a UI from XML */
	uigtk2->menu_manager = gtk_ui_manager_new();

	GBytes *res_ui = g_resources_lookup_data("/uk/org/6809/xroar/gtk2/menu.ui", 0, NULL);
	const gchar *ui_xml_string = g_bytes_get_data(res_ui, NULL);

	// Sigh, glib-compile-resources can strip blanks, but it then forcibly
	// adds an XML version tag, which gtk_ui_manager_add_ui_from_string()
	// objects to.  Skip to the second tag...
	if (ui_xml_string) {
		do { ui_xml_string++; } while (*ui_xml_string != '<');
	}
	// The proper way to do this (for the next five minutes) is probably to
	// transition to using GtkBuilder.
	gtk_ui_manager_add_ui_from_string(uigtk2->menu_manager, ui_xml_string, -1, &error);
	if (error) {
		g_message("building menus failed: %s", error->message);
		g_error_free(error);
	}
	g_bytes_unref(res_ui);

	/* Action groups */
	GtkActionGroup *main_action_group = gtk_action_group_new("Main");
	uigtk2->machine_action_group = gtk_action_group_new("Machine");
	uigtk2->cart_action_group = gtk_action_group_new("Cartridge");
	gtk_ui_manager_insert_action_group(uigtk2->menu_manager, main_action_group, 0);
	gtk_ui_manager_insert_action_group(uigtk2->menu_manager, uigtk2->machine_action_group, 0);
	gtk_ui_manager_insert_action_group(uigtk2->menu_manager, uigtk2->cart_action_group, 0);

	/* Set up main action group */
	gtk_action_group_add_actions(main_action_group, ui_entries, G_N_ELEMENTS(ui_entries), uigtk2);
	gtk_action_group_add_toggle_actions(main_action_group, ui_toggles, G_N_ELEMENTS(ui_toggles), uigtk2);
	gtk_action_group_add_radio_actions(main_action_group, keymap_radio_entries, G_N_ELEMENTS(keymap_radio_entries), 0, (GCallback)set_keymap, uigtk2);
	gtk_action_group_add_radio_actions(main_action_group, joy_right_radio_entries, G_N_ELEMENTS(joy_right_radio_entries), 0, (GCallback)set_joy_right, uigtk2);
	gtk_action_group_add_radio_actions(main_action_group, joy_left_radio_entries, G_N_ELEMENTS(joy_left_radio_entries), 0, (GCallback)set_joy_left, uigtk2);
	gtk_action_group_add_radio_actions(main_action_group, tv_input_radio_entries, G_N_ELEMENTS(tv_input_radio_entries), 0, (GCallback)set_tv_input, uigtk2);
	gtk_action_group_add_radio_actions(main_action_group, ccr_radio_entries, G_N_ELEMENTS(ccr_radio_entries), 0, (GCallback)set_ccr, uigtk2);

	/* Menu merge points */
	uigtk2->merge_machines = gtk_ui_manager_new_merge_id(uigtk2->menu_manager);
	uigtk2->merge_carts = gtk_ui_manager_new_merge_id(uigtk2->menu_manager);

	/* Update all dynamic menus */
	ui->update_machine_menu = DELEGATE_AS0(void, gtk2_update_machine_menu, uigtk2);
	ui->update_cartridge_menu = DELEGATE_AS0(void, gtk2_update_cartridge_menu, uigtk2);
	gtk2_update_machine_menu(uigtk2);
	gtk2_update_cartridge_menu(uigtk2);

	/* Extract menubar widget and add to vbox */
	uigtk2->menubar = gtk_ui_manager_get_widget(uigtk2->menu_manager, "/MainMenu");
	gtk_box_pack_start(GTK_BOX(vbox), uigtk2->menubar, FALSE, FALSE, 0);
	gtk_window_add_accel_group(GTK_WINDOW(uigtk2->top_window), gtk_ui_manager_get_accel_group(uigtk2->menu_manager));
	gtk_box_reorder_child(GTK_BOX(vbox), uigtk2->menubar, 0);

	/* Create drawing_area widget, add to vbox */
	uigtk2->drawing_area = GTK_WIDGET(gtk_builder_get_object(uigtk2->builder, "drawing_area"));
	GdkGeometry hints = {
		.min_width = 160, .min_height = 120,
		.base_width = 0, .base_height = 0,
	};
	gtk_window_set_geometry_hints(GTK_WINDOW(uigtk2->top_window), GTK_WIDGET(uigtk2->drawing_area), &hints, GDK_HINT_MIN_SIZE | GDK_HINT_BASE_SIZE);
	gtk_widget_show(uigtk2->drawing_area);

	/* Parse initial geometry */
	if (ui_cfg->vo_cfg.geometry) {
		gtk_window_parse_geometry(GTK_WINDOW(uigtk2->top_window), ui_cfg->vo_cfg.geometry);
		uigtk2->user_specified_geometry = 1;
	}

	// Cursor hiding
	uigtk2->blank_cursor = gdk_cursor_new(GDK_BLANK_CURSOR);

	// Create (hidden) drive control window
	gtk2_create_dc_window(uigtk2);

	// Create (hidden) tape control window
	gtk2_create_tc_window(uigtk2);

	// Create (hidden) video options window
	gtk2_vo_create_window(uigtk2);

	// Window geometry sensible defaults
	uigtk2->picture_area.w = 640;
	uigtk2->picture_area.h = 480;

	struct module *vo_mod = (struct module *)module_select_by_arg((struct module * const *)gtk2_vo_module_list, uigtk2->cfg->vo);
	if (!module_init(vo_mod, uigtk2)) {
		return NULL;
	}

	gtk2_keyboard_init(ui_cfg);

	// Connect relevant event signals
	g_signal_connect(G_OBJECT(uigtk2->top_window), "key-press-event", G_CALLBACK(gtk2_handle_key_press), uigtk2);
	g_signal_connect(G_OBJECT(uigtk2->top_window), "key-release-event", G_CALLBACK(gtk2_handle_key_release), uigtk2);
	g_signal_connect(G_OBJECT(uigtk2->drawing_area), "motion-notify-event", G_CALLBACK(gtk2_handle_motion_notify), uigtk2);
	g_signal_connect(G_OBJECT(uigtk2->drawing_area), "button-press-event", G_CALLBACK(gtk2_handle_button_press), uigtk2);
	g_signal_connect(G_OBJECT(uigtk2->drawing_area), "button-release-event", G_CALLBACK(gtk2_handle_button_release), uigtk2);

	// Any remaining signals
	gtk_builder_connect_signals(uigtk2->builder, uigtk2);

	// Ensure we get those events
	gtk_widget_add_events(uigtk2->top_window, GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK);
	gtk_widget_add_events(uigtk2->drawing_area, GDK_POINTER_MOTION_MASK | GDK_POINTER_MOTION_HINT_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);

	return ui;
}

static void ui_gtk2_free(void *sptr) {
	struct ui_gtk2_interface *uigtk2 = sptr;
	g_object_unref(uigtk2->builder);
	gtk_widget_destroy(uigtk2->drawing_area);
	gtk_widget_destroy(uigtk2->top_window);
	// we can't actually have more than one, but i also can't stop myself
	// coding it like this:
	if (global_uigtk2 == uigtk2)
		global_uigtk2 = NULL;
	g_free(uigtk2);
}

static gboolean run_cpu(gpointer data) {
	(void)data;
	xroar_run(EVENT_MS(10));
	return 1;
}

static void ui_gtk2_run(void *sptr) {
	struct ui_gtk2_interface *uigtk2 = sptr;
	g_idle_add(run_cpu, uigtk2->top_window);
	gtk_main();
}

static void ui_gtk2_update_state(void *sptr, int tag, int value, const void *data) {
	struct ui_gtk2_interface *uigtk2 = sptr;

	switch (tag) {

	/* Hardware */

	case ui_tag_machine:
		uigtk2_notify_radio_action_set_current_value(uigtk2, "/MainMenu/HardwareMenu/MachineMenu/machine1", value, set_machine);
		break;

	case ui_tag_cartridge:
		uigtk2_notify_radio_action_set_current_value(uigtk2, "/MainMenu/HardwareMenu/CartridgeMenu/cart0", value, set_cart);
		break;

	/* Tape */

	case ui_tag_tape_flags:
		gtk2_update_tape_state(uigtk2, value);
		break;

	case ui_tag_tape_input_filename:
		gtk2_input_tape_filename_cb(uigtk2, (const char *)data);
		break;

	case ui_tag_tape_output_filename:
		gtk2_output_tape_filename_cb(uigtk2, (const char *)data);
		break;

	case ui_tag_tape_playing:
		gtk2_update_tape_playing(uigtk2, value);
		break;

	/* Disk */

	case ui_tag_disk_write_enable:
		gtk2_update_drive_write_enable(uigtk2, value, (intptr_t)data);
		break;

	case ui_tag_disk_write_back:
		gtk2_update_drive_write_back(uigtk2, value, (intptr_t)data);
		break;

	case ui_tag_disk_data:
		gtk2_update_drive_disk(uigtk2, value, (const struct vdisk *)data);
		break;

	/* Video */

	case ui_tag_fullscreen:
		uigtk2_notify_toggle_action_set_active(uigtk2, "/MainMenu/ViewMenu/FullScreen", value ? TRUE : FALSE, set_fullscreen);
		break;

	case ui_tag_vdg_inverse:
		uigtk2_notify_toggle_action_set_active(uigtk2, "/MainMenu/ViewMenu/InverseText", value ? TRUE : FALSE, toggle_inverse_text);
		break;

	case ui_tag_ccr:
		uigtk2_notify_radio_action_set_current_value(uigtk2, "/MainMenu/ViewMenu/CCRMenu/ccr-palette", value, set_ccr);
		gtk2_vo_update_cmp_renderer(uigtk2, value);
		break;

	case ui_tag_tv_input:
		uigtk2_notify_radio_action_set_current_value(uigtk2, "/MainMenu/ViewMenu/TVInputMenu/tv-input-svideo", value, set_tv_input);
		break;

	case ui_tag_brightness:
		gtk2_vo_update_brightness(uigtk2, value);
		break;

	case ui_tag_contrast:
		gtk2_vo_update_contrast(uigtk2, value);
		break;

	case ui_tag_saturation:
		gtk2_vo_update_saturation(uigtk2, value);
		break;

	case ui_tag_hue:
		gtk2_vo_update_hue(uigtk2, value);
		break;

	case ui_tag_picture:
		gtk2_vo_update_picture(uigtk2, value);
		break;

	case ui_tag_ntsc_scaling:
		gtk2_vo_update_ntsc_scaling(uigtk2, value);
		break;

	case ui_tag_cmp_fs:
		gtk2_vo_update_cmp_fs(uigtk2, value);
		break;

	case ui_tag_cmp_fsc:
		gtk2_vo_update_cmp_fsc(uigtk2, value);
		break;

	case ui_tag_cmp_system:
		gtk2_vo_update_cmp_system(uigtk2, value);
		break;

	case ui_tag_cmp_colour_killer:
		gtk2_vo_update_cmp_colour_killer(uigtk2, value);
		break;

	/* Audio */

	case ui_tag_ratelimit:
		uigtk2_notify_toggle_action_set_active(uigtk2, "/MainMenu/ToolMenu/RateLimit", value ? TRUE : FALSE, toggle_ratelimit);
		break;

	/* Keyboard */

	case ui_tag_keymap:
		uigtk2_notify_radio_action_set_current_value(uigtk2, "/MainMenu/HardwareMenu/KeymapMenu/keymap_dragon", value, set_keymap);
		break;

	case ui_tag_kbd_translate:
		uigtk2_notify_toggle_action_set_active(uigtk2, "/MainMenu/ToolMenu/TranslateKeyboard", value ? TRUE : FALSE, toggle_keyboard_translation);
		break;

	/* Joysticks */

	case ui_tag_joy_right:
	case ui_tag_joy_left:
		{
			const gchar *path;
			gpointer func;
			if (tag == ui_tag_joy_right) {
				path = "/MainMenu/HardwareMenu/JoyRightMenu/joy_right_none";
				func = set_joy_right;
			} else {
				path = "/MainMenu/HardwareMenu/JoyLeftMenu/joy_left_none";
				func = set_joy_left;
			}
			int joy = 0;
			if (data) {
				for (int i = 1; i < 5; i++) {
					if (0 == strcmp((const char *)data, joystick_name[i])) {
						joy = i;
						break;
					}
				}
			}
			uigtk2_notify_radio_action_set_current_value(uigtk2, path, joy, func);
		}
		break;

	default:
		break;
	}
}

static void remove_action_from_group(gpointer data, gpointer user_data) {
	GtkAction *action = data;
	GtkActionGroup *action_group = user_data;
	gtk_action_group_remove_action(action_group, action);
}

static void free_action_group(GtkActionGroup *action_group) {
	GList *list = gtk_action_group_list_actions(action_group);
	g_list_foreach(list, remove_action_from_group, action_group);
	g_list_free(list);
}

// Dynamic machine menu

static void gtk2_update_machine_menu(void *sptr) {
	struct ui_gtk2_interface *uigtk2 = sptr;
	// Get list of machine configs
	struct slist *mcl = slist_reverse(slist_copy(machine_config_list()));
	int num_machines = slist_length(mcl);

	// Remove old entries
	free_action_group(uigtk2->machine_action_group);
	gtk_ui_manager_remove_ui(uigtk2->menu_manager, uigtk2->merge_machines);
	GtkRadioActionEntry *radio_entries = g_malloc0(num_machines * sizeof(*radio_entries));

	// Jump through alloc hoops just to avoid const-ness warnings
	gchar **names = g_malloc0(num_machines * sizeof(gchar *));
	gchar **labels = g_malloc0(num_machines * sizeof(gchar *));

	// Add new entries in reverse order, as each will be inserted before
	// the previous.
	int selected = -1;
	int i = 0;
	for (struct slist *iter = mcl; iter; iter = iter->next, i++) {
		struct machine_config *mc = iter->data;
		if (mc == xroar.machine_config)
			selected = mc->id;
		names[i] = g_strdup_printf("machine%d", i+1);
		radio_entries[i].name = names[i];
		labels[i] = escape_underscores(mc->description);
		radio_entries[i].label = labels[i];
		radio_entries[i].value = mc->id;
		gtk_ui_manager_add_ui(uigtk2->menu_manager, uigtk2->merge_machines, "/MainMenu/HardwareMenu/MachineMenu", radio_entries[i].name, radio_entries[i].name, GTK_UI_MANAGER_MENUITEM, TRUE);
	}
	gtk_action_group_add_radio_actions(uigtk2->machine_action_group, radio_entries, num_machines, selected, (GCallback)set_machine, uigtk2);

	// Back through the hoops
	for (i = 0; i < num_machines; i++) {
		g_free(names[i]);
		g_free(labels[i]);
	}
	g_free(names);
	g_free(labels);
	g_free(radio_entries);
	slist_free(mcl);
}

// Dynamic cartridge menu

static void gtk2_update_cartridge_menu(void *sptr) {
	struct ui_gtk2_interface *uigtk2 = sptr;
	// Get list of cart configs
	struct slist *ccl = NULL;
	int num_carts = 0;
	struct cart *cart = NULL;
	if (xroar.machine) {
		const struct machine_partdb_extra *mpe = xroar.machine->part.partdb->extra[0];
		const char *cart_arch = mpe->cart_arch;
		ccl = slist_reverse(cart_config_list_is_a(cart_arch));
		num_carts = slist_length(ccl);
		cart = (struct cart *)part_component_by_id(&xroar.machine->part, "cart");
	}

	// Remove old entries
	free_action_group(uigtk2->cart_action_group);
	gtk_ui_manager_remove_ui(uigtk2->menu_manager, uigtk2->merge_carts);

	// Jump through alloc hoops just to avoid const-ness warnings.
	// Note: final entry's name & label is const, no need to allow space
	// for it in names[] & labels[].
	GtkRadioActionEntry *radio_entries = g_malloc0((num_carts+1) * sizeof(*radio_entries));
	gchar **names = g_malloc0(num_carts * sizeof(gchar *));
	gchar **labels = g_malloc0(num_carts * sizeof(gchar *));

	// Add new entries in reverse order, as each will be inserted before
	// the previous.
	int selected = 0;
	int i = 0;
	for (struct slist *iter = ccl; iter; iter = iter->next, i++) {
		struct cart_config *cc = iter->data;
		if (cart && cc == cart->config)
			selected = cc->id;
		names[i] = g_strdup_printf("cart%d", i+1);
		radio_entries[i].name = names[i];
		labels[i] = escape_underscores(cc->description);
		radio_entries[i].label = labels[i];
		radio_entries[i].value = cc->id;
		gtk_ui_manager_add_ui(uigtk2->menu_manager, uigtk2->merge_carts, "/MainMenu/HardwareMenu/CartridgeMenu", radio_entries[i].name, radio_entries[i].name, GTK_UI_MANAGER_MENUITEM, TRUE);
	}
	radio_entries[num_carts].name = "cart0";
	radio_entries[num_carts].label = "None";
	radio_entries[num_carts].value = -1;
	gtk_ui_manager_add_ui(uigtk2->menu_manager, uigtk2->merge_carts, "/MainMenu/HardwareMenu/CartridgeMenu", radio_entries[num_carts].name, radio_entries[num_carts].name, GTK_UI_MANAGER_MENUITEM, TRUE);
	gtk_action_group_add_radio_actions(uigtk2->cart_action_group, radio_entries, num_carts+1, selected, (GCallback)set_cart, uigtk2);

	// Back through the hoops
	for (i = 0; i < num_carts; i++) {
		g_free(names[i]);
		g_free(labels[i]);
	}
	g_free(names);
	g_free(labels);
	g_free(radio_entries);
	slist_free(ccl);
}

/* Tool callbacks */

static char *escape_underscores(const char *str) {
	if (!str) return NULL;
	int len = strlen(str);
	const char *in;
	char *out;
	for (in = str; *in; in++) {
		if (*in == '_')
			len++;
	}
	char *ret_str = g_malloc(len + 1);
	for (in = str, out = ret_str; *in; in++) {
		*(out++) = *in;
		if (*in == '_') {
			*(out++) = '_';
		}
	}
	*out = 0;
	return ret_str;
}
