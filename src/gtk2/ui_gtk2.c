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
#ifdef HAVE_X11
#include <gdk/gdkx.h>
#endif
#pragma GCC diagnostic pop

#include "pl-string.h"
#include "slist.h"

#include "cart.h"
#include "events.h"
#include "hkbd.h"
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

#include "x11/hkbd_x11.h"

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
static void gtk2_update_joystick_menus(void *);

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

static void zoom_reset(GtkEntry *entry, gpointer user_data) {
	(void)entry;
	struct ui_gtk2_interface *uigtk2 = user_data;
	struct vo_interface *vo = uigtk2->public.vo_interface;
	vo_zoom_reset(vo);
}

static void zoom_in(GtkEntry *entry, gpointer user_data) {
	(void)entry;
	struct ui_gtk2_interface *uigtk2 = user_data;
	struct vo_interface *vo = uigtk2->public.vo_interface;
	vo_zoom_in(vo);
}

static void zoom_out(GtkEntry *entry, gpointer user_data) {
	(void)entry;
	struct ui_gtk2_interface *uigtk2 = user_data;
	struct vo_interface *vo = uigtk2->public.vo_interface;
	vo_zoom_out(vo);
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

static void set_hkbd_layout(GtkRadioAction *action, GtkRadioAction *current, gpointer user_data) {
	(void)user_data;
	gint val = gtk_radio_action_get_current_value(current);
	(void)action;
	xroar_set_hkbd_layout(0, val);
}

static void set_hkbd_lang(GtkRadioAction *action, GtkRadioAction *current, gpointer user_data) {
	(void)user_data;
	gint val = gtk_radio_action_get_current_value(current);
	(void)action;
	xroar_set_hkbd_lang(0, val);
}

static void set_joy_right(GtkRadioAction *action, GtkRadioAction *current, gpointer user_data) {
	gint val = gtk_radio_action_get_current_value(current);
	(void)action;
	(void)user_data;
	struct joystick_config *jc = joystick_config_by_id(val);
	xroar_set_joystick(0, 0, jc ? jc->name : NULL);
}

static void set_joy_left(GtkRadioAction *action, GtkRadioAction *current, gpointer user_data) {
	gint val = gtk_radio_action_get_current_value(current);
	(void)action;
	(void)user_data;
	struct joystick_config *jc = joystick_config_by_id(val);
	xroar_set_joystick(0, 1, jc ? jc->name : NULL);
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
	{ .name = "zoom_reset", .label = "Reset",
	  .accelerator = "<control>0",
	  .callback = G_CALLBACK(zoom_reset) },
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
	/* Tool */
	{ .name = "HKBDLayoutMenuAction", .label = "Keyboard la_yout" },
	{ .name = "HKBDLangMenuAction", .label = "Keyboard lan_guage" },
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

#ifdef HAVE_X11
	Display *display = gdk_x11_get_default_xdisplay();
	hk_x11_set_display(display);
#endif

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
	gtk_ui_manager_insert_action_group(uigtk2->menu_manager, main_action_group, 0);

	/* Set up main action group */
	gtk_action_group_add_actions(main_action_group, ui_entries, G_N_ELEMENTS(ui_entries), uigtk2);
	gtk_action_group_add_toggle_actions(main_action_group, ui_toggles, G_N_ELEMENTS(ui_toggles), uigtk2);

	// Dynamic radio menus
	uigtk2->tv_input_radio_menu = uigtk2_radio_menu_new(uigtk2, "/MainMenu/ViewMenu/TVInputMenu", (GCallback)set_tv_input);
	uigtk2->ccr_radio_menu = uigtk2_radio_menu_new(uigtk2, "/MainMenu/ViewMenu/CCRMenu", (GCallback)set_ccr);
	uigtk2->machine_radio_menu = uigtk2_radio_menu_new(uigtk2, "/MainMenu/HardwareMenu/MachineMenu", (GCallback)set_machine);
	uigtk2->cart_radio_menu = uigtk2_radio_menu_new(uigtk2, "/MainMenu/HardwareMenu/CartridgeMenu", (GCallback)set_cart);
	uigtk2->keymap_radio_menu = uigtk2_radio_menu_new(uigtk2, "/MainMenu/HardwareMenu/KeymapMenu", (GCallback)set_keymap);
	uigtk2->joy_right_radio_menu = uigtk2_radio_menu_new(uigtk2, "/MainMenu/HardwareMenu/JoyRightMenu", (GCallback)set_joy_right);
	uigtk2->joy_left_radio_menu = uigtk2_radio_menu_new(uigtk2, "/MainMenu/HardwareMenu/JoyLeftMenu", (GCallback)set_joy_left);
	uigtk2->hkbd_layout_radio_menu = uigtk2_radio_menu_new(uigtk2, "/MainMenu/ToolMenu/HKBDLayoutMenu", (GCallback)set_hkbd_layout);
	uigtk2->hkbd_lang_radio_menu = uigtk2_radio_menu_new(uigtk2, "/MainMenu/ToolMenu/HKBDLangMenu", (GCallback)set_hkbd_lang);

	/* Update all dynamic menus */
	uigtk2_update_radio_menu_from_enum(uigtk2->tv_input_radio_menu, machine_tv_input_list, "tv-input-%s", NULL, 0);
	uigtk2_update_radio_menu_from_enum(uigtk2->ccr_radio_menu, vo_cmp_ccr_list, "ccr-%s", NULL, 0);
	ui->update_machine_menu = DELEGATE_AS0(void, gtk2_update_machine_menu, uigtk2);
	ui->update_cartridge_menu = DELEGATE_AS0(void, gtk2_update_cartridge_menu, uigtk2);
	ui->update_joystick_menus = DELEGATE_AS0(void, gtk2_update_joystick_menus, uigtk2);
	gtk2_update_machine_menu(uigtk2);
	gtk2_update_cartridge_menu(uigtk2);
	uigtk2_update_radio_menu_from_enum(uigtk2->keymap_radio_menu, machine_keyboard_list, "machine-keyboard-%s", NULL, 0);
	gtk2_update_joystick_menus(uigtk2);
	uigtk2_update_radio_menu_from_enum(uigtk2->hkbd_layout_radio_menu, hkbd_layout_list, "hkbd-layout-%s", NULL, xroar.cfg.kbd.layout);
	uigtk2_update_radio_menu_from_enum(uigtk2->hkbd_lang_radio_menu, hkbd_lang_list, "hkbd-lang-%s", NULL, xroar.cfg.kbd.lang);

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

	// File requester
	struct module *fr_module = module_select_by_arg(gtk2_filereq_module_list, ui_cfg->filereq);
	if (fr_module == &filereq_gtk2_module) {
		ui->filereq_interface = module_init(fr_module, uigtk2);
	} else {
		ui->filereq_interface = module_init(fr_module, NULL);
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
	DELEGATE_SAFE_CALL(uigtk2->public.filereq_interface->free);
	uigtk2_radio_menu_free(uigtk2->hkbd_lang_radio_menu);
	uigtk2_radio_menu_free(uigtk2->hkbd_layout_radio_menu);
	uigtk2_radio_menu_free(uigtk2->joy_left_radio_menu);
	uigtk2_radio_menu_free(uigtk2->joy_right_radio_menu);
	uigtk2_radio_menu_free(uigtk2->keymap_radio_menu);
	uigtk2_radio_menu_free(uigtk2->cart_radio_menu);
	uigtk2_radio_menu_free(uigtk2->machine_radio_menu);
	uigtk2_radio_menu_free(uigtk2->ccr_radio_menu);
	uigtk2_radio_menu_free(uigtk2->tv_input_radio_menu);
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
		uigtk2_notify_radio_menu_set_current_value(uigtk2->machine_radio_menu, value);
		break;

	case ui_tag_cartridge:
		uigtk2_notify_radio_menu_set_current_value(uigtk2->cart_radio_menu, value);
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
		uigtk2_notify_radio_menu_set_current_value(uigtk2->ccr_radio_menu, value);
		gtk2_vo_update_cmp_renderer(uigtk2, value);
		break;

	case ui_tag_tv_input:
		uigtk2_notify_radio_menu_set_current_value(uigtk2->tv_input_radio_menu, value);
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
		uigtk2_notify_radio_menu_set_current_value(uigtk2->keymap_radio_menu, value);
		break;

	case ui_tag_hkbd_layout:
		uigtk2_notify_radio_menu_set_current_value(uigtk2->hkbd_layout_radio_menu, value);
		break;

	case ui_tag_hkbd_lang:
		uigtk2_notify_radio_menu_set_current_value(uigtk2->hkbd_lang_radio_menu, value);
		break;

	case ui_tag_kbd_translate:
		uigtk2_notify_toggle_action_set_active(uigtk2, "/MainMenu/ToolMenu/TranslateKeyboard", value ? TRUE : FALSE, toggle_keyboard_translation);
		break;

	/* Joysticks */

	case ui_tag_joy_right:
		{
			struct joystick_config *jc = joystick_config_by_name(data);
			uigtk2_notify_radio_menu_set_current_value(uigtk2->joy_right_radio_menu, jc ? jc->id : (unsigned)-1);
		}
		break;

	case ui_tag_joy_left:
		{
			struct joystick_config *jc = joystick_config_by_name(data);
			uigtk2_notify_radio_menu_set_current_value(uigtk2->joy_left_radio_menu, jc ? jc->id : (unsigned)-1);
		}
		break;

	default:
		break;
	}
}

// Dynamic machine menu

static void gtk2_update_machine_menu(void *sptr) {
	struct ui_gtk2_interface *uigtk2 = sptr;
	struct uigtk2_radio_menu *rm = uigtk2->machine_radio_menu;

	// Get list of machine configs
	struct slist *mcl = slist_reverse(slist_copy(machine_config_list()));
	int num_machines = slist_length(mcl);

	// Remove old entries
	uigtk2_free_action_group(rm->action_group);
	gtk_ui_manager_remove_ui(uigtk2->menu_manager, rm->merge_id);

	// Jump through alloc hoops just to avoid const-ness warnings
	GtkRadioActionEntry *radio_entries = g_malloc0(num_machines * sizeof(*radio_entries));
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
		gtk_ui_manager_add_ui(uigtk2->menu_manager, rm->merge_id, "/MainMenu/HardwareMenu/MachineMenu", radio_entries[i].name, radio_entries[i].name, GTK_UI_MANAGER_MENUITEM, TRUE);
	}
	gtk_action_group_add_radio_actions(rm->action_group, radio_entries, num_machines, selected, (GCallback)set_machine, uigtk2);

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
	struct uigtk2_radio_menu *rm = uigtk2->cart_radio_menu;

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
	uigtk2_free_action_group(rm->action_group);
	gtk_ui_manager_remove_ui(uigtk2->menu_manager, rm->merge_id);

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
		gtk_ui_manager_add_ui(uigtk2->menu_manager, rm->merge_id, "/MainMenu/HardwareMenu/CartridgeMenu", radio_entries[i].name, radio_entries[i].name, GTK_UI_MANAGER_MENUITEM, TRUE);
	}
	radio_entries[num_carts].name = "cart0";
	radio_entries[num_carts].label = "None";
	radio_entries[num_carts].value = -1;
	gtk_ui_manager_add_ui(uigtk2->menu_manager, rm->merge_id, "/MainMenu/HardwareMenu/CartridgeMenu", radio_entries[num_carts].name, radio_entries[num_carts].name, GTK_UI_MANAGER_MENUITEM, TRUE);
	gtk_action_group_add_radio_actions(rm->action_group, radio_entries, num_carts+1, selected, (GCallback)set_cart, uigtk2);

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

