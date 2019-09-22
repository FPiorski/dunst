/* copyright 2013 Sascha Kruse and contributors (see LICENSE for licensing information) */
#include "x.h"

#include <assert.h>
#include <cairo.h>
#include <cairo-xlib.h>
#include <glib-object.h>
#include <locale.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/extensions/shape.h>
#include <X11/Xatom.h>
#include <X11/X.h>
#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>

#include "../dbus.h"
#include "../draw.h"
#include "../dunst.h"
#include "../log.h"
#include "../markup.h"
#include "../menu.h"
#include "../notification.h"
#include "../queues.h"
#include "../settings.h"
#include "../utils.h"

#include "screen.h"

#define WIDTH 400
#define HEIGHT 400

struct window_x11 {
        Window xwin;
        cairo_surface_t *root_surface;
        cairo_t *c_ctx;
        GSource *esrc;
        int cur_screen;
        bool visible;
        struct dimensions dim;
};

struct x11_source {
        GSource source;
        struct window_x11 *win;
};

struct x_context xctx;
bool dunst_grab_errored = false;

static bool fullscreen_last = false;

static void XRM_update_db(void);

static void x_shortcut_init(struct keyboard_shortcut *ks);
static int x_shortcut_grab(struct keyboard_shortcut *ks);
static void x_shortcut_ungrab(struct keyboard_shortcut *ks);
/* FIXME refactor setup teardown handlers into one setup and one teardown */
static void x_shortcut_setup_error_handler(void);
static int x_shortcut_tear_down_error_handler(void);
static void setopacity(Window win, unsigned long opacity);
static void x_handle_click(XEvent ev);

static void x_win_move(struct window_x11 *win, int x, int y, int width, int height)
{
        /* move and resize */
        if (x != win->dim.x || y != win->dim.y) {
                XMoveWindow(xctx.dpy, win->xwin, x, y);

                win->dim.x = x;
                win->dim.y = y;
        }

        if (width != win->dim.w || height != win->dim.h) {
                XResizeWindow(xctx.dpy, win->xwin, width, height);

                win->dim.h = height;
                win->dim.w = width;
        }
}

