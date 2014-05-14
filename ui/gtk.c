/*
 * GTK UI
 *
 * Copyright IBM, Corp. 2012
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * Portions from gtk-vnc:
 *
 * GTK VNC Widget
 *
 * Copyright (C) 2006  Anthony Liguori <anthony@codemonkey.ws>
 * Copyright (C) 2009-2010 Daniel P. Berrange <dan@berrange.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.0 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#define GETTEXT_PACKAGE "qemu"
#define LOCALEDIR "po"

#ifdef _WIN32
# define _WIN32_WINNT 0x0601 /* needed to get definition of MAPVK_VK_TO_VSC */
#endif

#include "qemu-common.h"

#ifdef CONFIG_PRAGMA_DIAGNOSTIC_AVAILABLE
/* Work around an -Wstrict-prototypes warning in GTK headers */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#endif
#include <gtk/gtk.h>
#ifdef CONFIG_PRAGMA_DIAGNOSTIC_AVAILABLE
#pragma GCC diagnostic pop
#endif


#include <gdk/gdkkeysyms.h>
#include <glib/gi18n.h>
#include <locale.h>
#if defined(CONFIG_VTE)
#include <vte/vte.h>
#endif
#include <math.h>

#include "trace.h"
#include "ui/console.h"
#include "ui/input.h"
#include "sysemu/sysemu.h"
#include "qmp-commands.h"
#include "x_keymap.h"
#include "keymaps.h"
#include "sysemu/char.h"

#define MAX_VCS 10

#if !defined(CONFIG_VTE)
# define VTE_CHECK_VERSION(a, b, c) 0
#endif

/* Compatibility define to let us build on both Gtk2 and Gtk3 */
#if GTK_CHECK_VERSION(3, 0, 0)
static inline void gdk_drawable_get_size(GdkWindow *w, gint *ww, gint *wh)
{
    *ww = gdk_window_get_width(w);
    *wh = gdk_window_get_height(w);
}
#endif

#if !GTK_CHECK_VERSION(2, 20, 0)
#define gtk_widget_get_realized(widget) GTK_WIDGET_REALIZED(widget)
#endif

#ifndef GDK_KEY_0
#define GDK_KEY_0 GDK_0
#define GDK_KEY_1 GDK_1
#define GDK_KEY_2 GDK_2
#define GDK_KEY_f GDK_f
#define GDK_KEY_g GDK_g
#define GDK_KEY_plus GDK_plus
#define GDK_KEY_minus GDK_minus
#endif

#define HOTKEY_MODIFIERS        (GDK_CONTROL_MASK | GDK_MOD1_MASK)
#define IGNORE_MODIFIER_MASK \
    (GDK_MODIFIER_MASK & ~(GDK_LOCK_MASK | GDK_MOD2_MASK))

static const int modifier_keycode[] = {
    /* shift, control, alt keys, meta keys, both left & right */
    0x2a, 0x36, 0x1d, 0x9d, 0x38, 0xb8, 0xdb, 0xdd,
};

typedef struct VirtualConsole
{
    GtkWidget *menu_item;
    GtkWidget *terminal;
#if defined(CONFIG_VTE)
    GtkWidget *scrolled_window;
    CharDriverState *chr;
#endif
    int fd;
} VirtualConsole;

typedef struct GtkDisplayState
{
    GtkWidget *window;

    GtkWidget *menu_bar;

    GtkAccelGroup *accel_group;

    GtkWidget *machine_menu_item;
    GtkWidget *machine_menu;
    GtkWidget *pause_item;
    GtkWidget *reset_item;
    GtkWidget *powerdown_item;
    GtkWidget *quit_item;

    GtkWidget *view_menu_item;
    GtkWidget *view_menu;
    GtkWidget *full_screen_item;
    GtkWidget *zoom_in_item;
    GtkWidget *zoom_out_item;
    GtkWidget *zoom_fixed_item;
    GtkWidget *zoom_fit_item;
    GtkWidget *grab_item;
    GtkWidget *grab_on_hover_item;
    GtkWidget *vga_item;

    int nb_vcs;
    VirtualConsole vc[MAX_VCS];

    GtkWidget *show_tabs_item;

    GtkWidget *vbox;
    GtkWidget *notebook;
    GtkWidget *drawing_area;
    cairo_surface_t *surface;
    pixman_image_t *convert;
    DisplayChangeListener dcl;
    DisplaySurface *ds;
    int button_mask;
    gboolean last_set;
    int last_x;
    int last_y;
    int grab_x_root;
    int grab_y_root;

    double scale_x;
    double scale_y;
    gboolean full_screen;

    GdkCursor *null_cursor;
    Notifier mouse_mode_notifier;
    gboolean free_scale;

    bool external_pause_update;

    bool modifier_pressed[ARRAY_SIZE(modifier_keycode)];
} GtkDisplayState;

static GtkDisplayState *global_state;

/** Utility Functions **/

static bool gd_is_grab_active(GtkDisplayState *s)
{
    return gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(s->grab_item));
}

static bool gd_grab_on_hover(GtkDisplayState *s)
{
    return gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(s->grab_on_hover_item));
}

static bool gd_on_vga(GtkDisplayState *s)
{
    return gtk_notebook_get_current_page(GTK_NOTEBOOK(s->notebook)) == 0;
}

static void gd_update_cursor(GtkDisplayState *s, gboolean override)
{
    GdkWindow *window;
    bool on_vga;

    window = gtk_widget_get_window(GTK_WIDGET(s->drawing_area));

    on_vga = gd_on_vga(s);

    if ((override || on_vga) &&
        (s->full_screen || qemu_input_is_absolute() || gd_is_grab_active(s))) {
        gdk_window_set_cursor(window, s->null_cursor);
    } else {
        gdk_window_set_cursor(window, NULL);
    }
}

static void gd_update_caption(GtkDisplayState *s)
{
    const char *status = "";
    gchar *title;
    const char *grab = "";
    bool is_paused = !runstate_is_running();

    if (gd_is_grab_active(s)) {
        grab = _(" - Press Ctrl+Alt+G to release grab");
    }

    if (is_paused) {
        status = _(" [Paused]");
    }
    s->external_pause_update = true;
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(s->pause_item),
                                   is_paused);
    s->external_pause_update = false;

    if (qemu_name) {
        title = g_strdup_printf("QEMU (%s)%s%s", qemu_name, status, grab);
    } else {
        title = g_strdup_printf("QEMU%s%s", status, grab);
    }

    gtk_window_set_title(GTK_WINDOW(s->window), title);

    g_free(title);
}