// Dynamic joystick menus

static void update_joystick_menu(struct ui_gtk2_interface *uigtk2,
				 struct uigtk2_radio_menu *rm,
				 const char *name_fmt, const char *name0) {
	// Get list of joystick configs
	struct slist *jcl = slist_reverse(slist_copy(joystick_config_list()));

	int num_joystick_configs = slist_length(jcl);

	// Remove old entries
	uigtk2_free_action_group(rm->action_group);
	gtk_ui_manager_remove_ui(uigtk2->menu_manager, rm->merge_id);

	// Jump through alloc hoops just to avoid const-ness warnings.
	GtkRadioActionEntry *radio_entries = g_malloc0((num_joystick_configs+1) * sizeof(*radio_entries));
	gchar **names = g_malloc0(num_joystick_configs * sizeof(gchar *));
	gchar **labels = g_malloc0(num_joystick_configs * sizeof(gchar *));

	// Add new entries in reverse order, as each will be inserted before
	// the previous.
	int i = 0;
	for (struct slist *iter = jcl; iter; iter = iter->next, i++) {
		struct joystick_config *jc = iter->data;
		names[i] = g_strdup_printf(name_fmt, i+1);
		radio_entries[i].name = names[i];
		labels[i] = escape_underscores(jc->description);
		radio_entries[i].label = labels[i];
		radio_entries[i].value = jc->id;
		gtk_ui_manager_add_ui(uigtk2->menu_manager, rm->merge_id, rm->path, radio_entries[i].name, radio_entries[i].name, GTK_UI_MANAGER_MENUITEM, TRUE);
	}
	radio_entries[num_joystick_configs].name = name0;
	radio_entries[num_joystick_configs].label = "None";
	radio_entries[num_joystick_configs].value = -1;

	gtk_ui_manager_add_ui(uigtk2->menu_manager, rm->merge_id, rm->path, radio_entries[num_joystick_configs].name, radio_entries[num_joystick_configs].name, GTK_UI_MANAGER_MENUITEM, TRUE);
	gtk_action_group_add_radio_actions(rm->action_group, radio_entries, num_joystick_configs+1, 0, rm->callback, uigtk2);

	// Back through the hoops
	for (i = 0; i < num_joystick_configs; i++) {
		g_free(names[i]);
		g_free(labels[i]);
	}
	g_free(names);
	g_free(labels);
	g_free(radio_entries);
	slist_free(jcl);
}

static void gtk2_update_joystick_menus(void *sptr) {
	struct ui_gtk2_interface *uigtk2 = sptr;

	update_joystick_menu(uigtk2, uigtk2->joy_right_radio_menu, "rjoy%d", "rjoy0");
	update_joystick_menu(uigtk2, uigtk2->joy_left_radio_menu, "ljoy%d", "ljoy0");
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