static void x_win_round_corners(struct window_x11 *win, const int rad)
{
        const int width = win->dim.w;
        const int height = win->dim.h - 96;
        const int dia = 2 * rad;
        const int degrees = 64; // the factor to convert degrees to XFillArc's angle param

        Pixmap mask = XCreatePixmap(xctx.dpy, win->xwin, width, height+96, 1);
        XGCValues xgcv;

        GC shape_gc = XCreateGC(xctx.dpy, mask, 0, &xgcv);

        XSetForeground(xctx.dpy, shape_gc, 0);
        XFillRectangle(xctx.dpy,
                       mask,
                       shape_gc,
                       0,
                       0,
                       width,
                       height+96);

        XSetForeground(xctx.dpy, shape_gc, 1);

        /* To mark all pixels, which should get exposed, we
         * use a circle for every corner and two overlapping rectangles */
        unsigned const int centercoords[] = {
                0,               0,
                width - dia - 1, 0,
                0,               height - dia - 1,
                width - dia - 1, height - dia - 1,
        };

        for (int i = 0; i < sizeof(centercoords)/sizeof(unsigned int); i = i+2) {
                XFillArc(xctx.dpy,
                         mask,
                         shape_gc,
                         centercoords[i],
                         centercoords[i+1],
                         dia,
                         dia,
                         degrees * 0,
                         degrees * 360);
        }
        XFillRectangle(xctx.dpy,
                       mask,
                       shape_gc,
                       rad,
                       0,
                       width-dia,
                       height);
        XFillRectangle(xctx.dpy,
                       mask,
                       shape_gc,
                       0,
                       rad,
                       width,
                       height-dia);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 0 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 1 , 0 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 1 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 2 , 0 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 2 , 1 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 2 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 2 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 3 , 0 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 3 , 1 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 3 , 2 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 3 , 3 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 3 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 3 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 4 , 0 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 4 , 1 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 4 , 2 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 4 , 3 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 4 , 4 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 4 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 4 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 5 , 0 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 5 , 1 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 5 , 2 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 5 , 3 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 5 , 4 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 5 , 5 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 5 , 6 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 5 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 5 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 5 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 6 , 0 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 6 , 1 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 6 , 2 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 6 , 3 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 6 , 4 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 6 , 5 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 6 , 6 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 6 , 7 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 6 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 6 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 6 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 6 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 7 , 0 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 7 , 1 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 7 , 2 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 7 , 3 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 7 , 4 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 7 , 5 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 7 , 6 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 7 , 7 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 7 , 8 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 7 , 9 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 7 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 7 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 7 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 7 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 8 , 0 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 8 , 1 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 8 , 2 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 8 , 3 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 8 , 4 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 8 , 5 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 8 , 6 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 8 , 7 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 8 , 8 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 8 , 9 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 8 , 10 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 8 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 8 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 8 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 8 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 8 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 9 , 0 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 9 , 1 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 9 , 2 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 9 , 3 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 9 , 4 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 9 , 5 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 9 , 6 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 9 , 7 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 9 , 8 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 9 , 9 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 9 , 10 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 9 , 11 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 9 , 12 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 9 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 9 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 9 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 9 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 9 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 9 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 9 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 10 , 0 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 10 , 1 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 10 , 2 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 10 , 3 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 10 , 4 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 10 , 5 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 10 , 6 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 10 , 7 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 10 , 8 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 10 , 9 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 10 , 10 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 10 , 11 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 10 , 12 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 10 , 13 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 10 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 10 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 10 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 10 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 10 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 10 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 10 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 11 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 11 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 11 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 11 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 11 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 11 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 11 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 11 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 12 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 12 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 12 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 12 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 12 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 12 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 12 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 12 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 13 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 13 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 13 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 13 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 13 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 13 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 13 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 13 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 13 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 13 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 14 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 14 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 14 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 14 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 14 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 14 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 14 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 14 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 14 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 14 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 15 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 15 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 15 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 15 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 15 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 15 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 15 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 15 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 15 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 15 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 15 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 16 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 16 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 16 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 16 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 16 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 16 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 16 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 16 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 16 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 16 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 16 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 16 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 17 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 17 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 17 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 17 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 17 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 17 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 17 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 17 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 17 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 17 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 17 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 17 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 17 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 18 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 18 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 18 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 18 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 18 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 18 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 18 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 18 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 18 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 18 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 18 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 18 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 18 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 18 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 19 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 19 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 19 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 19 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 19 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 19 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 19 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 19 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 19 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 19 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 19 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 19 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 19 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 19 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 19 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 19 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 20 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 20 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 20 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 20 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 20 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 20 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 20 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 20 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 20 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 20 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 20 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 20 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 20 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 20 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 20 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 20 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 20 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 20 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 21 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 21 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 21 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 21 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 21 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 21 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 21 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 21 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 21 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 21 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 21 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 21 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 21 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 21 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 21 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 21 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 21 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 21 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 21 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 21 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 21 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 21 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 21 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 21 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 21 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 22 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 22 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 22 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 22 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 22 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 22 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 22 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 22 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 22 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 22 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 22 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 22 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 22 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 22 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 22 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 22 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 22 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 22 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 22 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 22 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 22 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 22 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 22 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 22 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 22 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 22 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 22 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 22 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 22 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 23 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 23 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 23 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 23 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 23 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 23 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 23 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 23 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 23 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 23 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 23 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 23 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 23 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 23 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 23 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 23 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 23 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 23 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 23 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 23 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 23 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 23 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 23 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 23 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 23 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 23 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 23 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 23 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 23 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 23 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 23 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 24 , 25 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 24 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 24 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 24 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 24 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 24 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 24 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 24 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 24 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 24 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 24 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 24 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 24 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 24 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 24 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 24 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 24 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 24 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 24 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 24 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 24 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 24 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 24 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 24 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 24 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 24 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 24 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 24 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 24 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 24 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 24 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 24 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 24 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 24 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 24 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 25 , 25 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 25 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 25 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 25 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 25 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 25 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 25 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 25 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 25 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 25 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 25 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 25 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 25 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 25 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 25 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 25 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 25 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 25 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 25 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 25 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 25 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 25 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 25 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 25 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 25 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 25 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 25 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 25 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 25 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 25 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 25 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 25 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 25 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 25 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 25 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 25 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 25 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 24 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 27 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 26 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 23 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 24 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 25 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 26 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 27 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 27 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 19 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 20 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 21 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 22 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 23 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 24 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 25 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 26 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 27 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 28 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 17 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 18 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 19 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 20 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 21 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 22 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 23 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 24 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 25 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 26 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 27 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 29 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 15 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 16 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 17 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 18 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 19 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 20 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 21 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 22 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 23 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 24 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 25 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 26 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 27 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 30 , 90 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 14 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 15 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 16 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 17 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 18 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 19 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 20 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 21 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 22 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 23 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 24 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 31 , 90 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 13 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 14 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 15 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 16 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 17 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 18 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 19 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 22 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 23 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 24 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 32 , 90 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 12 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 13 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 14 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 15 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 16 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 17 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 22 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 23 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 24 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 25 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 90 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 33 , 91 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 12 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 13 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 14 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 15 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 16 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 23 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 24 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 90 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 34 , 91 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 11 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 12 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 13 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 14 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 15 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 90 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 35 , 91 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 11 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 12 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 13 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 14 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 15 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 90 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 36 , 91 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 11 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 12 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 13 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 14 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 15 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 90 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 37 , 91 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 11 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 12 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 13 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 14 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 15 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 90 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 38 , 91 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 11 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 12 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 13 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 14 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 15 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 90 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 91 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 39 , 92 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 11 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 12 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 13 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 14 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 15 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 90 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 91 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 40 , 92 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 11 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 12 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 13 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 14 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 15 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 90 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 91 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 41 , 92 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 12 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 13 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 14 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 15 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 90 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 91 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 42 , 92 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 12 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 13 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 14 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 15 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 16 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 90 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 91 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 92 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 43 , 93 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 13 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 14 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 15 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 16 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 17 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 90 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 91 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 92 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 44 , 93 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 13 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 14 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 15 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 16 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 17 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 18 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 25 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 26 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 27 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 90 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 91 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 92 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 45 , 93 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 14 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 15 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 16 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 17 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 18 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 19 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 20 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 21 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 22 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 23 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 24 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 25 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 26 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 27 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 90 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 91 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 92 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 46 , 93 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 15 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 16 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 17 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 18 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 19 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 20 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 21 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 22 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 23 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 24 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 25 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 26 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 27 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 90 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 91 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 92 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 47 , 93 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 17 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 18 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 19 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 20 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 21 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 22 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 23 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 24 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 25 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 26 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 27 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 90 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 91 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 92 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 93 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 48 , 94 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 18 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 19 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 20 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 21 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 22 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 23 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 24 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 25 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 26 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 90 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 91 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 92 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 93 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 49 , 94 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 90 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 91 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 92 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 93 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 50 , 94 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 90 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 91 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 92 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 93 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 51 , 94 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 90 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 91 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 92 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 93 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 94 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 52 , 95 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 90 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 91 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 92 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 93 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 94 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 53 , 95 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 90 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 91 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 92 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 93 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 94 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 54 , 95 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 90 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 91 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 92 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 93 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 94 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 55 , 95 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 90 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 91 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 92 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 93 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 94 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 56 , 95 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 90 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 91 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 92 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 93 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 94 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 57 , 95 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 90 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 91 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 92 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 93 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 94 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 58 , 95 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 90 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 91 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 92 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 93 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 94 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 59 , 95 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 90 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 91 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 92 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 93 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 94 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 60 , 95 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 90 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 91 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 92 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 93 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 94 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 61 , 95 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 90 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 91 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 92 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 93 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 94 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 62 , 95 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 90 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 91 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 92 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 93 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 63 , 94 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 90 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 91 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 92 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 64 , 93 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 90 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 91 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 92 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 65 , 93 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 90 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 91 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 66 , 92 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 90 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 67 , 91 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 27 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 68 , 90 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 27 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 69 , 89 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 27 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 70 , 88 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 27 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 71 , 87 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 27 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 85 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 72 , 86 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 27 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 83 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 73 , 84 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 27 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 74 , 82 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 27 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 80 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 75 , 81 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 27 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 76 , 79 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 27 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 75 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 76 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 77 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 77 , 78 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 27 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 73 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 78 , 74 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 27 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 71 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 79 , 72 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 27 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 68 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 69 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 80 , 70 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 81 , 27 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 81 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 81 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 81 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 81 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 81 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 81 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 81 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 81 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 81 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 81 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 81 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 81 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 81 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 81 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 81 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 81 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 81 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 81 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 81 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 81 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 81 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 81 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 81 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 81 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 81 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 81 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 81 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 81 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 81 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 81 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 81 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 81 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 81 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 81 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 81 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 81 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 81 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 81 , 65 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 81 , 66 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 81 , 67 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 82 , 27 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 82 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 82 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 82 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 82 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 82 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 82 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 82 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 82 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 82 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 82 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 82 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 82 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 82 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 82 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 82 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 82 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 82 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 82 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 82 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 82 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 82 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 82 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 82 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 82 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 82 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 82 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 82 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 82 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 82 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 82 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 82 , 58 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 82 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 82 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 82 , 61 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 82 , 62 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 82 , 63 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 82 , 64 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 83 , 27 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 83 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 83 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 83 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 83 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 83 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 83 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 83 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 83 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 83 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 83 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 83 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 83 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 83 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 83 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 83 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 83 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 83 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 83 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 83 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 83 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 83 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 83 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 83 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 83 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 83 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 83 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 83 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 83 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 83 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 83 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 83 , 59 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 83 , 60 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 84 , 27 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 84 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 84 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 84 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 84 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 84 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 84 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 84 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 84 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 84 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 84 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 84 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 84 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 84 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 84 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 84 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 84 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 84 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 84 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 84 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 84 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 84 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 84 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 84 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 84 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 84 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 84 , 53 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 84 , 54 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 84 , 55 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 84 , 56 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 84 , 57 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 85 , 27 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 85 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 85 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 85 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 85 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 85 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 85 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 85 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 85 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 85 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 85 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 85 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 85 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 85 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 85 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 85 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 85 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 85 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 85 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 85 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 85 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 85 , 48 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 85 , 49 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 85 , 50 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 85 , 51 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 85 , 52 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 86 , 27 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 86 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 86 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 86 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 86 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 86 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 86 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 86 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 86 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 86 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 86 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 86 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 86 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 86 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 86 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 86 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 86 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 86 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 86 , 45 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 86 , 46 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 86 , 47 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 87 , 27 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 87 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 87 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 87 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 87 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 87 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 87 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 87 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 87 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 87 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 87 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 87 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 87 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 87 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 87 , 41 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 87 , 42 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 87 , 43 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 87 , 44 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 88 , 27 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 88 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 88 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 88 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 88 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 88 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 88 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 88 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 88 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 88 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 88 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 88 , 38 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 88 , 39 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 88 , 40 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 89 , 27 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 89 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 89 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 89 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 89 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 89 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 89 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 89 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 89 , 35 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 89 , 36 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 89 , 37 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 90 , 27 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 90 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 90 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 90 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 90 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 90 , 32 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 90 , 33 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 90 , 34 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 91 , 27 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 91 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 91 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 91 , 30 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 91 , 31 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 92 , 27 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 92 , 28 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 92 , 29 +height);
