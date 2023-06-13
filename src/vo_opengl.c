/** \file
 *
 *  \brief Generic OpenGL support for video output modules.
 *
 *  \copyright Copyright 2012-2023 Ciaran Anscomb
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
 *  OpenGL code is common to several video modules.  All the stuff that's not
 *  toolkit-specific goes in here.
 */

#include "top-config.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#if defined(__APPLE_CC__)
# include <OpenGL/gl.h>
#else
# include <GL/gl.h>
#endif

#ifdef WINDOWS32
#include <GL/glext.h>
#endif

#include "xalloc.h"

#include "vo.h"
#include "vo_opengl.h"
#include "vo_render.h"
#include "xroar.h"

// MAX_VIEWPORT_* defines maximum viewport

#define MAX_VIEWPORT_WIDTH  (800)
#define MAX_VIEWPORT_HEIGHT (300)

// TEX_INT_PITCH is the pitch of the texture internally.  This used to be
// best kept as a power of 2 - no idea how necessary that still is, but might
// as well keep it that way.
//
// TEX_BUF_WIDTH is the width of the buffer transferred to the texture.

#define TEX_INT_PITCH (1024)
#define TEX_INT_HEIGHT (384)
#define TEX_BUF_WIDTH (640)
#define TEX_BUF_HEIGHT (240)

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void *vo_opengl_new(size_t isize) {
	if (isize < sizeof(struct vo_opengl_interface))
		isize = sizeof(struct vo_opengl_interface);
	struct vo_opengl_interface *vogl = xmalloc(isize);
	*vogl = (struct vo_opengl_interface){0};
	return vogl;
}

void vo_opengl_free(void *sptr) {
	struct vo_opengl_interface *vogl = sptr;
	glDeleteTextures(1, &vogl->texture.num);
	free(vogl->texture.pixels);
}

void vo_opengl_configure(struct vo_opengl_interface *vogl, struct vo_cfg *cfg) {
	struct vo_interface *vo = &vogl->vo;

	vogl->texture.buf_format = GL_RGBA;

	switch (cfg->pixel_fmt) {
	default:
		cfg->pixel_fmt = VO_RENDER_FMT_RGBA8;
		// fall through

	case VO_RENDER_FMT_RGBA8:
		vogl->texture.internal_format = GL_RGB8;
		vogl->texture.buf_format = GL_RGBA;
		vogl->texture.buf_type = GL_UNSIGNED_INT_8_8_8_8;
		vogl->texture.pixel_size = 4;
		break;

	case VO_RENDER_FMT_BGRA8:
		vogl->texture.internal_format = GL_RGB8;
		vogl->texture.buf_format = GL_BGRA;
		vogl->texture.buf_type = GL_UNSIGNED_INT_8_8_8_8;
		vogl->texture.pixel_size = 4;
		break;

	case VO_RENDER_FMT_ARGB8:
		vogl->texture.internal_format = GL_RGB8;
		vogl->texture.buf_format = GL_BGRA;
		vogl->texture.buf_type = GL_UNSIGNED_INT_8_8_8_8_REV;
		vogl->texture.pixel_size = 4;
		break;

	case VO_RENDER_FMT_ABGR8:
		vogl->texture.internal_format = GL_RGB8;
		vogl->texture.buf_format = GL_RGBA;
		vogl->texture.buf_type = GL_UNSIGNED_INT_8_8_8_8_REV;
		vogl->texture.pixel_size = 4;
		break;

	case VO_RENDER_FMT_RGB565:
#ifdef GL_RGB565
		vogl->texture.internal_format = GL_RGB565;
#else
		vogl->texture.internal_format = GL_RGB5;
#endif
		vogl->texture.buf_format = GL_RGB;
		vogl->texture.buf_type = GL_UNSIGNED_SHORT_5_6_5;
		vogl->texture.pixel_size = 2;
		break;

	case VO_RENDER_FMT_RGBA4:
		vogl->texture.internal_format = GL_RGB4;
		vogl->texture.buf_format = GL_RGBA;
		vogl->texture.buf_type = GL_UNSIGNED_SHORT_4_4_4_4;
		vogl->texture.pixel_size = 2;
		break;
	}

	struct vo_render *vr = vo_render_new(cfg->pixel_fmt);
	vr->cmp.colour_killer = cfg->colour_killer;
	vo_set_renderer(vo, vr);

	vo->free = DELEGATE_AS0(void, vo_opengl_free, vo);
	vo->draw = DELEGATE_AS0(void, vo_opengl_draw, vogl);

	vogl->texture.pixels = xmalloc(MAX_VIEWPORT_WIDTH * MAX_VIEWPORT_HEIGHT * vogl->texture.pixel_size);
	vo_render_set_buffer(vr, vogl->texture.pixels);

	vogl->picture_area.x = vogl->picture_area.y = 0;
	vogl->filter = cfg->gl_filter;
}

