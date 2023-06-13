/** \file
 *
 *  \brief GtkGLExt video output module.
 *
 *  \copyright Copyright 2010-2023 Ciaran Anscomb
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

#include <stdlib.h>
#include <string.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <gtk/gtk.h>
#pragma GCC diagnostic pop
#ifdef HAVE_X11
#include <gdk/gdkx.h>
#include <GL/glx.h>
#endif
#include <gtk/gtkgl.h>

#include "xalloc.h"

#include "logging.h"
#include "module.h"
#include "vo.h"
#include "vo_opengl.h"
#include "xroar.h"

#include "gtk2/common.h"

// MAX_VIEWPORT_* defines maximum viewport

#define MAX_VIEWPORT_WIDTH  (800)
#define MAX_VIEWPORT_HEIGHT (300)

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void *new(void *cfg);

struct module vo_gtkgl_module = {
	.name = "gtkgl", .description = "GtkGLExt video",
	.new = new,
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct vo_gtkgl_interface {
	struct vo_opengl_interface vogl;

	// Menus affect the size of the draw area, so we need to track how much
	// to add to the window size to get the draw area we want.

	int woff, hoff;

	// However, OpenGL will render only into the draw area, so we don't
	// need to keep track of any offsets for it, just the overall
	// dimensions.  Therefore vo_window_area is suitable.

	struct vo_window_area window_area;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void vo_gtkgl_free(void *);
static void resize(void *, unsigned int w, unsigned int h);
static int set_fullscreen(void *, _Bool fullscreen);
static void set_menubar(void *, _Bool show_menubar);
static void draw(void *);
static void set_viewport(void *, int vp_w, int vp_h);

static void notify_frame_rate(void *, _Bool is_60hz);

static gboolean window_state(GtkWidget *, GdkEventWindowState *, gpointer);
static gboolean configure(GtkWidget *, GdkEventConfigure *, gpointer);
static void vo_gtkgl_set_vsync(int val);

static void *new(void *sptr) {
	(void)sptr;

	gtk_gl_init(NULL, NULL);

	if (gdk_gl_query_extension() != TRUE) {
		LOG_ERROR("OpenGL not available\n");
		return NULL;
	}

	struct vo_gtkgl_interface *vogtkgl = vo_opengl_new(sizeof(*vogtkgl));
	*vogtkgl = (struct vo_gtkgl_interface){0};
	struct vo_opengl_interface *vogl = &vogtkgl->vogl;
	struct vo_interface *vo = &vogl->vo;

	struct vo_cfg *vo_cfg = &global_uigtk2->cfg->vo_cfg;
	vo_opengl_configure(vogl, vo_cfg);

	vo->free = DELEGATE_AS0(void, vo_gtkgl_free, vogtkgl);
	vo->draw = DELEGATE_AS0(void, draw, vogl);

	struct vo_render *vr = vo->renderer;

	// Used by UI to adjust viewing parameters
	vo->set_viewport = DELEGATE_AS2(void, int, int, set_viewport, vogtkgl);
	vo->resize = DELEGATE_AS2(void, unsigned, unsigned, resize, vogtkgl);
	vo->set_fullscreen = DELEGATE_AS1(int, bool, set_fullscreen, vogtkgl);
	vo->set_menubar = DELEGATE_AS1(void, bool, set_menubar, vogtkgl);

	vr->notify_frame_rate = DELEGATE_AS1(void, bool, notify_frame_rate, vogtkgl);

	// Configure drawing_area widget
	vogtkgl->window_area.w = 640;
	vogtkgl->window_area.h = 480;
	gtk_widget_set_size_request(global_uigtk2->drawing_area, vogtkgl->window_area.w, vogtkgl->window_area.h);
	GdkGLConfig *glconfig = gdk_gl_config_new_by_mode(GDK_GL_MODE_RGB | GDK_GL_MODE_DOUBLE);
	if (!glconfig) {
		LOG_ERROR("Failed to create OpenGL config\n");
		vo_gtkgl_free(vo);
		return NULL;
	}
	if (!gtk_widget_set_gl_capability(global_uigtk2->drawing_area, glconfig, NULL, TRUE, GDK_GL_RGBA_TYPE)) {
		LOG_ERROR("Failed to add OpenGL support to GTK widget\n");
		g_object_unref(glconfig);
		vo_gtkgl_free(vo);
		return NULL;
	}
	g_object_unref(glconfig);

	g_signal_connect(global_uigtk2->top_window, "window-state-event", G_CALLBACK(window_state), vo);
	g_signal_connect(global_uigtk2->drawing_area, "configure-event", G_CALLBACK(configure), vo);

	/* Show top window first so that drawing area is realised to the
	 * right size even if we then fullscreen.  */
	vo->show_menubar = 1;
	gtk_widget_show(global_uigtk2->top_window);
	/* Set fullscreen. */
	set_fullscreen(vo, vo_cfg->fullscreen);

	return vo;
}