XDrawPoint(xctx.dpy,mask,shape_gc,width/2-47+ 92 , 30 +height);

        XShapeCombineMask(xctx.dpy, win->xwin, ShapeBounding, 0, 0, mask, ShapeSet);

        XFreeGC(xctx.dpy, shape_gc);
        XFreePixmap(xctx.dpy, mask);

        XShapeSelectInput(xctx.dpy,
                win->xwin, ShapeNotifyMask);
}

void x_display_surface(cairo_surface_t *srf, struct window_x11 *win, const struct dimensions *dim)
{
        x_win_move(win, dim->x, dim->y, dim->w, dim->h);
        cairo_xlib_surface_set_size(win->root_surface, dim->w, dim->h);

        cairo_set_source_surface(win->c_ctx, srf, 0, 0);
        cairo_paint(win->c_ctx);
        cairo_show_page(win->c_ctx);

        if (settings.corner_radius != 0)
                x_win_round_corners(win, dim->corner_radius);

        XFlush(xctx.dpy);

}

bool x_win_visible(struct window_x11 *win)
{
        return win->visible;
}

cairo_t* x_win_get_context(struct window_x11 *win)
{
        return win->c_ctx;
}

static void setopacity(Window win, unsigned long opacity)
{
        Atom _NET_WM_WINDOW_OPACITY =
            XInternAtom(xctx.dpy, "_NET_WM_WINDOW_OPACITY", false);
        XChangeProperty(xctx.dpy,
                        win,
                        _NET_WM_WINDOW_OPACITY,
                        XA_CARDINAL,
                        32,
                        PropModeReplace,
                        (unsigned char *)&opacity,
                        1L);
}

