/** \file
 *
 *  \brief Windows user-interface module.
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

#include "top-config.h"

#include <windows.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <commctrl.h>

#include <SDL.h>
#include <SDL_syswm.h>

#include "array.h"
#include "slist.h"
#include "xalloc.h"

#include "cart.h"
#include "events.h"
#include "joystick.h"
#include "keyboard.h"
#include "logging.h"
#include "machine.h"
#include "module.h"
#include "tape.h"
#include "ui.h"
#include "vdisk.h"
#include "vo.h"
#include "xroar.h"

#include "sdl2/common.h"
#include "windows32/common_windows32.h"
#include "windows32/dialogs.h"
#include "windows32/drivecontrol.h"
#include "windows32/tapecontrol.h"
#include "windows32/video_options.h"

#define TAG(t) (((t) & 0x7f) << 8)
#define TAGV(t,v) (TAG(t) | ((v) & 0xff))
#define TAG_TYPE(t) (((t) >> 8) & 0x7f)
#define TAG_VALUE(t) ((t) & 0xff)

static int max_machine_id = 0;
static int max_cartridge_id = 0;

static struct {
	const char *name;
	const char *description;
} const joystick_names[] = {
	{ NULL, "None" },
	{ "joy0", "Joystick 0" },
	{ "joy1", "Joystick 1" },
	{ "kjoy0", "Keyboard" },
	{ "mjoy0", "Mouse" },
};
#define NUM_JOYSTICK_NAMES ARRAY_N_ELEMENTS(joystick_names)

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/* Note: prefer the default order for sound and joystick modules, which
 * will include the SDL options. */

static HMENU top_menu;

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static HWND about_dialog = NULL;
static WNDPROC sdl_window_proc = NULL;
static INT_PTR CALLBACK about_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static HMENU machine_menu = NULL;
static HMENU cartridge_menu = NULL;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void setup_file_menu(void);
static void setup_view_menu(void);
static void setup_hardware_menu(struct ui_sdl2_interface *uisdl2);
static void setup_tool_menu(void);
static void setup_help_menu(void);

void windows32_create_menus(struct ui_sdl2_interface *uisdl2) {
	top_menu = CreateMenu();
	setup_file_menu();
	setup_view_menu();
	setup_hardware_menu(uisdl2);
	setup_tool_menu();
	setup_help_menu();
	windows32_dc_create_window(uisdl2);
	windows32_tc_create_window(uisdl2);
	windows32_vo_create_window(uisdl2);
}