static void vo_gtkgl_free(void *sptr) {
	struct vo_gtkgl_interface *vogtkgl = sptr;
	struct vo_opengl_interface *vogl = &vogtkgl->vogl;
	set_fullscreen(vogtkgl, 0);
	vo_opengl_free(vogl);
}

static void set_viewport(void *sptr, int vp_w, int vp_h) {
	struct vo_gtkgl_interface *vogtkgl = sptr;
	struct vo_opengl_interface *vogl = &vogtkgl->vogl;
	struct vo_interface *vo = &vogl->vo;
	struct vo_render *vr = vo->renderer;

	GdkGLContext *glcontext = gtk_widget_get_gl_context(global_uigtk2->drawing_area);
	GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable(global_uigtk2->drawing_area);

	if (!gdk_gl_drawable_gl_begin(gldrawable, glcontext)) {
		g_assert_not_reached();
	}

	_Bool is_exact_multiple = 0;
	int multiple = 1;
	int mw = vr->viewport.w;
	int mh = vr->viewport.h * 2;

	if (mw > 0 && mh > 0) {
		if ((vogtkgl->window_area.w % mw) == 0 &&
		    (vogtkgl->window_area.h % mh) == 0) {
			int wmul = vogtkgl->window_area.w / mw;
			int hmul = vogtkgl->window_area.h / mh;
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

	mw = vp_w;
	mh = vp_h * 2;

	if (is_exact_multiple) {
		vogtkgl->window_area.w = multiple * mw;
		vogtkgl->window_area.h = multiple * mh;
		if (!vo->is_fullscreen) {
			int w = vogtkgl->window_area.w + vogtkgl->woff;
			int h = vogtkgl->window_area.h + vogtkgl->hoff;
			gtk_window_resize(GTK_WINDOW(global_uigtk2->top_window), w, h);
		}
	}

	vo_opengl_set_viewport(vogl, vp_w, vp_h);

	gdk_gl_drawable_gl_end(gldrawable);
}

static void notify_frame_rate(void *sptr, _Bool is_60hz) {
	struct vo_gtkgl_interface *vogtkgl = sptr;
	struct vo_opengl_interface *vogl = &vogtkgl->vogl;
	vo_opengl_set_frame_rate(vogl, is_60hz);
}

// Manual resizing of window

static void resize(void *sptr, unsigned int w, unsigned int h) {
	struct vo_gtkgl_interface *vogtkgl = sptr;
	struct vo_opengl_interface *vogl = &vogtkgl->vogl;
	struct vo_interface *vo = &vogl->vo;

	if (vo->is_fullscreen) {
		return;
	}
	GdkScreen *s = gtk_window_get_screen(GTK_WINDOW(global_uigtk2->top_window));
	unsigned sw = 1024, sh = 768;
	if (s) {
		sw = gdk_screen_get_width(s);
		sh = gdk_screen_get_height(s);
	}
	if (w < 160 || h < 120) {
		return;
	}
	if (w > sw || h > sh) {
		return;
	}
	/* You can't just set the widget size and expect GTK to adapt the
	 * containing window, or indeed ask it to.  This will hopefully work
	 * consistently.  It seems to be basically how GIMP "shrink wrap"s its
	 * windows.  */
	GtkAllocation win_allocation, draw_allocation;
	gtk_widget_get_allocation(global_uigtk2->top_window, &win_allocation);
	gtk_widget_get_allocation(global_uigtk2->drawing_area, &draw_allocation);
	int woff = win_allocation.width - draw_allocation.width;
	int hoff = win_allocation.height - draw_allocation.height;
	vogtkgl->woff = woff;
	vogtkgl->hoff = hoff;
	gtk_window_resize(GTK_WINDOW(global_uigtk2->top_window), w + woff, h + hoff);
}

static int set_fullscreen(void *sptr, _Bool fullscreen) {
	struct vo_gtkgl_interface *vogtkgl = sptr;
	struct vo_opengl_interface *vogl = &vogtkgl->vogl;
	struct vo_interface *vo = &vogl->vo;

	vo->is_fullscreen = fullscreen;
	vo->show_menubar = !fullscreen;
	if (fullscreen) {
		gtk_window_fullscreen(GTK_WINDOW(global_uigtk2->top_window));
	} else {
		gtk_window_unfullscreen(GTK_WINDOW(global_uigtk2->top_window));
	}
	return 0;
}

static void set_menubar(void *sptr, _Bool show_menubar) {
	struct vo_gtkgl_interface *vogtkgl = sptr;
	struct vo_opengl_interface *vogl = &vogtkgl->vogl;
	struct vo_interface *vo = &vogl->vo;

	GtkAllocation allocation;
	if (vo->is_fullscreen) {
		gtk_widget_get_allocation(global_uigtk2->top_window, &allocation);
	} else {
		gtk_widget_get_allocation(global_uigtk2->drawing_area, &allocation);
	}
	int w = allocation.width;
	int h = allocation.height;

	if (show_menubar && !vo->is_fullscreen) {
		w += vogtkgl->woff;
		h += vogtkgl->hoff;
	}

	vo->show_menubar = show_menubar;
	if (show_menubar) {
		gtk_widget_show(global_uigtk2->menubar);
	} else {
		gtk_widget_hide(global_uigtk2->menubar);
	}
	gtk_window_resize(GTK_WINDOW(global_uigtk2->top_window), w, h);
}

static gboolean window_state(GtkWidget *tw, GdkEventWindowState *event, gpointer data) {
	struct vo_interface *vo = data;
	(void)tw;
	(void)data;
	if ((event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED) && !vo->is_fullscreen) {
		gtk_widget_hide(global_uigtk2->menubar);
		vo->is_fullscreen = 1;
		vo->show_menubar = 0;
	}
	if (!(event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED) && vo->is_fullscreen) {
		gtk_widget_show(global_uigtk2->menubar);
		vo->is_fullscreen = 0;
		vo->show_menubar = 1;
	}
	return 0;
}

// Called whenever the window changes size (including when first created)

static gboolean configure(GtkWidget *da, GdkEventConfigure *event, gpointer data) {
	struct vo_gtkgl_interface *vogtkgl = data;
	struct vo_opengl_interface *vogl = &vogtkgl->vogl;
	struct vo_interface *vo = &vogl->vo;
	(void)event;

	GdkGLContext *glcontext = gtk_widget_get_gl_context(da);
	GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable(da);

	if (!gdk_gl_drawable_gl_begin(gldrawable, glcontext)) {
		g_assert_not_reached();
	}

	GtkAllocation draw_allocation;
	gtk_widget_get_allocation(da, &draw_allocation);

	// Preserve geometry offsets introduced by menubar
	if (vo->show_menubar) {
		vogtkgl->woff = draw_allocation.x;
		vogtkgl->hoff = draw_allocation.y;
	}

	vogtkgl->window_area.w = draw_allocation.width;
	vogtkgl->window_area.h = draw_allocation.height;

	// Although GTK+ reports how the drawable is offset into the window,
	// the OpenGL context will render with the drawable's origin, so set X
	// and Y to 0.
	global_uigtk2->draw_area.x = 0;
	global_uigtk2->draw_area.y = 0;
	global_uigtk2->draw_area.w = draw_allocation.width;
	global_uigtk2->draw_area.h = draw_allocation.height;
	vo_opengl_setup_context(vogl, &global_uigtk2->draw_area);

	// Copy picture dimensions back out (for mouse calculations)
	global_uigtk2->picture_area.x = vogl->picture_area.x;
	global_uigtk2->picture_area.y = vogl->picture_area.y;
	global_uigtk2->picture_area.w = vogl->picture_area.w;
	global_uigtk2->picture_area.h = vogl->picture_area.h;

	vo_gtkgl_set_vsync(-1);

	gdk_gl_drawable_gl_end(gldrawable);

	return 0;
}

static void draw(void *sptr) {
	struct vo_gtkgl_interface *vogtkgl = sptr;
	struct vo_opengl_interface *vogl = &vogtkgl->vogl;

	GdkGLContext *glcontext = gtk_widget_get_gl_context(global_uigtk2->drawing_area);
	GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable(global_uigtk2->drawing_area);

	if (!gdk_gl_drawable_gl_begin(gldrawable, glcontext)) {
		g_assert_not_reached();
	}

	vo_opengl_draw(vogl);

	gdk_gl_drawable_swap_buffers(gldrawable);
	gdk_gl_drawable_gl_end(gldrawable);
}

#ifdef HAVE_X11

// Test glX extensions string for presence of a particular extension.

static _Bool opengl_has_extension(Display *display, const char *extension) {
        const char *(*glXQueryExtensionsStringFunc)(Display *, int) = (const char *(*)(Display *, int))glXGetProcAddress((const GLubyte *)"glXQueryExtensionsString");
        if (!glXQueryExtensionsStringFunc)
                return 0;

        int screen = DefaultScreen(display);

        const char *extensions = glXQueryExtensionsStringFunc(display, screen);
        if (!extensions)
                return 0;

        LOG_DEBUG(3, "gtkgl: extensions: %s\n", extensions);

        const char *start;
        const char *where, *terminator;

        // It takes a bit of care to be fool-proof about parsing the OpenGL
        // extensions string. Don't be fooled by sub-strings, etc.

        start = extensions;

        for (;;) {
                where = strstr(start, extension);
                if (!where)
                        break;

                terminator = where + strlen(extension);
                if (where == start || *(where - 1) == ' ')
                        if (*terminator == ' ' || *terminator == '\0')
                                return 1;

                start = terminator;
        }
        return 0;
}

#endif

// Set "swap interval" - that is, how many vsyncs should be waited for on
// buffer swap.  Usually this should be 1.  However, a negative value here
// tries to use GLX_EXT_swap_control_tear, which allows unsynchronised buffer
// swaps if a vsync was already missed.  If that particular extension is not
// found, just uses the absolute value.

static void vo_gtkgl_set_vsync(int val) {
	(void)val;

#ifdef HAVE_X11

	PFNGLXSWAPINTERVALEXTPROC glXSwapIntervalEXT = (PFNGLXSWAPINTERVALEXTPROC)glXGetProcAddress((const GLubyte *)"glXSwapIntervalEXT");
	if (glXSwapIntervalEXT) {
		Display *dpy = gdk_x11_drawable_get_xdisplay(gtk_widget_get_window(global_uigtk2->drawing_area));
		Window win = gdk_x11_drawable_get_xid(gtk_widget_get_window(global_uigtk2->drawing_area));
		if (!opengl_has_extension(dpy, "GLX_EXT_swap_control_tear")) {
			val = abs(val);
		}
		if (dpy && win) {
			LOG_DEBUG(3, "vo_gtkgl: glXSwapIntervalEXT(%p, %lu, %d)\n", dpy, win, val);
			glXSwapIntervalEXT(dpy, win, val);
			return;
		}
	}

	val = abs(val);

	PFNGLXSWAPINTERVALMESAPROC glXSwapIntervalMESA = (PFNGLXSWAPINTERVALMESAPROC)glXGetProcAddress((const GLubyte *)"glXSwapIntervalMESA");
	if (glXSwapIntervalMESA) {
		LOG_DEBUG(3, "vo_gtkgl: glXSwapIntervalMESA(%d)\n", val);
		glXSwapIntervalMESA(val);
		return;
	}

	PFNGLXSWAPINTERVALSGIPROC glXSwapIntervalSGI = (PFNGLXSWAPINTERVALSGIPROC)glXGetProcAddress((const GLubyte *)"glXSwapIntervalSGI");
	if (glXSwapIntervalSGI) {
		LOG_DEBUG(3, "vo_gtkgl: glXSwapIntervalSGI(%d)\n", val);
		glXSwapIntervalSGI(val);
		return;
	}

#endif

	LOG_DEBUG(3, "vo_gtkgl: Found no way to set swap interval\n");
}