/*
 * Returns the modifier which is NumLock.
 */
static KeySym x_numlock_mod(void)
{
        static KeyCode nl = 0;
        KeySym sym = 0;
        XModifierKeymap *map = XGetModifierMapping(xctx.dpy);

        if (!nl)
                nl = XKeysymToKeycode(xctx.dpy, XStringToKeysym("Num_Lock"));

        for (int mod = 0; mod < 8; mod++) {
                for (int j = 0; j < map->max_keypermod; j++) {
                        if (map->modifiermap[mod*map->max_keypermod+j] == nl) {
                                /* In theory, one could use `1 << mod`, but this
                                 * could count as 'using implementation details',
                                 * so use this large switch. */
                                switch (mod) {
                                case ShiftMapIndex:
                                        sym = ShiftMask;
                                        goto end;
                                case LockMapIndex:
                                        sym = LockMask;
                                        goto end;
                                case ControlMapIndex:
                                        sym = ControlMask;
                                        goto end;
                                case Mod1MapIndex:
                                        sym = Mod1Mask;
                                        goto end;
                                case Mod2MapIndex:
                                        sym = Mod2Mask;
                                        goto end;
                                case Mod3MapIndex:
                                        sym = Mod3Mask;
                                        goto end;
                                case Mod4MapIndex:
                                        sym = Mod4Mask;
                                        goto end;
                                case Mod5MapIndex:
                                        sym = Mod5Mask;
                                        goto end;
                                }
                        }
                }
        }

end:
        XFreeModifiermap(map);
        return sym;
}

/*
 * Helper function to use glib's mainloop mechanic
 * with Xlib
 */
gboolean x_mainloop_fd_prepare(GSource *source, gint *timeout)
{
        if (timeout)
                *timeout = -1;
        return false;
}

/*
 * Helper function to use glib's mainloop mechanic
 * with Xlib
 */
gboolean x_mainloop_fd_check(GSource *source)
{
        return XPending(xctx.dpy) > 0;
}

/*
 * Main Dispatcher for XEvents
 */