static void gd_update_windowsize(GtkDisplayState *s)
{
    if (!s->full_screen) {
        GtkRequisition req;
        double sx, sy;

        if (s->free_scale) {
            sx = s->scale_x;
            sy = s->scale_y;

            s->scale_y = 1.0;
            s->scale_x = 1.0;
        } else {
            sx = 1.0;
            sy = 1.0;
        }

        gtk_widget_set_size_request(s->drawing_area,
                                    surface_width(s->ds) * s->scale_x,
                                    surface_height(s->ds) * s->scale_y);
#if GTK_CHECK_VERSION(3, 0, 0)
        gtk_widget_get_preferred_size(s->vbox, NULL, &req);
#else
        gtk_widget_size_request(s->vbox, &req);
#endif

        gtk_window_resize(GTK_WINDOW(s->window),
                          req.width * sx, req.height * sy);
    }
}

static void gd_update_full_redraw(GtkDisplayState *s)
{
    int ww, wh;
    gdk_drawable_get_size(gtk_widget_get_window(s->drawing_area), &ww, &wh);
    gtk_widget_queue_draw_area(s->drawing_area, 0, 0, ww, wh);
}

static void gtk_release_modifiers(GtkDisplayState *s)
{
    int i, keycode;

    if (!gd_on_vga(s)) {
        return;
    }
    for (i = 0; i < ARRAY_SIZE(modifier_keycode); i++) {
        keycode = modifier_keycode[i];
        if (!s->modifier_pressed[i]) {
            continue;
        }
        qemu_input_event_send_key_number(s->dcl.con, keycode, false);
        s->modifier_pressed[i] = false;
    }
}

/** DisplayState Callbacks **/

static void gd_update(DisplayChangeListener *dcl,
                      int x, int y, int w, int h)
{
    GtkDisplayState *s = container_of(dcl, GtkDisplayState, dcl);
    int x1, x2, y1, y2;
    int mx, my;
    int fbw, fbh;
    int ww, wh;

    trace_gd_update(x, y, w, h);

    if (s->convert) {
        pixman_image_composite(PIXMAN_OP_SRC, s->ds->image, NULL, s->convert,
                               x, y, 0, 0, x, y, w, h);
    }

    x1 = floor(x * s->scale_x);
    y1 = floor(y * s->scale_y);

    x2 = ceil(x * s->scale_x + w * s->scale_x);
    y2 = ceil(y * s->scale_y + h * s->scale_y);

    fbw = surface_width(s->ds) * s->scale_x;
    fbh = surface_height(s->ds) * s->scale_y;

    gdk_drawable_get_size(gtk_widget_get_window(s->drawing_area), &ww, &wh);

    mx = my = 0;
    if (ww > fbw) {
        mx = (ww - fbw) / 2;
    }
    if (wh > fbh) {
        my = (wh - fbh) / 2;
    }

    gtk_widget_queue_draw_area(s->drawing_area, mx + x1, my + y1, (x2 - x1), (y2 - y1));
}

static void gd_refresh(DisplayChangeListener *dcl)
{
    graphic_hw_update(dcl->con);
}

#if GTK_CHECK_VERSION(3, 0, 0)
static void gd_mouse_set(DisplayChangeListener *dcl,
                         int x, int y, int visible)
{
    GtkDisplayState *s = container_of(dcl, GtkDisplayState, dcl);
    GdkDisplay *dpy;
    GdkDeviceManager *mgr;
    gint x_root, y_root;

    if (qemu_input_is_absolute()) {
        return;
    }

    dpy = gtk_widget_get_display(s->drawing_area);
    mgr = gdk_display_get_device_manager(dpy);
    gdk_window_get_root_coords(gtk_widget_get_window(s->drawing_area),
                               x, y, &x_root, &y_root);
    gdk_device_warp(gdk_device_manager_get_client_pointer(mgr),
                    gtk_widget_get_screen(s->drawing_area),
                    x_root, y_root);
}
#else
static void gd_mouse_set(DisplayChangeListener *dcl,
                         int x, int y, int visible)
{
    GtkDisplayState *s = container_of(dcl, GtkDisplayState, dcl);
    gint x_root, y_root;

    if (qemu_input_is_absolute()) {
        return;
    }

    gdk_window_get_root_coords(gtk_widget_get_window(s->drawing_area),
                               x, y, &x_root, &y_root);
    gdk_display_warp_pointer(gtk_widget_get_display(s->drawing_area),
                             gtk_widget_get_screen(s->drawing_area),
                             x_root, y_root);
}
#endif

static void gd_cursor_define(DisplayChangeListener *dcl,
                             QEMUCursor *c)
{
    GtkDisplayState *s = container_of(dcl, GtkDisplayState, dcl);
    GdkPixbuf *pixbuf;
    GdkCursor *cursor;

    pixbuf = gdk_pixbuf_new_from_data((guchar *)(c->data),
                                      GDK_COLORSPACE_RGB, true, 8,
                                      c->width, c->height, c->width * 4,
                                      NULL, NULL);
    cursor = gdk_cursor_new_from_pixbuf(gtk_widget_get_display(s->drawing_area),
                                        pixbuf, c->hot_x, c->hot_y);
    gdk_window_set_cursor(gtk_widget_get_window(s->drawing_area), cursor);
    g_object_unref(pixbuf);
#if !GTK_CHECK_VERSION(3, 0, 0)
    gdk_cursor_unref(cursor);
#else
    g_object_unref(cursor);
#endif
}

static void gd_switch(DisplayChangeListener *dcl,
                      DisplaySurface *surface)
{
    GtkDisplayState *s = container_of(dcl, GtkDisplayState, dcl);
    bool resized = true;

    trace_gd_switch(surface_width(surface), surface_height(surface));

    if (s->surface) {
        cairo_surface_destroy(s->surface);
    }

    if (s->ds &&
        surface_width(s->ds) == surface_width(surface) &&
        surface_height(s->ds) == surface_height(surface)) {
        resized = false;
    }
    s->ds = surface;

    if (s->convert) {
        pixman_image_unref(s->convert);
        s->convert = NULL;
    }

    if (surface->format == PIXMAN_x8r8g8b8) {
        /*
         * PIXMAN_x8r8g8b8 == CAIRO_FORMAT_RGB24
         *
         * No need to convert, use surface directly.  Should be the
         * common case as this is qemu_default_pixelformat(32) too.
         */
        s->surface = cairo_image_surface_create_for_data
            (surface_data(surface),
             CAIRO_FORMAT_RGB24,
             surface_width(surface),
             surface_height(surface),
             surface_stride(surface));
    } else {
        /* Must convert surface, use pixman to do it. */
        s->convert = pixman_image_create_bits(PIXMAN_x8r8g8b8,
                                              surface_width(surface),
                                              surface_height(surface),
                                              NULL, 0);
        s->surface = cairo_image_surface_create_for_data
            ((void *)pixman_image_get_data(s->convert),
             CAIRO_FORMAT_RGB24,
             pixman_image_get_width(s->convert),
             pixman_image_get_height(s->convert),
             pixman_image_get_stride(s->convert));
        pixman_image_composite(PIXMAN_OP_SRC, s->ds->image, NULL, s->convert,
                               0, 0, 0, 0, 0, 0,
                               pixman_image_get_width(s->convert),
                               pixman_image_get_height(s->convert));
    }

    if (resized) {
        gd_update_windowsize(s);
    } else {
        gd_update_full_redraw(s);
    }
}