void windows32_destroy_menus(struct ui_sdl2_interface *uisdl2) {
	(void)uisdl2;
	DestroyMenu(top_menu);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void setup_file_menu(void) {
	HMENU file_menu;

	file_menu = CreatePopupMenu();

	AppendMenu(file_menu, MF_STRING, TAGV(ui_tag_action, ui_action_file_run), "&Run...");
	AppendMenu(file_menu, MF_STRING, TAGV(ui_tag_action, ui_action_file_load), "&Load...");

	AppendMenu(file_menu, MF_SEPARATOR, 0, NULL);
	AppendMenu(file_menu, MF_STRING, TAGV(ui_tag_action, ui_action_file_save_snapshot), "&Save Snapshot...");
	AppendMenu(file_menu, MF_SEPARATOR, 0, NULL);
	AppendMenu(file_menu, MF_STRING, TAGV(ui_tag_action, ui_action_quit), "&Quit");

	AppendMenu(top_menu, MF_STRING | MF_POPUP, (UINT_PTR)file_menu, "&File");
}

static void setup_view_menu(void) {
	HMENU view_menu;
	HMENU submenu;

	view_menu = CreatePopupMenu();

	submenu = CreatePopupMenu();
	AppendMenu(view_menu, MF_STRING | MF_POPUP, (UINT_PTR)submenu, "&TV Input");
	for (int i = 0; machine_tv_input_list[i].name; i++) {
		if (!machine_tv_input_list[i].description)
			continue;
		AppendMenu(submenu, MF_STRING, TAGV(ui_tag_tv_input, machine_tv_input_list[i].value), machine_tv_input_list[i].description);
	}

	submenu = CreatePopupMenu();
	AppendMenu(view_menu, MF_STRING | MF_POPUP, (UINT_PTR)submenu, "Composite &Rendering");
	AppendMenu(submenu, MF_STRING, TAGV(ui_tag_ccr, VO_CMP_CCR_PALETTE), "None");
	AppendMenu(submenu, MF_STRING, TAGV(ui_tag_ccr, VO_CMP_CCR_2BIT), "Simple (2-bit LUT)");
	AppendMenu(submenu, MF_STRING, TAGV(ui_tag_ccr, VO_CMP_CCR_5BIT), "5-bit LUT");
	AppendMenu(submenu, MF_STRING, TAGV(ui_tag_ccr, VO_CMP_CCR_PARTIAL), "Partial NTSC");
	AppendMenu(submenu, MF_STRING, TAGV(ui_tag_ccr, VO_CMP_CCR_SIMULATED), "Simulated");

	AppendMenu(view_menu, MF_STRING, TAG(ui_tag_tv_controls), "TV &Controls");

	AppendMenu(view_menu, MF_SEPARATOR, 0, NULL);
	AppendMenu(view_menu, MF_STRING, TAG(ui_tag_vdg_inverse), "&Inverse Text");

	AppendMenu(view_menu, MF_SEPARATOR, 0, NULL);
	submenu = CreatePopupMenu();
	AppendMenu(view_menu, MF_STRING | MF_POPUP, (UINT_PTR)submenu, "Zoom");
	AppendMenu(submenu, MF_STRING, TAGV(ui_tag_action, ui_action_zoom_in), "Zoom In");
	AppendMenu(submenu, MF_STRING, TAGV(ui_tag_action, ui_action_zoom_out), "Zoom Out");

	AppendMenu(view_menu, MF_SEPARATOR, 0, NULL);
	AppendMenu(view_menu, MF_STRING, TAG(ui_tag_fullscreen), "&Full Screen");

	AppendMenu(top_menu, MF_STRING | MF_POPUP, (UINT_PTR)view_menu, "&View");
}

static void setup_hardware_menu(struct ui_sdl2_interface *uisdl2) {
	HMENU hardware_menu;
	HMENU submenu;

	hardware_menu = CreatePopupMenu();

	machine_menu = submenu = CreatePopupMenu();
	AppendMenu(hardware_menu, MF_STRING | MF_POPUP, (UINT_PTR)submenu, "Machine");

	AppendMenu(hardware_menu, MF_SEPARATOR, 0, NULL);
	cartridge_menu = submenu = CreatePopupMenu();
	AppendMenu(hardware_menu, MF_STRING | MF_POPUP, (UINT_PTR)submenu, "Cartridge");

	AppendMenu(hardware_menu, MF_SEPARATOR, 0, NULL);
	submenu = CreatePopupMenu();
	AppendMenu(hardware_menu, MF_STRING | MF_POPUP, (UINT_PTR)submenu, "Keyboard Map");
	AppendMenu(submenu, MF_STRING, TAGV(ui_tag_keymap, dkbd_layout_dragon), "Dragon Layout");
	AppendMenu(submenu, MF_STRING, TAGV(ui_tag_keymap, dkbd_layout_dragon200e), "Dragon 200-E Layout");
	AppendMenu(submenu, MF_STRING, TAGV(ui_tag_keymap, dkbd_layout_coco), "CoCo Layout");
	AppendMenu(submenu, MF_STRING, TAGV(ui_tag_keymap, dkbd_layout_coco3), "CoCo 3 Layout");
	AppendMenu(submenu, MF_STRING, TAGV(ui_tag_keymap, dkbd_layout_mc10), "MC-10 Layout");
	AppendMenu(submenu, MF_STRING, TAGV(ui_tag_keymap, dkbd_layout_alice), "Alice Layout");

	AppendMenu(hardware_menu, MF_SEPARATOR, 0, NULL);
	submenu = CreatePopupMenu();
	AppendMenu(hardware_menu, MF_STRING | MF_POPUP, (UINT_PTR)submenu, "Right Joystick");
	for (unsigned i = 0; i < NUM_JOYSTICK_NAMES; i++) {
		AppendMenu(submenu, MF_STRING, TAGV(ui_tag_joy_right, i), joystick_names[i].description);
	}
	submenu = CreatePopupMenu();
	AppendMenu(hardware_menu, MF_STRING | MF_POPUP, (UINT_PTR)submenu, "Left Joystick");
	for (unsigned i = 0; i < NUM_JOYSTICK_NAMES; i++) {
		AppendMenu(submenu, MF_STRING, TAGV(ui_tag_joy_left, i), joystick_names[i].description);
	}
	AppendMenu(hardware_menu, MF_STRING, TAGV(ui_tag_action, ui_action_joystick_swap), "Swap Joysticks");

	AppendMenu(hardware_menu, MF_SEPARATOR, 0, NULL);
	AppendMenu(hardware_menu, MF_STRING, TAGV(ui_tag_action, ui_action_reset_soft), "Soft Reset");
	AppendMenu(hardware_menu, MF_STRING, TAGV(ui_tag_action, ui_action_reset_hard), "Hard Reset");

	AppendMenu(top_menu, MF_STRING | MF_POPUP, (UINT_PTR)hardware_menu, "&Hardware");

	windows32_ui_update_state(uisdl2, ui_tag_machine, xroar_machine_config ? xroar_machine_config->id : 0, NULL);
	struct cart *cart = xroar_machine ? xroar_machine->get_interface(xroar_machine, "cart") : NULL;
	windows32_ui_update_state(uisdl2, ui_tag_cartridge, cart ? cart->config->id : 0, NULL);
}

static void setup_tool_menu(void) {
	HMENU tool_menu;

	tool_menu = CreatePopupMenu();

	AppendMenu(tool_menu, MF_STRING, TAG(ui_tag_kbd_translate), "&Keyboard Translation");
	AppendMenu(tool_menu, MF_STRING, TAG(ui_tag_drive_control), "&Drive Control");
	AppendMenu(tool_menu, MF_STRING, TAG(ui_tag_tape_control), "&Tape Control");
	AppendMenu(tool_menu, MF_STRING, TAG(ui_tag_ratelimit), "&Rate Limit");

	AppendMenu(top_menu, MF_STRING | MF_POPUP, (UINT_PTR)tool_menu, "&Tool");
}

static void setup_help_menu(void) {
	HMENU help_menu;

	help_menu = CreatePopupMenu();
	AppendMenu(help_menu, MF_STRING, TAG(ui_tag_about), "About");

	AppendMenu(top_menu, MF_STRING | MF_POPUP, (UINT_PTR)help_menu, "&Help");
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void windows32_update_machine_menu(void *sptr) {
	(void)sptr;
	// Get list of machine configs
	struct slist *mcl = machine_config_list();
	// Note: this list is not a copy, so does not need freeing

	// Remove old entries
	while (DeleteMenu(machine_menu, 0, MF_BYPOSITION))
		;

	// Add new entries
	max_machine_id = 0;
	while (mcl) {
		struct machine_config *mc = mcl->data;
		if (mc->id > max_machine_id)
			max_machine_id = mc->id;
		AppendMenu(machine_menu, MF_STRING, TAGV(ui_tag_machine, mc->id), mc->description);
		mcl = mcl->next;
	}
}

void windows32_update_cartridge_menu(void *sptr) {
	(void)sptr;
	// Get list of cart configs
	struct slist *ccl = NULL;
	if (xroar_machine) {
		const struct machine_partdb_extra *mpe = xroar_machine->part.partdb->extra[0];
		const char *cart_arch = mpe->cart_arch;
		ccl = cart_config_list_is_a(cart_arch);
	}

	// Remove old entries
	while (DeleteMenu(cartridge_menu, 0, MF_BYPOSITION))
		;

	// Add new entries
	AppendMenu(cartridge_menu, MF_STRING, TAGV(ui_tag_cartridge, 0), "None");
	max_cartridge_id = 0;
	for (struct slist *iter = ccl; iter; iter = iter->next) {
		struct cart_config *cc = iter->data;
		if ((cc->id + 1) > max_cartridge_id)
			max_cartridge_id = cc->id + 1;
		AppendMenu(cartridge_menu, MF_STRING, TAGV(ui_tag_cartridge, cc->id + 1), cc->description);
	}
	slist_free(ccl);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void sdl_windows32_handle_syswmevent(SDL_SysWMmsg *wmmsg) {
	HWND hwnd = wmmsg->msg.win.hwnd;
	UINT msg = wmmsg->msg.win.msg;
	WPARAM wParam = wmmsg->msg.win.wParam;

	if (msg != WM_COMMAND)
		return;

	int tag = LOWORD(wParam);
	int tag_type = TAG_TYPE(tag);
	int tag_value = TAG_VALUE(tag);

	switch (tag_type) {

	// Simple actions:
	case ui_tag_action:
		switch (tag_value) {
		case ui_action_quit:
			{
				SDL_Event event;
				event.type = SDL_QUIT;
				SDL_PushEvent(&event);
			}
			break;
		case ui_action_reset_soft:
			xroar_soft_reset();
			break;
		case ui_action_reset_hard:
			xroar_hard_reset();
			break;
		case ui_action_file_run:
			xroar_run_file(NULL);
			break;
		case ui_action_file_load:
			xroar_load_file(NULL);
			break;
		case ui_action_file_save_snapshot:
			xroar_save_snapshot();
			break;
		case ui_action_tape_input:
			xroar_insert_input_tape();
			break;
		case ui_action_tape_input_rewind:
			if (xroar_tape_interface && xroar_tape_interface->tape_input)
				tape_rewind(xroar_tape_interface->tape_input);
			break;
		case ui_action_tape_output:
			xroar_insert_output_tape();
			break;

		case ui_action_tape_output_rewind:
			if (xroar_tape_interface && xroar_tape_interface->tape_output)
				tape_rewind(xroar_tape_interface->tape_output);
			break;
		case ui_action_tape_play_pause:
			tape_set_playing(xroar_tape_interface, !(GetMenuState(top_menu, TAGV(ui_tag_action, ui_action_tape_play_pause), MF_BYCOMMAND) & MF_CHECKED), 1);
			break;
		case ui_action_zoom_in:
			sdl_zoom_in(global_uisdl2);
			break;
		case ui_action_zoom_out:
			sdl_zoom_out(global_uisdl2);
			break;
		case ui_action_joystick_swap:
			xroar_swap_joysticks(1);
			break;
		default:
			break;
		}
		break;

	// Machines:
	case ui_tag_machine:
		xroar_set_machine(1, tag_value);
		break;

	// Cartridges:
	case ui_tag_cartridge:
		{
			struct cart_config *cc = cart_config_by_id(tag_value - 1);
			xroar_set_cart(1, cc ? cc->name : NULL);
		}
		break;

	// Cassettes:
	case ui_tag_tape_control:
		windows32_tc_show_window(global_uisdl2);
		break;
	case ui_tag_tape_flags:
		tape_select_state(xroar_tape_interface, tape_get_state(xroar_tape_interface) ^ tag_value);
		break;

	// Disks:
	case ui_tag_drive_control:
		windows32_dc_show_window(global_uisdl2);
		break;
	case ui_tag_disk_insert:
		xroar_insert_disk(tag_value);
		break;
	case ui_tag_disk_new:
		xroar_new_disk(tag_value);
		break;
	case ui_tag_disk_write_enable:
		xroar_set_write_enable(1, tag_value, XROAR_NEXT);
		break;
	case ui_tag_disk_write_back:
		xroar_set_write_back(1, tag_value, XROAR_NEXT);
		break;
	case ui_tag_disk_eject:
		xroar_eject_disk(tag_value);
		break;

	// Video:

	// TV controls:
	case ui_tag_tv_controls:
		windows32_vo_show_window(global_uisdl2);
		break;

	case ui_tag_fullscreen:
		xroar_set_fullscreen(1, XROAR_NEXT);
		break;
	case ui_tag_ccr:
		xroar_set_ccr(1, tag_value);
		break;
	case ui_tag_tv_input:
		xroar_set_tv_input(1, tag_value);
		break;
	case ui_tag_vdg_inverse:
		xroar_set_vdg_inverted_text(1, XROAR_NEXT);
		break;

	// Audio:

	case ui_tag_ratelimit:
		xroar_set_ratelimit_latch(1, XROAR_NEXT);
		break;

	// Keyboard:
	case ui_tag_keymap:
		xroar_set_keyboard_type(1, tag_value);
		break;
	case ui_tag_kbd_translate:
		xroar_set_kbd_translate(1, XROAR_NEXT);
		break;

	// Joysticks:
	case ui_tag_joy_right:
		xroar_set_joystick(1, 0, joystick_names[tag_value].name);
		break;
	case ui_tag_joy_left:
		xroar_set_joystick(1, 1, joystick_names[tag_value].name);
		break;

	// Help:
	case ui_tag_about:
		if (!IsWindow(about_dialog)) {
			about_dialog = CreateDialog(NULL, MAKEINTRESOURCE(1), hwnd, (DLGPROC)about_proc);
			if (about_dialog) {
				ShowWindow(about_dialog, SW_SHOW);
			}
		}
		break;

	default:
		break;
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void windows32_ui_update_state(void *sptr, int tag, int value, const void *data) {
	struct ui_sdl2_interface *uisdl2 = sptr;
	switch (tag) {

	// Simple toggles

	case ui_tag_fullscreen:
	case ui_tag_vdg_inverse:
		CheckMenuItem(top_menu, TAG(tag), MF_BYCOMMAND | (value ? MF_CHECKED : MF_UNCHECKED));
		break;

	// Hardware

	case ui_tag_machine:
		CheckMenuRadioItem(top_menu, TAGV(tag, 0), TAGV(tag, max_machine_id), TAGV(tag, value), MF_BYCOMMAND);
		break;

	case ui_tag_cartridge:
		CheckMenuRadioItem(top_menu, TAGV(tag, 0), TAGV(tag, max_cartridge_id), TAGV(tag, value + 1), MF_BYCOMMAND);
		break;

	// Tape

	case ui_tag_tape_control:
		windows32_tc_show_window(global_uisdl2);
		break;

	case ui_tag_tape_flags:
		windows32_tc_update_tape_state(uisdl2, value);
		break;

	case ui_tag_tape_input_filename:
		windows32_tc_update_input_filename(uisdl2, (const char *)data);
		break;

	case ui_tag_tape_playing:
		windows32_tc_update_tape_playing(uisdl2, value);
		break;

	// Disk

	case ui_tag_drive_control:
		windows32_dc_show_window(global_uisdl2);
		break;

	case ui_tag_disk_data:
		windows32_dc_update_drive_disk(uisdl2, value, (const struct vdisk *)data);
		break;

	case ui_tag_disk_write_enable:
		windows32_dc_update_drive_write_enable(uisdl2, value, (intptr_t)data);
		break;

	case ui_tag_disk_write_back:
		windows32_dc_update_drive_write_back(uisdl2, value, (intptr_t)data);
		break;

	// Video

	case ui_tag_ccr:
		CheckMenuRadioItem(top_menu, TAGV(tag, 0), TAGV(tag, 3), TAGV(tag, value), MF_BYCOMMAND);
		break;

	case ui_tag_tv_input:
		CheckMenuRadioItem(top_menu, TAGV(tag, 0), TAGV(tag, 3), TAGV(tag, value), MF_BYCOMMAND);
		break;

	case ui_tag_tv_controls:
		windows32_vo_show_window(global_uisdl2);
		break;

	case ui_tag_brightness:
		windows32_vo_update_brightness(uisdl2, value);
		break;

	case ui_tag_contrast:
		windows32_vo_update_contrast(uisdl2, value);
		break;

	case ui_tag_saturation:
		windows32_vo_update_saturation(uisdl2, value);
		break;

	case ui_tag_hue:
		windows32_vo_update_hue(uisdl2, value);
		break;

	case ui_tag_picture:
		windows32_vo_update_picture(uisdl2, value);
		break;

	case ui_tag_ntsc_scaling:
		windows32_vo_update_ntsc_scaling(uisdl2, value);
		break;

	case ui_tag_cmp_fs:
		windows32_vo_update_cmp_fs(uisdl2, value);
		break;

	case ui_tag_cmp_fsc:
		windows32_vo_update_cmp_fsc(uisdl2, value);
		break;

	case ui_tag_cmp_system:
		windows32_vo_update_cmp_system(uisdl2, value);
		break;

	case ui_tag_cmp_colour_killer:
		windows32_vo_update_cmp_colour_killer(uisdl2, value);
		break;

	// Audio

	case ui_tag_ratelimit:
		CheckMenuItem(top_menu, TAG(tag), MF_BYCOMMAND | (value ? MF_CHECKED : MF_UNCHECKED));
		break;

	case ui_tag_gain:
		windows32_vo_update_volume(uisdl2, value);
		break;

	// Keyboard

	case ui_tag_keymap:
		CheckMenuRadioItem(top_menu, TAGV(tag, 0), TAGV(tag, (dkbd_num_layouts - 1)), TAGV(tag, value), MF_BYCOMMAND);
		break;

	case ui_tag_kbd_translate:
		CheckMenuItem(top_menu, TAG(tag), MF_BYCOMMAND | (value ? MF_CHECKED : MF_UNCHECKED));
		break;

	// Joysticks

	case ui_tag_joy_right:
	case ui_tag_joy_left:
		{
			int joy = 0;
			if (data) {
				for (unsigned i = 1; i < NUM_JOYSTICK_NAMES; i++) {
					if (0 == strcmp((const char *)data, joystick_names[i].name)) {
						joy = i;
						break;
					}
				}
			}
			CheckMenuRadioItem(top_menu, TAGV(tag, 0), TAGV(tag, NUM_JOYSTICK_NAMES - 1), TAGV(tag, joy), MF_BYCOMMAND);
		}
		break;

	default:
		break;

	}

}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// SDL integration.  The SDL2 video modules call out to these when
// WINDOWS32 is defined to add and remove the menu bar.

/* Get underlying window handle from SDL. */

static HWND get_hwnd(SDL_Window *w) {
	SDL_version sdlver;
	SDL_SysWMinfo sdlinfo;
	SDL_VERSION(&sdlver);
	sdlinfo.version = sdlver;
#ifdef HAVE_SDL2
	SDL_GetWindowWMInfo(w, &sdlinfo);
	return sdlinfo.info.win.window;
#else
	(void)w;
	SDL_GetWMInfo(&sdlinfo);
	return sdlinfo.window;
#endif
}

/* Custom window event handler to intercept menu selections. */

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	SDL_Event event;
	SDL_SysWMmsg wmmsg;

	switch (msg) {

	case WM_COMMAND:
		// Selectively push WM events onto the SDL queue.
#ifdef HAVE_SDL2
		wmmsg.msg.win.hwnd = hwnd;
		wmmsg.msg.win.msg = msg;
		wmmsg.msg.win.wParam = wParam;
		wmmsg.msg.win.lParam = lParam;
#else
		wmmsg.hwnd = hwnd;
		wmmsg.msg = msg;
		wmmsg.wParam = wParam;
		wmmsg.lParam = lParam;
#endif
		event.type = SDL_SYSWMEVENT;
		event.syswm.msg = &wmmsg;
		SDL_PushEvent(&event);
		break;

	case WM_UNINITMENUPOPUP:
		DELEGATE_SAFE_CALL(xroar_vo_interface->draw);
		return CallWindowProc(sdl_window_proc, hwnd, msg, wParam, lParam);

	default:
		// Fall back to original SDL handler for anything else -
		// SysWMEvent handling is not enabled, so this should not flood
		// the queue.
		return CallWindowProc(sdl_window_proc, hwnd, msg, wParam, lParam);

	}
	return 0;
}

/* While the menu is being navigated, the main application is blocked. If event
 * processing is enabled for SysWMEvent, SDL quickly runs out of space in its
 * event queue, leading to the ultimate menu option often being missed.  This
 * sets up a custom Windows event handler that pushes a SDL_SysWMEvent only for
 * WM_COMMAND messages. */

void sdl_windows32_set_events_window(SDL_Window *sw) {
	HWND hwnd = get_hwnd(sw);
	WNDPROC old_window_proc = (WNDPROC)GetWindowLongPtr(hwnd, GWLP_WNDPROC);
	if (old_window_proc != window_proc) {
		// Preserve SDL's "windowproc"
		sdl_window_proc = old_window_proc;
		// Set my own to process wm events.  Without this, the windows menu
		// blocks and the internal SDL event queue overflows, causing missed
		// selections.
		SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)window_proc);
		// Explicitly disable SDL processing of these events
		SDL_EventState(SDL_SYSWMEVENT, SDL_DISABLE);
	}
	windows32_main_hwnd = hwnd;
}

/* Add menubar to window. This will reduce the size of the client area while
 * leaving the window size the same, so the video module should then resize
 * itself to account for this. */

void sdl_windows32_add_menu(SDL_Window *sw) {
	HWND hwnd = get_hwnd(sw);
	fprintf(stderr, "sdl_windows32_add_menu()\n");
	if (GetMenu(hwnd) != NULL) {
		fprintf(stderr, "\tmenu already present - skipping\n");
		return;
	}
	int w, h;
	SDL_GetWindowSize(sw, &w, &h);
	fprintf(stderr, "\tbefore: %dx%d\n", w, h);
	SetMenu(hwnd, top_menu);
	SDL_GetWindowSize(sw, &w, &h);
	fprintf(stderr, "\tafter: %dx%d\n", w, h);
}

/* Remove menubar from window. */

void sdl_windows32_remove_menu(SDL_Window *sw) {
	HWND hwnd = get_hwnd(sw);
	fprintf(stderr, "sdl_windows32_remove_menu()\n");
	if (GetMenu(hwnd) == NULL) {
		fprintf(stderr, "\tmenu not present - skipping\n");
		return;
	}
	int w, h;
	SDL_GetWindowSize(sw, &w, &h);
	fprintf(stderr, "\tbefore: %dx%d\n", w, h);
	SetMenu(hwnd, NULL);
	SDL_GetWindowSize(sw, &w, &h);
	fprintf(stderr, "\tafter: %dx%d\n", w, h);
}

static INT_PTR CALLBACK about_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	(void)lParam;
	switch (msg) {

	case WM_INITDIALOG:
		return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK:
		case IDCANCEL:
			DestroyWindow(hwnd);
			about_dialog = NULL;
			return TRUE;

		default:
			break;
		}

	default:
		break;
	}
	return FALSE;
}