gboolean x_mainloop_fd_dispatch(GSource *source, GSourceFunc callback, gpointer user_data)
{
        struct window_x11 *win = ((struct x11_source*) source)->win;

        bool fullscreen_now;
        struct screen_info *scr;
        XEvent ev;
        unsigned int state;
        while (XPending(xctx.dpy) > 0) {
                XNextEvent(xctx.dpy, &ev);

                switch (ev.type) {
                case Expose:
                        LOG_D("XEvent: processing 'Expose'");
                        if (ev.xexpose.count == 0 && win->visible) {
                                draw();
                        }
                        break;
                case ButtonRelease:
                        LOG_D("XEvent: processing 'ButtonRelease'");
                        if (ev.xbutton.window == win->xwin) {
                                x_handle_click(ev);
                                wake_up();
                        }
                        break;
                case KeyPress:
                        LOG_D("XEvent: processing 'KeyPress'");
                        state = ev.xkey.state;
                        /* NumLock is also encoded in the state. Remove it. */
                        state &= ~x_numlock_mod();
                        if (settings.close_ks.str
                            && XLookupKeysym(&ev.xkey,
                                             0) == settings.close_ks.sym
                            && settings.close_ks.mask == state) {
                                const GList *displayed = queues_get_displayed();
                                if (displayed && displayed->data) {
                                        queues_notification_close(displayed->data, REASON_USER);
                                        wake_up();
                                }
                        }
                        if (settings.history_ks.str
                            && XLookupKeysym(&ev.xkey,
                                             0) == settings.history_ks.sym
                            && settings.history_ks.mask == state) {
                                queues_history_pop();
                                wake_up();
                        }
                        if (settings.close_all_ks.str
                            && XLookupKeysym(&ev.xkey,
                                             0) == settings.close_all_ks.sym
                            && settings.close_all_ks.mask == state) {
                                queues_history_push_all();
                                wake_up();
                        }
                        if (settings.context_ks.str
                            && XLookupKeysym(&ev.xkey,
                                             0) == settings.context_ks.sym
                            && settings.context_ks.mask == state) {
                                context_menu();
                                wake_up();
                        }
                        break;
                case CreateNotify:
                        LOG_D("XEvent: processing 'CreateNotify'");
                        if (win->visible &&
                            ev.xcreatewindow.override_redirect == 0)
                                XRaiseWindow(xctx.dpy, win->xwin);
                        break;
                case PropertyNotify:
                        if (ev.xproperty.atom == XA_RESOURCE_MANAGER) {
                                LOG_D("XEvent: processing PropertyNotify for Resource manager");
                                XRM_update_db();
                                screen_dpi_xft_cache_purge();

                                if (win->visible) {
                                        draw();
                                }
                                break;
                        }
                        /* Explicitly fallthrough. Other PropertyNotify events, e.g. catching
                         * _NET_WM get handled in the Focus(In|Out) section */
                case ConfigureNotify:
                case FocusIn:
                case FocusOut:
                        LOG_D("XEvent: Checking for active screen changes");
                        fullscreen_now = have_fullscreen_window();
                        scr = get_active_screen();

                        if (fullscreen_now != fullscreen_last) {
                                fullscreen_last = fullscreen_now;
                                wake_up();
                        } else if (   settings.f_mode != FOLLOW_NONE
                        /* Ignore PropertyNotify, when we're still on the
                         * same screen. PropertyNotify is only necessary
                         * to detect a focus change to another screen
                         */
                                   && win->visible
                                   && scr->id != win->cur_screen) {
                                draw();
                                win->cur_screen = scr->id;
                        }
                        break;
                default:
                        if (!screen_check_event(&ev)) {
                                LOG_D("XEvent: Ignoring '%d'", ev.type);
                        }

                        break;
                }
        }
        return G_SOURCE_CONTINUE;
}

/*
 * Check whether the user is currently idle.
 */
bool x_is_idle(void)
{
        XScreenSaverQueryInfo(xctx.dpy, DefaultRootWindow(xctx.dpy),
                              xctx.screensaver_info);
        if (settings.idle_threshold == 0) {
                return false;
        }
        return xctx.screensaver_info->idle > settings.idle_threshold / 1000;
}

/* TODO move to x_mainloop_* */
/*
 * Handle incoming mouse click events
 */
static void x_handle_click(XEvent ev)
{
        enum mouse_action act;

        switch (ev.xbutton.button) {
                case Button1:
                        act = settings.mouse_left_click;
                        break;
                case Button2:
                        act = settings.mouse_middle_click;
                        break;
                case Button3:
                        act = settings.mouse_right_click;
                        break;
                default:
                        LOG_W("Unsupported mouse button: '%d'", ev.xbutton.button);
                        return;
        }

        if (act == MOUSE_CLOSE_ALL) {
                queues_history_push_all();

                return;
        }

        if (act == MOUSE_DO_ACTION || act == MOUSE_CLOSE_CURRENT) {
                int y = settings.separator_height;
                struct notification *n = NULL;
                int first = true;
                for (const GList *iter = queues_get_displayed(); iter;
                     iter = iter->next) {
                        n = iter->data;
                        if (ev.xbutton.y > y && ev.xbutton.y < y + n->displayed_height)
                                break;

                        y += n->displayed_height + settings.separator_height;
                        if (first)
                                y += settings.frame_width;
                }

                if (n) {
                        if (act == MOUSE_CLOSE_CURRENT)
                                queues_notification_close(n, REASON_USER);
                        else
                                notification_do_action(n);
                }
        }
}