void vo_opengl_setup_context(struct vo_opengl_interface *vogl, struct vo_draw_area *draw_area) {
	int x = draw_area->x;
	int y = draw_area->y;
	int w = draw_area->w;
	int h = draw_area->h;

	// Set up picture area
	if (((double)w / (double)h) > (4.0 / 3.0)) {
		vogl->picture_area.h = h;
		vogl->picture_area.w = (((double)vogl->picture_area.h / 3.0) * 4.0) + 0.5;
		vogl->picture_area.x = x + (w - vogl->picture_area.w) / 2;
		vogl->picture_area.y = y;
	} else {
		vogl->picture_area.w = w;
		vogl->picture_area.h = (((double)vogl->picture_area.w / 4.0) * 3.0) + 0.5;
		vogl->picture_area.x = x;
		vogl->picture_area.y = y + (h - vogl->picture_area.h)/2;
	}

	// Configure OpenGL
	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glDisable(GL_CULL_FACE);
	glEnable(GL_TEXTURE_2D);

	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);

	glViewport(0, 0, w, h);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, w, h , 0, -1.0, 1.0);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClearDepth(1.0f);

	glDeleteTextures(1, &vogl->texture.num);
	glGenTextures(1, &vogl->texture.num);
	glBindTexture(GL_TEXTURE_2D, vogl->texture.num);
	glTexImage2D(GL_TEXTURE_2D, 0, vogl->texture.internal_format, TEX_INT_PITCH, TEX_INT_HEIGHT, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glColor4f(1.0, 1.0, 1.0, 1.0);

	// The same vertex & texcoord lists will be used every draw,
	// so configure them here rather than in vsync()

	// Vertex array defines where in the window the texture will be rendered
	vogl->vertices[0][0] = vogl->picture_area.x;
	vogl->vertices[0][1] = vogl->picture_area.y;
	vogl->vertices[1][0] = vogl->picture_area.x;
	vogl->vertices[1][1] = h - vogl->picture_area.y;
	vogl->vertices[2][0] = w - vogl->picture_area.x;
	vogl->vertices[2][1] = vogl->picture_area.y;
	vogl->vertices[3][0] = w - vogl->picture_area.x;
	vogl->vertices[3][1] = h - vogl->picture_area.y;
	glVertexPointer(2, GL_FLOAT, 0, vogl->vertices);

	vo_opengl_update_viewport(vogl);
}

void vo_opengl_update_viewport(struct vo_opengl_interface *vogl) {
	struct vo_interface *vo = &vogl->vo;
	struct vo_render *vr = vo->renderer;

	int hw = vr->viewport.w / 2;
	int hh = vr->viewport.h;

	if (vogl->filter == UI_GL_FILTER_NEAREST
	    || (vogl->filter == UI_GL_FILTER_AUTO && (vogl->picture_area.w % hw) == 0 && (vogl->picture_area.h % hh) == 0)) {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	} else {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	}

	// Texture coordinates select a subset of the texture to update
	vogl->tex_coords[0][0] = 0.0;
	vogl->tex_coords[0][1] = 0.0;
	vogl->tex_coords[1][0] = 0.0;
	vogl->tex_coords[1][1] = (double)vr->viewport.h / (double)TEX_INT_HEIGHT;
	vogl->tex_coords[2][0] = (double)vr->viewport.w / (double)TEX_INT_PITCH;
	vogl->tex_coords[2][1] = 0.0;
	vogl->tex_coords[3][0] = (double)vr->viewport.w / (double)TEX_INT_PITCH;
	vogl->tex_coords[3][1] = (double)vr->viewport.h / (double)TEX_INT_HEIGHT;
	glTexCoordPointer(2, GL_FLOAT, 0, vogl->tex_coords);

	// OpenGL 4.4+ has glClearTexImage(), but for now let's just clear a
	// line just to the right and just below the area in the texture we'll
	// be updating.  This prevents weird fringing effects.
	memset(vogl->texture.pixels, 0, TEX_INT_PITCH * vogl->texture.pixel_size);
	glTexSubImage2D(GL_TEXTURE_2D, 0,
			vr->viewport.w, 0, 1, TEX_INT_HEIGHT,
			vogl->texture.buf_format, vogl->texture.buf_type, vogl->texture.pixels);
	glTexSubImage2D(GL_TEXTURE_2D, 0,
			0, vr->viewport.h, TEX_INT_PITCH, 1,
			vogl->texture.buf_format, vogl->texture.buf_type, vogl->texture.pixels);

	vr->buffer_pitch = vr->viewport.w;
}

void vo_opengl_draw(void *sptr) {
	struct vo_opengl_interface *vogl = sptr;
	struct vo_render *vr = vogl->vo.renderer;
	glClear(GL_COLOR_BUFFER_BIT);
	glTexSubImage2D(GL_TEXTURE_2D, 0,
			0, 0, vr->viewport.w, vr->viewport.h,
			vogl->texture.buf_format, vogl->texture.buf_type, vogl->texture.pixels);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}