/** QEMU Events **/

static void gd_change_runstate(void *opaque, int running, RunState state)
{
    GtkDisplayState *s = opaque;

    gd_update_caption(s);
}

static void gd_mouse_mode_change(Notifier *notify, void *data)
{
    GtkDisplayState *s;

    s = container_of(notify, GtkDisplayState, mouse_mode_notifier);
    /* release the grab at switching to absolute mode */
    if (qemu_input_is_absolute() && gd_is_grab_active(s)) {
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(s->grab_item),
                                       FALSE);
    }
    gd_update_cursor(s, FALSE);
}

/** GTK Events **/

static gboolean gd_window_key_event(GtkWidget *widget, GdkEventKey *key, void *opaque)
{
    GtkDisplayState *s = opaque;
    gboolean handled = FALSE;

    if (!gd_is_grab_active(s) ||
        (key->state & IGNORE_MODIFIER_MASK) == HOTKEY_MODIFIERS) {
        handled = gtk_window_activate_key(GTK_WINDOW(widget), key);
    }
    if (handled) {
        gtk_release_modifiers(s);
    } else {
        handled = gtk_window_propagate_key_event(GTK_WINDOW(widget), key);
    }

    return handled;
}

static gboolean gd_window_close(GtkWidget *widget, GdkEvent *event,
                                void *opaque)
{
    GtkDisplayState *s = opaque;

    if (!no_quit) {
        unregister_displaychangelistener(&s->dcl);
        qmp_quit(NULL);
        return FALSE;
    }

    return TRUE;
}

static gboolean gd_draw_event(GtkWidget *widget, cairo_t *cr, void *opaque)
{
    GtkDisplayState *s = opaque;
    int mx, my;
    int ww, wh;
    int fbw, fbh;

    if (!gtk_widget_get_realized(widget)) {
        return FALSE;
    }

    fbw = surface_width(s->ds);
    fbh = surface_height(s->ds);

    gdk_drawable_get_size(gtk_widget_get_window(widget), &ww, &wh);

    if (s->full_screen) {
        s->scale_x = (double)ww / fbw;
        s->scale_y = (double)wh / fbh;
    } else if (s->free_scale) {
        double sx, sy;

        sx = (double)ww / fbw;
        sy = (double)wh / fbh;

        s->scale_x = s->scale_y = MIN(sx, sy);
    }

    fbw *= s->scale_x;
    fbh *= s->scale_y;

    mx = my = 0;
    if (ww > fbw) {
        mx = (ww - fbw) / 2;
    }
    if (wh > fbh) {
        my = (wh - fbh) / 2;
    }

    cairo_rectangle(cr, 0, 0, ww, wh);

    /* Optionally cut out the inner area where the pixmap
       will be drawn. This avoids 'flashing' since we're
       not double-buffering. Note we're using the undocumented
       behaviour of drawing the rectangle from right to left
       to cut out the whole */
    cairo_rectangle(cr, mx + fbw, my,
                    -1 * fbw, fbh);
    cairo_fill(cr);

    cairo_scale(cr, s->scale_x, s->scale_y);
    cairo_set_source_surface(cr, s->surface, mx / s->scale_x, my / s->scale_y);
    cairo_paint(cr);

    return TRUE;
}

#if !GTK_CHECK_VERSION(3, 0, 0)
static gboolean gd_expose_event(GtkWidget *widget, GdkEventExpose *expose,
                                void *opaque)
{
    cairo_t *cr;
    gboolean ret;

    cr = gdk_cairo_create(gtk_widget_get_window(widget));
    cairo_rectangle(cr,
                    expose->area.x,
                    expose->area.y,
                    expose->area.width,
                    expose->area.height);
    cairo_clip(cr);

    ret = gd_draw_event(widget, cr, opaque);

    cairo_destroy(cr);

    return ret;
}
#endif

static gboolean gd_motion_event(GtkWidget *widget, GdkEventMotion *motion,
                                void *opaque)
{
    GtkDisplayState *s = opaque;
    int x, y;
    int mx, my;
    int fbh, fbw;
    int ww, wh;

    fbw = surface_width(s->ds) * s->scale_x;
    fbh = surface_height(s->ds) * s->scale_y;

    gdk_drawable_get_size(gtk_widget_get_window(s->drawing_area), &ww, &wh);

    mx = my = 0;
    if (ww > fbw) {
        mx = (ww - fbw) / 2;
    }
    if (wh > fbh) {
        my = (wh - fbh) / 2;
    }

    x = (motion->x - mx) / s->scale_x;
    y = (motion->y - my) / s->scale_y;

    if (qemu_input_is_absolute()) {
        if (x < 0 || y < 0 ||
            x >= surface_width(s->ds) ||
            y >= surface_height(s->ds)) {
            return TRUE;
        }
        qemu_input_queue_abs(s->dcl.con, INPUT_AXIS_X, x,
                             surface_width(s->ds));
        qemu_input_queue_abs(s->dcl.con, INPUT_AXIS_Y, y,
                             surface_height(s->ds));
        qemu_input_event_sync();
    } else if (s->last_set && gd_is_grab_active(s)) {
        qemu_input_queue_rel(s->dcl.con, INPUT_AXIS_X, x - s->last_x);
        qemu_input_queue_rel(s->dcl.con, INPUT_AXIS_Y, y - s->last_y);
        qemu_input_event_sync();
    }
    s->last_x = x;
    s->last_y = y;
    s->last_set = TRUE;

    if (!qemu_input_is_absolute() && gd_is_grab_active(s)) {
        GdkScreen *screen = gtk_widget_get_screen(s->drawing_area);
        int x = (int)motion->x_root;
        int y = (int)motion->y_root;

        /* In relative mode check to see if client pointer hit
         * one of the screen edges, and if so move it back by
         * 200 pixels. This is important because the pointer
         * in the server doesn't correspond 1-for-1, and so
         * may still be only half way across the screen. Without
         * this warp, the server pointer would thus appear to hit
         * an invisible wall */
        if (x == 0) {
            x += 200;
        }
        if (y == 0) {
            y += 200;
        }
        if (x == (gdk_screen_get_width(screen) - 1)) {
            x -= 200;
        }
        if (y == (gdk_screen_get_height(screen) - 1)) {
            y -= 200;
        }

        if (x != (int)motion->x_root || y != (int)motion->y_root) {
#if GTK_CHECK_VERSION(3, 0, 0)
            GdkDevice *dev = gdk_event_get_device((GdkEvent *)motion);
            gdk_device_warp(dev, screen, x, y);
#else
            GdkDisplay *display = gtk_widget_get_display(widget);
            gdk_display_warp_pointer(display, screen, x, y);
#endif
            s->last_set = FALSE;
            return FALSE;
        }
    }
    return TRUE;
}