void x_free(void)
{
        if (xctx.screensaver_info)
                XFree(xctx.screensaver_info);

        if (xctx.dpy)
                XCloseDisplay(xctx.dpy);
}

static int XErrorHandlerDB(Display *display, XErrorEvent *e)
{
        char err_buf[BUFSIZ];
        XGetErrorText(display, e->error_code, err_buf, BUFSIZ);
        LOG_W("%s", err_buf);
        return 0;
}

static void XRM_update_db(void)
{
        XrmDatabase db;
        XTextProperty prop;
        Window root;
        // We shouldn't destroy the first DB coming
        // from the display object itself
        static bool runonce = false;

        XFlush(xctx.dpy);
        XSetErrorHandler(XErrorHandlerDB);

        root = RootWindow(xctx.dpy, DefaultScreen(xctx.dpy));

        XLockDisplay(xctx.dpy);
        if (XGetTextProperty(xctx.dpy, root, &prop, XA_RESOURCE_MANAGER)) {
                if (runonce) {
                        db = XrmGetDatabase(xctx.dpy);
                        XrmDestroyDatabase(db);
                }

                db = XrmGetStringDatabase((const char*)prop.value);
                XrmSetDatabase(xctx.dpy, db);
        }
        XUnlockDisplay(xctx.dpy);

        runonce = true;

        XFlush(xctx.dpy);
        XSync(xctx.dpy, false);
        XSetErrorHandler(NULL);
}

/*
 * Setup X11 stuff
 */
void x_setup(void)
{

        /* initialize xctx.dc, font, keyboard, colors */
        if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
                LOG_W("No locale support");
        if (!(xctx.dpy = XOpenDisplay(NULL))) {
                DIE("Cannot open X11 display.");
        }

        x_shortcut_init(&settings.close_ks);
        x_shortcut_init(&settings.close_all_ks);
        x_shortcut_init(&settings.history_ks);
        x_shortcut_init(&settings.context_ks);

        x_shortcut_grab(&settings.close_ks);
        x_shortcut_ungrab(&settings.close_ks);
        x_shortcut_grab(&settings.close_all_ks);
        x_shortcut_ungrab(&settings.close_all_ks);
        x_shortcut_grab(&settings.history_ks);
        x_shortcut_ungrab(&settings.history_ks);
        x_shortcut_grab(&settings.context_ks);
        x_shortcut_ungrab(&settings.context_ks);

        xctx.screensaver_info = XScreenSaverAllocInfo();

        init_screens();
        x_shortcut_grab(&settings.history_ks);

        XrmInitialize();
}

struct geometry x_parse_geometry(const char *geom_str)
{
        assert(geom_str);
        struct geometry geometry = { 0 };

        if (geom_str[0] == '-') {
                geometry.negative_width = true;
                geom_str++;
        } else {
                geometry.negative_width = false;
        }

        int mask = XParseGeometry(geom_str,
                                  &geometry.x, &geometry.y,
                                  &geometry.w, &geometry.h);
        geometry.width_set = mask & WidthValue;
        geometry.negative_x = mask & XNegative;
        geometry.negative_y = mask & YNegative;

        return geometry;
}

static void x_set_wm(Window win)
{

        Atom data[2];

        /* set window title */
        char *title = settings.title != NULL ? settings.title : "Dunst";
        Atom _net_wm_title =
                XInternAtom(xctx.dpy, "_NET_WM_NAME", false);

        XStoreName(xctx.dpy, win, title);
        XChangeProperty(xctx.dpy,
                        win,
                        _net_wm_title,
                        XInternAtom(xctx.dpy, "UTF8_STRING", false),
                        8,
                        PropModeReplace,
                        (unsigned char *)title,
                        strlen(title));

        /* set window class */
        char *class = settings.class != NULL ? settings.class : "Dunst";
        XClassHint classhint = { class, "Dunst" };

        XSetClassHint(xctx.dpy, win, &classhint);

        /* set window type */
        Atom net_wm_window_type =
                XInternAtom(xctx.dpy, "_NET_WM_WINDOW_TYPE", false);

        data[0] = XInternAtom(xctx.dpy, "_NET_WM_WINDOW_TYPE_NOTIFICATION", false);
        data[1] = XInternAtom(xctx.dpy, "_NET_WM_WINDOW_TYPE_UTILITY", false);

        XChangeProperty(xctx.dpy,
                        win,
                        net_wm_window_type,
                        XA_ATOM,
                        32,
                        PropModeReplace,
                        (unsigned char *)data,
                        2L);

        /* set state above */
        Atom net_wm_state =
                XInternAtom(xctx.dpy, "_NET_WM_STATE", false);

        data[0] = XInternAtom(xctx.dpy, "_NET_WM_STATE_ABOVE", false);

        XChangeProperty(xctx.dpy, win, net_wm_state, XA_ATOM, 32,
                PropModeReplace, (unsigned char *) data, 1L);
}

