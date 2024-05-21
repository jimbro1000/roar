/** \file
 *
 *  \brief Video ouput modules & interfaces.
 *
 *  \copyright Copyright 2003-2023 Ciaran Anscomb
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
 *
 *  Successfully initialising a video module returns a (struct vo_interface),
 *  which is used by various parts of XRoar to do different things:
 *
 *  - The UI may ask it to resize, toggle menubar, etc.
 *
 *  - Selecting a machine may define colour palettes and select how things are
 *    to be rendered.
 *
 *  - While running, the emulated machine will use it to render scanlines,
 *    indicate vertical sync, or just ask to refresh the screen.
 *
 *  Palette entries are specified either as YPbPr (Y scaled 0-1, Pb and Pr
 *  scaled ±0.5) or as RGB (each scaled 0-1).
 */

#ifndef XROAR_VO_H_
#define XROAR_VO_H_

#include <stdint.h>

#include "delegate.h"

#include "vo_render.h"
#include "xconfig.h"

struct module;

// Monitor input signal

enum {
	VO_SIGNAL_SVIDEO,
	VO_SIGNAL_CMP,
	VO_SIGNAL_RGB,
	NUM_VO_SIGNAL
};

// Picture area

enum {
	VO_PICTURE_ZOOMED,
	VO_PICTURE_TITLE,
	VO_PICTURE_ACTION,
	VO_PICTURE_UNDERSCAN,
	NUM_VO_PICTURE
};

extern const char *vo_picture_name[NUM_VO_PICTURE];

// Composite cross-colour renderer.

enum {
	VO_CMP_CCR_PALETTE,
	VO_CMP_CCR_2BIT,
	VO_CMP_CCR_5BIT,
	VO_CMP_CCR_PARTIAL,
	VO_CMP_CCR_SIMULATED,
	NUM_VO_CMP_CCR
};

struct vo_cfg {
	char *geometry;
	int gl_filter;
	int pixel_fmt;
	_Bool fullscreen;
	_Bool colour_killer;
};

// Window Area is the obvious top level.  Defined in host screen pixels, and
// mainly of interest to the video modules.  Most toolkits can present the Draw
// Area as a target directly, so may not need to record the whole window
// dimensions.

struct vo_window_area {
	int w, h;
};

// Draw Area is the space within the Window Area that we're allowed to draw
// into.  This may account for areas used by a menu bar, for example.  Also
// defined in host screen pixels.

struct vo_draw_area {
	int x, y;
	int w, h;
};

// Picture Area is the largest 4:3 region contained within the Draw Area.  A
// renderer's Picture Area will be copied (probably with scaling) into this
// region.  Also defined in host screen pixels.  Origin defined relative to
// Window Area.

struct vo_picture_area {
	int x, y;
	int w, h;
};

// Viewport and Active Area defined in vo_render.h.

struct vo_interface {
	_Bool is_fullscreen;
	_Bool show_menubar;

	// Renderer
	struct vo_render *renderer;

	// Selected input signal
	int signal;      // VO_SIGNAL_*

	// Selected cross-colour renderer
	int cmp_ccr;    // VO_CMP_CCR_*

	// Current defined picture area
	int picture;    // VO_PICTURE_*

	// Called by vo_free before freeing the struct to handle
	// module-specific allocations
	DELEGATE_T0(void) free;

	// Used by UI to adjust viewing parameters

	// Resize window
	//     unsigned w, h;  // dimensions in pixels
	DELEGATE_T2(void, unsigned, unsigned) resize;

	// Configure viewport dimensions
	//     int w, h;  // dimensions in pixels/scanlines
	DELEGATE_T2(void, int, int) set_viewport;

	// Configure active area (used to centre display)
	//     int x, y;  // top-left of active area
	//     int w, h;  // size of active area
	DELEGATE_T4(void, int, int, int, int) set_active_area;

	// Set fullscreen mode on or off
	//     _Bool fullscreen;
	DELEGATE_T1(int, bool) set_fullscreen;

	// Set menubar on or off
	//     _Bool menubar;
	DELEGATE_T1(void, bool) set_menubar;

	// Set brightness
	//     int brightness;  // 0-100
	DELEGATE_T1(void, int) set_brightness;

	// Set contrast
	//     int contrast;  // 0-100
	DELEGATE_T1(void, int) set_contrast;

	// Set colour saturation
	//     int saturation;  // 0-100
	DELEGATE_T1(void, int) set_saturation;

	// Set hue
	//     int hue;  // -179 to +180
	DELEGATE_T1(void, int) set_hue;

	// Set cross-colour phase
	//     int phase;  // in degrees
	DELEGATE_T1(void, int) set_cmp_phase;

	// Used by machine to configure video output

