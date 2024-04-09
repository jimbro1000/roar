/** \file
 *
 *  \brief SDL2 user-interface common functions.
 *
 *  \copyright Copyright 2015-2024 Ciaran Anscomb
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

#ifndef XROAR_SDL2_COMMON_H_
#define XROAR_SDL2_COMMON_H_

#include <stdint.h>

#include <SDL.h>
#include <SDL_syswm.h>

#include "ui.h"
#include "vo.h"

struct joystick_module;
struct keyboard_sdl2_state;

struct ui_sdl2_interface {
	struct ui_interface ui_interface;

	struct ui_cfg *cfg;

	// Shared SDL2 data
	SDL_Window *vo_window;
	Uint32 vo_window_id;

	// Window geometry
	struct vo_draw_area draw_area;
	// Viewport size not modified by 60Hz scaling
	struct vo_viewport viewport;
	// User-specified geometry inhibits auto-resize
	_Bool user_specified_geometry;

	// Keyboard state
	struct {
		// Translate scancode into emulator key
		int8_t scancode_to_dkey[SDL_NUM_SCANCODES];
		// Scancode preempts unicode translation, GUI
		_Bool scancode_preempt[SDL_NUM_SCANCODES];
		// Last unicode value determined for each scancode
		int unicode_last_scancode[SDL_NUM_SCANCODES];
		// Is a control key pressed that's not bound to a dkey?
		_Bool control;
	} keyboard;

	// Pointer state
	_Bool mouse_hidden;
};

extern struct ui_sdl2_interface *global_uisdl2;

struct ui_sdl2_interface *ui_sdl_allocate(size_t usize);
void ui_sdl_init(struct ui_sdl2_interface *, struct ui_cfg *ui_cfg);
void ui_sdl_free(void *);
void ui_sdl_run(void *);

_Bool sdl_vo_init(struct ui_sdl2_interface *);
void sdl_keyboard_init(struct ui_sdl2_interface *);

extern struct joystick_submodule sdl_js_submod_physical;
extern struct joystick_submodule sdl_js_submod_keyboard;
extern struct joystick_module sdl_js_internal;

extern struct module * const sdl2_vo_module_list[];
extern struct joystick_module * const sdl_js_modlist[];

int filter_sdl_events(void *userdata, SDL_Event *event);
void run_sdl_event_loop(struct ui_sdl2_interface *uisdl2);
void sdl_keypress(struct ui_sdl2_interface *uisdl2, SDL_Keysym *keysym);
void sdl_keyrelease(struct ui_sdl2_interface *uisdl2, SDL_Keysym *keysym);
void sdl_js_physical_shutdown(void);

void sdl_vo_notify_size_changed(struct ui_sdl2_interface *uisdl2, int w, int h);

void sdl_update_draw_area(struct ui_sdl2_interface *uisdl2, int w, int h);
void sdl_zoom_in(struct ui_sdl2_interface *uisdl2);
void sdl_zoom_out(struct ui_sdl2_interface *uisdl2);

/* Platform-specific support */

#ifdef HAVE_X11

/* X11 event interception. */

void sdl_x11_handle_syswmevent(SDL_SysWMmsg *);

/* X11 keyboard handling. */

void sdl_x11_keyboard_init(SDL_Window *sw);
void sdl_x11_keyboard_free(SDL_Window *sw);

void sdl_x11_mapping_notify(XMappingEvent *);
void sdl_x11_keymap_notify(XKeymapEvent *);

void sdl_x11_fix_keyboard_event(SDL_Event *);
int sdl_x11_keysym_to_unicode(SDL_Keysym *);

#endif

#ifdef HAVE_COCOA

void sdl_cocoa_keyboard_init(SDL_Window *);
int sdl_cocoa_keysym_to_unicode(SDL_Keysym *keysym);

#endif

#ifdef WINDOWS32

void sdl_windows32_keyboard_init(SDL_Window *);
int sdl_windows32_keysym_to_unicode(SDL_Keysym *);

/* These functions will be in the windows32-specific code. */

void sdl_windows32_handle_syswmevent(SDL_SysWMmsg *);
void sdl_windows32_set_events_window(SDL_Window *);
void sdl_windows32_add_menu(SDL_Window *);
void sdl_windows32_remove_menu(SDL_Window *);

#endif

/* Now wrap all of the above in inline functions so that common code doesn't
 * need to be littered with these conditionals. */

inline void sdl_os_keyboard_init(SDL_Window *sw) {
	(void)sw;
#if defined(HAVE_X11)
	sdl_x11_keyboard_init(sw);
#elif defined(HAVE_COCOA)
	sdl_cocoa_keyboard_init(sw);
#elif defined(WINDOWS32)
	sdl_windows32_keyboard_init(sw);
#endif
}

inline void sdl_os_keyboard_free(SDL_Window *sw) {
	(void)sw;
#if defined(HAVE_X11)
	sdl_x11_keyboard_free(sw);
#endif
}

inline void sdl_os_handle_syswmevent(SDL_SysWMmsg *wmmsg) {
	(void)wmmsg;
#if defined(HAVE_X11)
	sdl_x11_handle_syswmevent(wmmsg);
#elif defined(WINDOWS32)
	sdl_windows32_handle_syswmevent(wmmsg);
#endif
}

inline void sdl_os_fix_keyboard_event(SDL_Event *ev) {
	(void)ev;
#if defined(HAVE_X11)
	sdl_x11_fix_keyboard_event(ev);
#endif
}

inline int sdl_os_keysym_to_unicode(SDL_Keysym *keysym) {
#if defined(HAVE_X11)
	return sdl_x11_keysym_to_unicode(keysym);
#elif defined(HAVE_COCOA)
	return sdl_cocoa_keysym_to_unicode(keysym);
#elif defined(WINDOWS32)
	return sdl_windows32_keysym_to_unicode(keysym);
#endif
	return keysym->sym;
}

#endif