GSource* x_win_reg_source(struct window_x11 *win)
{
        // Static is necessary here because glib keeps the pointer and we need
        // to keep the reference alive.
        static GSourceFuncs xsrc_fn = {
                x_mainloop_fd_prepare,
                x_mainloop_fd_check,
                x_mainloop_fd_dispatch,
                NULL,
                NULL,
                NULL
        };

        struct x11_source *xsrc = (struct x11_source*) g_source_new(&xsrc_fn,
                                                        sizeof(struct x11_source));

        xsrc->win = win;

        g_source_add_unix_fd((GSource*) xsrc, xctx.dpy->fd, G_IO_IN | G_IO_HUP | G_IO_ERR);

        g_source_attach((GSource*) xsrc, NULL);

        return (GSource*)xsrc;
}

/*
 * Setup the window
 */
struct window_x11 *x_win_create(void)
{
        struct window_x11 *win = g_malloc0(sizeof(struct window_x11));

        Window root;
        XSetWindowAttributes wa;

        root = RootWindow(xctx.dpy, DefaultScreen(xctx.dpy));

        wa.override_redirect = true;
        wa.background_pixmap = ParentRelative;
        wa.event_mask =
            ExposureMask | KeyPressMask | VisibilityChangeMask |
            ButtonReleaseMask | FocusChangeMask| StructureNotifyMask;

        struct screen_info *scr = get_active_screen();
        win->xwin = XCreateWindow(xctx.dpy,
                                 root,
                                 scr->x,
                                 scr->y,
                                 scr->w,
                                 1,
                                 0,
                                 DefaultDepth(xctx.dpy, DefaultScreen(xctx.dpy)),
                                 CopyFromParent,
                                 DefaultVisual(xctx.dpy, DefaultScreen(xctx.dpy)),
                                 CWOverrideRedirect | CWBackPixmap | CWEventMask,
                                 &wa);

        x_set_wm(win->xwin);
        settings.transparency =
            settings.transparency > 100 ? 100 : settings.transparency;
        setopacity(win->xwin,
                   (unsigned long)((100 - settings.transparency) *
                                   (0xffffffff / 100)));

        win->root_surface = cairo_xlib_surface_create(xctx.dpy, win->xwin,
                                                      DefaultVisual(xctx.dpy, 0),
                                                      WIDTH, HEIGHT);
        win->c_ctx = cairo_create(win->root_surface);

        win->esrc = x_win_reg_source(win);

        /* SubstructureNotifyMask is required for receiving CreateNotify events
         * in order to raise the window when something covers us. See #160
         *
         * PropertyChangeMask is requred for getting screen change events when follow_mode != none
         *                    and it's also needed to receive
         *                    XA_RESOURCE_MANAGER events to update the dpi when
         *                    the xresource value is updated
         */
        long root_event_mask = SubstructureNotifyMask | PropertyChangeMask;
        if (settings.f_mode != FOLLOW_NONE) {
                root_event_mask |= FocusChangeMask;
        }
        XSelectInput(xctx.dpy, root, root_event_mask);

        return win;
}

void x_win_destroy(struct window_x11 *win)
{
        g_source_destroy(win->esrc);
        g_source_unref(win->esrc);

        cairo_destroy(win->c_ctx);
        cairo_surface_destroy(win->root_surface);
        XDestroyWindow(xctx.dpy, win->xwin);

        g_free(win);
}

/*
 * Show the window and grab shortcuts.
 */
void x_win_show(struct window_x11 *win)
{
        /* window is already mapped or there's nothing to show */
        if (win->visible)
                return;

        x_shortcut_grab(&settings.close_ks);
        x_shortcut_grab(&settings.close_all_ks);
        x_shortcut_grab(&settings.context_ks);

        x_shortcut_setup_error_handler();
        XGrabButton(xctx.dpy,
                    AnyButton,
                    AnyModifier,
                    win->xwin,
                    false,
                    (ButtonPressMask|ButtonReleaseMask),
                    GrabModeAsync,
                    GrabModeSync,
                    None,
                    None);
        if (x_shortcut_tear_down_error_handler()) {
                LOG_W("Unable to grab mouse button(s).");
        }

        XMapRaised(xctx.dpy, win->xwin);
        win->visible = true;
}

/*
 * Hide the window and ungrab unused keyboard_shortcuts
 */
void x_win_hide(struct window_x11 *win)
{
        ASSERT_OR_RET(win->visible,);

        x_shortcut_ungrab(&settings.close_ks);
        x_shortcut_ungrab(&settings.close_all_ks);
        x_shortcut_ungrab(&settings.context_ks);

        XUngrabButton(xctx.dpy, AnyButton, AnyModifier, win->xwin);
        XUnmapWindow(xctx.dpy, win->xwin);
        XFlush(xctx.dpy);
        win->visible = false;
}