static gboolean gd_button_event(GtkWidget *widget, GdkEventButton *button,
                                void *opaque)
{
    GtkDisplayState *s = opaque;
    InputButton btn;

    /* implicitly grab the input at the first click in the relative mode */
    if (button->button == 1 && button->type == GDK_BUTTON_PRESS &&
        !qemu_input_is_absolute() && !gd_is_grab_active(s)) {
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(s->grab_item),
                                       TRUE);
        return TRUE;
    }

    if (button->button == 1) {
        btn = INPUT_BUTTON_LEFT;
    } else if (button->button == 2) {
        btn = INPUT_BUTTON_MIDDLE;
    } else if (button->button == 3) {
        btn = INPUT_BUTTON_RIGHT;
    } else {
        return TRUE;
    }

    qemu_input_queue_btn(s->dcl.con, btn, button->type == GDK_BUTTON_PRESS);
    qemu_input_event_sync();
    return TRUE;
}

static gboolean gd_scroll_event(GtkWidget *widget, GdkEventScroll *scroll,
                                void *opaque)
{
    GtkDisplayState *s = opaque;
    InputButton btn;

    if (scroll->direction == GDK_SCROLL_UP) {
        btn = INPUT_BUTTON_WHEEL_UP;
    } else if (scroll->direction == GDK_SCROLL_DOWN) {
        btn = INPUT_BUTTON_WHEEL_DOWN;
    } else {
        return TRUE;
    }

    qemu_input_queue_btn(s->dcl.con, btn, true);
    qemu_input_event_sync();
    qemu_input_queue_btn(s->dcl.con, btn, false);
    qemu_input_event_sync();
    return TRUE;
}

static gboolean gd_key_event(GtkWidget *widget, GdkEventKey *key, void *opaque)
{
    GtkDisplayState *s = opaque;
    int gdk_keycode = key->hardware_keycode;
    int i;

#ifdef _WIN32
    UINT qemu_keycode = MapVirtualKey(gdk_keycode, MAPVK_VK_TO_VSC);
    switch (qemu_keycode) {
    case 103:   /* alt gr */
        qemu_keycode = 56 | SCANCODE_GREY;
        break;
    }
#else
    int qemu_keycode;

    if (gdk_keycode < 9) {
        qemu_keycode = 0;
    } else if (gdk_keycode < 97) {
        qemu_keycode = gdk_keycode - 8;
    } else if (gdk_keycode < 158) {
        qemu_keycode = translate_evdev_keycode(gdk_keycode - 97);
    } else if (gdk_keycode == 208) { /* Hiragana_Katakana */
        qemu_keycode = 0x70;
    } else if (gdk_keycode == 211) { /* backslash */
        qemu_keycode = 0x73;
    } else {
        qemu_keycode = 0;
    }
#endif

    trace_gd_key_event(gdk_keycode, qemu_keycode,
                       (key->type == GDK_KEY_PRESS) ? "down" : "up");

    for (i = 0; i < ARRAY_SIZE(modifier_keycode); i++) {
        if (qemu_keycode == modifier_keycode[i]) {
            s->modifier_pressed[i] = (key->type == GDK_KEY_PRESS);
        }
    }

    qemu_input_event_send_key_number(s->dcl.con, qemu_keycode,
                                     key->type == GDK_KEY_PRESS);

    return TRUE;
}

static gboolean gd_event(GtkWidget *widget, GdkEvent *event, void *opaque)
{
    if (event->type == GDK_MOTION_NOTIFY) {
        return gd_motion_event(widget, &event->motion, opaque);
    }
    return FALSE;
}

/** Window Menu Actions **/

static void gd_menu_pause(GtkMenuItem *item, void *opaque)
{
    GtkDisplayState *s = opaque;

    if (s->external_pause_update) {
        return;
    }
    if (runstate_is_running()) {
        qmp_stop(NULL);
    } else {
        qmp_cont(NULL);
    }
}

static void gd_menu_reset(GtkMenuItem *item, void *opaque)
{
    qmp_system_reset(NULL);
}

static void gd_menu_powerdown(GtkMenuItem *item, void *opaque)
{
    qmp_system_powerdown(NULL);
}

static void gd_menu_quit(GtkMenuItem *item, void *opaque)
{
    qmp_quit(NULL);
}

static void gd_menu_switch_vc(GtkMenuItem *item, void *opaque)
{
    GtkDisplayState *s = opaque;

    if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(s->vga_item))) {
        gtk_notebook_set_current_page(GTK_NOTEBOOK(s->notebook), 0);
    } else {
        int i;

        gtk_release_modifiers(s);
        for (i = 0; i < s->nb_vcs; i++) {
            if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(s->vc[i].menu_item))) {
                gtk_notebook_set_current_page(GTK_NOTEBOOK(s->notebook), i + 1);
                break;
            }
        }
    }
}

static void gd_menu_show_tabs(GtkMenuItem *item, void *opaque)
{
    GtkDisplayState *s = opaque;

    if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(s->show_tabs_item))) {
        gtk_notebook_set_show_tabs(GTK_NOTEBOOK(s->notebook), TRUE);
    } else {
        gtk_notebook_set_show_tabs(GTK_NOTEBOOK(s->notebook), FALSE);
    }
}

static void gd_menu_full_screen(GtkMenuItem *item, void *opaque)
{
    GtkDisplayState *s = opaque;

    if (!s->full_screen) {
        gtk_notebook_set_show_tabs(GTK_NOTEBOOK(s->notebook), FALSE);
        gtk_widget_set_size_request(s->menu_bar, 0, 0);
        gtk_widget_set_size_request(s->drawing_area, -1, -1);
        gtk_window_fullscreen(GTK_WINDOW(s->window));
        if (gd_on_vga(s)) {
            gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(s->grab_item), TRUE);
        }
        s->full_screen = TRUE;
    } else {
        gtk_window_unfullscreen(GTK_WINDOW(s->window));
        gd_menu_show_tabs(GTK_MENU_ITEM(s->show_tabs_item), s);
        gtk_widget_set_size_request(s->menu_bar, -1, -1);
        gtk_widget_set_size_request(s->drawing_area,
                                    surface_width(s->ds),
                                    surface_height(s->ds));
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(s->grab_item), FALSE);
        s->full_screen = FALSE;
        s->scale_x = 1.0;
        s->scale_y = 1.0;
    }

    gd_update_cursor(s, FALSE);
}