	// Set how the chroma components relate to each other (in degrees)
	//     float chb_phase;  // øB phase, default 0°
	//     float cha_phase;  // øA phase, default 90°
	DELEGATE_T2(void, float, float) set_cmp_lead_lag;

	// Add a colour to the palette using Y', Pb, Pr values
	//     uint8_t index;    // palette index
	//     float y, pb, pr;  // colour
	DELEGATE_T4(void, uint8, float, float, float) palette_set_ybr;

	// Add a colour to the palette usine RGB values
	//     uint8_t index;  // palette index
	//     float R, G, B;  // colour
	DELEGATE_T4(void, uint8, float, float, float) palette_set_rgb;

	// Set a burst phase
	//     unsigned burstn;  // burst index
	//     int phase;        // in degrees
	DELEGATE_T2(void, unsigned, int) set_cmp_burst;

	// Set burst phase in terms of B'-Y' and R'-Y'
	//     unsigned burstn;  // burst index
	//     float b_y, r_y;
	DELEGATE_T3(void, unsigned, float, float) set_cmp_burst_br;

	// Set machine default cross-colour phase
	//     int phase;  // in degrees
	DELEGATE_T1(void, int) set_cmp_phase_offset;

	// Used by machine to render video

	// Currently selected line renderer
	//     unsigned burst;       // burst index for this line
	//     unsigned npixels;     // no. pixels in scanline
	//     const uint8_t *data;  // palettised data, NULL for dummy line
	DELEGATE_T3(void, unsigned, unsigned, uint8cp) render_line;

	// Draw the current buffer.  Called by vo_vsync() and vo_refresh().
	DELEGATE_T0(void) draw;
};

// Geometry handling

#define VO_GEOMETRY_W (1 << 0)
#define VO_GEOMETRY_H (1 << 1)
#define VO_GEOMETRY_X (1 << 2)
#define VO_GEOMETRY_Y (1 << 3)
#define VO_GEOMETRY_XNEGATIVE (1 << 4)
#define VO_GEOMETRY_YNEGATIVE (1 << 5)

struct vo_geometry {
	unsigned flags;
	int w, h;
	int x, y;
};

extern struct xconfig_enum vo_cmp_ccr_list[];
extern struct xconfig_enum vo_pixel_fmt_list[];

extern const uint8_t vo_cmp_lut_2bit[2][4][3];
extern const uint8_t vo_cmp_lut_5bit[2][32][3];

// Allocates at least enough space for (struct vo_interface)

void *vo_interface_new(size_t isize);

// Calls free() delegate then frees structure

void vo_free(void *);

// Set renderer and use its contents to prepopulate various delegates.  Call
// this before overriding any locally in video modules.

void vo_set_renderer(struct vo_interface *vo, struct vo_render *vr);

// Select input signal
//     int signal;  // VO_SIGNAL_*

void vo_set_signal(struct vo_interface *vo, int signal);

// Set picture area

void vo_set_viewport(struct vo_interface *vo, int picture);

inline void vo_set_ntsc_scaling(struct vo_interface *vo, _Bool notify, _Bool value) {
	vo_render_set_ntsc_scaling(vo->renderer, notify, value);
}

// Select cross-colour renderer
//     int ccr;  // VO_CMP_CCR_*

void vo_set_cmp_ccr(struct vo_interface *vo, _Bool notify, int value);

// Configure composite video

inline void vo_set_cmp_fs(struct vo_interface *vo, _Bool notify, int value) {
	vo_render_set_cmp_fs(vo->renderer, notify, value);
}

inline void vo_set_cmp_fsc(struct vo_interface *vo, _Bool notify, int value) {
	vo_render_set_cmp_fsc(vo->renderer, notify, value);
}

inline void vo_set_cmp_system(struct vo_interface *vo, _Bool notify, int value) {
	vo_render_set_cmp_system(vo->renderer, notify, value);
}

inline void vo_set_cmp_colour_killer(struct vo_interface *vo, _Bool notify, _Bool value) {
	vo_render_set_cmp_colour_killer(vo->renderer, notify, value);
}

// Vertical sync.  Calls any module-specific draw function if requested, then
// vo_render_vsync().  Called with draw=0 during frameskip, as we still want to
// count scanlines.

inline void vo_vsync(struct vo_interface *vo, _Bool draw) {
	if (draw)
		DELEGATE_SAFE_CALL(vo->draw);
	vo_render_vsync(vo->renderer);
}

// Refresh the display by calling draw().  Useful while single-stepping, where
// the usual render functions won't be called.

inline void vo_refresh(struct vo_interface *vo) {
	DELEGATE_SAFE_CALL(vo->draw);
}

// Helper function to parse geometry string

void vo_parse_geometry(const char *str, struct vo_geometry *geometry);

#endif