/*
 * Parse a string into a modifier mask.
 */
KeySym x_shortcut_string_to_mask(const char *str)
{
        if (STR_EQ(str, "ctrl")) {
                return ControlMask;
        } else if (STR_EQ(str, "mod4")) {
                return Mod4Mask;
        } else if (STR_EQ(str, "mod3")) {
                return Mod3Mask;
        } else if (STR_EQ(str, "mod2")) {
                return Mod2Mask;
        } else if (STR_EQ(str, "mod1")) {
                return Mod1Mask;
        } else if (STR_EQ(str, "shift")) {
                return ShiftMask;
        } else {
                LOG_W("Unknown Modifier: '%s'", str);
                return 0;
        }
}

/*
 * Error handler for grabbing mouse and keyboard errors.
 */
static int GrabXErrorHandler(Display *display, XErrorEvent *e)
{
        dunst_grab_errored = true;
        char err_buf[BUFSIZ];
        XGetErrorText(display, e->error_code, err_buf, BUFSIZ);

        if (e->error_code != BadAccess) {
                DIE("%s", err_buf);
        } else {
                LOG_W("%s", err_buf);
        }

        return 0;
}

/*
 * Setup the Error handler.
 */
static void x_shortcut_setup_error_handler(void)
{
        dunst_grab_errored = false;

        XFlush(xctx.dpy);
        XSetErrorHandler(GrabXErrorHandler);
}

/*
 * Tear down the Error handler.
 */
static int x_shortcut_tear_down_error_handler(void)
{
        XFlush(xctx.dpy);
        XSync(xctx.dpy, false);
        XSetErrorHandler(NULL);
        return dunst_grab_errored;
}

/*
 * Grab the given keyboard shortcut.
 */
static int x_shortcut_grab(struct keyboard_shortcut *ks)
{
        ASSERT_OR_RET(ks->is_valid, 1);
        Window root;
        root = RootWindow(xctx.dpy, DefaultScreen(xctx.dpy));

        x_shortcut_setup_error_handler();

        if (ks->is_valid) {
                XGrabKey(xctx.dpy,
                         ks->code,
                         ks->mask,
                         root,
                         true,
                         GrabModeAsync,
                         GrabModeAsync);
                XGrabKey(xctx.dpy,
                         ks->code,
                         ks->mask | x_numlock_mod(),
                         root,
                         true,
                         GrabModeAsync,
                         GrabModeAsync);
        }

        if (x_shortcut_tear_down_error_handler()) {
                LOG_W("Unable to grab key '%s'.", ks->str);
                ks->is_valid = false;
                return 1;
        }
        return 0;
}

/*
 * Ungrab the given keyboard shortcut.
 */
static void x_shortcut_ungrab(struct keyboard_shortcut *ks)
{
        Window root;
        root = RootWindow(xctx.dpy, DefaultScreen(xctx.dpy));
        if (ks->is_valid) {
                XUngrabKey(xctx.dpy, ks->code, ks->mask, root);
                XUngrabKey(xctx.dpy, ks->code, ks->mask | x_numlock_mod(), root);
        }
}

/*
 * Initialize the keyboard shortcut.
 */
static void x_shortcut_init(struct keyboard_shortcut *ks)
{
        ASSERT_OR_RET(ks && ks->str,);

        if (STR_EQ(ks->str, "none") || (STR_EQ(ks->str, ""))) {
                ks->is_valid = false;
                return;
        }

        char *str = g_strdup(ks->str);
        char *str_begin = str;

        while (strchr(str, '+')) {
                char *mod = str;
                while (*str != '+')
                        str++;
                *str = '\0';
                str++;
                g_strchomp(mod);
                ks->mask = ks->mask | x_shortcut_string_to_mask(mod);
        }
        g_strstrip(str);

        ks->sym = XStringToKeysym(str);
        /* find matching keycode for ks->sym */
        int min_keysym, max_keysym;
        XDisplayKeycodes(xctx.dpy, &min_keysym, &max_keysym);

        ks->code = NoSymbol;

        for (int i = min_keysym; i <= max_keysym; i++) {
                if (XkbKeycodeToKeysym(xctx.dpy, i, 0, 0) == ks->sym
                    || XkbKeycodeToKeysym(xctx.dpy, i, 0, 1) == ks->sym) {
                        ks->code = i;
                        break;
                }
        }

        if (ks->sym == NoSymbol || ks->code == NoSymbol) {
                LOG_W("Unknown keyboard shortcut: '%s'", ks->str);
                ks->is_valid = false;
        } else {
                ks->is_valid = true;
        }

        g_free(str_begin);
}

/* vim: set tabstop=8 shiftwidth=8 expandtab textwidth=0: */