static void gd_menu_zoom_in(GtkMenuItem *item, void *opaque)
{
    GtkDisplayState *s = opaque;

    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(s->zoom_fit_item),
                                   FALSE);

    s->scale_x += .25;
    s->scale_y += .25;

    gd_update_windowsize(s);
}

static void gd_menu_zoom_out(GtkMenuItem *item, void *opaque)
{
    GtkDisplayState *s = opaque;

    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(s->zoom_fit_item),
                                   FALSE);

    s->scale_x -= .25;
    s->scale_y -= .25;

    s->scale_x = MAX(s->scale_x, .25);
    s->scale_y = MAX(s->scale_y, .25);

    gd_update_windowsize(s);
}

static void gd_menu_zoom_fixed(GtkMenuItem *item, void *opaque)
{
    GtkDisplayState *s = opaque;

    s->scale_x = 1.0;
    s->scale_y = 1.0;

    gd_update_windowsize(s);
}

static void gd_menu_zoom_fit(GtkMenuItem *item, void *opaque)
{
    GtkDisplayState *s = opaque;

    if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(s->zoom_fit_item))) {
        s->free_scale = TRUE;
    } else {
        s->free_scale = FALSE;
        s->scale_x = 1.0;
        s->scale_y = 1.0;
        gd_update_windowsize(s);
    }

    gd_update_full_redraw(s);
}

static void gd_grab_keyboard(GtkDisplayState *s)
{
#if GTK_CHECK_VERSION(3, 0, 0)
    GdkDisplay *display = gtk_widget_get_display(s->drawing_area);
    GdkDeviceManager *mgr = gdk_display_get_device_manager(display);
    GList *devices = gdk_device_manager_list_devices(mgr,
                                                     GDK_DEVICE_TYPE_MASTER);
    GList *tmp = devices;
    while (tmp) {
        GdkDevice *dev = tmp->data;
        if (gdk_device_get_source(dev) == GDK_SOURCE_KEYBOARD) {
            gdk_device_grab(dev,
                            gtk_widget_get_window(s->drawing_area),
                            GDK_OWNERSHIP_NONE,
                            FALSE,
                            GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK,
                            NULL,
                            GDK_CURRENT_TIME);
        }
        tmp = tmp->next;
    }
    g_list_free(devices);
#else
    gdk_keyboard_grab(gtk_widget_get_window(s->drawing_area),
                      FALSE,
                      GDK_CURRENT_TIME);
#endif
}

static void gd_ungrab_keyboard(GtkDisplayState *s)
{
#if GTK_CHECK_VERSION(3, 0, 0)
    GdkDisplay *display = gtk_widget_get_display(s->drawing_area);
    GdkDeviceManager *mgr = gdk_display_get_device_manager(display);
    GList *devices = gdk_device_manager_list_devices(mgr,
                                                     GDK_DEVICE_TYPE_MASTER);
    GList *tmp = devices;
    while (tmp) {
        GdkDevice *dev = tmp->data;
        if (gdk_device_get_source(dev) == GDK_SOURCE_KEYBOARD) {
            gdk_device_ungrab(dev,
                              GDK_CURRENT_TIME);
        }
        tmp = tmp->next;
    }
    g_list_free(devices);
#else
    gdk_keyboard_ungrab(GDK_CURRENT_TIME);
#endif
}

static void gd_grab_pointer(GtkDisplayState *s)
{
    GdkDisplay *display = gtk_widget_get_display(s->drawing_area);
#if GTK_CHECK_VERSION(3, 0, 0)
    GdkDeviceManager *mgr = gdk_display_get_device_manager(display);
    GList *devices = gdk_device_manager_list_devices(mgr,
                                                     GDK_DEVICE_TYPE_MASTER);
    GList *tmp = devices;
    while (tmp) {
        GdkDevice *dev = tmp->data;
        if (gdk_device_get_source(dev) == GDK_SOURCE_MOUSE) {
            gdk_device_grab(dev,
                            gtk_widget_get_window(s->drawing_area),
                            GDK_OWNERSHIP_NONE,
                            FALSE, /* All events to come to our
                                      window directly */
                            GDK_POINTER_MOTION_MASK |
                            GDK_BUTTON_PRESS_MASK |
                            GDK_BUTTON_RELEASE_MASK |
                            GDK_BUTTON_MOTION_MASK |
                            GDK_SCROLL_MASK,
                            s->null_cursor,
                            GDK_CURRENT_TIME);
        }
        tmp = tmp->next;
    }
    g_list_free(devices);
    gdk_device_get_position(gdk_device_manager_get_client_pointer(mgr),
                            NULL, &s->grab_x_root, &s->grab_y_root);
#else
    gdk_pointer_grab(gtk_widget_get_window(s->drawing_area),
                     FALSE, /* All events to come to our window directly */
                     GDK_POINTER_MOTION_MASK |
                     GDK_BUTTON_PRESS_MASK |
                     GDK_BUTTON_RELEASE_MASK |
                     GDK_BUTTON_MOTION_MASK |
                     GDK_SCROLL_MASK,
                     NULL, /* Allow cursor to move over entire desktop */
                     s->null_cursor,
                     GDK_CURRENT_TIME);
    gdk_display_get_pointer(display, NULL,
                            &s->grab_x_root, &s->grab_y_root, NULL);
#endif
}

static void gd_ungrab_pointer(GtkDisplayState *s)
{
    GdkDisplay *display = gtk_widget_get_display(s->drawing_area);
#if GTK_CHECK_VERSION(3, 0, 0)
    GdkDeviceManager *mgr = gdk_display_get_device_manager(display);
    GList *devices = gdk_device_manager_list_devices(mgr,
                                                     GDK_DEVICE_TYPE_MASTER);
    GList *tmp = devices;
    while (tmp) {
        GdkDevice *dev = tmp->data;
        if (gdk_device_get_source(dev) == GDK_SOURCE_MOUSE) {
            gdk_device_ungrab(dev,
                              GDK_CURRENT_TIME);
        }
        tmp = tmp->next;
    }
    g_list_free(devices);
    gdk_device_warp(gdk_device_manager_get_client_pointer(mgr),
                    gtk_widget_get_screen(s->drawing_area),
                    s->grab_x_root, s->grab_y_root);
#else
    gdk_pointer_ungrab(GDK_CURRENT_TIME);
    gdk_display_warp_pointer(display,
                             gtk_widget_get_screen(s->drawing_area),
                             s->grab_x_root, s->grab_y_root);
#endif
}

