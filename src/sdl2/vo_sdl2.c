/** \file
 *
 *  \brief SDL2 video output module.
 *
 *  \copyright Copyright 2015-2023 Ciaran Anscomb
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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "array.h"
#include "xalloc.h"

#include "logging.h"
#include "mc6847/mc6847.h"
#include "module.h"
#include "vo.h"
#include "vo_render.h"
#include "xroar.h"

#include "sdl2/common.h"

// MAX_VIEWPORT_* defines maximum viewport

#define MAX_VIEWPORT_WIDTH  (800)
#define MAX_VIEWPORT_HEIGHT (300)

static void *new(void *cfg);

struct module vo_sdl_module = {
	.name = "sdl", .description = "SDL2 video",
	.new = new,
};

struct vo_sdl_interface {
	struct vo_interface public;

	struct {
		// Format SDL is asked to make the texture
		Uint32 format;

		// Texture handle
		SDL_Texture *texture;

		// Size of one pixel, in bytes
		unsigned pixel_size;

		// Pixel buffer
		void *pixels;
	} texture;

	SDL_Renderer *sdl_renderer;
	int filter;

	struct vo_window_area window_area;
	struct vo_window_area picture_area;
	_Bool scale_60hz;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static const Uint32 renderer_flags[] = {
	SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC,
	SDL_RENDERER_ACCELERATED,
	SDL_RENDERER_SOFTWARE | SDL_RENDERER_PRESENTVSYNC,
	SDL_RENDERER_SOFTWARE
};

static void vo_sdl_free(void *sptr);
static void set_viewport(void *sptr, int vp_w, int vp_h);
static void draw(void *sptr);
static int set_fullscreen(void *sptr, _Bool fullscreen);
static void set_menubar(void *sptr, _Bool show_menubar);

static void notify_frame_rate(void *, _Bool is_60hz);

static void *new(void *sptr) {
	struct ui_sdl2_interface *uisdl2 = sptr;
	struct vo_cfg *vo_cfg = &uisdl2->cfg->vo_cfg;

	struct vo_sdl_interface *vosdl = vo_interface_new(sizeof(*vosdl));
	*vosdl = (struct vo_sdl_interface){0};
	struct vo_interface *vo = &vosdl->public;

	switch (vo_cfg->pixel_fmt) {
	default:
		vo_cfg->pixel_fmt = VO_RENDER_FMT_RGBA8;
		// fall through

	case VO_RENDER_FMT_RGBA8:
		vosdl->texture.format = SDL_PIXELFORMAT_RGBA8888;
		vosdl->texture.pixel_size = 4;
		break;

	case VO_RENDER_FMT_BGRA8:
		vosdl->texture.format = SDL_PIXELFORMAT_BGRA8888;
		vosdl->texture.pixel_size = 4;
		break;

	case VO_RENDER_FMT_ARGB8:
		vosdl->texture.format = SDL_PIXELFORMAT_ARGB8888;
		vosdl->texture.pixel_size = 4;
		break;

	case VO_RENDER_FMT_ABGR8:
		vosdl->texture.format = SDL_PIXELFORMAT_ABGR8888;
		vosdl->texture.pixel_size = 4;
		break;

	case VO_RENDER_FMT_RGB565:
		vosdl->texture.format = SDL_PIXELFORMAT_RGB565;
		vosdl->texture.pixel_size = 2;
		break;

	case VO_RENDER_FMT_RGBA4:
		vosdl->texture.format = SDL_PIXELFORMAT_RGBA4444;
		vosdl->texture.pixel_size = 2;
		break;
	}

	struct vo_render *vr = vo_render_new(vo_cfg->pixel_fmt);
	vr->cmp.colour_killer = vo_cfg->colour_killer;

	vo_set_renderer(vo, vr);

	vosdl->texture.pixels = xmalloc(MAX_VIEWPORT_WIDTH * MAX_VIEWPORT_HEIGHT * vosdl->texture.pixel_size);
	vo_render_set_buffer(vr, vosdl->texture.pixels);
	memset(vosdl->texture.pixels, 0, MAX_VIEWPORT_WIDTH * MAX_VIEWPORT_HEIGHT * vosdl->texture.pixel_size);

	vosdl->filter = vo_cfg->gl_filter;

	vo->free = DELEGATE_AS0(void, vo_sdl_free, vosdl);

	// Used by UI to adjust viewing parameters
	vo->set_viewport = DELEGATE_AS2(void, int, int, set_viewport, vosdl);
	vo->set_fullscreen = DELEGATE_AS1(int, bool, set_fullscreen, vosdl);
	vo->set_menubar = DELEGATE_AS1(void, bool, set_menubar, vosdl);

	vr->notify_frame_rate = DELEGATE_AS1(void, bool, notify_frame_rate, vosdl);

	// Used by machine to render video
	vo->draw = DELEGATE_AS0(void, draw, vosdl);

	vosdl->window_area.w = 640;
	vosdl->window_area.h = 480;
	global_uisdl2->viewport.w = 640;
	global_uisdl2->viewport.h = 240;
	if (vo_cfg->geometry) {
		struct vo_geometry geometry;
		vo_parse_geometry(vo_cfg->geometry, &geometry);
		if (geometry.flags & VO_GEOMETRY_W)
			vosdl->window_area.w = geometry.w;
		if (geometry.flags & VO_GEOMETRY_H)
			vosdl->window_area.h = geometry.h;
	}

	// Create window, setting fullscreen hint if appropriate
	Uint32 wflags = SDL_WINDOW_RESIZABLE;
	if (vo_cfg->fullscreen) {
		wflags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
	}
	uisdl2->vo_window = SDL_CreateWindow("XRoar", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, vosdl->window_area.w, vosdl->window_area.h, wflags);
	SDL_SetWindowMinimumSize(uisdl2->vo_window, 160, 120);
	uisdl2->vo_window_id = SDL_GetWindowID(uisdl2->vo_window);

	// Add menubar if the created window is not fullscreen
	vo->is_fullscreen = SDL_GetWindowFlags(uisdl2->vo_window) & (SDL_WINDOW_FULLSCREEN|SDL_WINDOW_FULLSCREEN_DESKTOP);
	vo->show_menubar = !vo->is_fullscreen;
#ifdef WINDOWS32
	if (vo->show_menubar) {
		sdl_windows32_add_menu(uisdl2->vo_window);
		SDL_SetWindowSize(uisdl2->vo_window, vosdl->window_area.w, vosdl->window_area.h);
	}
#endif
	{
		int w, h;
		SDL_GetWindowSize(uisdl2->vo_window, &w, &h);
		sdl_update_draw_area(uisdl2, w, h);
	}

	// Create renderer

#ifdef WINDOWS32
	// from https://github.com/libsdl-org/SDL/issues/5099
	SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d11");
#endif

	for (unsigned i = 0; i < ARRAY_N_ELEMENTS(renderer_flags); i++) {
		vosdl->sdl_renderer = SDL_CreateRenderer(global_uisdl2->vo_window, -1, renderer_flags[i]);
		if (vosdl->sdl_renderer)
			break;
	}
	if (!vosdl->sdl_renderer) {
		LOG_ERROR("Failed to create renderer\n");
		return 0;
	}

	if (logging.level >= 3) {
		SDL_RendererInfo renderer_info;
		if (SDL_GetRendererInfo(vosdl->sdl_renderer, &renderer_info) == 0) {
			LOG_PRINT("SDL_GetRendererInfo()\n");
			LOG_PRINT("\tname = %s\n", renderer_info.name);
			LOG_PRINT("\tflags = 0x%x\n", renderer_info.flags);
			if (renderer_info.flags & SDL_RENDERER_SOFTWARE)
				LOG_PRINT("\t\tSDL_RENDERER_SOFTWARE\n");
			if (renderer_info.flags & SDL_RENDERER_ACCELERATED)
				LOG_PRINT("\t\tSDL_RENDERER_ACCELERATED\n");
			if (renderer_info.flags & SDL_RENDERER_PRESENTVSYNC)
				LOG_PRINT("\t\tSDL_RENDERER_PRESENTVSYNC\n");
			if (renderer_info.flags & SDL_RENDERER_TARGETTEXTURE)
				LOG_PRINT("\t\tSDL_RENDERER_TARGETTEXTURE\n");
			for (unsigned i = 0; i < renderer_info.num_texture_formats; i++) {
				LOG_PRINT("\ttexture_formats[%u] = %s\n", i, SDL_GetPixelFormatName(renderer_info.texture_formats[i]));
			}
			LOG_PRINT("\tmax_texture_width = %d\n", renderer_info.max_texture_width);
			LOG_PRINT("\tmax_texture_height = %d\n", renderer_info.max_texture_height);
		}
	}

#ifdef WINDOWS32
	// Need an event handler to prevent events backing up while menus are
	// being used.
	sdl_windows32_set_events_window(uisdl2->vo_window);
#endif

	// Initialise keyboard
	sdl_os_keyboard_init(global_uisdl2->vo_window);

	return vo;
}

// We need to recreate the texture whenever the viewport changes (it needs to
// be a different size) or the window size changes (texture scaling argument
// may change).

static void recreate_texture(struct vo_sdl_interface *vosdl) {
	struct vo_interface *vo = &vosdl->public;
	struct vo_render *vr = vo->renderer;

	// Destroy old
	if (vosdl->texture.texture) {
		SDL_DestroyTexture(vosdl->texture.texture);
		vosdl->texture.texture = NULL;
	}

	int vp_w = vr->viewport.w;
	int vp_h = vr->viewport.h;

	// Set scaling method according to options and window dimensions
	if (!vosdl->scale_60hz && (vosdl->filter == UI_GL_FILTER_NEAREST ||
				   (vosdl->filter == UI_GL_FILTER_AUTO &&
				    (vosdl->window_area.w % vp_w) == 0 &&
				    (vosdl->window_area.h % vp_h) == 0))) {
		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
	} else {
		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
	}

	// Create new
	vosdl->texture.texture = SDL_CreateTexture(vosdl->sdl_renderer, vosdl->texture.format, SDL_TEXTUREACCESS_STREAMING, vp_w, vp_h);
	if (!vosdl->texture.texture) {
		LOG_ERROR("Failed to create texture\n");
		abort();
	}

	vr->buffer_pitch = vr->viewport.w;
}

// Update viewport based on requested dimensions and 60Hz scaling.

static void update_viewport(struct vo_sdl_interface *vosdl) {
	struct vo_interface *vo = &vosdl->public;
	struct vo_render *vr = vo->renderer;

	int vp_w = global_uisdl2->viewport.w;
	int vp_h = global_uisdl2->viewport.h;

	if (vosdl->scale_60hz) {
		vp_h = (vp_h * 5) / 6;
	}

	vo_render_set_viewport(vr, vp_w, vp_h);

	recreate_texture(vosdl);

	int mw = global_uisdl2->viewport.w;
	int mh = global_uisdl2->viewport.h * 2;
	SDL_RenderSetLogicalSize(vosdl->sdl_renderer, mw, mh);
}

static void set_viewport(void *sptr, int vp_w, int vp_h) {
	struct vo_sdl_interface *vosdl = sptr;
	struct vo_interface *vo = &vosdl->public;
	struct vo_render *vr = vo->renderer;

	_Bool is_exact_multiple = 0;
	int multiple = 1;
	int mw = vr->viewport.w;
	int mh = vr->viewport.h * 2;

	if (vr->is_60hz) {
		mh = (mh * 6) / 5;
	}

	if (!vo->is_fullscreen && mw > 0 && mh > 0) {
		if ((vosdl->window_area.w % mw) == 0 &&
		    (vosdl->window_area.h % mh) == 0) {
			int wmul = vosdl->window_area.w / mw;
			int hmul = vosdl->window_area.h / mh;
			if (wmul == hmul && wmul > 0) {
				is_exact_multiple = 1;
				multiple = wmul;
			}
		}
	}

	if (vp_w < 16)
		vp_w = 16;
	if (vp_w > MAX_VIEWPORT_WIDTH)
		vp_w = MAX_VIEWPORT_WIDTH;
	if (vp_h < 6)
		vp_h = 6;
	if (vp_h > MAX_VIEWPORT_HEIGHT)
		vp_h = MAX_VIEWPORT_HEIGHT;

	global_uisdl2->viewport.w = vp_w;
	global_uisdl2->viewport.h = vp_h;

	if (is_exact_multiple) {
		int new_w = multiple * vp_w;
		int new_h = multiple * vp_h * 2;
		SDL_SetWindowSize(global_uisdl2->vo_window, new_w, new_h);
	} else {
		update_viewport(vosdl);
	}
}

static void notify_frame_rate(void *sptr, _Bool is_60hz) {
	struct vo_sdl_interface *vosdl = sptr;
	vosdl->scale_60hz = is_60hz;
	update_viewport(vosdl);
}

void sdl_vo_notify_size_changed(struct ui_sdl2_interface *uisdl2, int w, int h) {
	struct ui_interface *ui = &uisdl2->public;
	struct vo_interface *vo = ui->vo_interface;
	struct vo_sdl_interface *vosdl = (struct vo_sdl_interface *)vo;

	if (!vo->is_fullscreen) {
		vosdl->window_area.w = w;
		vosdl->window_area.h = h;
	}
	update_viewport(vosdl);
}

static int set_fullscreen(void *sptr, _Bool fullscreen) {
	struct vo_sdl_interface *vosdl = sptr;
	struct vo_interface *vo = &vosdl->public;

#ifdef HAVE_WASM
	// Until WebAssembly fullscreen interaction becomes a little more
	// predictable, we just don't support it.
	return 0;
#endif

	_Bool is_fullscreen = SDL_GetWindowFlags(global_uisdl2->vo_window) & (SDL_WINDOW_FULLSCREEN|SDL_WINDOW_FULLSCREEN_DESKTOP);

	if (is_fullscreen == fullscreen) {
		return 0;
	}

	if (fullscreen && vo->show_menubar) {
#ifdef WINDOWS32
		sdl_windows32_remove_menu(global_uisdl2->vo_window);
#endif
		vo->show_menubar = 0;
	} else if (!fullscreen && !vo->show_menubar) {
#ifdef WINDOWS32
		sdl_windows32_add_menu(global_uisdl2->vo_window);
#endif
		vo->show_menubar = 1;
	}

	vo->is_fullscreen = fullscreen;
	SDL_SetWindowFullscreen(global_uisdl2->vo_window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);

	return 0;
}

static void set_menubar(void *sptr, _Bool show_menubar) {
	struct vo_sdl_interface *vosdl = sptr;
	struct vo_interface *vo = &vosdl->public;

#ifdef WINDOWS32
	if (show_menubar && !vo->show_menubar) {
		sdl_windows32_add_menu(global_uisdl2->vo_window);
	} else if (!show_menubar && vo->show_menubar) {
		sdl_windows32_remove_menu(global_uisdl2->vo_window);
	}
	if (!vo->is_fullscreen) {
		SDL_SetWindowSize(global_uisdl2->vo_window, vosdl->window_area.w, vosdl->window_area.h);
	} else {
		int w, h;
		SDL_GetWindowSize(global_uisdl2->vo_window, &w, &h);
		sdl_vo_notify_size_changed(global_uisdl2, w, h);
	}
#endif
	vo->show_menubar = show_menubar;
}

static void vo_sdl_free(void *sptr) {
	struct vo_sdl_interface *vosdl = sptr;

	if (vosdl->texture.pixels) {
		free(vosdl->texture.pixels);
		vosdl->texture.pixels = NULL;
	}

	// TODO: I used to have a note here that destroying the renderer caused
	// a SEGV deep down in the video driver.  This doesn't seem to happen
	// in my current environment, but I need to test it in others.
	if (vosdl->sdl_renderer) {
		SDL_DestroyRenderer(vosdl->sdl_renderer);
		vosdl->sdl_renderer = NULL;
	}

	if (global_uisdl2->vo_window) {
		sdl_os_keyboard_free(global_uisdl2->vo_window);
		SDL_DestroyWindow(global_uisdl2->vo_window);
		global_uisdl2->vo_window = NULL;
	}

	free(vosdl);
}

static void draw(void *sptr) {
	struct vo_sdl_interface *vosdl = sptr;
	struct vo_interface *vo = &vosdl->public;
	struct vo_render *vr = vo->renderer;

	SDL_UpdateTexture(vosdl->texture.texture, NULL, vosdl->texture.pixels, vr->viewport.w * vosdl->texture.pixel_size);
	SDL_RenderClear(vosdl->sdl_renderer);
	SDL_RenderCopy(vosdl->sdl_renderer, vosdl->texture.texture, NULL, NULL);
	SDL_RenderPresent(vosdl->sdl_renderer);
}