static void gd_menu_grab_input(GtkMenuItem *item, void *opaque)
{
    GtkDisplayState *s = opaque;

    if (gd_is_grab_active(s)) {
        gd_grab_keyboard(s);
        gd_grab_pointer(s);
    } else {
        gd_ungrab_keyboard(s);
        gd_ungrab_pointer(s);
    }

    gd_update_caption(s);
    gd_update_cursor(s, FALSE);
}

static void gd_change_page(GtkNotebook *nb, gpointer arg1, guint arg2,
                           gpointer data)
{
    GtkDisplayState *s = data;
    guint last_page;
    gboolean on_vga;

    if (!gtk_widget_get_realized(s->notebook)) {
        return;
    }

    last_page = gtk_notebook_get_current_page(nb);

    if (last_page) {
        gtk_widget_set_size_request(s->vc[last_page - 1].terminal, -1, -1);
    }

    on_vga = arg2 == 0;

    if (!on_vga) {
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(s->grab_item),
                                       FALSE);
    } else if (s->full_screen) {
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(s->grab_item),
                                       TRUE);
    }

    if (arg2 == 0) {
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(s->vga_item), TRUE);
    } else {
#if defined(CONFIG_VTE)
        VirtualConsole *vc = &s->vc[arg2 - 1];
        VteTerminal *term = VTE_TERMINAL(vc->terminal);
        int width, height;

        width = 80 * vte_terminal_get_char_width(term);
        height = 25 * vte_terminal_get_char_height(term);

        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(vc->menu_item), TRUE);
        gtk_widget_set_size_request(vc->terminal, width, height);
#else
        g_assert_not_reached();
#endif
    }

    gtk_widget_set_sensitive(s->grab_item, on_vga);

    gd_update_cursor(s, TRUE);
}

static gboolean gd_enter_event(GtkWidget *widget, GdkEventCrossing *crossing, gpointer data)
{
    GtkDisplayState *s = data;

    if (!gd_is_grab_active(s) && gd_grab_on_hover(s)) {
        gd_grab_keyboard(s);
    }

    return TRUE;
}

static gboolean gd_leave_event(GtkWidget *widget, GdkEventCrossing *crossing, gpointer data)
{
    GtkDisplayState *s = data;

    if (!gd_is_grab_active(s) && gd_grab_on_hover(s)) {
        gd_ungrab_keyboard(s);
    }

    return TRUE;
}

static gboolean gd_focus_out_event(GtkWidget *widget,
                                   GdkEventCrossing *crossing, gpointer data)
{
    GtkDisplayState *s = data;

    gtk_release_modifiers(s);

    return TRUE;
}

/** Virtual Console Callbacks **/

static int gd_vc_chr_write(CharDriverState *chr, const uint8_t *buf, int len)
{
    VirtualConsole *vc = chr->opaque;

    return vc ? write(vc->fd, buf, len) : len;
}

static int nb_vcs;
static CharDriverState *vcs[MAX_VCS];

static CharDriverState *gd_vc_handler(ChardevVC *unused)
{
    CharDriverState *chr;

    chr = g_malloc0(sizeof(*chr));
    chr->chr_write = gd_vc_chr_write;
    /* defer OPENED events until our vc is fully initialized */
    chr->explicit_be_open = true;

    vcs[nb_vcs++] = chr;

    return chr;
}

void early_gtk_display_init(void)
{
    register_vc_handler(gd_vc_handler);
}

#if defined(CONFIG_VTE)
static gboolean gd_vc_in(GIOChannel *chan, GIOCondition cond, void *opaque)
{
    VirtualConsole *vc = opaque;
    uint8_t buffer[1024];
    ssize_t len;

    len = read(vc->fd, buffer, sizeof(buffer));
    if (len <= 0) {
        return FALSE;
    }

    qemu_chr_be_write(vc->chr, buffer, len);

    return TRUE;
}
#endif

static GSList *gd_vc_init(GtkDisplayState *s, VirtualConsole *vc, int index, GSList *group,
                          GtkWidget *view_menu)
{
#if defined(CONFIG_VTE)
    const char *label;
    char buffer[32];
    char path[32];
#if VTE_CHECK_VERSION(0, 26, 0)
    VtePty *pty;
#endif
    GIOChannel *chan;
    GtkWidget *scrolled_window;
    GtkAdjustment *vadjustment;
    int master_fd, slave_fd;

    snprintf(buffer, sizeof(buffer), "vc%d", index);
    snprintf(path, sizeof(path), "<QEMU>/View/VC%d", index);

    vc->chr = vcs[index];

    if (vc->chr->label) {
        label = vc->chr->label;
    } else {
        label = buffer;
    }

    vc->menu_item = gtk_radio_menu_item_new_with_mnemonic(group, label);
    group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(vc->menu_item));
    gtk_menu_item_set_accel_path(GTK_MENU_ITEM(vc->menu_item), path);
    gtk_accel_map_add_entry(path, GDK_KEY_2 + index, HOTKEY_MODIFIERS);

    vc->terminal = vte_terminal_new();

    master_fd = qemu_openpty_raw(&slave_fd, NULL);
    g_assert(master_fd != -1);

#if VTE_CHECK_VERSION(0, 26, 0)
    pty = vte_pty_new_foreign(master_fd, NULL);
    vte_terminal_set_pty_object(VTE_TERMINAL(vc->terminal), pty);
#else
    vte_terminal_set_pty(VTE_TERMINAL(vc->terminal), master_fd);
#endif

    vte_terminal_set_scrollback_lines(VTE_TERMINAL(vc->terminal), -1);

    vadjustment = vte_terminal_get_adjustment(VTE_TERMINAL(vc->terminal));

    scrolled_window = gtk_scrolled_window_new(NULL, vadjustment);
    gtk_container_add(GTK_CONTAINER(scrolled_window), vc->terminal);

    vte_terminal_set_size(VTE_TERMINAL(vc->terminal), 80, 25);

    vc->fd = slave_fd;
    vc->chr->opaque = vc;
    vc->scrolled_window = scrolled_window;

    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(vc->scrolled_window),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    gtk_notebook_append_page(GTK_NOTEBOOK(s->notebook), scrolled_window, gtk_label_new(label));
    g_signal_connect(vc->menu_item, "activate",
                     G_CALLBACK(gd_menu_switch_vc), s);

    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), vc->menu_item);

    qemu_chr_be_generic_open(vc->chr);
    if (vc->chr->init) {
        vc->chr->init(vc->chr);
    }

    chan = g_io_channel_unix_new(vc->fd);
    g_io_add_watch(chan, G_IO_IN, gd_vc_in, vc);

#endif /* CONFIG_VTE */
    return group;
}

/** Window Creation **/

static void gd_connect_signals(GtkDisplayState *s)
{
    g_signal_connect(s->show_tabs_item, "activate",
                     G_CALLBACK(gd_menu_show_tabs), s);

    g_signal_connect(s->window, "key-press-event",
                     G_CALLBACK(gd_window_key_event), s);
    g_signal_connect(s->window, "delete-event",
                     G_CALLBACK(gd_window_close), s);

#if GTK_CHECK_VERSION(3, 0, 0)
    g_signal_connect(s->drawing_area, "draw",
                     G_CALLBACK(gd_draw_event), s);
#else
    g_signal_connect(s->drawing_area, "expose-event",
                     G_CALLBACK(gd_expose_event), s);
#endif
    g_signal_connect(s->drawing_area, "event",
                     G_CALLBACK(gd_event), s);
    g_signal_connect(s->drawing_area, "button-press-event",
                     G_CALLBACK(gd_button_event), s);
    g_signal_connect(s->drawing_area, "button-release-event",
                     G_CALLBACK(gd_button_event), s);
    g_signal_connect(s->drawing_area, "scroll-event",
                     G_CALLBACK(gd_scroll_event), s);
    g_signal_connect(s->drawing_area, "key-press-event",
                     G_CALLBACK(gd_key_event), s);
    g_signal_connect(s->drawing_area, "key-release-event",
                     G_CALLBACK(gd_key_event), s);

    g_signal_connect(s->pause_item, "activate",
                     G_CALLBACK(gd_menu_pause), s);
    g_signal_connect(s->reset_item, "activate",
                     G_CALLBACK(gd_menu_reset), s);
    g_signal_connect(s->powerdown_item, "activate",
                     G_CALLBACK(gd_menu_powerdown), s);
    g_signal_connect(s->quit_item, "activate",
                     G_CALLBACK(gd_menu_quit), s);
    g_signal_connect(s->full_screen_item, "activate",
                     G_CALLBACK(gd_menu_full_screen), s);
    g_signal_connect(s->zoom_in_item, "activate",
                     G_CALLBACK(gd_menu_zoom_in), s);
    g_signal_connect(s->zoom_out_item, "activate",
                     G_CALLBACK(gd_menu_zoom_out), s);
    g_signal_connect(s->zoom_fixed_item, "activate",
                     G_CALLBACK(gd_menu_zoom_fixed), s);
    g_signal_connect(s->zoom_fit_item, "activate",
                     G_CALLBACK(gd_menu_zoom_fit), s);
    g_signal_connect(s->vga_item, "activate",
                     G_CALLBACK(gd_menu_switch_vc), s);
    g_signal_connect(s->grab_item, "activate",
                     G_CALLBACK(gd_menu_grab_input), s);
    g_signal_connect(s->notebook, "switch-page",
                     G_CALLBACK(gd_change_page), s);
    g_signal_connect(s->drawing_area, "enter-notify-event",
                     G_CALLBACK(gd_enter_event), s);
    g_signal_connect(s->drawing_area, "leave-notify-event",
                     G_CALLBACK(gd_leave_event), s);
    g_signal_connect(s->drawing_area, "focus-out-event",
                     G_CALLBACK(gd_focus_out_event), s);
}

static GtkWidget *gd_create_menu_machine(GtkDisplayState *s, GtkAccelGroup *accel_group)
{
    GtkWidget *machine_menu;
    GtkWidget *separator;
    GtkStockItem item;

    machine_menu = gtk_menu_new();
    gtk_menu_set_accel_group(GTK_MENU(machine_menu), accel_group);

    s->pause_item = gtk_check_menu_item_new_with_mnemonic(_("_Pause"));
    gtk_menu_shell_append(GTK_MENU_SHELL(machine_menu), s->pause_item);

    separator = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(machine_menu), separator);

    s->reset_item = gtk_image_menu_item_new_with_mnemonic(_("_Reset"));
    gtk_menu_shell_append(GTK_MENU_SHELL(machine_menu), s->reset_item);

    s->powerdown_item = gtk_image_menu_item_new_with_mnemonic(_("Power _Down"));
    gtk_menu_shell_append(GTK_MENU_SHELL(machine_menu), s->powerdown_item);

    separator = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(machine_menu), separator);

    s->quit_item = gtk_image_menu_item_new_from_stock(GTK_STOCK_QUIT, NULL);
    gtk_stock_lookup(GTK_STOCK_QUIT, &item);
    gtk_menu_item_set_accel_path(GTK_MENU_ITEM(s->quit_item),
                                 "<QEMU>/Machine/Quit");
    gtk_accel_map_add_entry("<QEMU>/Machine/Quit", item.keyval, item.modifier);
    gtk_menu_shell_append(GTK_MENU_SHELL(machine_menu), s->quit_item);

    return machine_menu;
}

static GtkWidget *gd_create_menu_view(GtkDisplayState *s, GtkAccelGroup *accel_group)
{
    GSList *group = NULL;
    GtkWidget *view_menu;
    GtkWidget *separator;
    int i;

    view_menu = gtk_menu_new();
    gtk_menu_set_accel_group(GTK_MENU(view_menu), accel_group);

    s->full_screen_item =
        gtk_image_menu_item_new_from_stock(GTK_STOCK_FULLSCREEN, NULL);
    gtk_menu_item_set_accel_path(GTK_MENU_ITEM(s->full_screen_item),
                                 "<QEMU>/View/Full Screen");
    gtk_accel_map_add_entry("<QEMU>/View/Full Screen", GDK_KEY_f,
                            HOTKEY_MODIFIERS);
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), s->full_screen_item);

    separator = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), separator);

    s->zoom_in_item = gtk_image_menu_item_new_from_stock(GTK_STOCK_ZOOM_IN, NULL);
    gtk_menu_item_set_accel_path(GTK_MENU_ITEM(s->zoom_in_item),
                                 "<QEMU>/View/Zoom In");
    gtk_accel_map_add_entry("<QEMU>/View/Zoom In", GDK_KEY_plus,
                            HOTKEY_MODIFIERS);
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), s->zoom_in_item);

    s->zoom_out_item = gtk_image_menu_item_new_from_stock(GTK_STOCK_ZOOM_OUT, NULL);
    gtk_menu_item_set_accel_path(GTK_MENU_ITEM(s->zoom_out_item),
                                 "<QEMU>/View/Zoom Out");
    gtk_accel_map_add_entry("<QEMU>/View/Zoom Out", GDK_KEY_minus,
                            HOTKEY_MODIFIERS);
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), s->zoom_out_item);

    s->zoom_fixed_item = gtk_image_menu_item_new_from_stock(GTK_STOCK_ZOOM_100, NULL);
    gtk_menu_item_set_accel_path(GTK_MENU_ITEM(s->zoom_fixed_item),
                                 "<QEMU>/View/Zoom Fixed");
    gtk_accel_map_add_entry("<QEMU>/View/Zoom Fixed", GDK_KEY_0,
                            HOTKEY_MODIFIERS);
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), s->zoom_fixed_item);

    s->zoom_fit_item = gtk_check_menu_item_new_with_mnemonic(_("Zoom To _Fit"));
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), s->zoom_fit_item);

    separator = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), separator);

    s->grab_on_hover_item = gtk_check_menu_item_new_with_mnemonic(_("Grab On _Hover"));
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), s->grab_on_hover_item);

    s->grab_item = gtk_check_menu_item_new_with_mnemonic(_("_Grab Input"));
    gtk_menu_item_set_accel_path(GTK_MENU_ITEM(s->grab_item),
                                 "<QEMU>/View/Grab Input");
    gtk_accel_map_add_entry("<QEMU>/View/Grab Input", GDK_KEY_g,
                            HOTKEY_MODIFIERS);
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), s->grab_item);

    separator = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), separator);

    s->vga_item = gtk_radio_menu_item_new_with_mnemonic(group, "_VGA");
    group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(s->vga_item));
    gtk_menu_item_set_accel_path(GTK_MENU_ITEM(s->vga_item),
                                 "<QEMU>/View/VGA");
    gtk_accel_map_add_entry("<QEMU>/View/VGA", GDK_KEY_1, HOTKEY_MODIFIERS);
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), s->vga_item);

    for (i = 0; i < nb_vcs; i++) {
        VirtualConsole *vc = &s->vc[i];

        group = gd_vc_init(s, vc, i, group, view_menu);
        s->nb_vcs++;
    }

    separator = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), separator);

    s->show_tabs_item = gtk_check_menu_item_new_with_mnemonic(_("Show _Tabs"));
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), s->show_tabs_item);

    return view_menu;
}

static void gd_create_menus(GtkDisplayState *s)
{
    GtkAccelGroup *accel_group;

    accel_group = gtk_accel_group_new();
    s->machine_menu = gd_create_menu_machine(s, accel_group);
    s->view_menu = gd_create_menu_view(s, accel_group);

    s->machine_menu_item = gtk_menu_item_new_with_mnemonic(_("_Machine"));
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(s->machine_menu_item),
                              s->machine_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(s->menu_bar), s->machine_menu_item);

    s->view_menu_item = gtk_menu_item_new_with_mnemonic(_("_View"));
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(s->view_menu_item), s->view_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(s->menu_bar), s->view_menu_item);

    g_object_set_data(G_OBJECT(s->window), "accel_group", accel_group);
    gtk_window_add_accel_group(GTK_WINDOW(s->window), accel_group);
    s->accel_group = accel_group;
}

static const DisplayChangeListenerOps dcl_ops = {
    .dpy_name          = "gtk",
    .dpy_gfx_update    = gd_update,
    .dpy_gfx_switch    = gd_switch,
    .dpy_refresh       = gd_refresh,
    .dpy_mouse_set     = gd_mouse_set,
    .dpy_cursor_define = gd_cursor_define,
};

void gtk_display_init(DisplayState *ds, bool full_screen, bool grab_on_hover)
{
    GtkDisplayState *s = g_malloc0(sizeof(*s));
    char *filename;

    gtk_init(NULL, NULL);

    s->dcl.ops = &dcl_ops;
    s->dcl.con = qemu_console_lookup_by_index(0);

    s->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
#if GTK_CHECK_VERSION(3, 2, 0)
    s->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
#else
    s->vbox = gtk_vbox_new(FALSE, 0);
#endif
    s->notebook = gtk_notebook_new();
    s->drawing_area = gtk_drawing_area_new();
    s->menu_bar = gtk_menu_bar_new();

    s->scale_x = 1.0;
    s->scale_y = 1.0;
    s->free_scale = FALSE;

    setlocale(LC_ALL, "");
    bindtextdomain("qemu", CONFIG_QEMU_LOCALEDIR);
    textdomain("qemu");

    s->null_cursor = gdk_cursor_new(GDK_BLANK_CURSOR);

    s->mouse_mode_notifier.notify = gd_mouse_mode_change;
    qemu_add_mouse_mode_change_notifier(&s->mouse_mode_notifier);
    qemu_add_vm_change_state_handler(gd_change_runstate, s);

    gtk_notebook_append_page(GTK_NOTEBOOK(s->notebook), s->drawing_area, gtk_label_new("VGA"));

    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, "qemu_logo_no_text.svg");
    if (filename) {
        GError *error = NULL;
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(filename, &error);
        if (pixbuf) {
            gtk_window_set_icon(GTK_WINDOW(s->window), pixbuf);
        } else {
            g_error_free(error);
        }
        g_free(filename);
    }

    gd_create_menus(s);

    gd_connect_signals(s);

    gtk_widget_add_events(s->drawing_area,
                          GDK_POINTER_MOTION_MASK |
                          GDK_BUTTON_PRESS_MASK |
                          GDK_BUTTON_RELEASE_MASK |
                          GDK_BUTTON_MOTION_MASK |
                          GDK_ENTER_NOTIFY_MASK |
                          GDK_LEAVE_NOTIFY_MASK |
                          GDK_SCROLL_MASK |
                          GDK_KEY_PRESS_MASK);
    gtk_widget_set_double_buffered(s->drawing_area, FALSE);
    gtk_widget_set_can_focus(s->drawing_area, TRUE);

    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(s->notebook), FALSE);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(s->notebook), FALSE);

    gd_update_caption(s);

    gtk_box_pack_start(GTK_BOX(s->vbox), s->menu_bar, FALSE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(s->vbox), s->notebook, TRUE, TRUE, 0);

    gtk_container_add(GTK_CONTAINER(s->window), s->vbox);

    gtk_widget_show_all(s->window);

    if (full_screen) {
        gtk_menu_item_activate(GTK_MENU_ITEM(s->full_screen_item));
    }
    if (grab_on_hover) {
        gtk_menu_item_activate(GTK_MENU_ITEM(s->grab_on_hover_item));
    }

    register_displaychangelistener(&s->dcl);

    global_state = s;
}
